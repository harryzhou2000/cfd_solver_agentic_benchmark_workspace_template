/// @file solver.cpp
/// Implementation of the main finite-volume flow solver.
/// Phase 3: MPI parallelism + BDF2 transient time integration.

#include "solver.hpp"
#include "logging.hpp"
#include "partition.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace cfd {

// ============================================================================
// Constructor
// ============================================================================

Solver::Solver(const CaseConfig& config, Mesh& mesh, MPI_Comm comm)
    : config_(config), mesh_(mesh), comm_(comm) {

    MPI_Comm_rank(comm_, &mpi_rank_);
    MPI_Comm_size(comm_, &mpi_size_);
    cfd::logging::rank() = mpi_rank_;

    // If MPI size > 1, partition the mesh on rank 0 and scatter.
    // Only rank 0 has the full mesh; other ranks receive an empty mesh
    // that the partition function will fill.
    if (mpi_size_ > 1) {
        if (mpi_rank_ == 0) {
            full_mesh_copy_ = mesh;  // keep original for reference
            LOG_INFO("Partitioning mesh: {} cells -> {} ranks", mesh.n_cells(), mpi_size_);

            auto sub_meshes = partition_mesh(mesh, mpi_size_, comm_);
            if (static_cast<int>(sub_meshes.size()) != mpi_size_) {
                throw std::runtime_error("partition_mesh returned wrong number of sub-meshes");
            }

            // Keep local mesh for rank 0
            mesh_ = std::move(sub_meshes[0]);

            // Send sub-meshes to other ranks
            for (int r = 1; r < mpi_size_; ++r) {
                send_mesh(sub_meshes[static_cast<std::size_t>(r)], r, comm_);
            }
        } else {
            // Non-zero ranks receive their local mesh from rank 0
            mesh_ = recv_mesh(0, comm_);
        }

        // Build halo exchange pattern on all ranks
        halo_ = build_halo_exchange(mesh_, comm_);

        LOG_INFO("Rank {}: {} local cells ({} owned, {} ghost), {} faces, {} neighbor ranks",
                 mpi_rank_, mesh_.n_cells(), mesh_.n_owned(),
                 mesh_.n_cells() - mesh_.n_owned(),
                 mesh_.n_faces(), halo_.send_cells.size());
    } else {
        // Serial run: all cells are owned
        mesh_.n_owned_cells = mesh_.n_cells();
    }

    n_owned_ = mesh_.n_owned();

    const std::size_t nc = mesh_.n_cells();
    U_.resize(nc);
    R_.resize(nc);
    dU_.resize(nc);
    dt_local_.resize(nc);
    grad_rho_.resize(nc);
    grad_u_.resize(nc);
    grad_v_.resize(nc);
    grad_p_.resize(nc);
}

// ============================================================================
// Initialization
// ============================================================================

void Solver::initialize() {
    const Real rho_inf = config_.freestream.rho;
    const Real u_inf   = config_.freestream.velocity(0);
    const Real v_inf   = config_.freestream.velocity(1);
    const Real p_inf   = config_.freestream.pressure;
    const Real gamma   = config_.gas.gamma;

    const Real rhoE_inf = p_inf / (gamma - 1.0)
                          + 0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);

    for (std::size_t i = 0; i < mesh_.n_cells(); ++i) {
        U_[i] << rho_inf, rho_inf * u_inf, rho_inf * v_inf, rhoE_inf;
        R_[i].setZero();
        dU_[i].setZero();
    }
    LOG_INFO("Rank {}: Initialized {} cells with freestream state", mpi_rank_, mesh_.n_cells());
}

// ============================================================================
// CFL ramping
// ============================================================================

Real Solver::compute_cfl(int /*step*/) const {
    // Conservative CFL for first-order explicit-like stability
    return 0.05;
}

// ============================================================================
// Spectral radius helpers for LU-SGS
// ============================================================================

Real Solver::face_spectral_radius(const Vec4& UL, const Vec4& UR,
                                   const Vec2& normal, Real gamma,
                                   Real mu) const {
    const Real rhoL = UL(0);
    const Real uL   = UL(1) / std::max(rhoL, EPS);
    const Real vL   = UL(2) / std::max(rhoL, EPS);
    const Real pL   = std::max((gamma - 1.0) * (UL(3)
                                 - 0.5 * rhoL * (uL * uL + vL * vL)), EPS);
    const Real aL   = speed_of_sound(rhoL, pL, gamma);

    const Real rhoR = UR(0);
    const Real uR   = UR(1) / std::max(rhoR, EPS);
    const Real vR   = UR(2) / std::max(rhoR, EPS);
    const Real pR   = std::max((gamma - 1.0) * (UR(3)
                                 - 0.5 * rhoR * (uR * uR + vR * vR)), EPS);
    const Real aR   = speed_of_sound(rhoR, pR, gamma);

    const Real VnL = uL * normal(0) + vL * normal(1);
    const Real VnR = uR * normal(0) + vR * normal(1);

    const Real lambda_c = std::max(std::abs(VnL) + aL, std::abs(VnR) + aR);

    Real lambda_v = 0.0;
    if (mu > EPS) {
        const Real factor = std::max(4.0 / 3.0, gamma);
        lambda_v = factor * mu / std::max(rhoL, rhoR);
    }

    return lambda_c + 2.0 * lambda_v;
}

