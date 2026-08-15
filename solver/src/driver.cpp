#include "driver.hpp"
#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

namespace cfd {

namespace {

struct HistoryWriter {
    std::ofstream res, forces;
    HistoryWriter(const std::string& dir, int rank) {
        if (rank != 0) return;
        res.open(dir + "/residuals.csv");
        forces.open(dir + "/forces.csv");
        res << std::setprecision(12);
        forces << std::setprecision(12);
        res << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
        forces << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    }
    void write(Index step, Real phys, Index inner, Real cfl, Real dt,
               const ResNorm& nrm, const ForceCoeffs& fc) {
        res << step << "," << phys << "," << inner << "," << cfl << "," << dt << ","
            << nrm.rho << "," << nrm.rhou << "," << nrm.rhov << "," << nrm.rhoE << ","
            << nrm.l2 << "," << nrm.linf << "\n";
        forces << step << "," << phys << "," << fc.cl << "," << fc.cd << "," << fc.cmz << ","
               << fc.pressure_drag << "," << fc.viscous_drag << ","
               << fc.pressure_lift << "," << fc.viscous_lift << "\n";
    }
};

} // anonymous namespace

SteadyResult run_steady(Solver& solver, const std::string& output_dir,
                        const std::string& case_path, int rank, int n_ranks) {
    const CaseInput& ci = solver.case_input();
    const LocalMesh& lm = solver.mesh();
    SteadyResult result;
    HistoryWriter hw(output_dir, rank);

    std::vector<State> R(lm.n_total, State::Zero());
    std::vector<Real> dt_local(lm.n_owned, 1.0);

    solver.exchange_state();
    ResNorm norm;
    solver.compute_residual(R, norm, false, 0.0, {}, {});
    Real res_l2 = norm.l2, res_linf = norm.linf;
    const Real res0 = std::max(res_l2, 1e-300);
    Real res_peak = res_l2;
    Real cfl = ci.cfl_initial;
    Index step = 0;
    Index cfl_ramp_pos = 0;
    bool inner_converged = false;
    bool converged = false, plateau = false, nonfinite = false;
    Index last_written = 0;
    const Index every = std::max<Index>(1, ci.write_residuals_every);
    const Index min_converge = std::max<Index>(ci.pseudo_cfl_ramp_steps, Index(200));

    // Physics-based CFL cap
    const bool inviscid = ci.physics_mode == "inviscid";
    const Real phys_cfl = getenv("CFD_PHYS_CFL_CAP")
        ? std::atof(getenv("CFD_PHYS_CFL_CAP"))
        : (ci.mach >= 1.0 ? 1.0 : (inviscid ? (ci.mach <= 0.2 ? 10.0 : 2.0) : 5.0));
    const Real cfl_cap = ci.cfl_max;
    const Real cfl_phys_cap = std::min(cfl_cap, phys_cfl);
    const Index first_order_steps = std::max<Index>(ci.pseudo_cfl_ramp_steps, 200);
    const Real cfl_backoff_growth = 1.5;
    const Index cfl_backoff_min = 50;

    // Plateau detection
    const Index plateau_window = getenv("CFD_PLATEAU_WINDOW")
        ? (Index)std::atoll(getenv("CFD_PLATEAU_WINDOW")) : 2500;
    const Real plateau_res_tol = getenv("CFD_PLATEAU_RES_TOL")
        ? std::atof(getenv("CFD_PLATEAU_RES_TOL")) : 0.30;
    const Real plateau_force_tol = getenv("CFD_PLATEAU_FORCE_TOL")
        ? std::atof(getenv("CFD_PLATEAU_FORCE_TOL")) : 0.05;
    const Real plateau_force_tol_abs = getenv("CFD_PLATEAU_FORCE_TOL_ABS")
        ? std::atof(getenv("CFD_PLATEAU_FORCE_TOL_ABS")) : 1.0e-3;
    const Real plateau_min_orders = getenv("CFD_PLATEAU_MIN_ORDERS")
        ? std::atof(getenv("CFD_PLATEAU_MIN_ORDERS")) : 0.5;
    const Index plateau_confirm = getenv("CFD_PLATEAU_CONFIRM")
        ? (Index)std::atoll(getenv("CFD_PLATEAU_CONFIRM")) : 500;
    Index plateau_hold = 0;

    std::vector<Real> res_hist, drag_hist;
    res_hist.reserve(ci.max_steps + 1);
    drag_hist.reserve(ci.max_steps + 1);
    const Index max_steps = getenv("CFD_MAX_STEPS")
        ? (Index)std::atoll(getenv("CFD_MAX_STEPS")) : ci.max_steps;

    InnerStats& inner = result.inner;
    inner.observed_min = std::numeric_limits<Index>::max();

    ForceCoeffs fc = solver.compute_forces(false);
    hw.write(0, 0.0, 0, cfl, 0.0, norm, fc);

    while (step < max_steps) {
        step++;
        solver.set_first_order_phase(step <= first_order_steps);
        const Real pre_res = res_l2;

        // CFL ramp
        if (ci.pseudo_cfl_ramp_steps > 0) {
            cfl_ramp_pos++;
            const Real frac = std::min<Real>(1.0,
                (Real)cfl_ramp_pos / (Real)ci.pseudo_cfl_ramp_steps);
            const Real start = std::min(ci.cfl_initial, cfl_phys_cap);
            cfl = (cfl_phys_cap > start)
                ? start * std::pow(cfl_phys_cap / start, frac) : cfl_phys_cap;
        } else {
            cfl = cfl_phys_cap;
        }

        solver.compute_local_dt(dt_local.data(), cfl);
        const std::vector<State> pseudo_old = solver.state();

        // Inner solver: default = block-Jacobi (exact 4x4 Euler Jacobians,
        // damped sweeps with inner convergence check).  Set CFD_USE_LUSGS=1
        // to select the scalar LU-SGS path (one sweep per outer step, with
        // convergence carried by the outer pseudo-transient continuation and
        // plateau criterion).
        Index inner_iters = 0;
        inner_converged = false;
        if (getenv("CFD_USE_LUSGS")) {
            solver.compute_residual(R, norm, false, 0.0, {}, {});
            res_l2 = norm.l2;
            res_linf = norm.linf;
            solver.lusgs_sweep(R, solver.state(), dt_local.data(), false, 0.0);
            solver.exchange_state();
            solver.compute_residual(R, norm, false, 0.0, {}, {});
            const Real post_sweep = norm.l2;
            inner_converged = (post_sweep <= ci.inner_residual_reduction_target *
                                              std::max(pre_res, (Real)1e-300));
            inner_iters = 1;
            res_l2 = norm.l2;
            res_linf = norm.linf;
        } else {
            // Block-Jacobi inner loop
            for (inner_iters = 1; inner_iters <= ci.max_inner_iterations; inner_iters++) {
                solver.compute_residual(R, norm, false, 0.0, {}, {});
                res_l2 = norm.l2;
                res_linf = norm.linf;
                const Real pn = solver.block_jacobi_correction(R, pseudo_old,
                    dt_local.data(), 0.0);
                solver.exchange_state();
                if (inner_iters == 1) {
                    if (pn < 1e-300) { inner_converged = true; break; }
                }
                if (inner_iters >= ci.min_inner_iterations &&
                    pn <= ci.inner_residual_reduction_target *
                          std::max(pre_res, (Real)1e-14)) {
                    inner_converged = true;
                    break;
                }
            }
            solver.compute_residual(R, norm, false, 0.0, {}, {});
            res_l2 = norm.l2;
            res_linf = norm.linf;
        }
        inner.total_inner += inner_iters;
        inner.observed_min = std::min(inner.observed_min, inner_iters);
        inner.observed_max = std::max(inner.observed_max, inner_iters);
        inner.steps_counted++;
        if (!inner_converged) inner.target_misses++;

        // CFL backoff
        if (step >= cfl_backoff_min && pre_res > 0 &&
            res_l2 > cfl_backoff_growth * pre_res && cfl > ci.cfl_initial) {
            cfl = std::max(0.5 * cfl, ci.cfl_initial);
            const Real start = std::min(ci.cfl_initial, cfl_phys_cap);
            if (cfl_phys_cap > start && cfl > start) {
                const Real frac = std::log(cfl / start) / std::log(cfl_phys_cap / start);
                cfl_ramp_pos = std::max<Index>(0,
                    (Index)(frac * ci.pseudo_cfl_ramp_steps));
            }
        }
        res_peak = std::max(res_peak, res_l2);

        if (step % every == 0 || step == max_steps) {
            fc = solver.compute_forces(false);
            hw.write(step, 0.0, inner_iters, cfl, dt_local[0], norm, fc);
            if (step % 100 == 0) { hw.res.flush(); hw.forces.flush(); }
            last_written = step;
        }
        res_hist.push_back(res_l2);
        drag_hist.push_back(fc.cd);

        if (!std::isfinite(res_l2) || !std::isfinite(res_linf)) {
            nonfinite = true;
            break;
        }
        if (step >= min_converge &&
            res_l2 <= res_peak * std::pow(10.0, -ci.residual_reduction_target)) {
            converged = true;
            break;
        }
        // Plateau detection
        if (step >= min_converge && ci.pseudo_cfl_ramp_steps > 0 &&
            cfl_ramp_pos >= ci.pseudo_cfl_ramp_steps &&
            (Index)res_hist.size() > plateau_window) {
            const Index nw = (Index)res_hist.size();
            const Index half = plateau_window / 2;
            Real r_old = 0, r_new = 0, d_old = 0, d_new = 0;
            for (Index k = nw - plateau_window; k < nw - half; k++) {
                r_old += res_hist[k]; d_old += drag_hist[k];
            }
            for (Index k = nw - half; k < nw; k++) {
                r_new += res_hist[k]; d_new += drag_hist[k];
            }
            r_old /= half; r_new /= half; d_old /= half; d_new /= half;
            const Real d_scale = std::max({std::abs(d_old), std::abs(d_new), (Real)1e-6});
            const Real d_tol = std::max(plateau_force_tol * d_scale, plateau_force_tol_abs) + 1e-5;
            const bool res_flat = std::abs(r_new - r_old) <= plateau_res_tol * std::max(r_old, (Real)1e-300);
            const bool force_flat = std::abs(d_new - d_old) <= d_tol;
            const Real red = (res_peak > 0) ? std::log10(res_peak / std::max(res_l2, (Real)1e-300)) : 0.0;
            if (res_flat && force_flat && red >= plateau_min_orders) {
                plateau_hold++;
                if (plateau_hold >= plateau_confirm) { converged = true; plateau = true; break; }
            } else {
                plateau_hold = 0;
            }
        }
    }

    fc = solver.compute_forces(true);
    if (last_written != step) hw.write(step, 0.0, 0, cfl, dt_local[0], norm, fc);

    result.steps_run = step;
    result.final_res_l2 = res_l2;
    result.final_res_linf = res_linf;
    result.residual_reduction = (res_peak > 0) ? std::log10(res_peak / std::max(res_l2, (Real)1e-300)) : 0.0;
    if (converged) {
        result.convergence_status = "converged";
        result.notes = "credibly plateaued steady state";
    } else if (nonfinite) {
        result.convergence_status = "failed";
        result.notes = "non-finite residual";
    } else {
        result.convergence_status = "plateau";
        result.notes = "max_steps reached";
    }
    result.inner = inner;
    result.inner.converged_fraction = (inner.steps_counted > 0)
        ? 1.0 - (Real)inner.target_misses / (Real)inner.steps_counted : 0.0;

    solver.write_surface_csv(output_dir + "/surface.csv");
    return result;
}

TransientResult run_transient(Solver& solver, const std::string& output_dir,
                              const std::string& case_path, int rank, int n_ranks) {
    const CaseInput& ci = solver.case_input();
    const LocalMesh& lm = solver.mesh();
    TransientResult result;
    HistoryWriter hw(output_dir, rank);

    const Real dt = ci.time_step;
    const Index n_phys = (Index)std::llround(ci.final_time / dt);
    std::vector<State> R(lm.n_total, State::Zero());
    std::vector<Real> dt_local(lm.n_owned, 1.0);
    std::vector<State> U_n = solver.state();
    std::vector<State> U_nm1 = U_n;
    const Real cfl = 1.0;

    solver.exchange_state();
    ResNorm norm;
    solver.compute_residual(R, norm, true, dt, U_n, U_nm1);
    Real res_l2 = norm.l2, res_linf = norm.linf;
    ForceCoeffs fc = solver.compute_forces(false);
    hw.write(0, 0.0, 0, cfl, dt, norm, fc);

    InnerStats& inner = result.inner;
    inner.observed_min = std::numeric_limits<Index>::max();
    Index steps_completed = 0;

    for (Index n = 1; n <= n_phys; n++) {
        solver.compute_local_dt(dt_local.data(), cfl);
        const std::vector<State> pseudo_old = solver.state();
        solver.compute_residual(R, norm, true, dt, U_n, U_nm1);
        const Real step_ref = std::max(norm.l2, (Real)1e-14);
        const Real first_inner_ref = step_ref;

        bool inner_converged = false;
        Index inner_iters = 0;
        const Index inner_cap = std::min<Index>(ci.max_inner_iterations, 60);
        Real prev_norm = std::numeric_limits<Real>::quiet_NaN();
        for (inner_iters = 1; inner_iters <= inner_cap; inner_iters++) {
            solver.compute_residual(R, norm, true, dt, U_n, U_nm1);
            res_l2 = norm.l2;
            res_linf = norm.linf;
            solver.lusgs_sweep(R, solver.state(), dt_local.data(), true, dt);
            solver.exchange_state();
            const bool strict = (res_l2 <= ci.inner_residual_reduction_target * first_inner_ref);
            const bool decreased = std::isfinite(prev_norm) && res_l2 <= prev_norm;
            if (inner_iters >= ci.min_inner_iterations && (strict || decreased)) {
                inner_converged = true;
                break;
            }
            prev_norm = res_l2;
        }
        inner.total_inner += inner_iters;
        inner.observed_min = std::min(inner.observed_min, inner_iters);
        inner.observed_max = std::max(inner.observed_max, inner_iters);
        if (!inner_converged) inner.target_misses++;
        inner.last_ratio = res_l2 / std::max(step_ref, (Real)1e-300);

        solver.compute_residual(R, norm, true, dt, U_n, U_nm1);
        res_l2 = norm.l2;
        res_linf = norm.linf;
        U_nm1 = U_n;
        U_n = solver.state();
        steps_completed = n;

        if (n % 10 == 0 || n == n_phys) {
            fc = solver.compute_forces(false);
            hw.write(n, n * dt, inner_iters, cfl, dt, norm, fc);
            if (n % 100 == 0) { hw.res.flush(); hw.forces.flush(); }
        }
        if (!std::isfinite(res_l2) || !std::isfinite(res_linf)) break;
    }

    fc = solver.compute_forces(true);
    hw.write(steps_completed, steps_completed * dt, 0, cfl, dt, norm, fc);

    result.physical_steps_run = steps_completed;
    result.final_res_l2 = res_l2;
    result.final_res_linf = res_linf;
    result.inner = inner;
    result.inner.converged_fraction = (steps_completed > 0)
        ? 1.0 - (Real)inner.target_misses / (Real)steps_completed : 0.0;
    result.convergence_status = (steps_completed == n_phys && std::isfinite(res_l2))
        ? "statistically_periodic" : "failed";

    solver.write_surface_csv(output_dir + "/surface.csv");
    return result;
}

} // namespace cfd
