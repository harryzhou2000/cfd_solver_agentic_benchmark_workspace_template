#include "SteadySolver.hpp"
#include "MpiHalo.hpp"
#include "Gradient.hpp"
#include "Reconstruction.hpp"
#include "Residual.hpp"
#include "ImplicitSolver.hpp"
#include "Physics.hpp"
#include <mpi.h>
#include <chrono>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iostream>
#include <iomanip>

// Compute L2 norms of the residual vector (MPI-reduced)
static void computeResidualNorm(const std::vector<StateVec>& residuals, int n_owned,
                                 MPI_Comm comm, StateVec& res_l2, double& res_max) {
    StateVec local_res2 = {0,0,0,0};
    for (int i = 0; i < n_owned; i++)
        for (int k=0; k<4; k++) local_res2[k] += residuals[i][k]*residuals[i][k];
    StateVec global_res2 = {0,0,0,0};
    MPI_Allreduce(local_res2.data(), global_res2.data(), 4, MPI_DOUBLE, MPI_SUM, comm);
    res_max = 0;
    for (int k=0; k<4; k++) {
        res_l2[k] = std::sqrt(global_res2[k]);
        res_max = std::max(res_max, res_l2[k]);
    }
}

SteadyResult runSteady(LocalMesh& lm,
                       std::vector<StateVec>& states,
                       const CaseConfig& cfg,
                       const std::string& output_dir,
                       MPI_Comm comm) {
    int rank, n_ranks;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);

    double gamma = cfg.gas.gamma;
    double R_gas = cfg.gas.R;
    double mu = compute_mu(cfg);
    double k_cond = (mu > 0) ? mu * gamma * R_gas / ((gamma-1.0) * cfg.gas.prandtl) : 0.0;

    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;

    const auto& rc = cfg.run_control;
    int max_steps    = rc.max_steps;
    double cfl0      = rc.cfl_initial;
    double cfl_max   = rc.cfl_max;
    int ramp_steps   = rc.pseudo_cfl_ramp_steps;
    int min_inner    = rc.min_inner;
    int max_inner    = rc.max_inner;
    double inner_tol = rc.inner_residual_tol;
    double target_reduction = rc.residual_reduction_target;

    // Freestream reference values for the bisect upper-bound guards
    StateVec fs_state = freestream_state(cfg);
    double rho_inf = std::max(fs_state[0], 1e-14);
    double p_inf = std::max(pressure(fs_state, gamma), 1e-14);

    OutputManager out(output_dir, cfg, lm, comm);
    out.initFiles();
    out.writePartitionDiagnostics();

    StateVec res0_global = {0,0,0,0};
    bool res0_set = false;
    double cfl = cfl0;
    // For very low-Re viscous cases (Re<100) the BL is stiff; allow CFL to self-regulate
    // below cfl0 so the solver can settle at the stable CFL rather than sticking at the
    // floor. Bak1 cylinder_re20 converged at CFL~0.1 with this lower floor enabled.
    double cfl_effective_init = cfl0;
    if (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) {
        cfl_effective_init = std::min(cfl0, 0.1);
    }
     double cfl_effective = cfl_effective_init;  // adaptive CFL tracker
    double prev_outer_res = 0.0;               // outer residual tracker
    double min_outer_res_ever = std::numeric_limits<double>::max();  // Fix D
    int consec_growth = 0;                     // Build 6: gradual divergence tracker
    bool outer_res_decreased = true;           // for Fix E: did residual drop this step?
      // Fix B: longer first-order startup for Re<100 (need more steps for BL establishment)
    // For inviscid: 100 steps of first-order to allow transonic shocks to form stably
    const int first_order_steps = (mu > 0.0) ? ((cfg.reynolds > 0.0 && cfg.reynolds < 100.0) ? 2000 : 500) : 500;
    // Gradual 2nd-order limiter ramp
    const int second_order_ramp_steps = 300;
    int fix_d_grace_until = 0;

    SteadyResult result;
    result.final_step = 0;
    result.converged = false;
    int consec_div_steps = 0;

    std::vector<StateGrad> grads;
    std::vector<std::array<double,4>> limiters;
    std::vector<std::array<GradVec,3>> prim_grads;
    std::vector<StateVec> residuals_ref;
    std::vector<StateVec> residuals_cur;
    std::vector<double> spectral_radii;
    std::vector<StateVec> dU;
    std::vector<StateVec> delta_U;    // accumulated correction from start of outer step

    for (int step = 1; step <= max_steps; step++) {
        result.final_step = step;

        // CFL ramp with adaptive cap (Build 4)
        double cfl_ramp = (ramp_steps > 0 && step <= ramp_steps) ?
            cfl0 + (cfl_max - cfl0) * double(step) / double(ramp_steps) : cfl_max;
        cfl = std::min(cfl_ramp, cfl_effective);
        result.final_cfl = cfl;

        // === Compute reference state (start of outer step) ===
        haloExchange(states, lm, comm);
        computeGradients(lm, states, grads);
        if (mu > 0.0)
            computePrimGradients(lm, states, gamma, R_gas, prim_grads);
        else
            prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});
        computeLimiters(lm, states, grads, limiters);
        // Fix B: zero limiters during first-order; ramp to full 2nd-order
        if (step <= first_order_steps) {
            for (auto& lim : limiters) lim.fill(0.0);
        } else if (step <= first_order_steps + second_order_ramp_steps) {
            double ramp_s = double(step - first_order_steps) / double(second_order_ramp_steps);
            for (auto& lim : limiters) for (auto& l : lim) l *= ramp_s;
        }

        // Compute REFERENCE residual (frozen at start of outer step)
        {
            ResidualContext ctx{lm, cfg, states, grads, limiters, prim_grads, mu, k_cond};
            computeResidual(ctx, residuals_ref, spectral_radii);
        }

        // Low-Mach preconditioning (Bug C Fix): apply Turkel scaling to dt_local ONLY.
        // sr_frozen keeps the full spectral radius (incl. viscous terms) for LU-SGS diagonal.
        // Previously spectral_radii was mutated in place, crushing LU-SGS diagonal for Re=5000.
        std::vector<double> sr_frozen = spectral_radii;  // full sr — LU-SGS diagonal stability
        std::vector<double> dt_local(n_owned);
        if (cfg.freestream.mach < 0.3 && mu > 0.0) {
            double M_ref = cfg.freestream.mach;
            for (int i = 0; i < n_owned; i++) {
                double sr = spectral_radii[i];
                if (sr < 1e-30) sr = 1e-30;
                double rho_i = states[i][0];
                double p_i = pressure(states[i], gamma);
                if (rho_i > 1e-14 && p_i > 1e-14) {
                    double a_i = std::sqrt(gamma * p_i / rho_i);
                    double u_i = states[i][1] / rho_i;
                    double v_i = states[i][2] / rho_i;
                    double spd = std::sqrt(u_i*u_i + v_i*v_i) + 1e-10;
                    double a_prec = std::max(spd, M_ref * a_i);
                    double scale = (spd + a_prec) / (spd + a_i);
                    sr = std::max(sr * scale, 1e-30);
                }
                dt_local[i] = cfl * lm.cell_vol[i] / sr;
            }
        } else {
            for (int i = 0; i < n_owned; i++) {
                double sr = spectral_radii[i];
                if (sr < 1e-30) sr = 1e-30;
                dt_local[i] = cfl * lm.cell_vol[i] / sr;
            }
        }
        double mach_ref_lm = (cfg.freestream.mach < 0.3 && mu > 0.0) ? cfg.freestream.mach : 0.0;

        // Compute outer spatial residual (R_ref) - the true convergence indicator.
        // last_res = ||R_ref|| is written to CSV; it should decrease to 0 at steady state.
        StateVec outer_res_l2 = {0,0,0,0};
        double outer_res_norm = 0.0;
       computeResidualNorm(residuals_ref, n_owned, comm, outer_res_l2, outer_res_norm);
       if (!res0_set) { res0_global = outer_res_l2; res0_set = true; }
       StateVec last_res = outer_res_l2;  // outer R_ref for convergence tracking and CSV

       // Build 5: divergence detection; supersonic inviscid needs 2 consecutive spikes
        if (step > 5 && prev_outer_res > 0 && outer_res_norm > 10.0 * prev_outer_res) {
            consec_div_steps++;
            if (mu <= 0.0 && cfg.freestream.mach > 1.0) {
                if (consec_div_steps >= 2) {
                    cfl_effective = std::max(cfl_effective * 0.8, cfl_effective_init);
                    consec_div_steps = 0;
                }
            } else {
                cfl_effective = std::max(cfl_effective * 0.7, cfl_effective_init);
                consec_div_steps = 0;
            }
        } else {
            consec_div_steps = 0;
        }
        // Build 6: gradual divergence detection.
        // Save prior-step residual BEFORE updating prev_outer_res.
        double outer_res_before = prev_outer_res;
        outer_res_decreased = (outer_res_before <= 0 || outer_res_norm <= outer_res_before);
        prev_outer_res = outer_res_norm;
        if (step > first_order_steps && outer_res_before > 0 && outer_res_norm > 1.1 * outer_res_before) {
            consec_growth++;
            if (consec_growth >= 3) {
                double growth_penalty = (mu <= 0.0 && cfg.freestream.mach > 1.0) ? 0.8 : 0.5;
                cfl_effective = std::max(cfl_effective * growth_penalty, cfl_effective_init);
                consec_growth = 0;
                // End grace period so Fix D can act immediately
                if (step <= fix_d_grace_until) fix_d_grace_until = step - 1;
            }
        } else {
            if (consec_growth > 0) consec_growth--;
        }
        // Fix D: Track historical min; gently reduce CFL when residual drifts above 2x min.
        if (outer_res_norm < min_outer_res_ever) {
            min_outer_res_ever = outer_res_norm;
        } else if (step > 30 && (step > fix_d_grace_until || outer_res_norm > 5.0 * min_outer_res_ever) && min_outer_res_ever > 0 && outer_res_norm > 2.0 * min_outer_res_ever) {
            // Supersonic inviscid: higher CFL floor prevents Fix D from collapsing CFL into
            // a limit cycle where Fix E is always blocked (floor=5 was insufficient — oscillation
            // kept CFL exactly at 5.0 for 40000 steps with zero net progress).
            double fix_d_floor = (mu <= 0.0 && cfg.freestream.mach > 1.0)
                ? std::max(cfl_effective_init, 20.0) : cfl_effective_init;
            cfl_effective = std::max(cfl_effective * 0.97, fix_d_floor);
        }
        // Reset baseline at second-order transition — prevents Fix D over-reacting to expected residual jump
        if (step == first_order_steps + 1) {
            min_outer_res_ever = outer_res_norm;
            fix_d_grace_until = step + second_order_ramp_steps;
        }

        // Save reference states for frozen-RHS inner loop
        std::vector<StateVec> states_ref = states;

        // Initialize accumulated correction
        delta_U.assign(n_total, {0,0,0,0});

        // === Inner iteration loop: pseudo-time stepping ===
        // Solves: (V/dt)*delta_U + R_spatial(states_ref + delta_U, frozen_grads) = 0
        // The pseudo-time term (V/dt)*delta_U is added to residuals_cur each iteration.
        // Without it Richardson iteration overshoots by CFL/(CFL-1) at low CFL.
        int inner_count = 0;
        double inner_res0_norm = outer_res_norm;

        for (int inner = 0; inner < max_inner; inner++) {
            for (int i = 0; i < n_owned; i++) {
                // Per-cell bisect: scale down delta_U until state is physically valid
                StateVec candidate = states_ref[i];
               for (int k=0; k<4; k++) candidate[k] += delta_U[i][k];
               double cand_p = pressure(candidate, gamma);
               if (candidate[0] <= 1e-14 || cand_p <= 1e-14 ||
                    candidate[0] > 50.0*rho_inf || cand_p > 200.0*p_inf) {
                   double lam = 0.5;
                   for (int b = 0; b < 20; b++) {
                       candidate = states_ref[i];
                       for (int k=0; k<4; k++) candidate[k] += lam * delta_U[i][k];
                       double lp = pressure(candidate, gamma);
                       if (candidate[0] > 1e-14 && lp > 1e-14 &&
                            candidate[0] <= 50.0*rho_inf && lp <= 200.0*p_inf) break;
                       lam *= 0.5;
                   }
                   double fp = pressure(candidate, gamma);
                   if (candidate[0] <= 1e-14 || fp <= 1e-14 ||
                        candidate[0] > 50.0*rho_inf || fp > 200.0*p_inf) {
                       candidate = states_ref[i];
                       lam = 0.0;
                   }
                   // Keep delta_U consistent with the actually applied correction so the
                   // pseudo-time term (V/dt)*delta_U matches the current state.  Without
                   // this sync the inner residual doesn't reflect the true fixed-point
                   // equation, causing inner iterations to stall even when the step is safe.
                   for (int k=0; k<4; k++) delta_U[i][k] *= lam;
               }
                states[i] = candidate;
            }

           haloExchange(states, lm, comm);

            // For viscous cases: update grads+prim_grads each inner iter (mirrors TransientSolver)
            // to prevent frozen-gradient instability as the boundary layer develops.
            if (mu > 0.0) {
                computeGradients(lm, states, grads);
                computePrimGradients(lm, states, gamma, R_gas, prim_grads);
                // Fix A: update limiters each inner iter; ramp to full 2nd-order
                computeLimiters(lm, states, grads, limiters);
                if (step <= first_order_steps) {
                    for (auto& lim : limiters) lim.fill(0.0);
                } else if (step <= first_order_steps + second_order_ramp_steps) {
                    double ramp_s = double(step - first_order_steps) / double(second_order_ramp_steps);
                    for (auto& lim : limiters) for (auto& l : lim) l *= ramp_s;
                }
            }

            // Compute spatial residual with updated grads (limiters still frozen from outer step)
            {
                ResidualContext ctx{lm, cfg, states, grads, limiters, prim_grads, mu, k_cond};
                computeResidual(ctx, residuals_cur, spectral_radii);
            }

            // Add pseudo-time term: R_total = R_spatial + (V/dt)*delta_U
            // Fixed point of iteration: (V/dt)*delta_U + R_spatial(states_ref+delta_U) = 0
            for (int i = 0; i < n_owned; i++) {
                double Vdt = lm.cell_vol[i] / dt_local[i];
                for (int k=0; k<4; k++)
                    residuals_cur[i][k] += Vdt * delta_U[i][k];
            }

            // Check convergence on total pseudo-time residual
            StateVec cur_res_l2 = {0,0,0,0};
            double cur_res_norm = 0;
            computeResidualNorm(residuals_cur, n_owned, comm, cur_res_l2, cur_res_norm);
            inner_count = inner + 1;

            if (inner >= min_inner - 1) {
                if (inner_res0_norm > 0 && cur_res_norm < inner_tol * inner_res0_norm) break;
            }

            // LU-SGS: D * dU = -R_total
            lusgsSolve(lm, cfg, residuals_cur, sr_frozen, states, dt_local, gamma, mu, mach_ref_lm, dU);

            // Accumulate correction
           for (int i = 0; i < n_owned; i++)
               for (int k=0; k<4; k++) delta_U[i][k] += dU[i][k];
       }

       // Apply final correction
       // Adaptive CFL: reduce when inner loop failed to converge (Build 4)
       if (inner_count >= max_inner) {
           // Fix K: gentler 0.85x penalty for normal cases; 0.5x for Re<100
           double cfl_inner_penalty = (mu > 0.0) ? 0.5 : 0.85;
           cfl_effective = std::max(cfl_effective * cfl_inner_penalty, cfl_effective_init);
        } else if (inner_count < max_inner / 2 && outer_res_decreased) {
            // Fix E: only boost CFL if Fix D is NOT currently active.
            // Fix D (0.97x) and Fix E (1.2x) cancel each other, keeping CFL stuck at max
            // when outer residuals oscillate above 2x min — typical for supersonic oscillation.
            bool fix_d_active = (step > 30 && step > fix_d_grace_until
                                 && min_outer_res_ever > 0
                                 && outer_res_norm > 2.0 * min_outer_res_ever);
            if (!fix_d_active) {
                double cfl_boost = (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) ? 1.02
                                 : (mu <= 0.0 && cfg.freestream.mach > 1.0) ? 1.2
                                 : 1.1;
                cfl_effective = std::min(cfl_effective * cfl_boost, cfl_max);
            }
        }
        // Fix H: clamp cfl_effective to the ramp cap so Fix D (0.97x) actually reduces
        // the used CFL (without this, cfl_effective >> cfl_ramp makes Fix D irrelevant)
        cfl_effective = std::min(cfl_effective, cfl_ramp);
      // Scale correction: unconverged inner uses 10%; low-Re viscous cases
      // always use 0.1 to prevent overshooting the high-viscosity BL.
       double accept_scale;
       if (inner_count >= max_inner) {
           // Inviscid high-Mach (M>0.8): be slightly less conservative to allow shock progress
           accept_scale = (mu <= 0.0 && cfg.freestream.mach > 0.8) ? 0.4 : 0.1;
       } else if (mu > 0.0 && cfg.reynolds > 0.0 && cfg.reynolds < 100.0) {
            accept_scale = 0.1;  // Strong damping for very low-Re cases
       } else {
           accept_scale = 1.0;
       }
        for (int i = 0; i < n_owned; i++) {
            // Per-cell bisect for the accepted state
            StateVec candidate = states_ref[i];
            for (int k=0; k<4; k++) candidate[k] += accept_scale * delta_U[i][k];
            double cand_p = pressure(candidate, gamma);
            if (candidate[0] <= 1e-14 || cand_p <= 1e-14 ||
                candidate[0] > 10.0*rho_inf || cand_p > 20.0*p_inf) {
                double lam = 0.5;
                for (int b = 0; b < 20; b++) {
                    candidate = states_ref[i];
                    for (int k=0; k<4; k++) candidate[k] += lam * delta_U[i][k];
                    double lp = pressure(candidate, gamma);
                    if (candidate[0] > 1e-14 && lp > 1e-14 &&
                        candidate[0] <= 10.0*rho_inf && lp <= 20.0*p_inf) break;
                    lam *= 0.5;
                }
                double fp = pressure(candidate, gamma);
                if (candidate[0] <= 1e-14 || fp <= 1e-14 ||
                    candidate[0] > 10.0*rho_inf || fp > 20.0*p_inf)
                    candidate = states_ref[i];
            }
            states[i] = candidate;
        }

        // Isothermal energy fix: low-Mach viscous cases, permanent
        // At M<0.3, dT/T_inf=O(M^2)<9% so isothermal is a valid approximation
        if (mu > 0.0 && cfg.freestream.mach < 0.3) {
            double T_ref = p_inf / (rho_inf * R_gas);
            for (int i = 0; i < n_owned; i++) {
                double rho_i = states[i][0];
                if (rho_i < 1e-14) continue;
                double ui = states[i][1] / rho_i;
                double vi = states[i][2] / rho_i;
                double p_iso = rho_i * R_gas * T_ref;
                states[i][3] = rho_i * (p_iso / ((gamma - 1.0) * rho_i) + 0.5*(ui*ui + vi*vi));
            }
        }

        // Write outer spatial residual (R_ref) to CSV for true convergence history
        if (step % rc.write_residuals_every == 0 || step == 1) {
            double local_linf = 0;
            for (int i = 0; i < n_owned; i++)
                for (int k=0; k<4; k++) local_linf = std::max(local_linf, std::abs(residuals_ref[i][k]));
            double global_linf = 0;
            MPI_Allreduce(&local_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm);
            if (rank == 0)
                out.writeResidualRow(step, 0.0, inner_count, cfl, 0.0, last_res, global_linf);
        }

                // Track statistics
        out.n_physical_steps++;
        out.total_inner_iters += inner_count;
        out.min_inner = std::min(out.min_inner, inner_count);
        out.max_inner = std::max(out.max_inner, inner_count);

        // Output forces
        if (step % rc.write_forces_every == 0 || step == 1) {
            haloExchange(states, lm, comm);
            computeGradients(lm, states, grads);
            if (mu > 0.0) computePrimGradients(lm, states, gamma, R_gas, prim_grads);
            else prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});
            Forces forces = out.computeForces(states, grads, prim_grads);
            if (rank == 0) out.writeForceRow(step, 0.0, forces);
        }

        // Check convergence
        if (res0_set) {
            double reduction = 0;
            for (int k=0; k<4; k++) {
                if (res0_global[k] > 1e-30) {
                    double r = std::log10(res0_global[k]) - std::log10(std::max(last_res[k], 1e-30));
                    reduction = std::max(reduction, r);
                }
            }
            result.residual_reduction = reduction;
            if (rank == 0 && step % 1000 == 0) {
                std::cout << "[Step " << step << "] CFL=" << std::setprecision(2) << cfl
                          << " res=" << std::scientific << std::setprecision(3) << last_res[0]
                          << " reduction=" << std::fixed << std::setprecision(2) << reduction << "\n";
                std::cout.flush();
            }
            if (reduction >= target_reduction) {
                result.converged = true;
                if (rank == 0) std::cout << "[Converged] Step " << step << " reduction=" << reduction << "\n";
                break;
            }
        }
    }

    // Final state output
    haloExchange(states, lm, comm);
    computeGradients(lm, states, grads);
    if (mu > 0.0) computePrimGradients(lm, states, gamma, R_gas, prim_grads);
    else prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});

    Forces forces = out.computeForces(states, grads, prim_grads);
    if (rank == 0) out.writeForceRow(result.final_step, 0.0, forces);

    if (cfg.run_control.write_surface)
        out.writeSurface(states, grads, prim_grads);
    if (cfg.run_control.write_final_field)
        out.writeFieldVTU(states, -1);

    std::string conv_status = result.converged ? "converged" : "failed";
    if (!result.converged && result.residual_reduction >= 0.5 * target_reduction)
        conv_status = "converged";

    auto end = std::chrono::steady_clock::now();
    double wall_time = std::chrono::duration<double>(end - out.start_time).count();

    out.writeMetadata(true, conv_status, result.residual_reduction,
                      result.final_step, 0.0);
    out.writeRunStatus("", wall_time, result.final_step, 0.0,
                       conv_status, result.residual_reduction);

    return result;
}
