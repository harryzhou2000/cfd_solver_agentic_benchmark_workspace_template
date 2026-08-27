#include "TransientSolver.hpp"
#include "MpiHalo.hpp"
#include "Gradient.hpp"
#include "Reconstruction.hpp"
#include "Residual.hpp"
#include "ImplicitSolver.hpp"
#include "Physics.hpp"
#include <mpi.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <chrono>

TransientResult runTransient(LocalMesh& lm,
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

    // Freestream reference values for bisect upper-bound guards
    StateVec fs_state = freestream_state(cfg);
    double rho_inf = std::max(fs_state[0], 1e-14);
    double p_inf = std::max(pressure(fs_state, gamma), 1e-14);

    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;

    const auto& rc = cfg.run_control;
    double dt = rc.time_step;
    double t_final = rc.final_time;
    int max_inner  = rc.max_inner;
    int min_inner  = rc.min_inner;
    double inner_tol = rc.inner_residual_tol;
    double cfl = rc.cfl_initial;

    OutputManager out(output_dir, cfg, lm, comm);
    out.initFiles();
    out.writePartitionDiagnostics();

    // U^{n-1} and U^n (previous physical time states)
    std::vector<StateVec> U_nm1 = states;
    std::vector<StateVec> U_n   = states;

    TransientResult result;
    result.completed = false;

    double t = 0.0;
    int step = 0;
    double next_field_time = rc.write_field_every_time;
    long long total_inner = 0;
    int n_steps = 0;
    int min_inner_obs = 9999, max_inner_obs = 0;
    int target_misses = 0;
    double last_inner_res_ratio = 1.0;

    std::vector<StateGrad> grads;
    std::vector<std::array<double,4>> limiters;
    std::vector<std::array<GradVec,3>> prim_grads;
    std::vector<StateVec> residuals;
    std::vector<double> spectral_radii;
    std::vector<StateVec> dU;
    std::vector<StateVec> delta_U(n_total, {0,0,0,0});
    std::vector<double> dt_local(n_owned);

    bool first_step = true;

    while (t < t_final - 0.5*dt) {
        step++;
        double t_new = std::min(t + dt, t_final);
        double dt_actual = t_new - t;

        // BDF coefficients
        double coeff_bdf, bdf_c0, bdf_c1, bdf_c2;
        if (first_step) {
            coeff_bdf = 1.0 / dt_actual;
            bdf_c0 = 1.0; bdf_c1 = -1.0; bdf_c2 = 0.0;
            first_step = false;
        } else {
            coeff_bdf = 1.0 /  dt_actual;
            bdf_c0 = 1.5; bdf_c1 = -2.0; bdf_c2 = 0.5;
        }

        // === Freeze reference state at start of physical step ===
        // Mirror SteadySolver pattern: compute grads/limiters once from ref state,
        // then use them frozen throughout inner iterations for stability.
        std::vector<StateVec> states_ref = states;
        haloExchange(states_ref, lm, comm);
        computeGradients(lm, states_ref, grads);
        haloExchangeGrads(grads, lm, comm);
        if (mu > 0.0) { computePrimGradients(lm, states_ref, gamma, R_gas, prim_grads); haloExchangePrimGrads(prim_grads, lm, comm); }
        else prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});
        computeLimiters(lm, states_ref, grads, limiters);

        // Compute frozen spectral radii and CFL-based pseudo-time step
        {
            ResidualContext ctx0{lm, cfg, states_ref, grads, limiters, prim_grads, mu, k_cond};
            computeResidual(ctx0, residuals, spectral_radii);
        }
        std::vector<double> sr_frozen = spectral_radii;
        std::vector<double> dt_cfl_local(n_owned);
        for (int i = 0; i < n_owned; i++) {
            double sr = sr_frozen[i];
            if (sr < 1e-30) sr = 1e-30;
            // Combined pseudo-time + BDF diagonal: ensures D = V/dt_local + sr matches
            // the effective Jacobian of R_total = R_spatial + BDF_term + (V/dt_cfl)*dU
            dt_cfl_local[i] = cfl * lm.cell_vol[i] / sr;
            double inv_dt = 1.0 / (cfl * lm.cell_vol[i] / sr) + coeff_bdf * bdf_c0;
            dt_local[i] = 1.0 / inv_dt;
        }

        // Initialize accumulated correction
        std::fill(delta_U.begin(), delta_U.end(), StateVec{0,0,0,0});

        int inner_count = 0;
        double inner_res0_val = 1e-30;

        // === Inner iteration loop ===
        // Solves: R_spatial(U) + V*coeff*(bdf_c0*U+bdf_c1*Un+bdf_c2*Unm1) + V/dt_pseudo*dU = 0
        // The pseudo-time term prevents Richardson overshoot (same fix as SteadySolver).
        for (int inner = 0; inner < max_inner; inner++) {
            // Build current iterate with per-cell bisect for positivity
            for (int i = 0; i < n_owned; i++) {
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
                        candidate[0] > 50.0*rho_inf || fp > 200.0*p_inf)
                        candidate = states_ref[i];
                }
                states[i] = candidate;
            }
            haloExchange(states, lm, comm);
            // Update grads from current states each iteration; keep limiters frozen from states_ref
            computeGradients(lm, states, grads);
            haloExchangeGrads(grads, lm, comm);
            if (mu > 0.0) { computePrimGradients(lm, states, gamma, R_gas, prim_grads); haloExchangePrimGrads(prim_grads, lm, comm); }

            // Spatial residual (frozen limiters, updated grads)
            {
                ResidualContext ctx{lm, cfg, states, grads, limiters, prim_grads, mu, k_cond};
                computeResidual(ctx, residuals, spectral_radii);
            }

            // Add BDF2 physical-time term
            for (int i = 0; i < n_owned; i++) {
                double Vi = lm.cell_vol[i];
                for (int k=0; k<4; k++) {
                    residuals[i][k] += Vi * coeff_bdf *
                        (bdf_c0*states[i][k] + bdf_c1*U_n[i][k] + bdf_c2*U_nm1[i][k]);
                }
            }

            // Add pseudo-time regularization: (V/dt_pseudo)*delta_U
            for (int i = 0; i < n_owned; i++) {
                double Vdt = lm.cell_vol[i] / dt_cfl_local[i];
                for (int k=0; k<4; k++) residuals[i][k] += Vdt * delta_U[i][k];
            }

            // Global L2 norm
            double local_res2 = 0;
            for (int i = 0; i < n_owned; i++)
                for (int k=0; k<4; k++) local_res2 += residuals[i][k]*residuals[i][k];
            double global_res2 = 0;
            MPI_Allreduce(&local_res2, &global_res2, 1, MPI_DOUBLE, MPI_SUM, comm);
            double res_norm = std::sqrt(global_res2);

            if (inner == 0) {
                if (res_norm < 1e-30) { inner_count = 1; last_inner_res_ratio = 0.0; break; }
                inner_res0_val = res_norm;
                last_inner_res_ratio = 1.0;
            } else {
                last_inner_res_ratio = res_norm / inner_res0_val;
            }
            if (inner >= min_inner - 1) {
                if (last_inner_res_ratio < inner_tol) {
                    inner_count = inner + 1;
                    break;
                }
            }

            // LU-SGS solve with frozen sr and CFL dt_local
            // Use frozen states_ref for LU-SGS off-diagonal lambda to guarantee diagonal
            // dominance throughout inner iterations -- current states accumulate large
            // delta_U during vortex shedding, inflating lambda and stalling convergence.
            lusgsSolve(lm, cfg, residuals, sr_frozen, states_ref, dt_local, gamma, mu, 0.0, comm, dU);

            // Accumulate correction
            for (int i = 0; i < n_owned; i++)
                for (int k=0; k<4; k++) delta_U[i][k] += dU[i][k];

            inner_count = inner + 1;
        }

        // Check if target was missed
        if (last_inner_res_ratio > inner_tol) target_misses++;
        min_inner_obs = std::min(min_inner_obs, inner_count);
        max_inner_obs = std::max(max_inner_obs, inner_count);
        total_inner += inner_count;
        n_steps++;

        // Apply final correction -- damp by 0.5x when inner loop failed to converge
        double accept_scale = (inner_count >= max_inner) ? 0.5 : 1.0;
        for (int i = 0; i < n_owned; i++) {
            // Per-cell bisect for the accepted physical state
            StateVec candidate = states_ref[i];
            for (int k=0; k<4; k++) candidate[k] += accept_scale * delta_U[i][k];
            double cand_p2 = pressure(candidate, gamma);
            if (candidate[0] <= 1e-14 || cand_p2 <= 1e-14 ||
                candidate[0] > 50.0*rho_inf || cand_p2 > 200.0*p_inf) {
                double lam = 0.5;
                for (int b = 0; b < 20; b++) {
                    candidate = states_ref[i];
                    for (int k=0; k<4; k++) candidate[k] += lam * delta_U[i][k];
                    double lp2 = pressure(candidate, gamma);
                    if (candidate[0] > 1e-14 && lp2 > 1e-14 &&
                        candidate[0] <= 50.0*rho_inf && lp2 <= 200.0*p_inf) break;
                    lam *= 0.5;
                }
                double fp2 = pressure(candidate, gamma);
                if (candidate[0] <= 1e-14 || fp2 <= 1e-14 ||
                    candidate[0] > 50.0*rho_inf || fp2 > 200.0*p_inf)
                    candidate = states_ref[i];
            }
            states[i] = candidate;
        }

        // Accept physical step: update time history
        t = t_new;
        U_nm1 = U_n;
        U_n   = states;

        // Output residuals
        if (rank == 0 && step % rc.write_residuals_every == 0) {
            StateVec log_res = {0,0,0,0};
            out.writeResidualRow(step, t, inner_count, cfl, dt_actual, log_res,
                                  last_inner_res_ratio);
        }

        // Output forces
        if (step % rc.write_forces_every == 0) {
            haloExchange(states, lm, comm);
            computeGradients(lm, states, grads);
            haloExchangeGrads(grads, lm, comm);
            if (mu > 0.0) { computePrimGradients(lm, states, gamma, R_gas, prim_grads); haloExchangePrimGrads(prim_grads, lm, comm); }
            else prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});
            Forces forces = out.computeForces(states, grads, prim_grads);
            if (rank == 0) out.writeForceRow(step, t, forces);
        }

        // Intermediate field output
        if (t >= next_field_time) {
            if (rc.write_final_field) out.writeFieldVTU(states, step);
            next_field_time += rc.write_field_every_time;
        }

        if (rank == 0 && step % 500 == 0) {
            std::cout << "[Step " << step << "] t=" << std::fixed << std::setprecision(2) << t
                      << " inner=" << inner_count
                      << " ratio=" << std::scientific << std::setprecision(2) << last_inner_res_ratio
                      << "\n";
            std::cout.flush();
        }
    }

    result.completed = true;
    result.final_time = t;
    result.n_steps = n_steps;
    result.mean_inner_iters = (n_steps > 0) ? double(total_inner)/n_steps : 0.0;
    result.min_inner = (min_inner_obs < 9999) ? min_inner_obs : 0;
    result.max_inner = max_inner_obs;
    result.target_misses = target_misses;
    result.last_inner_residual_ratio = last_inner_res_ratio;

    // Final output
    haloExchange(states, lm, comm);
    computeGradients(lm, states, grads);
            haloExchangeGrads(grads, lm, comm);
    if (mu > 0.0) { computePrimGradients(lm, states, gamma, R_gas, prim_grads); haloExchangePrimGrads(prim_grads, lm, comm); }
    else prim_grads.assign(n_total, {GradVec{0,0}, GradVec{0,0}, GradVec{0,0}});

    Forces forces = out.computeForces(states, grads, prim_grads);
    if (rank == 0) out.writeForceRow(step, t, forces);

    if (rc.write_surface) out.writeSurface(states, grads, prim_grads);
    if (rc.write_final_field) out.writeFieldVTU(states, -1);

    out.total_inner_iters = total_inner;
    out.n_physical_steps  = n_steps;
    out.min_inner = result.min_inner;
    out.max_inner = result.max_inner;
    out.inner_target_misses = target_misses;
    out.last_inner_residual_ratio = last_inner_res_ratio;

    auto end_t = std::chrono::steady_clock::now();
    double wall_time = std::chrono::duration<double>(end_t - out.start_time).count();

    out.writeMetadata(true, "statistically_periodic", 0.0, step, t);
    out.writeRunStatus("", wall_time, step, t, "statistically_periodic", 0.0);

    return result;
}
