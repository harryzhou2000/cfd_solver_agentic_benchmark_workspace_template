#include "solver/steady_solver.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "numerics/limiter.hpp"
#include "numerics/lusgs.hpp"
#include "output/output_writer.hpp"
#include "numerics/reconstruction.hpp"
#include "numerics/time_integration.hpp"
#include "parallel/halo_exchange.hpp"
#include "physics/forces.hpp"
#include "physics/gas_model.hpp"
#include "solver/residual.hpp"

namespace cfd {

SteadyResult run_steady_solver(const CaseConfig& cfg, DistributedMesh& dmesh,
                               std::vector<double>& U,
                               const std::string& output_dir, MPI_Comm comm) {
  SteadyResult result;
  int rank = 0;
  MPI_Comm_rank(comm, &rank);

  const int max_steps = cfg.run_control.max_steps.value_or(-1);
  if (max_steps < 1)
    throw std::runtime_error("run_steady_solver: case '" + cfg.case_id +
                             "' has no valid run_control.max_steps");
  const double target_orders =
      cfg.run_control.residual_reduction_target.value_or(99.0);
  const double inner_target =
      cfg.run_control.inner_residual_reduction_target.value_or(0.01);
  const int min_inner = cfg.run_control.min_inner_iterations.value_or(3);
  const int max_inner = cfg.run_control.max_inner_iterations.value_or(50);
  const int write_every = std::max(1, cfg.outputs.write_forces_every);
  const int write_residuals_every =
      std::max(1, cfg.outputs.write_residuals_every);

  const double gamma = cfg.gas.gamma;
  const double R = cfg.gas.R;
  const double Pr = cfg.gas.prandtl;
  double mu = 0.0;
  if (cfg.physics.mode == "laminar") {
    if (!cfg.physics.reynolds.has_value())
      throw std::runtime_error("run_steady_solver: laminar case '" +
                               cfg.case_id + "' is missing physics.reynolds");
    mu = cfg.freestream.rho * cfg.freestream.velocity_magnitude *
         cfg.reference.reynolds_length / *cfg.physics.reynolds;
  }

  const long long n_owned = dmesh.n_owned;
  const long long nlocal = static_cast<long long>(dmesh.cells.size());
  const std::vector<std::vector<int>> cell_face_map =
      build_cell_face_map(dmesh);

  std::vector<double> residual(static_cast<std::size_t>(nlocal) * NVARS);
  std::vector<double> residual1(static_cast<std::size_t>(nlocal) * NVARS);
  std::vector<double> dU;
  int inner_used = 0;
  bool inner_converged = false;
  double R0 = 0.0;
  bool have_R0 = false;
  double norm_l2 = 0.0, norm_linf = 0.0;
  double norm_per_var[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double last_dt = 0.0;  // mean local pseudo-time step of the last outer step
  double max_dU = 0.0;
  int inner_total = 0;
  int steps_completed = 0;
  bool diverged = false;
  result.convergence_status = "max_steps_reached";

  // CFL controller (Phase 4 remediation): follow the case-file schedule
  // directly (initial -> max over pseudo_cfl_ramp_steps, then hold); only
  // retreat by x0.6 on a clear divergence indicator (non-finite residual or
  // >10x growth from the previous outer step) and then resume the schedule.
  double cfl_scale = 1.0;
  double prev_norm_l2 = -1.0;
  double last_cfl = cfg.run_control.cfl_initial;

  // Damped second-order defect (persistent across outer steps):
  //   defect <- (1-theta)*defect + theta*(R2(U) - R1(U))
  // Relaxing the defect update kills the chase oscillation of the inner
  // loop while preserving the R2(U) = 0 fixed point. theta = 1 on the first
  // step (plain defect correction), then damped.
  // Damped-defect relaxation factor (0.5: geometric convergence of the
  // defect; the fixed point is the true second-order state R2 = 0).
  constexpr double kDefectTheta = 0.5;
  // Persistent damped defect: declared OUTSIDE the outer loop so the
  // relaxation accumulates across steps (a local declaration would reset it
  // to zero every step, pinning the fixed point at defect = theta*D instead
  // of D and leaving R2 = (1-theta)*D as the residual floor).
  std::vector<double> defect;
  bool defect_initialized = false;

  // Inner-iteration statistics.
  int stat_min_inner = max_inner + 1;
  int stat_max_inner = 0;
  long long stat_misses = 0;
  long long stat_converged_steps = 0;
  long long stat_outer_steps = 0;
  double stat_last_ratio = 0.0;

  for (int step = 1; step <= max_steps; ++step) {
    steps_completed = step;
    const double cfl_sched = compute_cfl(step, cfg.run_control.cfl_initial,
                                         cfg.run_control.cfl_max,
                                         cfg.run_control.pseudo_cfl_ramp_steps);
    last_cfl = cfl_sched * cfl_scale;
    const double cfl = last_cfl;

    try {
      // Fresh ghosts and local time steps for this outer step.
      halo_exchange(dmesh, U, NVARS, comm);
      const std::vector<double> dt =
          compute_local_timesteps(U, dmesh, cfl, gamma, mu, Pr, R);
      if (!dt.empty()) {
        double dsum = 0.0;
        for (double d : dt) dsum += d;
        last_dt = dsum / static_cast<double>(dt.size());
      }

      // Second-order defect: D = R2(U) - R1(U), frozen during the inner
      // loop. The inner LU-SGS sweeps then solve the FIRST-ORDER problem
      // R1(U) + D = 0, whose scalar spectral-radius Jacobian is a faithful
      // model; the frozen defect restores second-order accuracy of the outer
      // iteration (defect correction).
      std::vector<double> gradients;
      if (!defect_initialized)
        defect.assign(static_cast<std::size_t>(n_owned) * NVARS, 0.0);
      compute_gradients(U, dmesh, cell_face_map, gradients, gamma);
      barth_jespersen_limit(gradients, U, dmesh, cell_face_map, gamma);
      halo_exchange(dmesh, gradients, NVARS * 2, comm);
      compute_residual(U, dmesh, cfg, mu, residual, &gradients);
      compute_residual(U, dmesh, cfg, mu, residual1, nullptr);
      std::vector<double> dnew(static_cast<std::size_t>(n_owned) * NVARS);
      for (long long c = 0; c < n_owned; ++c)
        for (int k = 0; k < NVARS; ++k)
          dnew[static_cast<std::size_t>(c) * NVARS + k] =
              residual[static_cast<std::size_t>(c) * NVARS + k] -
              residual1[static_cast<std::size_t>(c) * NVARS + k];
      if (!defect_initialized) {
        defect = dnew;
        defect_initialized = true;
      } else {
        for (std::size_t i = 0; i < defect.size(); ++i)
          defect[i] = (1.0 - kDefectTheta) * defect[i] +
                      kDefectTheta * dnew[i];
      }

      // Inner relaxation iterations on RHS = R1(U) + defect, with adaptive
      // under-relaxation: omega is compared against the FIXED start-of-step
      // residual (inner_ref) — it grows toward 1.0 while the sweeps keep
      // improving on the step start and retreats toward 0.3 when they
      // worsen (damping the weakly unstable flip mode of the simplified
      // Jacobian at transonic conditions).
      double omega = 0.6;
      double inner_ref = 0.0;
      double final_rhs = 0.0;
      inner_converged = false;
      inner_used = 0;
      for (int inner = 1; inner <= max_inner; ++inner) {
        inner_used = inner;
        halo_exchange(dmesh, U, NVARS, comm);
        compute_residual(U, dmesh, cfg, mu, residual1, nullptr);
        std::vector<double> rhs = residual1;
        for (long long c = 0; c < n_owned; ++c)
          for (int k = 0; k < NVARS; ++k)
            rhs[static_cast<std::size_t>(c) * NVARS + k] +=
                defect[static_cast<std::size_t>(c) * NVARS + k];
        const ResidualNorm norm = compute_norms(rhs, dmesh, comm);
        final_rhs = norm.l2;
        ++inner_total;
        if (std::getenv("CFD_DEBUG_INNER") != nullptr && step >= 150 &&
            step <= 151 && (inner <= 5 || inner % 5 == 0))
          fmt::print("[INN] step={} inner={:3d} |RHS|={:.6e} omega={:.3f}\n",
                     step, inner, norm.l2, omega);
        if (!have_R0) {
          R0 = norm.l2;
          have_R0 = true;
        }
        if (inner == 1) inner_ref = norm.l2;
        if (!std::isfinite(norm.l2)) {
          diverged = true;
          result.convergence_status = "diverged (non-finite residual)";
          break;
        }
        // Adaptive relaxation vs the start-of-step residual: grow toward
        // 1.0 while the sweeps keep improving on the step start, retreat
        // toward 0.3 when they worsen (protects the march when the implicit
        // operator misses the viscous off-diagonal coupling).
        if (inner > 1)
          omega = std::max(0.3,
                           std::min(1.0, omega * (norm.l2 < inner_ref ? 1.1 : 0.7)));
        // Convergence: config inner target, or a strong absolute reduction.
        if (inner >= min_inner &&
            (norm.l2 < inner_ref * inner_target ||
             (norm.l2 < inner_ref / 5.0 && norm.l2 < 1e-6))) {
          inner_converged = true;
          break;
        }
        if (inner >= max_inner) break;  // iteration cap
        lusgs_sweep(U, rhs, dmesh, dt, cell_face_map, gamma, dU);
        for (double& v : dU) v *= omega;
        limit_update_positivity(U, dU, n_owned, gamma);
        max_dU = 0.0;
        for (long long c = 0; c < n_owned; ++c) {
          double* u = U.data() + static_cast<std::size_t>(c) * NVARS;
          const double* du = dU.data() + static_cast<std::size_t>(c) * NVARS;
          for (int k = 0; k < NVARS; ++k) {
            u[k] += du[k];
            max_dU = std::max(max_dU, std::fabs(du[k]));
          }
        }
      }
      if (diverged) break;

      // Inner-iteration statistics.
      ++stat_outer_steps;
      stat_min_inner = std::min(stat_min_inner, inner_used);
      stat_max_inner = std::max(stat_max_inner, inner_used);
      if (inner_converged) {
        ++stat_converged_steps;
      } else {
        ++stat_misses;
      }
      if (inner_ref > 0.0) stat_last_ratio = final_rhs / inner_ref;

      // Outer residual of the updated state (second-order, for reporting and
      // the outer convergence test).
      halo_exchange(dmesh, U, NVARS, comm);
      compute_gradients(U, dmesh, cell_face_map, gradients, gamma);
      barth_jespersen_limit(gradients, U, dmesh, cell_face_map, gamma);
      halo_exchange(dmesh, gradients, NVARS * 2, comm);
      compute_residual(U, dmesh, cfg, mu, residual, &gradients);
      const ResidualNorm norm = compute_norms(residual, dmesh, comm);
      norm_l2 = norm.l2;
      norm_linf = norm.linf;
      for (int k = 0; k < NVARS; ++k) norm_per_var[k] = norm.l2_per_var[k];
      if (!std::isfinite(norm_l2)) {
        diverged = true;
        result.convergence_status = "diverged (non-finite residual)";
        break;
      }
    } catch (const std::exception& e) {
      diverged = true;
      result.convergence_status = std::string("diverged (") + e.what() + ")";
      break;
    }

    // CFL controller: retreat only on a clear divergence indicator, then
    // resume the schedule.
    if (prev_norm_l2 > 0.0 &&
        (!std::isfinite(norm_l2) || norm_l2 > 10.0 * prev_norm_l2)) {
      cfl_scale *= 0.6;
    } else {
      cfl_scale = std::min(1.0, cfl_scale * 1.05);  // recover towards schedule
    }
    prev_norm_l2 = norm_l2;

    // Diagnostics (all ranks participate in the force reduction).
    const bool verbose =
        rank == 0 && (step == 1 || step == max_steps || step % write_every == 0);
    const ForceResult F = compute_forces(U, dmesh, cfg, mu);  // per-rank
    double ftot[5] = {F.pressure_drag, F.viscous_drag, F.pressure_lift,
                      F.viscous_lift, F.moment_z};
    double ftot_g[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    MPI_Allreduce(ftot, ftot_g, 5, MPI_DOUBLE, MPI_SUM, comm);
    if (verbose) {
      const double orders = (have_R0 && R0 > 0.0 && norm_l2 > 0.0)
                                ? std::log10(R0 / norm_l2)
                                : 0.0;
      fmt::print(
          "[steady] step {:5d}/{:d}: cfl={:8.3f} L2={:11.4e} Linf={:10.3e} "
          "reduction={:7.3f} orders inner={:4d}/{:d} {} max|dU|={:9.2e} "
          "Cd={:+.6e}(p {:+.6e},v {:+.6e}) Cl={:+.6e} Cm={:+.6e}\n",
          step, max_steps, cfl, norm_l2, norm_linf, orders, inner_used,
          max_inner, inner_converged ? "conv" : "noconv", max_dU,
          ftot_g[0] + ftot_g[1], ftot_g[0], ftot_g[1], ftot_g[2] + ftot_g[3],
          ftot_g[4]);
    }
    if (rank == 0 &&
        (step == 1 || step == max_steps ||
         step % write_residuals_every == 0))
      write_residual_row(output_dir, step, 0.0, inner_used, cfl, last_dt,
                         norm_per_var[0], norm_per_var[1], norm_per_var[2],
                         norm_per_var[3], norm_l2, norm_linf);
    if (rank == 0 && (step == 1 || step == max_steps ||
                      step % write_every == 0))
      write_forces_row(output_dir, step, 0.0, ftot_g[2] + ftot_g[3],
                       ftot_g[0] + ftot_g[1], ftot_g[4], ftot_g[0], ftot_g[1],
                       ftot_g[2], ftot_g[3]);

    // Outer convergence: R/R0 below 10^-target.
    if (have_R0 && R0 > 0.0 && norm_l2 <= R0 * std::pow(10.0, -target_orders)) {
      result.convergence_status = "converged";
      break;
    }
  }

  result.steps_run = steps_completed;
  result.inner_iterations_total = inner_total;
  result.final_residual_l2 = norm_l2;
  result.final_residual_linf = norm_linf;
  result.final_cfl = last_cfl;
  result.residual_reduction_orders =
      (have_R0 && R0 > 0.0 && norm_l2 > 0.0) ? std::log10(R0 / norm_l2) : 0.0;
  result.observed_min_inner_iterations =
      stat_outer_steps > 0 ? stat_min_inner : 0;
  result.observed_max_inner_iterations = stat_max_inner;
  result.inner_target_misses = stat_misses;
  result.inner_target_converged_fraction =
      stat_outer_steps > 0
          ? static_cast<double>(stat_converged_steps) /
                static_cast<double>(stat_outer_steps)
          : 0.0;
  result.last_inner_residual_ratio = stat_last_ratio;
  return result;
}

}  // namespace cfd