Vec4 Solver::lusgs_diag(std::size_t cell_idx, Real dt, Real /*cfl*/, Real mu) const {
    const Cell& cell = mesh_.cells[cell_idx];
    const Real gamma = config_.gas.gamma;

    Real sum_spec = 0.0;
    for (std::size_t fi : cell.faces) {
        const Face& face = mesh_.faces[fi];
        const Vec4& UL = U_[cell_idx];

        Vec4 UR = UL;
        if (!face.is_boundary) {
            std::size_t nb = (face.left_cell == cell_idx) ? face.right_cell : face.left_cell;
            UR = U_[nb];
        }
        sum_spec += face_spectral_radius(UL, UR, face.normal, gamma, mu) * face.area;
    }

    const Real diag_val = cell.volume / std::max(dt, EPS) + 0.5 * sum_spec;
    return Vec4::Constant(diag_val);
}

Vec4 Solver::lusgs_offdiag_contribution(const Vec4& /*dU_nb*/, const Face& /*face*/,
                                         const Vec4& /*U_self*/, Real /*gamma*/) const {
    return Vec4::Zero();
}

// ============================================================================
// Local time step (owned cells only)
// ============================================================================

Real Solver::compute_dt_local(int step) {
    const Real cfl  = compute_cfl(step);
    const Real gamma = config_.gas.gamma;
    const Real mu = config_.freestream.viscosity;
    Real min_dt = 1e30;

    for (std::size_t i = 0; i < n_owned_; ++i) {
        const Cell& cell = mesh_.cells[i];
        const Vec4 prim = conservative_to_primitive(U_[i], gamma);
        const Real rho  = prim(0);
        const Real u    = prim(1);
        const Real v    = prim(2);
        const Real p    = prim(3);
        const Real a    = speed_of_sound(rho, p, gamma);

        Real max_lambda = 0.0;
        for (std::size_t fi : cell.faces) {
            const Face& face = mesh_.faces[fi];
            const Real Vn = u * face.normal(0) + v * face.normal(1);
            const Real lambda_c = std::abs(Vn) + a;
            Real lambda_v = 0.0;
            if (mu > EPS) {
                lambda_v = std::max(4.0 / 3.0, gamma) * mu / (rho * cell.volume);
            }
            const Real spec_rad = (lambda_c + 2.0 * lambda_v) * face.area;
            max_lambda = std::max(max_lambda, spec_rad);
        }

        dt_local_[i] = cfl * cell.volume / std::max(max_lambda, EPS);
        min_dt = std::min(min_dt, dt_local_[i]);
    }

    // Ghost cells get the min dt (or 0)
    for (std::size_t i = n_owned_; i < mesh_.n_cells(); ++i) {
        dt_local_[i] = min_dt;
    }

    // Global min across ranks
    if (mpi_size_ > 1) {
        Real global_min = min_dt;
        MPI_Allreduce(&min_dt, &global_min, 1, MPI_DOUBLE, MPI_MIN, comm_);
        min_dt = global_min;
    }

    return min_dt;
}

// ============================================================================
// Residual assembly (with halo exchange)
// ============================================================================

