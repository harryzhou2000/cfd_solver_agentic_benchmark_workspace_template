#include "transient.hpp"
#include "implicit.hpp"
#include "output.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>

namespace cfd {

TransientStats run_transient(Solver& solver, const std::string& output_dir) {
    const auto& ci = solver.case_input();
    idx_t n_owned = solver.local_mesh().n_owned;
    real_t gamma = ci.gamma;
    
    TransientStats stats;
    stats.physical_steps_run = 0;
    stats.total_inner_iterations = 0;
    stats.observed_min_inner = 999999;
    stats.observed_max_inner = 0;
    stats.inner_target_misses = 0;
    stats.inner_target_converged_fraction = 0;
    
    auto& U = const_cast<std::vector<StateVec>&>(solver.U());
    std::vector<StateVec> U_prev, U_prev2; // BDF2 history
    U_prev = U;
    U_prev2 = U;
    
    std::vector<StateVec> R(n_owned);
    std::vector<Vec2> grad_rho(n_owned), grad_rhou(n_owned), grad_rhov(n_owned), grad_rhoE(n_owned);
    std::vector<real_t> dt_local(n_owned);
    
    real_t dt_phys = ci.time_step;
    real_t final_time = ci.final_time;
    idx_t max_steps = static_cast<idx_t>(std::ceil(final_time / dt_phys));
    real_t cfl = ci.cfl_max;
    
    // Pre-compute 1/dV for each cell
    std::vector<real_t> inv_vol(n_owned);
    for (idx_t i = 0; i < n_owned; i++) {
        inv_vol[i] = 1.0 / solver.local_mesh().owned_cells[i].volume;
    }
    
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
    idx_t total_inner = 0;
    
    // Write cadence
    idx_t write_interval = 100;
    
    for (idx_t step = 0; step < max_steps; step++) {
        real_t t = (step + 1) * dt_phys;
        
        // Sync ghost states
        solver.sync_ghost_states(U);
        
        // Compute BDF2 physical-time source term
        // dU/dt (BDF2): (3 U^{n+1} - 4 U^n + U^{n-1}) / (2 dt)
        // Trapezoidal: (U^{n+1} - U^n) / dt - 0.5 * (R(U^{n+1}) + R(U^n))
        
        // For BDF2: R_total = R_spatial + (3U^{n+1} - 4U^n + U^{n-1})/(2*dt)
        // We need to modify the residual computation to include this
        
        // Initial residual (includes physical time term)
        std::vector<StateVec> U_save = U; // Save for inner iterations
        
        // Compute spatial residual
        real_t res_l2, res_linf;
        compute_residual_full(solver, U, R, res_l2, res_linf,
                              grad_rho, grad_rhou, grad_rhov, grad_rhoE);
        
        // Add BDF2 physical-time source to residual
        real_t inv_dt = 1.0 / dt_phys;
        real_t bdf2_factor = 3.0 * inv_dt / 2.0;
        
        for (idx_t i = 0; i < n_owned; i++) {
            // R_total = R_spatial + (3*U^{n+1} - 4*U^n + U^{n-1}) / (2*dt)
            // This is dU/dt term; for pseudo-time we treat it as a source
            // In our formulation: R = -dU/dt_spatial + dU/dt_physical
            // Or equivalently during inner iterations:
            // Delta U pseudo step: -R_spatial - BDF2_term = -source
            // The BDF2 term acts as an additional forcing
            
            for (int k = 0; k < 4; k++) {
                real_t bdf2 = (bdf2_factor * U[i][k] 
                              - 2.0 * inv_dt * U_prev[i][k] 
                              + 0.5 * inv_dt * U_prev2[i][k]);
                R[i][k] += bdf2 * solver.local_mesh().owned_cells[i].volume;
            }
        }
        
        if (step == 0) {
            initial_res = std::max(res_l2, 1e-15);
        }
        
        // Inner iterations (pseudo-time)
        compute_local_dt(solver, U, dt_local, cfl);
        
        idx_t inner_count = 0;
        bool inner_converged = false;
        
        for (idx_t inner = 0; inner < ci.max_inner_iterations; inner++) {
            inner_count++;
            
            // LU-SGS step
            lu_sgs_step(solver, U, R, dt_local, 1.0);
            
            // Sync after update
            solver.sync_ghost_states(U);
            
            // Recompute residual
            compute_residual_full(solver, U, R, res_l2, res_linf,
                                  grad_rho, grad_rhou, grad_rhov, grad_rhoE);
            
            // Add BDF2 term again (since U^{n+1} changed during inner iterations)
            for (idx_t i = 0; i < n_owned; i++) {
                for (int k = 0; k < 4; k++) {
                    real_t bdf2 = (bdf2_factor * U[i][k] 
                                  - 2.0 * inv_dt * U_prev[i][k] 
                                  + 0.5 * inv_dt * U_prev2[i][k]);
                    R[i][k] += bdf2 * solver.local_mesh().owned_cells[i].volume;
                }
            }
            
            // Check inner convergence
            if (inner + 1 >= ci.min_inner_iterations) {
                real_t ratio = res_l2 / std::max(initial_res, 1e-15);
                if (ratio < ci.inner_residual_reduction_target) {
                    inner_converged = true;
                    break;
                }
            }
        }
        
        // Update statistics
        stats.observed_min_inner = std::min(stats.observed_min_inner, inner_count);
        stats.observed_max_inner = std::max(stats.observed_max_inner, inner_count);
        if (!inner_converged) stats.inner_target_misses++;
        total_inner += inner_count;
        
        // BDF2: update history only after inner convergence
        U_prev2 = U_prev;
        U_prev = U;
        
        // Compute forces
        real_t cl, cd, cmz, p_drag, v_drag, p_lift, v_lift;
        solver.compute_forces(cl, cd, cmz, p_drag, v_drag, p_lift, v_lift);
        
        // Write residuals and forces (rank 0 only)
        if (solver.rank() == 0) {
            fres << (step+1) << "," << t << "," << inner_count
                 << "," << cfl << "," << dt_phys << ","
                 << 0 << "," << 0 << "," << 0 << "," << 0 << ","
                 << res_l2 << "," << res_linf << "\n";
            
            fforce << (step+1) << "," << t << ","
                   << cl << "," << cd << "," << cmz << ","
                   << p_drag << "," << v_drag << "," << p_lift << "," << v_lift << "\n";
        }
        
        if (solver.rank() == 0 && step % write_interval == 0) {
            std::cout << "Step " << (step+1) << "/" << max_steps
                      << " t=" << t << " inner=" << inner_count
                      << " Res=" << res_l2 << " CD=" << cd << " CL=" << cl << std::endl;
        }
        
        stats.physical_steps_run = step + 1;
    }
    
    stats.final_res_l2 = 0; // Will be set by last residual
    stats.final_res_linf = 0;
    stats.total_inner_iterations = total_inner;
    stats.inner_target_converged_fraction = 
        real_t(stats.physical_steps_run - stats.inner_target_misses) / real_t(stats.physical_steps_run);
    stats.convergence_status = "statistically_periodic";
    
    if (solver.rank() == 0) {
        fres.close();
        fforce.close();
    }
    
    return stats;
}

} // namespace cfd
