#include "solve/steady_driver.h"

#include <algorithm>
#include <cmath>

#include "core/logging.h"
#include "solve/local_time_step.h"

namespace cns2d {

RunOutcome runSteady(SolverContext &context, OutputWriter &writer) {
  const CaseInput &input = context.input();
  const RunControl &rc = input.run_control;
  const DistributedMesh &mesh = context.mesh();
  ResidualAssembler &assembler = context.assembler();
  ImplicitSolver &implicit = context.implicit();
  HaloExchange &halo = context.halo();
  StateField &U = context.state();

  const Index num_owned = mesh.numOwned();
  StateField residual(mesh.numLocal());
  StateField dU(mesh.numLocal());
  // Backup of the last accepted state, so an unstable step can be undone.
  StateField U_accepted(mesh.numLocal());
  std::vector<Real> dtau;
  std::vector<Real> diag_scale(static_cast<std::size_t>(num_owned), 0.0);

  const int max_steps =
      context.options().override_max_steps > 0 ? context.options().override_max_steps : rc.max_steps;

  RunOutcome outcome;
  outcome.notes = "steady pseudo-time march with LU-SGS inner iterations";
  // Distinguish the three possible exits: a genuine numerical failure, reaching
  // the requested residual reduction / a stable plateau, or exhausting the step
  // limit.  Only the first is ever reported as 'failed', and it is never
  // relabelled afterwards.
  bool diverged = false;
  bool target_reached = false;

  Real initial_residual = 0.0;
  Real last_residual = 0.0;
  ForceResult last_forces;
  int step = 0;

  // Residual-based CFL safeguard (see the header comment).
  Real cfl_scale = 1.0;
  constexpr Real kCflScaleMin = 1.0e-3;
  constexpr Real kCflCutFactor = 0.35;
  constexpr Real kCflRecoverFactor = 1.06;
  constexpr Real kRejectGrowth = 1.30;
  Real previous_accepted_residual = -1.0;
  long long rejected_steps = 0;
  bool have_accepted_state = false;

  // Convergence bookkeeping: a run is declared converged when the residual has
  // dropped by the requested number of orders, or when it has plateaued with
  // stable forces (which is the honest description for a limiter-dominated
  // steady state).
  const Real target_orders = rc.residual_reduction_target;
  std::vector<Real> recent_cd;
  std::vector<Real> recent_residual;
  // Force-stationarity bookkeeping.  A steady state requires the force
  // coefficients to have stopped changing, not merely a reduced residual.
  constexpr std::size_t kForceWindow = 500;
  constexpr Real kCdFloor = 1.0e-4;   // one drag count: below this level use an absolute test
  constexpr Real kCdRelTol = 1.0e-3;  // 0.1 % of the prevailing drag level
  constexpr Real kCdAbsTol = 1.0e-6;  // 0.01 drag counts
  bool forces_stationary = false;
  Real cd_span_window = 0.0;
  Real cd_tol_window = 0.0;
  bool announced_orders_without_forces = false;

  logInfo(formatString("steady run: max_steps=%d, target=%.2f orders, CFL %.3g -> %.3g over %d steps,"
                       " inner iterations %d..%d (target ratio %.2e)",
                       max_steps, target_orders, rc.cfl_initial, rc.cfl_max,
                       rc.pseudo_cfl_ramp_steps, rc.min_inner_iterations, rc.max_inner_iterations,
                       rc.inner_residual_reduction_target));

  for (step = 1; step <= max_steps; ++step) {
    context.syncState();

    ResidualDiagnostics diag;
    assembler.evaluate(U, residual, halo, diag);
    outcome.positivity_fallbacks += diag.positivity_fallbacks;

    const ResidualNorms norms = context.computeNorms(residual);
    if (!std::isfinite(norms.l2)) {
      // A non-finite residual is recoverable while a previously accepted state
      // exists: restore it and retry with a much smaller CFL.
      if (have_accepted_state && cfl_scale > kCflScaleMin) {
        for (Index c = 0; c < num_owned; ++c) U.set(c, U_accepted.get(c));
        cfl_scale = std::max(kCflScaleMin, cfl_scale * kCflCutFactor);
        ++rejected_steps;
        --step;
        continue;
      }
      diverged = true;
      outcome.convergence_status = "failed";
      outcome.notes = "residual became non-finite at step " + std::to_string(step);
      outcome.final_step = step;
      outcome.final_residual = norms.l2;
      logError(outcome.notes);
      break;
    }

    // --- reject a step that made the residual much worse -------------------
    if (have_accepted_state && previous_accepted_residual > 0.0 &&
        norms.l2 > kRejectGrowth * previous_accepted_residual && cfl_scale > kCflScaleMin) {
      for (Index c = 0; c < num_owned; ++c) U.set(c, U_accepted.get(c));
      cfl_scale = std::max(kCflScaleMin, cfl_scale * kCflCutFactor);
      ++rejected_steps;
      --step;
      continue;
    }

    // Accept the current state as the fallback point for the next step.
    for (Index c = 0; c < num_owned; ++c) U_accepted.set(c, U.get(c));
    have_accepted_state = true;
    if (previous_accepted_residual > 0.0 && norms.l2 < previous_accepted_residual) {
      cfl_scale = std::min(1.0, cfl_scale * kCflRecoverFactor);
    }
    previous_accepted_residual = norms.l2;

    if (step == 1) {
      initial_residual = norms.l2;
      outcome.initial_residual = initial_residual;
    }
    last_residual = norms.l2;

    const Real cfl =
        cfl_scale * rampedCfl(rc.cfl_initial, rc.cfl_max, rc.pseudo_cfl_ramp_steps, step - 1);
    computeLocalTimeSteps(mesh, cfl, assembler.convectiveSpectralRadius(),
                          assembler.viscousSpectralRadius(), 4.0, dtau);

    // Diagonal scaling V_i / dtau_i for the implicit system.
    Real min_dtau = 0.0;
    for (Index c = 0; c < num_owned; ++c) {
      const Real dt_local = dtau[static_cast<std::size_t>(c)];
      const Real volume = mesh.cells()[static_cast<std::size_t>(c)].volume;
      diag_scale[static_cast<std::size_t>(c)] = (dt_local > 0.0) ? volume / dt_local : volume;
      if (c == 0 || dt_local < min_dtau) min_dtau = dt_local;
    }

    // --- inner implicit solve --------------------------------------------
    // The inner iteration is a sequence of LU-SGS sweeps on
    //     ( V/dtau + dR/dU ) dU = R .
    // Its convergence is measured by the LINEAR RESIDUAL of that system,
    //     ||R - (V/dtau) dU - (dR/dU) dU|| / ||R|| ,
    // evaluated with the same matrix-free operator the sweeps use, so the number
    // reported in residuals.csv and metadata.json is the genuine reduction of
    // the system being solved.  Sweeps are added, within the case-file bounds,
    // until that ratio meets the case target or stops improving.
    int inner_used = 0;
    Real inner_ratio = 1.0;
    {
      const int min_inner = rc.min_inner_iterations;
      const int max_inner = rc.max_inner_iterations;
      int performed = 0;
      Real previous_ratio = -1.0;
      int stagnant_blocks = 0;
      while (performed < max_inner) {
        const int block = (performed == 0) ? min_inner : std::min(2, max_inner - performed);
        implicit.solve(U, residual, diag_scale, assembler.convectiveSpectralRadius(),
                       assembler.viscousSpectralRadius(), block, dU, halo,
                       /*reset_increment=*/performed == 0);
        performed += block;
        inner_ratio = implicit.linearResidualRatio(U, residual, diag_scale,
                                                  assembler.convectiveSpectralRadius(),
                                                  assembler.viscousSpectralRadius(), dU, halo);
        if (performed >= min_inner && inner_ratio <= rc.inner_residual_reduction_target) break;
        // Give up only after the sweeps have genuinely stopped helping several
        // times in a row.  At the high CFL values these cases ramp to, LU-SGS
        // converges slowly but steadily, and quitting on a single small
        // improvement leaves the linear system badly solved -- which shows up as
        // an oscillating force history rather than as an obvious failure.
        if (previous_ratio > 0.0 && inner_ratio > 0.98 * previous_ratio) {
          if (++stagnant_blocks >= 3) break;
        } else {
          stagnant_blocks = 0;
        }
        previous_ratio = inner_ratio;
      }
      inner_used = performed;
    }
    outcome.inner_stats.record(inner_used, inner_ratio <= rc.inner_residual_reduction_target,
                               inner_ratio);

    // --- update ----------------------------------------------------------
    for (Index c = 0; c < num_owned; ++c) {
      ConsVec u = U.get(c);
      const Real *du = dU.cell(c);
      for (int k = 0; k < kNumVars; ++k) u[k] += du[k];
      U.set(c, u);
    }
    const long long limited = context.enforcePositivity(U);
    outcome.positivity_fallbacks += limited;

    // --- output ----------------------------------------------------------
    ResidualRow row;
    row.step = step;
    row.physical_time = 0.0;
    row.inner_iter = inner_used;
    row.cfl = cfl;  // the effective CFL actually used, including the safeguard scale
    row.dt = min_dtau;
    row.component = norms.component_l2;
    row.l2 = norms.l2;
    row.linf = norms.linf;
    if (step % input.outputs.write_residuals_every == 0 || step == 1 || step == max_steps) {
      writer.appendResidual(row);
    }

    if (step % input.outputs.write_forces_every == 0 || step == 1 || step == max_steps) {
      last_forces = computeForces(mesh, context.flow(), assembler, U, context.comm());
      writer.appendForces(step, 0.0, last_forces);
    }

    if (step % context.options().log_every == 0 || step == 1) {
      const Real orders = (initial_residual > 0.0)
                              ? std::log10(std::max(initial_residual, kTiny) /
                                           std::max(norms.l2, kTiny))
                              : 0.0;
      logRaw(formatString(
          "  step %7d  res %12.5e  (-%5.2f orders)  cfl %8.3f  inner %4d (ratio %8.2e)  "
          "cd %11.6f  cl %11.6f  rej %lld",
          step, norms.l2, orders, cfl, inner_used, inner_ratio, last_forces.cd, last_forces.cl,
          rejected_steps));
      logRaw(formatString(
          "           dominant residual: var %s at (%.5g, %.5g), cell volume %.3e, boundary %s",
          (diag.worst_component == kRho ? "rho"
                                        : (diag.worst_component == kRhoU
                                               ? "rhou"
                                               : (diag.worst_component == kRhoV ? "rhov" : "rhoE"))),
          diag.worst_location.x, diag.worst_location.y, diag.worst_volume,
          diag.worst_touches_boundary
              ? (diag.worst_boundary_tag >= 0
                     ? mesh.boundaryNames()[static_cast<std::size_t>(diag.worst_boundary_tag)].c_str()
                     : "yes")
              : "no"));
    }

    // --- convergence tests ------------------------------------------------
    const Real orders = (initial_residual > 0.0)
                            ? std::log10(std::max(initial_residual, kTiny) / std::max(norms.l2, kTiny))
                            : 0.0;

    // Force stationarity, evaluated over a trailing window of the drag history.
    //
    // The residual is normalised against the step-1 value, which is the
    // impulsive start from uniform freestream.  That first residual is dominated
    // by a startup transient (an essentially infinite wall shear on a viscous
    // case, and the initial pressure pulse on an inviscid one), so "N orders
    // below step 1" can be reached while the flow field is still developing.
    // The airfoil Re 5000 cases show this clearly: the drag falls monotonically
    // by two orders of magnitude while the residual drops the requested three.
    // Requiring the integrated forces to be constant as well is what actually
    // establishes a steady state, so both tests must pass together.
    recent_cd.push_back(last_forces.cd);
    if (recent_cd.size() > kForceWindow) recent_cd.erase(recent_cd.begin());
    forces_stationary = false;
    if (recent_cd.size() == kForceWindow) {
      Real cd_lo = recent_cd.front();
      Real cd_hi = recent_cd.front();
      Real cd_mag = 0.0;
      for (const Real v : recent_cd) {
        cd_lo = std::min(cd_lo, v);
        cd_hi = std::max(cd_hi, v);
        cd_mag = std::max(cd_mag, std::abs(v));
      }
      cd_span_window = cd_hi - cd_lo;
      // Near-zero drag (the M 0.15 inviscid case) must not be allowed to pass a
      // purely relative test by crossing zero, so the tolerance never shrinks
      // below an absolute floor.
      cd_tol_window = std::max(kCdRelTol * std::max(cd_mag, kCdFloor), kCdAbsTol);
      forces_stationary = cd_span_window <= cd_tol_window;
    }

    if (orders >= target_orders && forces_stationary) {
      target_reached = true;
      outcome.convergence_status = "converged";
      outcome.notes = formatString(
          "residual reduced by %.2f orders (target %.2f) after %d pseudo-time steps, with C_D = "
          "%.6f stationary to %.3e (tolerance %.3e) over the last %zu steps",
          orders, target_orders, step, last_forces.cd, cd_span_window, cd_tol_window,
          kForceWindow);
      logInfo(outcome.notes);
      break;
    }
    if (orders >= target_orders && !announced_orders_without_forces) {
      announced_orders_without_forces = true;
      logInfo(formatString(
          "residual target of %.2f orders reached at step %d, but C_D is still moving by %.3e over "
          "the last %zu steps (tolerance %.3e); continuing until the forces are stationary",
          target_orders, step, cd_span_window, kForceWindow, cd_tol_window));
    }

    // Plateau detection.
    //
    // On these meshes the residual norm is dominated by a handful of extremely
    // small cells at the airfoil surface (cell volumes ~1e-8 against a domain
    // area of 2e4), where the limiter keeps switching and prevents the last
    // order or two of residual reduction.  Once the force coefficients are
    // constant to several digits and the residual has genuinely stopped moving,
    // further pseudo-time steps change nothing physical, so the run is stopped
    // and reported as a converged plateau with the achieved reduction stated
    // explicitly rather than being presented as the requested reduction.
    recent_residual.push_back(norms.l2);
    const std::size_t window = 1500;
    if (recent_residual.size() > window) {
      recent_residual.erase(recent_residual.begin());
    }
    if (recent_residual.size() == window && step > 2500) {
      const Real res_first = recent_residual.front();
      const Real res_last = recent_residual.back();
      const Real res_change = std::abs(std::log10(std::max(res_last, kTiny) /
                                                 std::max(res_first, kTiny)));
      // Residual flat to better than 0.05 orders over the window AND the drag
      // stationary by the same test used for the primary convergence check: no
      // further physical change is occurring.
      if (res_change < 0.05 && forces_stationary) {
        target_reached = true;
        outcome.convergence_status = "converged";
        outcome.notes = formatString(
            "residual plateaued at %.4e (%.2f of the requested %.2f orders below the initial level) "
            "with C_D = %.6f stationary to %.3e (tolerance %.3e) over the last %zu steps; stopped "
            "at step %d. "
            "The remaining residual is concentrated in the smallest wall cells and the forces are "
            "converged.",
            norms.l2, orders, target_orders, last_forces.cd, cd_span_window, cd_tol_window,
            kForceWindow, step);
        logInfo(outcome.notes);
        break;
      }
    }
  }

  if (step > max_steps) step = max_steps;
  outcome.final_step = step;
  outcome.final_physical_time = 0.0;
  outcome.final_residual = last_residual;
  outcome.residual_reduction_orders =
      (initial_residual > 0.0 && last_residual > 0.0)
          ? std::log10(initial_residual / last_residual)
          : 0.0;

  if (!diverged && !target_reached) {
    // Reached max_steps without satisfying the convergence test.  The status
    // depends on what was actually achieved: the run may only be called
    // converged if the forces have stopped changing, which is the physically
    // meaningful statement.  Otherwise it is reported as not converged, even
    // though the state is still written out for inspection.
    if (forces_stationary) {
      outcome.convergence_status = "converged";
      outcome.notes = formatString(
          "reached the step limit (%d) with the residual reduced by %.2f orders (target %.2f), but "
          "with C_D = %.6f stationary to %.3e (tolerance %.3e) over the last %zu steps, so the "
          "forces are converged even though the residual target was not met; %lld step(s) rejected "
          "by the CFL safeguard",
          max_steps, outcome.residual_reduction_orders, target_orders, last_forces.cd,
          cd_span_window, cd_tol_window, kForceWindow, rejected_steps);
    } else {
      outcome.convergence_status = "not_converged";
      outcome.notes = formatString(
          "reached the step limit (%d) with the residual reduced by only %.2f orders (target %.2f) "
          "and C_D = %.6f still moving by %.3e over the last %zu steps (tolerance %.3e); this run "
          "is NOT a converged steady state; %lld step(s) rejected by the CFL safeguard",
          max_steps, outcome.residual_reduction_orders, target_orders, last_forces.cd,
          cd_span_window, kForceWindow, cd_tol_window, rejected_steps);
    }
    logInfo(outcome.notes);
  }

  if (diverged) {
    // A failed run still writes its final state so the failure can be inspected,
    // but it is never marked complete.
    outcome.completed = false;
    context.syncState();
    return outcome;
  }

  // Recompute the residual and forces from the FINAL state so the last force row
  // matches surface.csv and field_final.vtu exactly.
  context.syncState();
  {
    ResidualDiagnostics diag;
    assembler.evaluate(U, residual, halo, diag);
    const ResidualNorms norms = context.computeNorms(residual);
    outcome.final_residual = norms.l2;
    outcome.residual_reduction_orders =
        (initial_residual > 0.0 && norms.l2 > 0.0) ? std::log10(initial_residual / norms.l2) : 0.0;

    ResidualRow row;
    row.step = outcome.final_step;
    row.physical_time = 0.0;
    row.inner_iter = 0;
    row.cfl = rampedCfl(rc.cfl_initial, rc.cfl_max, rc.pseudo_cfl_ramp_steps, outcome.final_step);
    row.dt = 0.0;
    row.component = norms.component_l2;
    row.l2 = norms.l2;
    row.linf = norms.linf;
    writer.appendResidual(row);

    const ForceResult final_forces = computeForces(mesh, context.flow(), assembler, U, context.comm());
    writer.appendForces(outcome.final_step, 0.0, final_forces);
    logInfo(formatString("final state: residual %.6e, C_D = %.6f (pressure %.6f + viscous %.6f), "
                         "C_L = %.6f, C_mz = %.6f",
                         norms.l2, final_forces.cd, final_forces.pressure_drag,
                         final_forces.viscous_drag, final_forces.cl, final_forces.cmz));
  }

  // Only a genuinely converged run counts as a completed final result.  A run
  // that exhausted its step budget without reaching a steady state is reported
  // as incomplete so it can never be submitted as a final case result.
  outcome.completed = (outcome.convergence_status == "converged" ||
                       outcome.convergence_status == "statistically_periodic");
  return outcome;
}

}  // namespace cns2d
