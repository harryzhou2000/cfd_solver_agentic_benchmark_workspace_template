#include "solve/transient_driver.h"

#include <algorithm>
#include <cmath>

#include "core/logging.h"
#include "solve/local_time_step.h"

namespace cns2d {

RunOutcome runTransient(SolverContext &context, OutputWriter &writer) {
  const CaseInput &input = context.input();
  const RunControl &rc = input.run_control;
  const DistributedMesh &mesh = context.mesh();
  ResidualAssembler &assembler = context.assembler();
  ImplicitSolver &implicit = context.implicit();
  HaloExchange &halo = context.halo();
  StateField &U = context.state();

  const Index num_owned = mesh.numOwned();
  const Index num_local = mesh.numLocal();

  StateField spatial_residual(num_local);
  StateField total_residual(num_local);
  StateField dU(num_local);
  // Frozen physical-time history.
  StateField U_n(num_local);
  StateField U_nm1(num_local);

  std::vector<Real> dtau;
  std::vector<Real> diag_scale(static_cast<std::size_t>(num_owned), 0.0);

  const Real dt = rc.time_step;
  const Real final_time = context.options().override_final_time > 0.0
                              ? context.options().override_final_time
                              : rc.final_time;
  const int num_physical_steps = static_cast<int>(std::llround(final_time / dt));

  RunOutcome outcome;
  outcome.notes = "dual-time BDF2 transient with an outer physical-time loop and inner LU-SGS "
                  "nonlinear iterations";

  logInfo(formatString(
      "transient run: dt=%.6g, final_time=%.6g (%d physical steps), inner iterations %d..%d, "
      "inner target %.2e on the total transient residual, pseudo-CFL %.3g -> %.3g",
      dt, final_time, num_physical_steps, rc.min_inner_iterations, rc.max_inner_iterations,
      rc.inner_residual_reduction_target, rc.cfl_initial, rc.cfl_max));

  // Initialise both history levels to the starting state.
  for (Index c = 0; c < num_local; ++c) {
    const ConsVec u = U.get(c);
    U_n.set(c, u);
    U_nm1.set(c, u);
  }

  Real physical_time = 0.0;
  int step = 0;
  ForceResult last_forces;
  Real first_step_residual = 0.0;
  int next_field_index = 1;
  Real next_field_time = rc.time_step > 0.0 ? input.outputs.write_field_every_time : 0.0;
  bool diverged = false;

  for (step = 1; step <= num_physical_steps; ++step) {
    // --- BDF2 coefficients (BDF1 on the first step, which is self-starting) ---
    Real a0 = 1.5;
    Real a1 = -2.0;
    Real a2 = 0.5;
    if (step == 1) {
      a0 = 1.0;
      a1 = -1.0;
      a2 = 0.0;
    }

    // ================= INNER NONLINEAR LOOP =================
    // U^n and U^{n-1} stay frozen throughout; only U (the U^{n+1} iterate)
    // changes.  Histories are shifted only after this loop is accepted.
    int inner_used = 0;
    Real inner_ratio = 1.0;
    Real initial_total_residual = 0.0;
    bool hit_target = false;

    for (int inner = 1; inner <= rc.max_inner_iterations; ++inner) {
      context.syncState();

      ResidualDiagnostics diag;
      assembler.evaluate(U, spatial_residual, halo, diag);
      outcome.positivity_fallbacks += diag.positivity_fallbacks;

      // Total transient residual:
      //   R* = R_spatial - V * ( a0 U^{n+1} + a1 U^n + a2 U^{n-1} ) / dt
      for (Index c = 0; c < num_owned; ++c) {
        const Real volume = mesh.cells()[static_cast<std::size_t>(c)].volume;
        const Real *rs = spatial_residual.cell(c);
        const Real *u = U.cell(c);
        const Real *un = U_n.cell(c);
        const Real *unm1 = U_nm1.cell(c);
        Real *rt = total_residual.cell(c);
        for (int k = 0; k < kNumVars; ++k) {
          const Real time_term = (a0 * u[k] + a1 * un[k] + a2 * unm1[k]) / dt;
          rt[k] = rs[k] - volume * time_term;
        }
      }

      const ResidualNorms norms = context.computeNorms(total_residual);
      if (!std::isfinite(norms.l2)) {
        diverged = true;
        outcome.convergence_status = "failed";
        outcome.notes = formatString(
            "total transient residual became non-finite at physical step %d (t = %.4f), inner "
            "iteration %d",
            step, physical_time, inner);
        logError(outcome.notes);
        break;
      }

      if (inner == 1) {
        initial_total_residual = norms.l2;
        if (step == 1) first_step_residual = norms.l2;
      }
      inner_ratio = (initial_total_residual > 0.0) ? norms.l2 / initial_total_residual : 0.0;
      inner_used = inner;

      // Stop once the inner target is met, provided the minimum number of inner
      // iterations has been performed.
      if (inner >= rc.min_inner_iterations && inner_ratio <= rc.inner_residual_reduction_target) {
        hit_target = true;
        break;
      }

      // --- implicit update of the iterate --------------------------------
      const Real cfl = rampedCfl(rc.cfl_initial, rc.cfl_max, rc.pseudo_cfl_ramp_steps, step - 1);
      computeLocalTimeSteps(mesh, cfl, assembler.convectiveSpectralRadius(),
                            assembler.viscousSpectralRadius(), 4.0, dtau);
      for (Index c = 0; c < num_owned; ++c) {
        const Real volume = mesh.cells()[static_cast<std::size_t>(c)].volume;
        const Real dt_local = dtau[static_cast<std::size_t>(c)];
        // Diagonal carries BOTH the pseudo-time term and the physical-time term,
        // so the inner iteration is a genuine Newton-like solve of the
        // time-accurate system rather than a pseudo-time march with a source.
        diag_scale[static_cast<std::size_t>(c)] =
            (dt_local > 0.0 ? volume / dt_local : volume) + volume * a0 / dt;
      }

      implicit.solve(U, total_residual, diag_scale, assembler.convectiveSpectralRadius(),
                     assembler.viscousSpectralRadius(), 2, dU, halo);

      for (Index c = 0; c < num_owned; ++c) {
        ConsVec u = U.get(c);
        const Real *du = dU.cell(c);
        for (int k = 0; k < kNumVars; ++k) u[k] += du[k];
        U.set(c, u);
      }
      outcome.positivity_fallbacks += context.enforcePositivity(U);
    }

    if (diverged) break;
    outcome.inner_stats.record(inner_used, hit_target, inner_ratio);

    // ================= ACCEPT THE PHYSICAL STEP =================
    // Only now are the histories shifted: U^{n-1} <- U^n, U^n <- U^{n+1}.
    for (Index c = 0; c < num_local; ++c) {
      U_nm1.set(c, U_n.get(c));
      U_n.set(c, U.get(c));
    }
    physical_time = static_cast<Real>(step) * dt;

    // --- output ------------------------------------------------------------
    context.syncState();
    {
      ResidualDiagnostics diag;
      assembler.evaluate(U, spatial_residual, halo, diag);
      for (Index c = 0; c < num_owned; ++c) {
        const Real volume = mesh.cells()[static_cast<std::size_t>(c)].volume;
        const Real *rs = spatial_residual.cell(c);
        const Real *u = U.cell(c);
        const Real *un = U_n.cell(c);
        const Real *unm1 = U_nm1.cell(c);
        Real *rt = total_residual.cell(c);
        for (int k = 0; k < kNumVars; ++k) {
          const Real time_term = (a0 * u[k] + a1 * un[k] + a2 * unm1[k]) / dt;
          rt[k] = rs[k] - volume * time_term;
        }
      }
      const ResidualNorms norms = context.computeNorms(total_residual);

      if (step % input.outputs.write_residuals_every == 0 || step == 1 ||
          step == num_physical_steps) {
        ResidualRow row;
        row.step = step;
        row.physical_time = physical_time;
        row.inner_iter = inner_used;
        row.cfl = rampedCfl(rc.cfl_initial, rc.cfl_max, rc.pseudo_cfl_ramp_steps, step - 1);
        row.dt = dt;
        row.component = norms.component_l2;
        row.l2 = norms.l2;
        row.linf = norms.linf;
        writer.appendResidual(row);
      }

      if (step % input.outputs.write_forces_every == 0 || step == 1 || step == num_physical_steps) {
        last_forces = computeForces(mesh, context.flow(), assembler, U, context.comm());
        // Forces are sampled by PHYSICAL time, as the contract requires.
        writer.appendForces(step, physical_time, last_forces);
      }
    }

    if (step % context.options().log_every == 0 || step == 1) {
      logRaw(formatString(
          "  step %6d  t %9.3f  inner %4d (ratio %8.2e%s)  cd %10.6f  cl %11.6f", step,
          physical_time, inner_used, inner_ratio, hit_target ? "" : " MISS", last_forces.cd,
          last_forces.cl));
    }

    // Intermediate field dumps at a fixed physical-time cadence.
    if (context.options().write_intermediate_fields && input.outputs.write_field_every_time > 0.0 &&
        physical_time + 1.0e-9 >= next_field_time) {
      writer.writeIntermediateField(next_field_index, physical_time);
      ++next_field_index;
      next_field_time += input.outputs.write_field_every_time;
    }
  }

  if (step > num_physical_steps) step = num_physical_steps;
  outcome.final_step = step;
  outcome.final_physical_time = physical_time;
  outcome.initial_residual = first_step_residual;

  if (diverged) {
    outcome.completed = false;
    return outcome;
  }

  // --- final state, forces and status -----------------------------------
  context.syncState();
  {
    ResidualDiagnostics diag;
    assembler.evaluate(U, spatial_residual, halo, diag);
    const ResidualNorms norms = context.computeNorms(spatial_residual);
    outcome.final_residual = norms.l2;
    outcome.residual_reduction_orders =
        (first_step_residual > 0.0 && norms.l2 > 0.0)
            ? std::log10(first_step_residual / norms.l2)
            : 0.0;

    const ForceResult final_forces = computeForces(mesh, context.flow(), assembler, U, context.comm());
    // The final force row corresponds exactly to the final field/surface output.
    writer.appendForces(outcome.final_step, physical_time, final_forces);
    logInfo(formatString("final transient state: t = %.4f, C_D = %.6f, C_L = %.6f", physical_time,
                         final_forces.cd, final_forces.cl));
  }

  // A transient vortex-street run is 'statistically_periodic' when it reached the
  // requested horizon and the inner solve actually converged on essentially every
  // physical step.  Otherwise the run is reported as failed: a run that reaches
  // the final time while missing the inner target is an under-converged dual-time
  // solve, not a usable transient result.
  const Real converged_fraction = outcome.inner_stats.convergedFraction();
  const bool reached_horizon = outcome.final_physical_time >= final_time - 1.0e-9;
  if (reached_horizon && converged_fraction >= 0.95) {
    outcome.convergence_status = "statistically_periodic";
    outcome.notes = formatString(
        "reached t = %.2f in %d physical steps of dt = %.4g; inner iterations min/mean/max = "
        "%d/%.1f/%d, inner target %.1e met on %.2f%% of steps (%lld misses)",
        physical_time, outcome.final_step, dt, outcome.inner_stats.min_inner,
        outcome.inner_stats.meanInner(), outcome.inner_stats.max_inner,
        rc.inner_residual_reduction_target, 100.0 * converged_fraction,
        outcome.inner_stats.target_misses);
    outcome.completed = true;
  } else {
    outcome.convergence_status = "failed";
    outcome.notes = formatString(
        "transient solve incomplete: reached t = %.2f of %.2f, inner target met on only %.2f%% of "
        "physical steps (%lld misses); an under-converged dual-time solve cannot be reported as "
        "statistically periodic",
        physical_time, final_time, 100.0 * converged_fraction, outcome.inner_stats.target_misses);
    outcome.completed = false;
    logWarn(outcome.notes);
  }

  return outcome;
}

}  // namespace cns2d
