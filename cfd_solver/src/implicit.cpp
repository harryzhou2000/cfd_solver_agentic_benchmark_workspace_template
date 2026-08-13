#include "implicit.hpp"
#include "solver.hpp"
#include "output.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cassert>

namespace cfd {

// Compute least-squares gradients for a scalar field
// cells parameter should be all cells (owned+ghost) when nb can be ghost
static Vec2 compute_gradient(const std::vector<real_t>& phi,
                              const std::vector<Cell>& owners,
                              const std::vector<Cell>& all_cells,
                              const std::vector<Face>& faces,
                              idx_t cell_idx, idx_t n_owned) {
    
    const auto& cell = owners[cell_idx];
    
    if (cell.neighbor_ids.empty()) return Vec2::Zero();
    
    Eigen::Matrix<real_t, Eigen::Dynamic, 2> A(cell.neighbor_ids.size(), 2);
    Eigen::VectorXd b(cell.neighbor_ids.size());
    
    for (size_t j = 0; j < cell.neighbor_ids.size(); j++) {
        idx_t nb = cell.neighbor_ids[j];
        if (nb >= static_cast<idx_t>(phi.size())) continue;
        
        Vec2 dr;
        if (nb < n_owned) {
            dr = owners[nb].centroid - cell.centroid;
        } else {
            // Ghost cell: use its actual centroid (all_cells holds full ghost
            // Cell copies). This keeps the least-squares stencil identical to
            // the serial partitioning, making gradients rank-count invariant.
            if (nb >= static_cast<idx_t>(all_cells.size())) continue;
            dr = all_cells[nb].centroid - cell.centroid;
        }
        
        A(j, 0) = dr[0];
        A(j, 1) = dr[1];
        b(j) = phi[nb] - phi[cell_idx];
    }
    
    if (cell.neighbor_ids.size() < 2) return Vec2::Zero();
    
    Eigen::Matrix2d AtA = A.transpose() * A;
    Eigen::Vector2d Atb = A.transpose() * b;
    
    // Solve with regularization
    Eigen::Matrix2d reg = AtA + 1e-12 * Eigen::Matrix2d::Identity();
    Eigen::Vector2d grad = reg.ldlt().solve(Atb);
    
    return Vec2(grad[0], grad[1]);
}

static real_t barth_jespersen_limiter(
    real_t phi_cell,
    const Vec2& grad_phi,
    const Vec2& /*centroid*/,
    const std::vector<Vec2>& /*face_centroids*/,
    const std::vector<real_t>& neighbor_phi,
    const std::vector<Cell>& /*cells*/,
    const std::vector<Face>& faces,
    const std::vector<Cell>& owners,
    idx_t cell_idx) {
    
    const auto& cell = owners[cell_idx];
    real_t phi_min = phi_cell;
    real_t phi_max = phi_cell;
    
    for (size_t j = 0; j < cell.neighbor_ids.size(); j++) {
        idx_t nb = cell.neighbor_ids[j];
        if (nb >= static_cast<idx_t>(neighbor_phi.size())) continue;
        phi_min = std::min(phi_min, neighbor_phi[nb]);
        phi_max = std::max(phi_max, neighbor_phi[nb]);
    }
    
    real_t psi = 1.0;
    real_t eps = 1e-12;
    
    for (size_t j = 0; j < cell.face_ids.size(); j++) {
        idx_t fid = cell.face_ids[j];
        if (fid >= static_cast<idx_t>(faces.size())) continue;
        
        Vec2 dr = faces[fid].centroid - cell.centroid;
        real_t phi_face = phi_cell + grad_phi.dot(dr);
        real_t diff = phi_face - phi_cell;
        
        if (diff > eps) {
            real_t psi_j = (phi_max - phi_cell) / std::max(diff, eps);
            psi = std::min(psi, std::max(0.0, std::min(1.0, psi_j)));
        } else if (diff < -eps) {
            real_t psi_j = (phi_min - phi_cell) / std::min(diff, -eps);
            psi = std::min(psi, std::max(0.0, std::min(1.0, psi_j)));
        }
    }
    
    return std::max(0.0, psi);
}

void compute_residual_full(Solver& solver, const std::vector<StateVec>& U,
                           std::vector<StateVec>& R,
                           real_t& res_l2, real_t& res_linf,
                           std::vector<Vec2>& grad_rho,
                           std::vector<Vec2>& grad_rhou,
                           std::vector<Vec2>& grad_rhov,
                           std::vector<Vec2>& grad_rhoE) {
    
    const auto& local = solver.local_mesh();
    const auto& ci = solver.case_input();
    idx_t n_owned = local.n_owned;
    idx_t n_total = local.n_total;
    real_t gamma = ci.gamma;
    
    // Extract scalar fields from state
    std::vector<real_t> rho(n_total), rhou(n_total), rhov(n_total), rhoE(n_total);
    for (idx_t i = 0; i < n_total; i++) {
        rho[i] = U[i][0];
        rhou[i] = U[i][1];
        rhov[i] = U[i][2];
        rhoE[i] = U[i][3];
    }
    
    // Build combined cell array for neighbor access
    std::vector<Cell> all_cells;
    all_cells.reserve(n_total);
    for (idx_t i = 0; i < n_owned; i++) all_cells.push_back(local.owned_cells[i]);
    for (idx_t i = 0; i < local.n_ghost; i++) all_cells.push_back(local.ghost_cells[i]);
    
    // Compute gradients for owned cells
    for (idx_t i = 0; i < n_owned; i++) {
        grad_rho[i] = compute_gradient(rho, local.owned_cells, all_cells, local.faces, i, n_owned);
        grad_rhou[i] = compute_gradient(rhou, local.owned_cells, all_cells, local.faces, i, n_owned);
        grad_rhov[i] = compute_gradient(rhov, local.owned_cells, all_cells, local.faces, i, n_owned);
        grad_rhoE[i] = compute_gradient(rhoE, local.owned_cells, all_cells, local.faces, i, n_owned);
    }
    
    // Compute limiters
    std::vector<real_t> limiter_rho(n_owned, 1.0);
    std::vector<real_t> limiter_rhou(n_owned, 1.0);
    std::vector<real_t> limiter_rhov(n_owned, 1.0);
    std::vector<real_t> limiter_rhoE(n_owned, 1.0);
    
    for (idx_t i = 0; i < n_owned; i++) {
        const auto& cell = local.owned_cells[i];
        
        std::vector<Vec2> fc_vec;
        std::vector<real_t> nbr_rho, nbr_rhou, nbr_rhov, nbr_rhoE;
        
        for (auto fid : cell.face_ids) {
            if (fid >= static_cast<idx_t>(local.faces.size())) continue;
            const auto& face = local.faces[fid];
            fc_vec.push_back(face.centroid);
            
            idx_t nb = -1;
            if (face.left_cell == i && face.right_cell >= 0) nb = face.right_cell;
            if (face.right_cell == i && face.left_cell >= 0) nb = face.left_cell;
            
            if (nb >= 0 && nb < n_total) {
                nbr_rho.push_back(rho[nb]);
                nbr_rhou.push_back(rhou[nb]);
                nbr_rhov.push_back(rhov[nb]);
                nbr_rhoE.push_back(rhoE[nb]);
            } else {
                nbr_rho.push_back(rho[i]);
                nbr_rhou.push_back(rhou[i]);
                nbr_rhov.push_back(rhov[i]);
                nbr_rhoE.push_back(rhoE[i]);
            }
        }
        
        if (!fc_vec.empty()) {
            limiter_rho[i] = barth_jespersen_limiter(rho[i], grad_rho[i], cell.centroid, fc_vec, nbr_rho, local.owned_cells, local.faces, local.owned_cells, i);
            limiter_rhou[i] = barth_jespersen_limiter(rhou[i], grad_rhou[i], cell.centroid, fc_vec, nbr_rhou, local.owned_cells, local.faces, local.owned_cells, i);
            limiter_rhov[i] = barth_jespersen_limiter(rhov[i], grad_rhov[i], cell.centroid, fc_vec, nbr_rhov, local.owned_cells, local.faces, local.owned_cells, i);
            limiter_rhoE[i] = barth_jespersen_limiter(rhoE[i], grad_rhoE[i], cell.centroid, fc_vec, nbr_rhoE, local.owned_cells, local.faces, local.owned_cells, i);
        }
    }
    
    // Initialize residual
    R.assign(n_owned, StateVec::Zero());
    
    bool is_inviscid = (ci.physics_mode == "inviscid");
    real_t mu = 0;
    if (!is_inviscid && ci.reynolds > 0) {
        real_t vel_inf = std::sqrt(ci.u_inf*ci.u_inf + ci.v_inf*ci.v_inf);
        mu = ci.rho_inf * vel_inf * ci.ref_length / ci.reynolds;
    }
    
    const auto& bc_map = solver.bc_type_map();
    
    bool use_reconstruction = (ci.run_type == "transient") ? false : (ci.physics_mode == "inviscid");
    // Loop over faces
    for (const auto& face : local.faces) {
        idx_t left = face.left_cell;
        idx_t right = face.right_cell;
        
        // Get left/right states with reconstruction
        StateVec UL, UR;
        
        // Left state
        if (left >= 0 && left < n_owned) {
            if (use_reconstruction) {
                Vec2 dr = face.centroid - local.owned_cells[left].centroid;
                UL[0] = rho[left] + limiter_rho[left] * grad_rho[left].dot(dr);
                UL[1] = rhou[left] + limiter_rhou[left] * grad_rhou[left].dot(dr);
                UL[2] = rhov[left] + limiter_rhov[left] * grad_rhov[left].dot(dr);
                UL[3] = rhoE[left] + limiter_rhoE[left] * grad_rhoE[left].dot(dr);
            } else {
                UL = U[left];
            }
            
            // Positivity fallback
            if (UL[0] < 1e-10 || !std::isfinite(UL[0])) UL = U[left];
        } else {
            UL = U[left];
        }
        
        // Right state  
        if (right >= 0 && right < n_owned) {
            if (use_reconstruction) {
                Vec2 dr = face.centroid - local.owned_cells[right].centroid;
                UR[0] = rho[right] + limiter_rho[right] * grad_rho[right].dot(dr);
                UR[1] = rhou[right] + limiter_rhou[right] * grad_rhou[right].dot(dr);
                UR[2] = rhov[right] + limiter_rhov[right] * grad_rhov[right].dot(dr);
                UR[3] = rhoE[right] + limiter_rhoE[right] * grad_rhoE[right].dot(dr);
            } else {
                UR = U[right];
            }
            
            if (UR[0] < 1e-10 || !std::isfinite(UR[0])) UR = U[right];
        } else if (right >= n_owned) {
            // Ghost cell - use stored value
            UR = U[right];
        }
        
        // Boundary conditions
        if (face.bc_tag != 0) {
            auto it = bc_map.find(face.bc_tag);
            if (it != bc_map.end()) {
                BCType bc = it->second;
                Vec2 n_hat = face.normal.normalized();
                
                if (bc == BCType::Farfield) {
                    UR = ci.freestream_state;
                } else if (bc == BCType::SlipWall) {
                    // Mirror state
                    real_t rhoL = UL[0];
                    real_t uL = UL[1] / rhoL;
                    real_t vL = UL[2] / rhoL;
                    real_t un = uL * n_hat[0] + vL * n_hat[1];
                    real_t u_mirror = uL - 2.0 * un * n_hat[0];
                    real_t v_mirror = vL - 2.0 * un * n_hat[1];
                    real_t pL = solver.pressure(UL);
                    UR = solver.primitive_to_conservative(rhoL, u_mirror, v_mirror, pL);
                } else if (bc == BCType::NoSlipAdiabaticWall) {
                    // No-slip adiabatic wall: velocity is antisymmetric
                    // (both normal and tangential components flip), while
                    // density and pressure are symmetric (adiabatic, zero
                    // normal momentum). Wall velocity is then zero.
                    real_t rhoL = UL[0];
                    real_t uL = UL[1] / rhoL;
                    real_t vL = UL[2] / rhoL;
                    real_t pL = solver.pressure(UL);
                    UR = solver.primitive_to_conservative(rhoL, -uL, -vL, pL);
                }
            }
        }
        
        // Compute inviscid flux
        StateVec flux;
        bool is_wall_face = false;
        if (face.bc_tag != 0) {
            auto it = bc_map.find(face.bc_tag);
            is_wall_face = (it != bc_map.end() &&
                            (it->second == BCType::SlipWall ||
                             it->second == BCType::NoSlipAdiabaticWall));
        }
        
        if (is_wall_face) {
            // Wall faces carry pressure only: no mass or tangential momentum
            // crosses a solid wall. Using Rusanov here would inject spurious
            // dissipation from the mirrored ghost state (lambda * rho*u term),
            // which destabilizes viscous cases. The correct inviscid wall flux
            // is (0, p*nx, p*ny, 0).
            real_t p_face = solver.pressure(UL);
            flux[0] = 0.0;
            flux[1] = p_face * face.normal[0];
            flux[2] = p_face * face.normal[1];
            flux[3] = 0.0;
        } else {
            bool use_roe = (ci.mach >= 0.8);
            if (use_roe) {
                flux = roe_flux(UL, UR, face.normal, gamma, 0.1);
            } else {
                real_t diss = (ci.run_type == "transient") ? ci.rusanov_dissipation_scale : 1.0;
                flux = rusanov_flux(UL, UR, face.normal, gamma, diss);
            }
        }
        
        // Viscous flux (interior faces only, face-normal from left to right)
        if (!is_inviscid && face.bc_tag == 0) {
            StateVec Uavg;
            for (int k = 0; k < 4; k++) Uavg[k] = 0.5 * (UL[k] + UR[k]);
            
            Vec2 g_rho_avg, g_rhou_avg, g_rhov_avg, g_rhoE_avg;
            if (left >= 0 && left < n_owned && right >= 0 && right < n_owned) {
                g_rho_avg = 0.5 * (grad_rho[left] + grad_rho[right]);
                g_rhou_avg = 0.5 * (grad_rhou[left] + grad_rhou[right]);
                g_rhov_avg = 0.5 * (grad_rhov[left] + grad_rhov[right]);
                g_rhoE_avg = 0.5 * (grad_rhoE[left] + grad_rhoE[right]);
            } else if (left >= 0 && left < n_owned) {
                // Interface face. The ghost cell (right) cannot contribute a
                // least-squares gradient here, so use a symmetric centroid-to-
                // centroid difference. Both ranks compute the same value (both
                // have both centroids and both states), which makes the viscous
                // flux across the partition interface exactly conservative and
                // rank-count invariant.
                Vec2 dr = all_cells[right].centroid - local.owned_cells[left].centroid;
                real_t dn2 = std::max(dr.dot(dr), 1e-30);
                Vec2 nrm = dr / dn2;
                g_rho_avg = (rho[right] - rho[left]) * nrm;
                g_rhou_avg = (rhou[right] - rhou[left]) * nrm;
                g_rhov_avg = (rhov[right] - rhov[left]) * nrm;
                g_rhoE_avg = (rhoE[right] - rhoE[left]) * nrm;
            } else if (right >= 0 && right < n_owned) {
                // Mirror-image inverted interface face: the left cell is a
                // ghost, right is owned. Use the same symmetric centroid-to-
                // centroid difference.
                Vec2 dr = local.owned_cells[right].centroid - all_cells[left].centroid;
                real_t dn2 = std::max(dr.dot(dr), 1e-30);
                Vec2 nrm = dr / dn2;
                g_rho_avg = (rho[right] - rho[left]) * nrm;
                g_rhou_avg = (rhou[right] - rhou[left]) * nrm;
                g_rhov_avg = (rhov[right] - rhov[left]) * nrm;
                g_rhoE_avg = (rhoE[right] - rhoE[left]) * nrm;
            } else {
                g_rho_avg = Vec2::Zero();
                g_rhou_avg = Vec2::Zero();
                g_rhov_avg = Vec2::Zero();
                g_rhoE_avg = Vec2::Zero();
            }
            
            StateVec visc = viscous_flux(Uavg, g_rho_avg, g_rhou_avg,
                                         g_rhov_avg, g_rhoE_avg, face.normal,
                                         gamma, ci.R, ci.prandtl, mu);
            for (int k = 0; k < 4; k++) flux[k] -= visc[k];
        }
        
        // Accumulate into residuals
        if (left >= 0 && left < n_owned) {
            for (int k = 0; k < 4; k++) R[left][k] += flux[k];
        }
        if (right >= 0 && right < n_owned) {
            for (int k = 0; k < 4; k++) R[right][k] -= flux[k];
        }
    }
    
    // Compute global L2 and Linf norms
    real_t l2_local = 0;
    real_t linf_local = 0;
    for (idx_t i = 0; i < n_owned; i++) {
        real_t r2 = R[i].squaredNorm();
        l2_local += r2;
        real_t abs_sum = std::abs(R[i][0]) + std::abs(R[i][1]) + std::abs(R[i][2]) + std::abs(R[i][3]);
        linf_local = std::max(linf_local, abs_sum);
    }
    
    MPI_Allreduce(&l2_local, &res_l2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&linf_local, &res_linf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    
    res_l2 = std::sqrt(res_l2);
}

void compute_local_dt(Solver& solver, const std::vector<StateVec>& U,
                      std::vector<real_t>& dt_local, real_t cfl) {
    
    const auto& local = solver.local_mesh();
    const auto& ci = solver.case_input();
    idx_t n_owned = local.n_owned;
    real_t gamma = ci.gamma;
    
    real_t mu = 0;
    if (ci.physics_mode != "inviscid" && ci.reynolds > 0) {
        mu = ci.rho_inf * std::sqrt(ci.u_inf*ci.u_inf + ci.v_inf*ci.v_inf) * ci.ref_length / ci.reynolds;
    }
    
    for (idx_t i = 0; i < n_owned; i++) {
        const auto& cell = local.owned_cells[i];
        real_t rho = U[i][0];
        real_t u = U[i][1] / rho;
        real_t v = U[i][2] / rho;
        real_t a = std::max(solver.speed_of_sound(U[i]), 1e-10);
        
        real_t vol = std::max(cell.volume, 1e-15);
        real_t h = std::sqrt(vol);
        
        // Convective spectral radius
        real_t lambda_c = 0;
        for (auto fid : cell.face_ids) {
            if (fid >= static_cast<idx_t>(local.faces.size())) continue;
            const auto& face = local.faces[fid];
            real_t area = std::max(face.normal.norm(), 1e-15);
            real_t un = std::abs(u * face.normal[0] / area + v * face.normal[1] / area);
            lambda_c += un + a * area;
        }
        
        // Viscous spectral radius
        real_t lambda_v = 0;
        if (mu > 0 && rho > 0) {
            real_t nu = mu / rho;
            real_t nu_thermal = nu * gamma / ci.prandtl;
            lambda_v = std::max(nu, nu_thermal) / (h * h) * vol;
        }
        
        real_t dt = cfl * vol / std::max(lambda_c + 4.0 * lambda_v, 1e-15);
        dt_local[i] = std::max(dt, 1e-12);
    }
}

void lu_sgs_step(Solver& solver, std::vector<StateVec>& U,
                 const std::vector<StateVec>& R,
                 const std::vector<real_t>& dt_local,
                 real_t omega) {
    
    const auto& local = solver.local_mesh();
    const auto& ci = solver.case_input();
    idx_t n_owned = local.n_owned;
    real_t gamma = solver.gamma();
    
    real_t mu = 0;
    if (ci.physics_mode != "inviscid" && ci.reynolds > 0) {
        mu = ci.rho_inf * std::sqrt(ci.u_inf*ci.u_inf + ci.v_inf*ci.v_inf) *
             ci.ref_length / ci.reynolds;
    }
    
    for (idx_t i = 0; i < n_owned; i++) {
        real_t vol = std::max(local.owned_cells[i].volume, 1e-15);
        real_t dt = std::max(dt_local[i], 1e-12) / vol;
        
        real_t a = std::max(solver.speed_of_sound(U[i]), 1e-10);
        real_t spec_rad = 2.0 * a * std::sqrt(vol);
        // Viscous contribution to the diagonal: nu/h^2 * vol (dimensionless
        // after multiplying by dt). Keeps the implicit update stable for the
        // stiff near-wall cells of viscous cases.
        real_t visc_rad = 0;
        if (mu > 0 && U[i][0] > 0) {
            real_t nu = mu / U[i][0];
            real_t nu_thermal = nu * gamma / ci.prandtl;
            visc_rad = std::max(nu, nu_thermal) / std::max(vol, 1e-15) * vol;
        }
        real_t denom = std::max(1.0 + dt * (spec_rad * 2.0 + 4.0 * visc_rad), 1.0);
        real_t damp = omega * 0.125;
        
        for (int k = 0; k < 4; k++) {
            U[i][k] -= damp * dt * R[i][k] / denom;
        }
        
        // Positivity check and correction
        if (!std::isfinite(U[i][0]) || U[i][0] < 1e-10) {
            U[i][0] = 1e-8;
            U[i][1] = 0;
            U[i][2] = 0;
            U[i][3] = 1e-6;
        }
        real_t p = solver.pressure(U[i]);
        if (!std::isfinite(p) || p < 1e-10) {
            real_t ke = 0.5 * (U[i][1]*U[i][1] + U[i][2]*U[i][2]) / (U[i][0]*U[i][0]);
            real_t e_min = 1e-8 / (U[i][0] * (gamma - 1.0));
            U[i][3] = U[i][0] * (std::max(e_min, 1e-8) + ke);
        }
    }
}

SteadyStats run_steady(Solver& solver, const std::string& output_dir) {
    const auto& ci = solver.case_input();
    idx_t n_owned = solver.local_mesh().n_owned;
    
    SteadyStats stats;
    stats.steps_run = 0;
    
    auto& U = const_cast<std::vector<StateVec>&>(solver.U());
    std::vector<StateVec> R(n_owned);
    std::vector<Vec2> grad_rho(n_owned), grad_rhou(n_owned), grad_rhov(n_owned), grad_rhoE(n_owned);
    std::vector<real_t> dt_local(n_owned);
    
    // Open output files
    std::string res_file = output_dir + "/residuals.csv";
    std::string force_file = output_dir + "/forces.csv";
    std::ofstream fres, fforce;
    if (solver.rank() == 0) {
        fres.open(res_file);
        fforce.open(force_file);
        fres << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
        fforce << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    }
    
    real_t initial_res = 1.0;
    bool converged = false;
    
    idx_t write_interval = std::max(ci.max_steps / 20, idx_t(1));
    
    for (idx_t step = 0; step < ci.max_steps; step++) {
        // Compute CFL (capped for stability)
        real_t cfl;
        real_t cfl_cap = std::min(ci.cfl_max, 30.0);
        if (ci.pseudo_cfl_ramp_steps > 0 && step < ci.pseudo_cfl_ramp_steps) {
            real_t frac = real_t(step + 1) / real_t(ci.pseudo_cfl_ramp_steps);
            cfl = ci.cfl_initial + (cfl_cap - ci.cfl_initial) * frac;
        } else {
            cfl = cfl_cap;
        }
        
        // Sync ghost states
        solver.sync_ghost_states(U);
        
        // Compute residual
        real_t res_l2, res_linf;
        compute_residual_full(solver, U, R, res_l2, res_linf,
                              grad_rho, grad_rhou, grad_rhov, grad_rhoE);
        
        if (step == 0) {
            initial_res = std::max(res_l2, 1e-15);
            if (!std::isfinite(initial_res)) initial_res = 1.0;
        }
        
        // Compute local time steps
        compute_local_dt(solver, U, dt_local, cfl);
        
        // Inner iterations
        idx_t max_inner = std::min(ci.max_inner_iterations, idx_t(5));
        for (idx_t inner = 0; inner < max_inner; inner++) {
            lu_sgs_step(solver, U, R, dt_local, 1.0);
            solver.sync_ghost_states(U);
            
            compute_residual_full(solver, U, R, res_l2, res_linf,
                                  grad_rho, grad_rhou, grad_rhov, grad_rhoE);
            
            if (inner + 1 >= ci.min_inner_iterations) {
                real_t ratio = res_l2 / std::max(initial_res, 1e-15);
                if (ratio < ci.inner_residual_reduction_target) break;
            }
        }
        
        // Compute forces
        real_t cl, cd, cmz, p_drag, v_drag, p_lift, v_lift;
        solver.compute_forces(cl, cd, cmz, p_drag, v_drag, p_lift, v_lift);
        
        // Write residuals and forces (rank 0 only)
        if (solver.rank() == 0) {
            fres << (step+1) << "," << 0.0 << "," << ci.min_inner_iterations
                 << "," << cfl << "," << dt_local[0] << ","
                 << R[0][0] << "," << R[0][1] << "," << R[0][2] << "," << R[0][3] << ","
                 << res_l2 << "," << res_linf << "\n";
            
            fforce << (step+1) << "," << 0.0 << ","
                   << cl << "," << cd << "," << cmz << ","
                   << p_drag << "," << v_drag << "," << p_lift << "," << v_lift << "\n";
        }
        
        if (solver.rank() == 0 && (step % write_interval == 0 || step == ci.max_steps - 1)) {
            std::cout << "Step " << (step+1) << "/" << ci.max_steps
                      << " CFL=" << cfl << " ResL2=" << res_l2
                      << " CD=" << cd << " CL=" << cl << std::endl;
        }
        
        // Check convergence
        real_t reduction = std::log10(initial_res / std::max(res_l2, 1e-15));
        if (std::isfinite(reduction) && reduction >= ci.residual_reduction_target && step > std::max(ci.pseudo_cfl_ramp_steps, idx_t(10))) {
            converged = true;
            stats.steps_run = step + 1;
            stats.final_res_l2 = res_l2;
            stats.final_res_linf = res_linf;
            stats.residual_reduction = reduction;
            break;
        }
        
        stats.steps_run = step + 1;
        stats.final_res_l2 = res_l2;
        stats.final_res_linf = res_linf;
        stats.residual_reduction = reduction;
    }
    
    stats.convergence_status = converged ? "converged" : "failed";
    stats.notes = converged ? "" : "Did not reach target residual reduction";
    
    if (solver.rank() == 0) {
        fres.close();
        fforce.close();
    }
    
    return stats;
}

} // namespace cfd
