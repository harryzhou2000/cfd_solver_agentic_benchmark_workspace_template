#include "solver/transient_solver.hpp"

#include <fmt/format.h>

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

TransientResult run_transient_solver(const CaseConfig& cfg,
                                     DistributedMesh& dmesh,
                                     std::vector<double>& U,
                                     const std::string& output_dir,
                                     MPI_Comm comm) {
  TransientResult result;
  int rank = 0;
  MPI_Comm_rank(comm, &rank);

  const double dt_phys = cfg.run_control.time_step.value_or(0.0);
  const double final_time = cfg.run_control.final_time.value_or(0.0);
  if (!(dt_phys > 0.0) || !(final_time > 0.0))
    throw std::runtime_error("run_transient_solver: case '" + cfg.case_id +
                             "' requires positive run_control.time_step and "
                             "final_time");
  const double inner_target =
      cfg.run_control.inner_residual_reduction_target.value_or(0.001);
  const int min_inner = cfg.run_control.min_inner_iterations.value_or(5);
  const int max_inner = cfg.run_control.max_inner_iterations.value_or(1000);
  const int write_every = std::max(1, cfg.outputs.write_forces_every);
  const int write_residuals_every =
      std::max(1, cfg.outputs.write_residuals_every);
  // Pseudo-time CFL for the dual-time loop (fixed near 1 for transients).
  const double cfl = cfg.run_control.cfl_initial;

  const double gamma = cfg.gas.gamma;
  const double R = cfg.gas.R;
  const double Pr = cfg.gas.prandtl;
  double mu = 0.0;
  if (cfg.physics.mode == "laminar") {
    if (!cfg.physics.reynolds.has_value())
      throw std::runtime_error("run_transient_solver: laminar case '" +
                               cfg.case_id + "' is missing physics.reynolds");
    mu = cfg.freestream.rho * cfg.freestream.velocity_magnitude *
         cfg.reference.reynolds_length / *cfg.physics.reynolds;
  }

  const long long n_owned = dmesh.n_owned;
  const long long nlocal = static_cast<long long>(dmesh.cells.size());
  const long long n_steps =
      cfg.run_control.max_steps.has_value()
          ? static_cast<long long>(*cfg.run_control.max_steps)
          : static_cast<long long>(std::ceil(final_time / dt_phys));

  const std::vector<std::vector<int>> cell_face_map =
      build_cell_face_map(dmesh);
  std::vector<double> residual(static_cast<std::size_t>(nlocal) * NVARS);
  std::vector<double> residual1(static_cast<std::size_t>(nlocal) * NVARS);
  std::vector<double> dU;
  std::vector<double> U_prev = U;  // U^{n-1}
  std::vector<double> U_cur = U;   // U^n

  long long inner_total = 0;
  bool diverged = false;
  result.convergence_status = "completed";
  int stat_min_inner = max_inner + 1;
  int stat_max_inner = 0;
  long long stat_misses = 0;
  long long stat_converged_steps = 0;
  double stat_last_ratio = 0.0;
  double step_norm_per_var[NVARS] = {0.0, 0.0, 0.0, 0.0};
  double step_norm_linf = 0.0;
  double last_cfl = cfl;

  for (long long step = 1; step <= n_steps; ++step) {
    const double t = static_cast<double>(step) * dt_phys;
    double norm_total = 0.0;
    double inner_ref = 0.0;
    int inner_used = 0;
    bool inner_converged = false;
    // BDF2 diagonal contribution: d(R_phys)/dU = 3V/(2*dt_phys).
    std::vector<double> diag_extra(static_cast<std::size_t>(n_owned));
    for (long long c = 0; c < n_owned; ++c)
      diag_extra[static_cast<std::size_t>(c)] =
          3.0 * dmesh.cells[static_cast<std::size_t>(c)].volume /
          (2.0 * dt_phys);

    // Pseudo-time steps for the dual-time inner loop (frozen per step).
    const std::vector<double> dt_pseudo =
        compute_local_timesteps(U, dmesh, cfl, gamma, mu, Pr, R);

    // Second-order spatial defect frozen during the inner loop (defect
    // correction, see run_steady_solver): the inner sweeps solve the
    // first-order spatial problem plus the frozen defect and the BDF2 term.
    std::vector<double> defect;
    {
      halo_exchange(dmesh, U, NVARS, comm);
      std::vector<double> gradients;
      compute_gradients(U, dmesh, cell_face_map, gradients, gamma);
      barth_jespersen_limit(gradients, U, dmesh, cell_face_map, gamma);
      halo_exchange(dmesh, gradients, NVARS * 2, comm);
      compute_residual(U, dmesh, cfg, mu, residual, &gradients);
      compute_residual(U, dmesh, cfg, mu, residual1, nullptr);
      defect.assign(static_cast<std::size_t>(n_owned) * NVARS, 0.0);
      for (long long c = 0; c < n_owned; ++c)
        for (int k = 0; k < NVARS; ++k)
          defect[static_cast<std::size_t>(c) * NVARS + k] =
              residual[static_cast<std::size_t>(c) * NVARS + k] -
              residual1[static_cast<std::size_t>(c) * NVARS + k];
    }

    // Inner-iteration statistics.
    double step_inner_ref = 0.0;
    double step_inner_final = 0.0;

    for (int inner = 1; inner <= max_inner; ++inner) {
      inner_used = inner;
      ++inner_total;
      try {
        // RHS = R1(U) + defect + BDF2 physical-time term (histories U^n and
        // U^{n-1} are frozen during the inner loop).
        halo_exchange(dmesh, U, NVARS, comm);
        compute_residual(U, dmesh, cfg, mu, residual1, nullptr);
        std::vector<double> rhs = residual1;
        const double fac = 1.0 / (2.0 * dt_phys);
        for (long long c = 0; c < n_owned; ++c) {
          double* r = rhs.data() + static_cast<std::size_t>(c) * NVARS;
          const double* u_nm1 =
              U_prev.data() + static_cast<std::size_t>(c) * NVARS;
          const double* u_n = U_cur.data() + static_cast<std::size_t>(c) * NVARS;
          const double* u_np1 = U.data() + static_cast<std::size_t>(c) * NVARS;
          const double V = dmesh.cells[static_cast<std::size_t>(c)].volume;
          for (int k = 0; k < NVARS; ++k) {
            r[k] += defect[static_cast<std::size_t>(c) * NVARS + k];
            r[k] += V * fac * (3.0 * u_np1[k] - 4.0 * u_n[k] + u_nm1[k]);
          }
        }

        const ResidualNorm norm = compute_norms(rhs, dmesh, comm);
        norm_total = norm.l2;
        step_inner_final = norm.l2;
        for (int k = 0; k < NVARS; ++k)
          step_norm_per_var[k] = norm.l2_per_var[k];
        step_norm_linf = norm.linf;
        if (inner == 1) {
          inner_ref = norm.l2;
          step_inner_ref = norm.l2;
        }
        if (!std::isfinite(norm.l2)) {
          diverged = true;
          result.convergence_status = "diverged (non-finite residual)";
          break;
        }
        if (inner >= min_inner && norm.l2 < inner_ref * inner_target) {
          inner_converged = true;
          break;
        }
        if (inner == max_inner) break;

        lusgs_sweep(U, rhs, dmesh, dt_pseudo, cell_face_map, gamma, dU,
                    &diag_extra);
        limit_update_positivity(U, dU, n_owned, gamma);
        for (long long c = 0; c < n_owned; ++c) {
          double* u = U.data() + static_cast<std::size_t>(c) * NVARS;
          const double* du = dU.data() + static_cast<std::size_t>(c) * NVARS;
          for (int k = 0; k < NVARS; ++k) u[k] += du[k];
        }
      } catch (const std::exception& e) {
        diverged = true;
        result.convergence_status = std::string("diverged (") + e.what() + ")";
        break;
      }
    }
    if (diverged) break;

    // Histories are updated ONLY after the inner loop (BDF2 rule: freeze
    // U^n and U^{n-1} throughout the inner iterations).
    U_prev = U_cur;
    U_cur = U;
    result.steps_run = step;
    result.final_time = t;

    // Inner-iteration statistics.
    stat_min_inner = std::min(stat_min_inner, inner_used);
    stat_max_inner = std::max(stat_max_inner, inner_used);
    if (inner_converged) {
      ++stat_converged_steps;
    } else {
      ++stat_misses;
    }
    if (step_inner_ref > 0.0)
      stat_last_ratio = step_inner_final / step_inner_ref;

    const bool verbose =
        rank == 0 && (step == 1 || step == n_steps || step % write_every == 0);
    const ForceResult F = compute_forces(U, dmesh, cfg, mu);  // per-rank
    double ftot[5] = {F.pressure_drag, F.viscous_drag, F.pressure_lift,
                      F.viscous_lift, F.moment_z};
    double ftot_g[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    MPI_Allreduce(ftot, ftot_g, 5, MPI_DOUBLE, MPI_SUM, comm);
    if (verbose) {
      fmt::print(
          "[transient] step {:6}/{}: t={:8.4f} inner={:4d}/{:d} "
          "inner_status={} |R_total|={:11.4e} Cd={:+.6e} Cl={:+.6e} "
          "Cm={:+.6e}\n",
          step, n_steps, t, inner_used, max_inner,
          inner_converged ? "converged" : "max_inner", norm_total,
          ftot_g[0] + ftot_g[1], ftot_g[2] + ftot_g[3], ftot_g[4]);
    }
    if (rank == 0 &&
        (step == 1 || step == n_steps ||
         step % write_residuals_every == 0))
      write_residual_row(output_dir, static_cast<int>(step), t, inner_used,
                         last_cfl, dt_phys, step_norm_per_var[0],
                         step_norm_per_var[1], step_norm_per_var[2],
                         step_norm_per_var[3], norm_total, step_norm_linf);
    if (rank == 0 && (step == 1 || step == n_steps ||
                      step % write_every == 0))
      write_forces_row(output_dir, static_cast<int>(step), t,
                       ftot_g[2] + ftot_g[3], ftot_g[0] + ftot_g[1], ftot_g[4],
                       ftot_g[0], ftot_g[1], ftot_g[2], ftot_g[3]);
    // Intermediate field snapshots (optional per contract).
    if (cfg.outputs.write_field_every_time.has_value() &&
        *cfg.outputs.write_field_every_time > 0.0 &&
        (step == n_steps ||
         std::fmod(t, *cfg.outputs.write_field_every_time) < 1e-9))
      write_field_vtu(output_dir, fmt::format("field_t{:.3f}", t), dmesh, U,
                      cfg, comm);
  }

  result.inner_iterations_total = inner_total;
  result.observed_min_inner_iterations =
      result.steps_run > 0 ? stat_min_inner : 0;
  result.observed_max_inner_iterations = stat_max_inner;
  result.inner_target_misses = stat_misses;
  result.inner_target_converged_fraction =
      result.steps_run > 0
          ? static_cast<double>(stat_converged_steps) /
                static_cast<double>(result.steps_run)
          : 0.0;
  result.last_inner_residual_ratio = stat_last_ratio;
  if (result.convergence_status != "diverged (non-finite residual)" &&
      result.convergence_status.rfind("diverged", 0) != 0)
    result.convergence_status = "completed";
  return result;
}

}  // namespace cfd
