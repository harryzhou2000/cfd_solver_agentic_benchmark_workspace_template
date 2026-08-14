#include "time_stepper.hpp"
#include "solver/residual.hpp"
#include "partition/halo_exchange.hpp"
#include "output/csv_writer.hpp"
#include "output/vtk_writer.hpp"
#include "output/metadata.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <vector>
#include <fstream>
#include <sstream>

static StateVector U_inf_static;
static GasModel  gas_static;
static Vec2  u_inf_vec;

static Real compute_cfl(const CaseConfig& cfg, int step) {
    if (cfg.pseudo_cfl_ramp_steps > 0 && step < cfg.pseudo_cfl_ramp_steps) {
        Real frac = (Real)step / cfg.pseudo_cfl_ramp_steps;
        return cfg.cfl_initial + frac * (cfg.cfl_max - cfg.cfl_initial);
    }
    return cfg.cfl_max;
}

// Compute forces on wall faces
static void compute_forces(const RankMesh& rm, const GasModel& gas, const CaseConfig& cfg,
                           Real& cl, Real& cd, Real& cmz,
                           Real& p_drag, Real& v_drag, Real& p_lift, Real& v_lift) {
    Real dynamic_pressure = 0.5 * cfg.rho_inf * cfg.velocity_magnitude * cfg.velocity_magnitude;
    Real ref_area = cfg.ref_area;
    Real ref_length = cfg.ref_length;
    
    Real total_pdrag = 0, total_vdrag = 0, total_plift = 0, total_vlift = 0, total_mz = 0;
    
    const auto& faces = rm.internal_faces;
    
    for (size_t fi = 0; fi < faces.size(); fi++) {
        const Face& face = faces[fi];
        if (face.right >= 0) continue; // not a boundary face
        
        if (face.bc_type != BCType::SlipWall && face.bc_type != BCType::NoSlipAdiabaticWall)
            continue;
        
        if (face.left < 0 || face.left >= (Index)rm.owned_state.size()) continue;
        
        const StateVector& U = rm.owned_state[face.left];
        Real p = gas.pressure(U);
        Real rho = U[0];
        Real u = U[1] / std::max(rho, 1e-14);
        Real v = U[2] / std::max(rho, 1e-14);
        
        // Pressure force: p * n * area (acts on wall boundary)
        // For inviscid, viscous = 0
        Vec2 force = p * face.normal * face.area;
        
        // Tangential force (skin friction) for viscous
        Real tau_wall = 0.0; // placeholder for viscous
        
        Vec2 drag_vector(1, 0); // drag in x direction
        Vec2 lift_vector(0, 1); // lift in y direction
        
        Real p_drag_contrib = force.dot(drag_vector);
        Real p_lift_contrib = force.dot(lift_vector);
        
        total_pdrag += p_drag_contrib;
        total_plift += p_lift_contrib;
        
        // Moment about reference center
        Vec2 r_vec = face.centroid - cfg.moment_center;
        Real moment_contrib = r_vec.x() * force.y() - r_vec.y() * force.x();
        total_mz += moment_contrib;
    }
    
    // Global reduction
    MPI_Allreduce(MPI_IN_PLACE, &total_pdrag, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, &total_plift, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, &total_mz, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    
    p_drag = total_pdrag / (dynamic_pressure * ref_area);
    p_lift = total_plift / (dynamic_pressure * ref_area);
    v_drag = 0.0;
    v_lift = 0.0;
    cd = p_drag + v_drag;
    cl = p_lift + v_lift;
    cmz = total_mz / (dynamic_pressure * ref_area * ref_length);
}

void steady_march(RankMesh& rm, const CaseConfig& cfg, const GasModel& gas) {
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    auto t_start = std::chrono::high_resolution_clock::now();
    int n_owned = (int)rm.owned_state.size();
    const auto& faces = rm.internal_faces;
    int n_faces = (int)faces.size();
    
    U_inf_static = rm.owned_state[0];
    gas_static = gas;
    u_inf_vec = Vec2(cfg.u_inf, cfg.v_inf);
    
    std::vector<Real> cell_diag(n_owned, 0.0);
    Real initial_res_norm = 0.0;
    int inner_iters = cfg.min_inner_iterations;
    
    // Output buffers
    std::vector<int> res_steps;
    std::vector<Real> res_times, res_cfls, res_dts, res_l2, res_linf;
    std::vector<StateVector> res_comp;
    
    std::vector<int> force_steps;
    std::vector<Real> force_times, force_cl, force_cd, force_cmz;
    std::vector<Real> force_pd, force_vd, force_pl, force_vl;
    
    for (int step = 0; step < cfg.max_steps; step++) {
        exchange_halo(rm, MPI_COMM_WORLD);
        
        Real cfl = compute_cfl(cfg, step);
        
        for (int inner = 0; inner < inner_iters; inner++) {
            compute_residual(rm, gas, cfg);
            
            // Face-based diagonal Jacobian
            std::fill(cell_diag.begin(), cell_diag.end(), 0.0);
            for (int fi = 0; fi < n_faces; fi++) {
                const Face& face = faces[fi];
                PrimVector WL, WR;
                if (face.left >= 0 && face.left < n_owned) {
                    WL = gas.conservative_to_primitive(rm.owned_state[face.left]);
                } else continue;
                
                if (face.right >= 0 && face.right < n_owned) {
                    WR = gas.conservative_to_primitive(rm.owned_state[face.right]);
                } else if (face.right < 0) { WR = WL; }
                else continue;
                
                Real aL = std::sqrt(gas.gamma * WL[3] / std::max(WL[0], 1e-14));
                Real aR = std::sqrt(gas.gamma * WR[3] / std::max(WR[0], 1e-14));
                Real lL = std::abs(WL[1]*face.normal.x() + WL[2]*face.normal.y()) + aL;
                Real lR = std::abs(WR[1]*face.normal.x() + WR[2]*face.normal.y()) + aR;
                
                if (face.left >= 0 && face.left < n_owned) cell_diag[face.left] += lL * face.area;
                if (face.right >= 0 && face.right < n_owned) cell_diag[face.right] += lR * face.area;
            }
            
            // Point-implicit update
            for (int i = 0; i < n_owned; i++) {
                Real vol = rm.owned_cells[i].volume;
                if (vol <= 1e-20) continue;
                StateVector& U = rm.owned_state[i];
                StateVector& R = rm.owned_residual[i];
                Real diag = cell_diag[i];
                Real dt = (diag > 1e-14) ? cfl * vol / diag : cfl * std::sqrt(vol);
                Real denom = std::max(vol / dt + diag, 1e-14);
                for (int k = 0; k < NCONS; k++) U[k] -= R[k] / denom;
                if (!std::isfinite(U[0]) || U[0] < 1e-10) U = U_inf_static;
                else if (!std::isfinite(U[3])) U = U_inf_static;
                else {
                    Real p = gas.pressure(U);
                    if (!std::isfinite(p) || p < 1e-10) U[3] += std::abs(p) + 1.0;
                    if (U[0] < 1e-10) U[0] = 1e-10;
                }
            }
        }
        
        // Final residual
        compute_residual(rm, gas, cfg);
        
        Real l2 = 0.0, linf = 0.0;
        for (int i = 0; i < n_owned; i++) {
            l2 += rm.owned_residual[i].squaredNorm();
            linf = std::max(linf, rm.owned_residual[i].cwiseAbs().maxCoeff());
        }
        Real gl2 = 0.0, glinf = 0.0;
        MPI_Allreduce(&l2, &gl2, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&linf, &glinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        gl2 = std::sqrt(gl2 / n_owned);
        
        if (step == 0) initial_res_norm = gl2;
        
        // Record residuals
        if (step % cfg.write_residuals_every == 0) {
            res_steps.push_back(step);
            res_times.push_back(0.0);
            res_cfls.push_back(cfl);
            res_dts.push_back(0.0);
            res_l2.push_back(gl2);
            res_linf.push_back(glinf);
            res_comp.push_back(StateVector::Zero()); // per-component not stored yet
        }
        
        // Compute and record forces
        if (step % cfg.write_forces_every == 0) {
            Real cl, cd, cmz, pd, vd, pl, vl;
            compute_forces(rm, gas, cfg, cl, cd, cmz, pd, vd, pl, vl);
            force_steps.push_back(step);
            force_times.push_back(0.0);
            force_cl.push_back(cl); force_cd.push_back(cd); force_cmz.push_back(cmz);
            force_pd.push_back(pd); force_vd.push_back(vd);
            force_pl.push_back(pl); force_vl.push_back(vl);
        }
        
        if (rank == 0 && (step % 1000 == 0 || step < 5)) {
            Real red = (initial_res_norm > 0 && gl2 > 0) ? 
                std::log10(initial_res_norm / gl2) : 0.0;
            std::cout << "S " << step << " CFL=" << cfl 
                      << " L2=" << gl2 << " L∞=" << glinf << " red=" << red << std::endl;
        }
        
        if (initial_res_norm > 0 && gl2 < initial_res_norm * 1e-8) {
            if (rank == 0) std::cout << "Converged at step " << step << std::endl;
            break;
        }
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    
    // ---- Write output files ----
    if (rank == 0) {
        std::cout << "Writing output files..." << std::endl;
        
        // Placeholder - output will be written by main.cpp
    }
    
    if (rank == 0) std::cout << "Steady march completed in " << elapsed << "s" << std::endl;
}