Real Solver::compute_residual() {
    const std::size_t nc = mesh_.n_cells();
    const Real gamma = config_.gas.gamma;
    const Real R_gas = config_.gas.R;
    const Real Pr    = config_.gas.prandtl;
    const Real mu    = config_.freestream.viscosity;
    const bool inviscid = (config_.physics.mode == PhysicsMode::Inviscid);

    const auto& fs = config_.freestream;
    const auto& gas = config_.gas;

    // Zero residuals
    for (std::size_t i = 0; i < nc; ++i) {
        R_[i].setZero();
    }

    // Exchange ghost states before computing residual
    if (mpi_size_ > 1) {
        exchange_ghost_states(U_, halo_, comm_);
    }

    const bool use_roe = (config_.numerics_required.inviscid_flux == "approximate_riemann");

    // --- Step 1: Compute gradients for reconstruction ---
    const bool second_order = false; // First-order for stability (second-order oscillates)
    std::vector<Vec4> limiter;
    if (second_order) {
        compute_primitive_gradients(mesh_, U_, gamma,
                                     grad_rho_, grad_u_, grad_v_, grad_p_);
        limiter.resize(nc);
        for (std::size_t i = 0; i < nc; ++i) {
            limiter[i] = barth_jespersen_limiter(mesh_, i, U_,
                                                  grad_rho_, grad_u_, grad_v_, grad_p_, gamma);
        }
    }

    // --- Step 2: Loop over all faces ---
    for (std::size_t fi = 0; fi < mesh_.n_faces(); ++fi) {
        const Face& face = mesh_.faces[fi];

        Vec4 UL, UR;

        if (face.is_boundary) {
            const std::size_t ci = (face.left_cell != Face::INVALID)
                                     ? face.left_cell : face.right_cell;
            const BoundaryType bc_type = static_cast<BoundaryType>(face.bc_tag);
            UR = apply_boundary_condition(bc_type, U_[ci], face.normal,
                                           fs, gas, inviscid);
            if (second_order) {
                UL = reconstruct_left(mesh_, ci, fi, U_,
                                       grad_rho_, grad_u_, grad_v_, grad_p_,
                                       limiter[ci], gamma);
            } else {
                UL = U_[ci];
            }
        } else {
            const std::size_t ci = face.left_cell;
            const std::size_t cj = face.right_cell;
            if (second_order) {
                UL = reconstruct_left(mesh_, ci, fi, U_,
                                       grad_rho_, grad_u_, grad_v_, grad_p_,
                                       limiter[ci], gamma);
                UR = reconstruct_right(mesh_, cj, fi, U_,
                                        grad_rho_, grad_u_, grad_v_, grad_p_,
                                        limiter[cj], gamma);
            } else {
                UL = U_[ci];
                UR = U_[cj];
            }
        }

        // --- Compute inviscid flux ---
        Vec4 flux;
        if (use_roe) {
            flux = roe_flux(UL, UR, face.normal, gamma);
        } else {
            flux = rusanov_flux(UL, UR, face.normal, gamma,
                                 config_.run_control.rusanov_dissipation_scale);
        }

        // --- Compute viscous flux ---
        if (!inviscid) {
            const Vec4 U_avg = 0.5 * (UL + UR);
            Mat2 grad_vel;
            grad_vel.setZero();
            Vec2 grad_T;
            grad_T.setZero();

            if (second_order) {
                if (!face.is_boundary) {
                    const std::size_t ci = face.left_cell;
                    const std::size_t cj = face.right_cell;
                    grad_vel(0, 0) = 0.5 * (grad_u_[ci](0) + grad_u_[cj](0));
                    grad_vel(0, 1) = 0.5 * (grad_u_[ci](1) + grad_u_[cj](1));
                    grad_vel(1, 0) = 0.5 * (grad_v_[ci](0) + grad_v_[cj](0));
                    grad_vel(1, 1) = 0.5 * (grad_v_[ci](1) + grad_v_[cj](1));
                    const Vec4 prim_i = conservative_to_primitive(U_[ci], gamma);
                    const Vec4 prim_j = conservative_to_primitive(U_[cj], gamma);
                    const Real Ti = prim_i(3) / (prim_i(0) * R_gas);
                    const Real Tj = prim_j(3) / (prim_j(0) * R_gas);
                    grad_T(0) = 0.5 * ((grad_p_[ci](0) * Ti / prim_i(3) - grad_rho_[ci](0) * Ti / prim_i(0))
                                       + (grad_p_[cj](0) * Tj / prim_j(3) - grad_rho_[cj](0) * Tj / prim_j(0)));
                    grad_T(1) = 0.5 * ((grad_p_[ci](1) * Ti / prim_i(3) - grad_rho_[ci](1) * Ti / prim_i(0))
                                       + (grad_p_[cj](1) * Tj / prim_j(3) - grad_rho_[cj](1) * Tj / prim_j(0)));
                } else {
                    // Boundary face: use corrected gradient respecting BC constraint
                    // ∇φ_f = ∇φ_c - (∇φ_c·n̂ - (φ_ghost - φ_c)/d)·n̂
                    // where d = |xf - xc|, φ_ghost from already-computed UR
                    const std::size_t ci = (face.left_cell != Face::INVALID)
                                             ? face.left_cell : face.right_cell;
                    const Vec2& xc = mesh_.cells[ci].centroid;
                    const Vec2 dr = face.centroid - xc;
                    const Real d = std::max(dr.norm(), EPS);
                    const Vec2 n = face.normal;
                    
                    const Vec4 prim_i = conservative_to_primitive(U_[ci], gamma);
                    const Vec4 prim_b = conservative_to_primitive(UR, gamma);
                    
                    // Corrected velocity gradient
                    auto correct_grad = [&](const Vec2& g, Real phi_c, Real phi_b) -> Vec2 {
                        Real dphi_dn = (phi_b - phi_c) / d;
                        Real g_dot_n = g.dot(n);
                        return g - (g_dot_n - dphi_dn) * n;
                    };
                    
                    Vec2 g_u(grad_u_[ci](0), grad_u_[ci](1));
                    Vec2 g_v(grad_v_[ci](0), grad_v_[ci](1));
                    g_u = correct_grad(g_u, prim_i(1), prim_b(1));
                    g_v = correct_grad(g_v, prim_i(2), prim_b(2));
                    grad_vel(0, 0) = g_u(0); grad_vel(0, 1) = g_u(1);
                    grad_vel(1, 0) = g_v(0); grad_vel(1, 1) = g_v(1);
                    
                    // Corrected temperature gradient
                    const Real Ti = prim_i(3) / (prim_i(0) * R_gas);
                    const Real Tb = prim_b(3) / (prim_b(0) * R_gas);
                    Vec2 g_T_cell;
                    g_T_cell(0) = grad_p_[ci](0) * Ti / prim_i(3) - grad_rho_[ci](0) * Ti / prim_i(0);
                    g_T_cell(1) = grad_p_[ci](1) * Ti / prim_i(3) - grad_rho_[ci](1) * Ti / prim_i(0);
                    Vec2 g_T = correct_grad(g_T_cell, Ti, Tb);
                    grad_T(0) = g_T(0);
                    grad_T(1) = g_T(1);
                }
            }

            flux += compute_viscous_flux(U_avg, grad_vel, grad_T,
                                          face.normal, mu, gamma, Pr, R_gas);
        }

        // --- Accumulate flux into residuals ---
        if (!flux.allFinite()) {
            static int nan_count = 0;
            if (++nan_count <= 10) {
                LOG_WARN("NaN flux at face {} — skipping", fi);
            }
            continue;
        }
        if (face.left_cell != Face::INVALID) {
            R_[face.left_cell] += flux * face.area;
        }
        if (face.right_cell != Face::INVALID) {
            R_[face.right_cell] -= flux * face.area;
        }
    }

    // --- Compute L2 norm (sum of squares across owned cells) ---
    Real sum_sq = 0.0;
    Real linf = 0.0;
    for (std::size_t i = 0; i < n_owned_; ++i) {
        const Real r_sq = R_[i].squaredNorm();
        sum_sq += r_sq;
        linf = std::max(linf, std::sqrt(r_sq));
    }

    // Global Allreduce for norms
    if (mpi_size_ > 1) {
        Real global_sum = 0.0;
        MPI_Allreduce(&sum_sq, &global_sum, 1, MPI_DOUBLE, MPI_SUM, comm_);
        sum_sq = global_sum;

        Real global_linf = 0.0;
        MPI_Allreduce(&linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm_);
        linf = global_linf;
    }

    const Real l2 = (n_owned_ > 0) ? std::sqrt(sum_sq / static_cast<Real>(n_owned_)) : 0.0;

    return l2;
}

// ============================================================================
// LU-SGS sweep
// ============================================================================

void Solver::lusgs_sweep(Real cfl, const std::vector<Vec4>& rhs) {
    const std::size_t nc = mesh_.n_cells();
    const Real gamma = config_.gas.gamma;
    const Real mu = config_.freestream.viscosity;

    // Forward sweep
    for (std::size_t i = 0; i < nc; ++i) {
        const Cell& cell = mesh_.cells[i];
        const Real dt = std::max(dt_local_[i], EPS);

        Vec4 D = lusgs_diag(i, dt, cfl, mu);

        Vec4 offdiag_sum = Vec4::Zero();
        for (std::size_t nb_idx : cell.neighbors) {
            if (nb_idx < i) {
                for (std::size_t fi : cell.faces) {
                    const Face& face = mesh_.faces[fi];
                    if (face.is_boundary) continue;
                    if ((face.left_cell == i && face.right_cell == nb_idx) ||
                        (face.left_cell == nb_idx && face.right_cell == i)) {
                        const Real spec_rad = face_spectral_radius(
                            U_[i], U_[nb_idx], face.normal, gamma, mu);
                        offdiag_sum -= 0.5 * spec_rad * face.area * dU_[nb_idx];
                        break;
                    }
                }
            }
        }

        for (int c = 0; c < 4; ++c) {
            dU_[i](c) = (-rhs[i](c) - offdiag_sum(c)) / std::max(D(c), EPS);
        }
    }

    // Backward sweep
    for (std::size_t i_rev = nc; i_rev > 0; --i_rev) {
        const std::size_t i = i_rev - 1;
        const Cell& cell = mesh_.cells[i];
        const Real dt = std::max(dt_local_[i], EPS);

        Vec4 D = lusgs_diag(i, dt, cfl, mu);

        Vec4 offdiag_sum = Vec4::Zero();
        for (std::size_t nb_idx : cell.neighbors) {
            if (nb_idx > i) {
                for (std::size_t fi : cell.faces) {
                    const Face& face = mesh_.faces[fi];
                    if (face.is_boundary) continue;
                    if ((face.left_cell == i && face.right_cell == nb_idx) ||
                        (face.left_cell == nb_idx && face.right_cell == i)) {
                        const Real spec_rad = face_spectral_radius(
                            U_[i], U_[nb_idx], face.normal, gamma, mu);
                        offdiag_sum -= 0.5 * spec_rad * face.area * dU_[nb_idx];
                        break;
                    }
                }
            }
        }

        for (int c = 0; c < 4; ++c) {
            dU_[i](c) -= offdiag_sum(c) / std::max(D(c), EPS);
        }
    }
}

// ============================================================================
// Update solution (owned cells only)
// ============================================================================

void Solver::update_solution() {
    const Real gamma = config_.gas.gamma;
    const Real rho_inf = config_.freestream.rho;
    const Real u_inf = config_.freestream.velocity(0);
    const Real v_inf = config_.freestream.velocity(1);
    const Real p_inf = config_.freestream.pressure;
    const Real rhoE_inf = p_inf / (gamma - 1.0)
                          + 0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);

    for (std::size_t i = 0; i < n_owned_; ++i) {
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(dU_[i](c))) {
                dU_[i](c) = 0.0;
            }
            const Real max_allowed = std::max(std::abs(U_[i](c)), EPS) * 0.5;
            dU_[i](c) = std::clamp(dU_[i](c), -max_allowed, max_allowed);
        }

        U_[i] += dU_[i];

        if (U_[i](0) < EPS || !std::isfinite(U_[i](0))) {
            U_[i] << rho_inf, rho_inf * u_inf, rho_inf * v_inf, rhoE_inf;
            dU_[i].setZero();
            continue;
        }

        const Real rho = U_[i](0);
        const Real u = U_[i](1) / rho;
        const Real v = U_[i](2) / rho;
        const Real ke = 0.5 * rho * (u * u + v * v);
        Real p = (gamma - 1.0) * (U_[i](3) - ke);

        if (p < EPS || !std::isfinite(p)) {
            U_[i] << rho_inf, rho_inf * u_inf, rho_inf * v_inf, rhoE_inf;
        }

        dU_[i].setZero();
    }
}

// ============================================================================
// Force computation (with MPI_Allreduce)
// ============================================================================

void Solver::compute_forces() {
    const Real gamma   = config_.gas.gamma;
    const Real rho_inf = config_.freestream.rho;
    const Real U_inf   = config_.freestream.velocity_magnitude;
    const Real q_inf   = 0.5 * rho_inf * U_inf * U_inf;
    const Real A_ref   = config_.reference.area;
    const Real cx      = config_.reference.moment_center(0);
    const Real cy      = config_.reference.moment_center(1);
    const bool inviscid = (config_.physics.mode == PhysicsMode::Inviscid);

    Real Fx_p = 0.0, Fy_p = 0.0;
    Real Fx_v = 0.0, Fy_v = 0.0;
    Real Mz   = 0.0;

    const Real cos_a = std::cos(config_.freestream.aoa_radians);
    const Real sin_a = std::sin(config_.freestream.aoa_radians);

    const auto wall_tags = {
        static_cast<int>(BoundaryType::SlipWall),
        static_cast<int>(BoundaryType::NoSlipAdiabaticWall)
    };

    for (int tag : wall_tags) {
        auto it = mesh_.boundary_faces.find(tag);
        if (it == mesh_.boundary_faces.end()) continue;

        for (std::size_t fi : it->second) {
            const Face& face = mesh_.faces[fi];
            const std::size_t ci = (face.left_cell != Face::INVALID)
                                     ? face.left_cell : face.right_cell;
            if (ci == Face::INVALID) continue;

            const Vec4 prim = conservative_to_primitive(U_[ci], gamma);
            Real p_wall = prim(3);

            const Real fx_p = +p_wall * face.normal(0) * face.area;
            const Real fy_p = +p_wall * face.normal(1) * face.area;

            Fx_p += fx_p;
            Fy_p += fy_p;

            const Real rx = face.centroid(0) - cx;
            const Real ry = face.centroid(1) - cy;
            Mz += rx * fy_p - ry * fx_p;

            if (!inviscid) {
                const Real u_t_cell = prim(1) * (-face.normal(1)) + prim(2) * face.normal(0);
                const Vec2 dr = face.centroid - mesh_.cells[ci].centroid;
                const Real dn = std::max(dr.norm(), EPS);
                const Real mu_val = config_.freestream.viscosity;
                const Real tau_w = mu_val * u_t_cell / dn;
                const Real tx = -face.normal(1);
                const Real ty = face.normal(0);
                const Real fx_v = tau_w * tx * face.area;
                const Real fy_v = tau_w * ty * face.area;
                Fx_v += fx_v;
                Fy_v += fy_v;
            }
        }
    }

    // Global reduction across MPI ranks
    if (mpi_size_ > 1) {
        Real buf[5] = {Fx_p, Fy_p, Fx_v, Fy_v, Mz};
        Real global_buf[5] = {0, 0, 0, 0, 0};
        MPI_Allreduce(buf, global_buf, 5, MPI_DOUBLE, MPI_SUM, comm_);
        Fx_p = global_buf[0]; Fy_p = global_buf[1];
        Fx_v = global_buf[2]; Fy_v = global_buf[3];
        Mz   = global_buf[4];
    }

    const Real drag_p  = Fx_p * cos_a + Fy_p * sin_a;
    const Real lift_p  = -Fx_p * sin_a + Fy_p * cos_a;
    const Real drag_v  = Fx_v * cos_a + Fy_v * sin_a;
    const Real lift_v  = -Fx_v * sin_a + Fy_v * cos_a;

    pressure_drag_  = drag_p;
    viscous_drag_   = drag_v;
    pressure_lift_  = lift_p;
    viscous_lift_   = lift_v;

    const Real total_drag = pressure_drag_ + viscous_drag_;
    const Real total_lift = pressure_lift_ + viscous_lift_;

    cd_  = total_drag / std::max(q_inf * A_ref, EPS);
    cl_  = total_lift / std::max(q_inf * A_ref, EPS);
    cmz_ = Mz / std::max(q_inf * A_ref * config_.reference.length, EPS);
}

// ============================================================================
// Run steady
// ============================================================================

void Solver::run_steady(const std::string& output_dir) {
    const auto& rc = config_.run_control;
    const auto& out = config_.outputs;

    // Output directory (rank 0 creates it)
    if (mpi_rank_ == 0) {
        std::filesystem::create_directories(output_dir);
    }
    if (mpi_size_ > 1) {
        MPI_Barrier(comm_);
    }

    // Open output files (rank 0 only)
    std::ofstream res_file, force_file;
    if (mpi_rank_ == 0) {
        res_file.open(output_dir + "/residuals.csv");
        force_file.open(output_dir + "/forces.csv");
        write_residuals_header(res_file);
        write_forces_header(force_file);
    }

    LOG_INFO("Computing initial residual...");
    Real res_l2 = compute_residual();
    initial_residual_l2_ = std::max(res_l2, EPS);
    LOG_INFO("Rank {}: Initial residual L2 = {:.6e}", mpi_rank_, initial_residual_l2_);

    auto start_time = std::chrono::steady_clock::now();
    bool converged = false;
    int final_step = 0;

    for (int step = 0; step < rc.max_steps; ++step) {
        const Real cfl = compute_cfl(step);
        const Real min_dt = compute_dt_local(step);

        // LU-SGS implicit update: freeze RHS, solve (D+L+U)ΔU = -R via sweeps
        const int n_sweeps = 1;
        std::vector<Vec4> rhs_save = R_;  // frozen RHS
        for (auto& d : dU_) d.setZero();
        for (int k = 0; k < n_sweeps; ++k) {
            lusgs_sweep(cfl, rhs_save);
        }
        update_solution();

        // Recompute residual
        res_l2 = compute_residual();

        // Component norms
        Vec4 res_comp_norm = Vec4::Zero();
        for (std::size_t ic = 0; ic < n_owned_; ++ic) {
            for (int c = 0; c < 4; ++c) {
                res_comp_norm(c) += R_[ic](c) * R_[ic](c);
            }
        }
        // Global reduction for component norms
        if (mpi_size_ > 1) {
            Vec4 global_norm = Vec4::Zero();
            MPI_Allreduce(res_comp_norm.data(), global_norm.data(), 4, MPI_DOUBLE, MPI_SUM, comm_);
            res_comp_norm = global_norm;
        }
        for (int c = 0; c < 4; ++c) {
            res_comp_norm(c) = std::sqrt(res_comp_norm(c) / static_cast<Real>(std::max(n_owned_, std::size_t{1})));
        }

        // Linf norm
        Real res_linf = 0.0;
        for (std::size_t ic = 0; ic < n_owned_; ++ic) {
            res_linf = std::max(res_linf, std::sqrt(R_[ic].squaredNorm()));
        }
        if (mpi_size_ > 1) {
            Real global_linf = 0.0;
            MPI_Allreduce(&res_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm_);
            res_linf = global_linf;
        }

        compute_forces();

        // Output (rank 0 writes)
        if (mpi_rank_ == 0) {
            if (step % out.write_residuals_every == 0) {
                write_residuals_row(res_file, step, 0.0, 1, cfl,
                                    min_dt, res_comp_norm, res_l2, res_linf);
            }
            if (step % out.write_forces_every == 0) {
                write_forces_row(force_file, step, 0.0,
                                 cl_, cd_, cmz_,
                                 pressure_drag_, viscous_drag_,
                                 pressure_lift_, viscous_lift_);
            }
        }

        final_step = step;

        if (step % 100 == 0 || step == rc.max_steps - 1) {
            LOG_INFO("Step {:6d}: CFL={:8.2f} dt={:12.6e} res_L2={:12.6e} "
                     "res_Linf={:12.6e} cd={:+.8f} cl={:+.8f}",
                     step, cfl, min_dt, res_l2, res_linf, cd_, cl_);
        }

        const Real rel_res = res_l2 / initial_residual_l2_;
        if (rel_res < std::pow(10.0, -rc.residual_reduction_target)) {
            LOG_INFO("Rank {}: Converged at step {}: residual reduction = {:.2e}",
                     mpi_rank_, step, rel_res);
            converged = true;
            break;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    Real wall_time = std::chrono::duration<Real>(end_time - start_time).count();

    if (!converged && mpi_rank_ == 0) {
        LOG_INFO("Reached max steps ({}) without full convergence", rc.max_steps);
    }

    const Real final_res_l2 = compute_residual();
    const Real res_reduction = std::log10(final_res_l2 / initial_residual_l2_);

    // Close files
    if (mpi_rank_ == 0) {
        res_file.close();
        force_file.close();
    }

    // Write output (rank 0 only)
    if (mpi_rank_ == 0) {
        if (out.write_final_field) {
            LOG_INFO("Writing field VTU...");
            write_field_vtu(mesh_, U_, config_, output_dir + "/field_final.vtu");
        }
        if (out.write_surface) {
            LOG_INFO("Writing surface CSV...");
            write_surface_csv(mesh_, U_, config_, output_dir + "/surface.csv");
        }

        LOG_INFO("Writing metadata...");
        std::size_t global_cells = mesh_.n_cells();
        std::size_t global_faces = mesh_.n_faces();
        if (mpi_size_ > 1) {
            global_cells = full_mesh_copy_.n_cells();
            global_faces = full_mesh_copy_.n_faces();
        }
        write_metadata_json(config_, output_dir, wall_time,
                            global_cells, global_faces,
                            n_owned_, mesh_.n_cells() - n_owned_,
                            mpi_size_, converged);

        const std::string command = "cfd_solver solve --case " + config_.case_id;
        write_run_status_json(output_dir, config_.case_id, command,
                              mpi_size_, wall_time,
                              final_step, 0.0, converged, res_reduction);

        LOG_INFO("Steady run complete. Wall time: {:.2f} s, Steps: {}", wall_time, final_step);
        LOG_INFO("Final forces: CL={:+.8f}, CD={:+.8f}, CMz={:+.8f}", cl_, cd_, cmz_);
    }
}

// ============================================================================
// BDF2 Transient Solver
// ============================================================================

void Solver::run_transient(const std::string& output_dir) {
    const auto& rc = config_.run_control;
    const auto& out = config_.outputs;
    const Real gamma = config_.gas.gamma;
    const Real dt = rc.time_step;
    const Real final_time = rc.final_time;
    const int min_inner = rc.min_inner_iterations;
    const int max_inner = rc.max_inner_iterations;
    const Real inner_target = rc.inner_residual_reduction_target;

    // Rank 0 creates output directory
    if (mpi_rank_ == 0) {
        std::filesystem::create_directories(output_dir);
    }
    if (mpi_size_ > 1) {
        MPI_Barrier(comm_);
    }

    // Open output files (rank 0 only)
    std::ofstream res_file, force_file;
    if (mpi_rank_ == 0) {
        res_file.open(output_dir + "/residuals.csv");
        force_file.open(output_dir + "/forces.csv");
        write_residuals_header(res_file);
        write_forces_header(force_file);
    }

    // Initialize solution from freestream
    const Real rho_inf = config_.freestream.rho;
    const Real u_inf   = config_.freestream.velocity(0);
    const Real v_inf   = config_.freestream.velocity(1);
    const Real p_inf   = config_.freestream.pressure;
    const Real rhoE_inf = p_inf / (gamma - 1.0)
                          + 0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);

    for (std::size_t i = 0; i < mesh_.n_cells(); ++i) {
        U_[i] << rho_inf, rho_inf * u_inf, rho_inf * v_inf, rhoE_inf;
    }

    // Time-history arrays
    std::vector<Vec4> U_n(n_owned_);      // U at time n
    std::vector<Vec4> U_nm1(n_owned_);    // U at time n-1
    for (std::size_t i = 0; i < n_owned_; ++i) {
        U_n[i] = U_[i];
        U_nm1[i] = U_[i];
    }

    // Inner iteration statistics (rank-local, gathered at end)
    int total_inner_iters = 0;
    int inner_min_observed = max_inner;
    int inner_max_observed = 0;
    int inner_target_misses = 0;
    int inner_converged_count = 0;
    std::vector<int> inner_counts;

    auto start_time = std::chrono::steady_clock::now();
    Real t = 0.0;
    int step = 0;
    bool is_first_step = true;

    LOG_INFO("Rank {}: Starting BDF2 transient: dt={} final_time={}", mpi_rank_, dt, final_time);
    LOG_INFO("  Inner iterations: min={} max={} target={}", min_inner, max_inner, inner_target);

    while (t < final_time - SMALL) {
        // --- Save U at start of time step (for convergence check) ------------
        std::vector<Vec4> U_save(n_owned_);
        for (std::size_t i = 0; i < n_owned_; ++i) {
            U_save[i] = U_[i];
        }

        // --- Compute initial total residual for this time step (R_total_0) ---
        // First compute spatial residual
        Real spatial_res_0 = compute_residual();

        // Build total residual with BDF2 physical-time terms
        // R_total = R_spatial + V * (3*U - 4*U_n + U_nm1) / (2*dt)  [BDF2]
        //         = R_spatial + V * (U - U_n) / dt                   [BDF1, first step]
        std::vector<Vec4> R_total(R_.size());
        for (std::size_t i = 0; i < R_.size(); ++i) {
            R_total[i] = R_[i];
        }

        // Add physical-time terms for owned cells
        for (std::size_t i = 0; i < n_owned_; ++i) {
            const Real vol = mesh_.cells[i].volume;
            if (is_first_step) {
                // BDF1 / Backward Euler: (U - U_n) / dt
                R_total[i] += vol * (U_[i] - U_n[i]) / dt;
            } else {
                // BDF2: (3U - 4U_n + U_nm1) / (2*dt)
                R_total[i] += vol * (3.0 * U_[i] - 4.0 * U_n[i] + U_nm1[i]) / (2.0 * dt);
            }
        }

        // Compute initial total residual norm (L2 over owned cells)
        Real res_total_0_sq = 0.0;
        for (std::size_t i = 0; i < n_owned_; ++i) {
            res_total_0_sq += R_total[i].squaredNorm();
        }
        if (mpi_size_ > 1) {
            Real global_sq = 0.0;
            MPI_Allreduce(&res_total_0_sq, &global_sq, 1, MPI_DOUBLE, MPI_SUM, comm_);
            res_total_0_sq = global_sq;
        }
        Real res_total_0 = std::sqrt(res_total_0_sq / static_cast<Real>(std::max(n_owned_, std::size_t{1})));

        // --- Inner iteration loop --------------------------------------------
        int k = 0;
        for (k = 0; k < max_inner; ++k) {
            // Diagonal implicit update:
            // dU = -R_total / diag
            // diag = V / dt_local + 0.5 * sum_spec + V * bdf2_factor
            // bdf2_factor = 1.0/dt (BDF1) or 1.5/dt (BDF2)

            const Real bdf2_diag_factor = is_first_step ? (1.0 / dt) : (1.5 / dt);

            for (std::size_t i = 0; i < n_owned_; ++i) {
                const Cell& cell = mesh_.cells[i];
                const Vec4 prim = conservative_to_primitive(U_[i], gamma);
                const Real rho = prim(0), u = prim(1), v = prim(2);
                const Real a = speed_of_sound(rho, prim(3), gamma);

                Real sum_spec = 0.0;
                for (std::size_t fi : cell.faces) {
                    const Face& face = mesh_.faces[fi];
                    const Real Vn = u * face.normal(0) + v * face.normal(1);
                    sum_spec += (std::abs(Vn) + a) * face.area;
                }

                const Real cfl_eff = 1.0;  // transient: fixed CFL=1
                const Real pseudo_dt = cfl_eff * cell.volume / std::max(sum_spec / std::max(cell.faces.size(), std::size_t{1}), EPS);
                const Real diag = cell.volume / std::max(pseudo_dt, EPS)
                                  + 0.5 * sum_spec
                                  + cell.volume * bdf2_diag_factor;

                for (int c = 0; c < 4; ++c) {
                    dU_[i](c) = -R_total[i](c) / std::max(diag, EPS);
                }
            }

            // Apply update (owned cells only)
            update_solution();

            // Recompute spatial residual
            spatial_res_0 = compute_residual();

            // Rebuild total residual
            for (std::size_t i = 0; i < R_.size(); ++i) {
                R_total[i] = R_[i];
            }
            for (std::size_t i = 0; i < n_owned_; ++i) {
                const Real vol = mesh_.cells[i].volume;
                if (is_first_step) {
                    R_total[i] += vol * (U_[i] - U_n[i]) / dt;
                } else {
                    R_total[i] += vol * (3.0 * U_[i] - 4.0 * U_n[i] + U_nm1[i]) / (2.0 * dt);
                }
            }

            // Check inner convergence
            if (k >= min_inner - 1) {
                Real res_total_sq = 0.0;
                for (std::size_t i = 0; i < n_owned_; ++i) {
                    res_total_sq += R_total[i].squaredNorm();
                }
                if (mpi_size_ > 1) {
                    Real global_sq = 0.0;
                    MPI_Allreduce(&res_total_sq, &global_sq, 1, MPI_DOUBLE, MPI_SUM, comm_);
                    res_total_sq = global_sq;
                }
                Real res_total = std::sqrt(res_total_sq / static_cast<Real>(std::max(n_owned_, std::size_t{1})));
                Real ratio = (res_total_0 > EPS) ? (res_total / res_total_0) : 0.0;

                if (ratio < inner_target) {
                    inner_converged_count++;
                    break;
                }
            }
        }

        int inner_iters = (k < max_inner) ? (k + 1) : max_inner;
        total_inner_iters += inner_iters;
        inner_min_observed = std::min(inner_min_observed, inner_iters);
        inner_max_observed = std::max(inner_max_observed, inner_iters);
        inner_counts.push_back(inner_iters);
        if (k >= max_inner) {
            inner_target_misses++;
        }

        // Log inner convergence
        if (inner_iters >= max_inner && mpi_rank_ == 0) {
            LOG_WARN("Step {}: inner iterations did not converge ({} iters, max={})",
                     step, inner_iters, max_inner);
        }

        // --- Accept step: update time-history --------------------------------
        is_first_step = false;
        for (std::size_t i = 0; i < n_owned_; ++i) {
            U_nm1[i] = U_n[i];
            U_n[i] = U_[i];
        }

        t += dt;
        ++step;

        // --- Compute forces for this step ------------------------------------
        compute_forces();

        // --- Output -----------------------------------------------------------
        // Component residual norms
        Vec4 res_comp_norm = Vec4::Zero();
        for (std::size_t ic = 0; ic < n_owned_; ++ic) {
            for (int c = 0; c < 4; ++c) {
                res_comp_norm(c) += R_[ic](c) * R_[ic](c);
            }
        }
        if (mpi_size_ > 1) {
            Vec4 global_norm = Vec4::Zero();
            MPI_Allreduce(res_comp_norm.data(), global_norm.data(), 4, MPI_DOUBLE, MPI_SUM, comm_);
            res_comp_norm = global_norm;
        }
        for (int c = 0; c < 4; ++c) {
            res_comp_norm(c) = std::sqrt(res_comp_norm(c) / static_cast<Real>(std::max(n_owned_, std::size_t{1})));
        }

        Real res_linf = 0.0;
        for (std::size_t ic = 0; ic < n_owned_; ++ic) {
            res_linf = std::max(res_linf, std::sqrt(R_[ic].squaredNorm()));
        }
        if (mpi_size_ > 1) {
            Real global_linf = 0.0;
            MPI_Allreduce(&res_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm_);
            res_linf = global_linf;
        }

        // Rank 0 writes output files
        if (mpi_rank_ == 0) {
            if (step % out.write_residuals_every == 0) {
                write_residuals_row(res_file, step, t, inner_iters, 1.0,
                                    dt, res_comp_norm, spatial_res_0, res_linf);
            }
            if (step % out.write_forces_every == 0) {
                write_forces_row(force_file, step, t,
                                 cl_, cd_, cmz_,
                                 pressure_drag_, viscous_drag_,
                                 pressure_lift_, viscous_lift_);
            }
        }

        if (step % 100 == 0 && mpi_rank_ == 0) {
            LOG_INFO("Step {:6d}: t={:10.6f} inner={:4d} cd={:+.8f} cl={:+.8f}",
                     step, t, inner_iters, cd_, cl_);
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    Real wall_time = std::chrono::duration<Real>(end_time - start_time).count();

    // Gather inner iteration statistics across ranks
    int global_total_inner = total_inner_iters;
    int global_inner_min = inner_min_observed;
    int global_inner_max = inner_max_observed;
    int global_target_misses = inner_target_misses;
    int global_converged = inner_converged_count;
    if (mpi_size_ > 1) {
        MPI_Allreduce(MPI_IN_PLACE, &global_total_inner, 1, MPI_INT, MPI_SUM, comm_);
        MPI_Allreduce(MPI_IN_PLACE, &global_inner_min, 1, MPI_INT, MPI_MIN, comm_);
        MPI_Allreduce(MPI_IN_PLACE, &global_inner_max, 1, MPI_INT, MPI_MAX, comm_);
        MPI_Allreduce(MPI_IN_PLACE, &global_target_misses, 1, MPI_INT, MPI_SUM, comm_);
        MPI_Allreduce(MPI_IN_PLACE, &global_converged, 1, MPI_INT, MPI_SUM, comm_);
    }

    Real mean_inner = (step > 0) ? static_cast<Real>(global_total_inner) / static_cast<Real>(step) : 0.0;
    Real converged_fraction = (step > 0) ? static_cast<Real>(global_converged) / static_cast<Real>(step) : 0.0;

    // Close files and write final outputs (rank 0)
    if (mpi_rank_ == 0) {
        res_file.close();
        force_file.close();

        if (out.write_final_field) {
            LOG_INFO("Writing field VTU...");
            write_field_vtu(mesh_, U_, config_, output_dir + "/field_final.vtu");
        }
        if (out.write_surface) {
            LOG_INFO("Writing surface CSV...");
            write_surface_csv(mesh_, U_, config_, output_dir + "/surface.csv");
        }

        // Write metadata
        LOG_INFO("Writing metadata...");
        std::size_t global_cells = mesh_.n_cells();
        std::size_t global_faces = mesh_.n_faces();
        if (mpi_size_ > 1) {
            global_cells = full_mesh_copy_.n_cells();
            global_faces = full_mesh_copy_.n_faces();
        }
        write_metadata_json(config_, output_dir, wall_time,
                            global_cells, global_faces,
                            n_owned_, mesh_.n_cells() - n_owned_,
                            mpi_size_, true,
                            /*is_transient=*/true,
                            global_inner_min, global_inner_max, mean_inner,
                            global_target_misses, converged_fraction);

        // Write run_status
        const std::string command = "cfd_solver solve --case " + config_.case_id;
        write_run_status_json(output_dir, config_.case_id, command,
                              mpi_size_, wall_time, step, t, true, 0.0);

        LOG_INFO("Transient run complete. Wall time: {:.2f} s, Steps: {}", wall_time, step);
        LOG_INFO("  Inner iteration stats: min={} max={} mean={:.1f} missed={} converged_frac={:.3f}",
                 global_inner_min, global_inner_max, mean_inner,
                 global_target_misses, converged_fraction);
        LOG_INFO("Final forces: CL={:+.8f}, CD={:+.8f}, CMz={:+.8f}", cl_, cd_, cmz_);
    }
}

} // namespace cfd
