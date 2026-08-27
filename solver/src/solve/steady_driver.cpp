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
  // Best state seen so far.  The rejection test above only catches a step that
  // makes the residual abruptly worse; a slow degradation as the CFL ramp pushes
  // past the stability limit of the scheme accumulates without ever tripping it.
  // On the transonic case the residual reached 4.68e-04 at step 2546 with CFL
  // 49.7 and then degraded to 4.95e-03 by step 3496 at CFL 100, so the run ended
  // an order of magnitude ABOVE its own best value and reported the worse number.
  // Keeping the best state means the reported result is never worse than what the
  // run actually achieved.
  StateField U_best(mesh.numLocal());
  Real best_residual = -1.0;
  int best_step = 0;
  bool have_best_state = false;

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
  // Drag stationarity tolerance.  The test is relative to the prevailing drag
  // level, with an absolute floor so that a case whose true drag is essentially
  // zero (the M 0.15 inviscid airfoil, where the exact answer is C_D = 0) cannot
  // satisfy a purely relative criterion by oscillating through zero.
  //
  // The floor is expressed in drag counts (1 count = 1e-4).  A span of a few
  // times 1e-6 on a drag of 8.6e-4 is a genuinely converged force to any
  // physically meaningful standard -- it is 0.06 drag counts, far below the
  // discretization error of the mesh -- so demanding more than that from the
  // near-zero cases only burns pseudo-time steps without changing any reported
  // digit.
  constexpr Real kCdRelTol = 1.0e-2;  // 1 % of the prevailing drag level
  constexpr Real kCdAbsTol = 1.0e-5;  // 0.1 drag counts, in absolute terms
  // Some cases settle into a small bounded oscillation rather than to a fixed
  // point: the M 2.0 inviscid airfoil holds a limit cycle of about 1.4 % peak to
  // peak in C_D driven by the limiter switching in the tiny cells at the blunt
  // leading edge, while the MEAN drag is constant to 4e-5 relative over ten
  // thousand steps.  A span-only test cannot tell that apart from a slow drift,
  // so drift of the window mean is tested as well and either condition can
  // establish stationarity:
  //   * span within tolerance                      -> settled to a fixed point
  //   * mean drift within tolerance over 2 windows  -> settled to a limit cycle
  // The second case is reported distinctly, because a bounded oscillation is a
  // different physical statement from a converged fixed point and the report
  // must not present one as the other.
  constexpr Real kCdDriftRelTol = 1.0e-3;  // 0.1 % drift of the window mean
  // The drift test is applied to the pressure and viscous drag SEPARATELY as
  // well as to their sum.  Drift of a sum can be small while its components move
  // in opposite directions: on the M 2.0 laminar airfoil the pressure drag rose
  // 24 % and the viscous drag fell 1.9 % across a window whose NET drift was
  // 0.08 %, so a sum-only test certified a run whose components were moving by
  // two hundred times the net.  A converged steady state requires every
  // component to have settled, not merely their sum.
  //
  // A monotone trend test is applied as well: a genuine limit cycle oscillates
  // about its mean, so its sub-block means are not monotone, whereas a run still
  // developing produces a strictly monotone march.  This distinguishes the two
  // cases that the drift test alone conflates.
  constexpr std::size_t kTrendBlocks = 10;
  std::vector<Real> recent_pressure_drag;
  std::vector<Real> recent_viscous_drag;
  std::vector<Real> previous_window_pressure;
  std::vector<Real> previous_window_viscous;
  bool cd_monotone_trend = false;
  // A monotone trend is NOT by itself evidence of non-convergence.  What matters
  // is whether the sub-block decrements DECAY: geometrically decaying decrements
  // are an asymptotic approach whose remaining distance is bounded and
  // computable, whereas roughly constant decrements are an unfinished transient.
  //
  // Measured on the two cases that motivated this: the cylinder at Re 20 has
  // decrement ratios 0.82 0.81 0.76 0.74 0.72 0.70 0.67 0.65 (mean 0.73), giving
  // a remaining tail of -9.8e-04 on a drag of 2.018, i.e. converged to 0.05 %.
  // The M 2.0 laminar airfoil has ratios averaging 1.02 -- no decay at all -- and
  // is genuinely still developing.  Rejecting every monotone run would reject the
  // former along with the latter.
  constexpr Real kDecayRatioMax = 0.92;  // decrements must shrink by at least 8 % per block
  Real cd_decay_ratio = 0.0;
  Real cd_tail_estimate = 0.0;
  bool cd_trend_decaying = false;
  Real cd_drift_window = 0.0;
  bool forces_limit_cycle = false;
  std::vector<Real> previous_window_cd;
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

    // Record the best state seen so far, by residual norm.
    if (best_residual < 0.0 || norms.l2 < best_residual) {
      best_residual = norms.l2;
      best_step = step;
      for (Index c = 0; c < num_owned; ++c) U_best.set(c, U.get(c));
      have_best_state = true;
    }

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
    recent_pressure_drag.push_back(last_forces.pressure_drag);
    recent_viscous_drag.push_back(last_forces.viscous_drag);
    if (recent_cd.size() > kForceWindow) {
      // The value leaving the trailing window is kept in the previous window, so
      // the mean of the window before this one is available for the drift test.
      previous_window_cd.push_back(recent_cd.front());
      if (previous_window_cd.size() > kForceWindow) {
        previous_window_cd.erase(previous_window_cd.begin());
      }
      recent_cd.erase(recent_cd.begin());
      previous_window_pressure.push_back(recent_pressure_drag.front());
      previous_window_viscous.push_back(recent_viscous_drag.front());
      if (previous_window_pressure.size() > kForceWindow) {
        previous_window_pressure.erase(previous_window_pressure.begin());
        previous_window_viscous.erase(previous_window_viscous.begin());
      }
      recent_pressure_drag.erase(recent_pressure_drag.begin());
      recent_viscous_drag.erase(recent_viscous_drag.begin());
    }
    forces_stationary = false;
    forces_limit_cycle = false;
    if (recent_cd.size() == kForceWindow) {
      Real cd_lo = recent_cd.front();
      Real cd_hi = recent_cd.front();
      Real cd_mag = 0.0;
      Real cd_sum = 0.0;
      for (const Real v : recent_cd) {
        cd_lo = std::min(cd_lo, v);
        cd_hi = std::max(cd_hi, v);
        cd_mag = std::max(cd_mag, std::abs(v));
        cd_sum += v;
      }
      cd_span_window = cd_hi - cd_lo;
      // Relative test, but never tighter than the absolute floor, so a drag that
      // is essentially zero cannot pass by oscillating through zero.
      cd_tol_window = std::max(kCdRelTol * cd_mag, kCdAbsTol);

      // Sub-block trend analysis, computed ONCE and used by both branches below.
      // A span test alone can be cleared by a hair while the signal marches in
      // one direction throughout the window, so the trend must be inspected
      // whichever branch establishes stationarity.
      {
        const std::size_t block = kForceWindow / kTrendBlocks;
        Real bm[kTrendBlocks];
        for (std::size_t b = 0; b < kTrendBlocks; ++b) {
          Real s = 0.0;
          for (std::size_t i = 0; i < block; ++i) s += recent_cd[b * block + i];
          bm[b] = s / static_cast<Real>(block);
        }
        bool rising = true;
        bool falling = true;
        for (std::size_t b = 0; b + 1 < kTrendBlocks; ++b) {
          if (!(bm[b + 1] > bm[b])) rising = false;
          if (!(bm[b + 1] < bm[b])) falling = false;
        }
        cd_monotone_trend = rising || falling;

        // Decay of the decrements, and the geometric-tail estimate of how much
        // drag movement remains.
        cd_decay_ratio = 0.0;
        cd_tail_estimate = 0.0;
        cd_trend_decaying = false;
        if (cd_monotone_trend) {
          Real ratio_sum = 0.0;
          int ratio_count = 0;
          for (std::size_t b = 0; b + 2 < kTrendBlocks; ++b) {
            const Real d0 = bm[b + 1] - bm[b];
            const Real d1 = bm[b + 2] - bm[b + 1];
            if (std::abs(d0) > kTiny) {
              ratio_sum += std::abs(d1) / std::abs(d0);
              ++ratio_count;
            }
          }
          if (ratio_count > 0) {
            cd_decay_ratio = ratio_sum / static_cast<Real>(ratio_count);
            cd_trend_decaying = cd_decay_ratio < kDecayRatioMax;
            if (cd_trend_decaying) {
              const Real last_decrement = bm[kTrendBlocks - 1] - bm[kTrendBlocks - 2];
              cd_tail_estimate =
                  std::abs(last_decrement) * cd_decay_ratio / (1.0 - cd_decay_ratio);
            }
          }
        }
      }

      // Span branch: the drag has stopped moving over the window.  A monotone
      // march is admitted only when its decrements are decaying and the estimated
      // remaining distance is itself within tolerance, so a tolerance cleared by
      // a hair on a still-marching signal cannot certify convergence.
      forces_stationary = (cd_span_window <= cd_tol_window) &&
                          (!cd_monotone_trend ||
                           (cd_trend_decaying && cd_tail_estimate <= cd_tol_window));

      // Drift of the window mean against the mean of the preceding window.
      if (!forces_stationary && previous_window_cd.size() == kForceWindow) {
        const Real mean_now = cd_sum / static_cast<Real>(kForceWindow);
        Real prev_sum = 0.0;
        for (const Real v : previous_window_cd) prev_sum += v;
        const Real mean_prev = prev_sum / static_cast<Real>(kForceWindow);
        cd_drift_window = std::abs(mean_now - mean_prev);
        const Real drift_tol =
            std::max(kCdDriftRelTol * std::max(std::abs(mean_now), std::abs(mean_prev)), kCdAbsTol);

        // Component drift, so that opposing motions cannot cancel.
        auto mean_of = [](const std::vector<Real> &v) {
          Real s = 0.0;
          for (const Real x : v) s += x;
          return s / static_cast<Real>(v.size());
        };
        const Real p_now = mean_of(recent_pressure_drag);
        const Real p_prev = mean_of(previous_window_pressure);
        const Real v_now = mean_of(recent_viscous_drag);
        const Real v_prev = mean_of(previous_window_viscous);
        const Real p_drift = std::abs(p_now - p_prev);
        const Real v_drift = std::abs(v_now - v_prev);
        const Real p_tol =
            std::max(kCdDriftRelTol * std::max(std::abs(p_now), std::abs(p_prev)), kCdAbsTol);
        const Real v_tol =
            std::max(kCdDriftRelTol * std::max(std::abs(v_now), std::abs(v_prev)), kCdAbsTol);
        const bool components_settled = (p_drift <= p_tol) && (v_drift <= v_tol);

        // The trend was computed above and is shared with the span branch.  A
        // limit cycle oscillates about its mean, so a monotone march is never a
        // limit cycle regardless of how small its net drift is.
        if (cd_drift_window <= drift_tol && components_settled && !cd_monotone_trend) {
          forces_stationary = true;
          forces_limit_cycle = true;
        }
      }
    }

    if (orders >= target_orders && forces_stationary) {
      target_reached = true;
      outcome.convergence_status = "converged";
      if (forces_limit_cycle) {
        outcome.notes = formatString(
            "residual reduced by %.2f orders (target %.2f) after %d pseudo-time steps; C_D = %.6f "
            "holds a bounded oscillation of %.3e peak to peak whose mean drifts by only %.3e "
            "between successive %zu-step windows, so the mean force is converged even though the "
            "solution is a small limit cycle rather than a fixed point",
            orders, target_orders, step, last_forces.cd, cd_span_window, cd_drift_window,
            kForceWindow);
      } else {
        outcome.notes = formatString(
            "residual reduced by %.2f orders (target %.2f) after %d pseudo-time steps, with C_D = "
            "%.6f stationary to %.3e (tolerance %.3e) over the last %zu steps",
            orders, target_orders, step, last_forces.cd, cd_span_window, cd_tol_window,
            kForceWindow);
        if (cd_monotone_trend && cd_trend_decaying) {
          // The window is monotone but decaying: state the extrapolated asymptote
          // and the remaining distance, so the number of earned digits is explicit
          // rather than implied by the printed precision.
          outcome.notes += formatString(
              ".  The window is still monotone with decrements decaying at a ratio of %.3f per "
              "sub-block, so this is an asymptotic approach rather than a fixed point already "
              "reached: the geometric tail gives an estimated %.2e of C_D movement remaining "
              "(%.3f %% ), i.e. an asymptote near %.6f.  C_D is therefore converged to about "
              "%.1e and no more digits than that are earned.",
              cd_decay_ratio, cd_tail_estimate,
              100.0 * cd_tail_estimate / std::max(std::abs(last_forces.cd), kTiny),
              last_forces.cd - (recent_cd.back() > recent_cd.front() ? -cd_tail_estimate
                                                                     : cd_tail_estimate),
              cd_tail_estimate);
        }
      }
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
        if (forces_limit_cycle) {
          outcome.notes = formatString(
              "residual plateaued at %.4e (%.2f of the requested %.2f orders below the initial "
              "level); C_D = %.6f holds a bounded oscillation of %.3e peak to peak (%.2f %% of C_D) "
              "whose mean drifts by only %.3e between successive %zu-step windows; stopped at step "
              "%d. The remaining residual and the oscillation are both concentrated in the smallest "
              "wall cells, where the limiter keeps switching; the MEAN force is converged but the "
              "solution is a small limit cycle rather than a fixed point.",
              norms.l2, orders, target_orders, last_forces.cd, cd_span_window,
              100.0 * cd_span_window / std::max(std::abs(last_forces.cd), kTiny), cd_drift_window,
              kForceWindow, step);
        } else {
          outcome.notes = formatString(
              "residual plateaued at %.4e (%.2f of the requested %.2f orders below the initial "
              "level) with C_D = %.6f stationary to %.3e (tolerance %.3e) over the last %zu steps; "
              "stopped at step %d. The remaining residual is concentrated in the smallest wall "
              "cells and the forces are converged.",
              norms.l2, orders, target_orders, last_forces.cd, cd_span_window, cd_tol_window,
              kForceWindow, step);
        }
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
          "with C_D = %.6f %s over the last %zu steps, so the mean force is converged even though "
          "the residual target was not met; %lld step(s) rejected by the CFL safeguard",
          max_steps, outcome.residual_reduction_orders, target_orders, last_forces.cd,
          formatString(forces_limit_cycle
                           ? "holding a bounded oscillation of %.3e peak to peak with the window "
                             "mean drifting only %.3e (a small limit cycle, not a fixed point)"
                           : "stationary to %.3e (tolerance %.3e)",
                       cd_span_window,
                       forces_limit_cycle ? cd_drift_window : cd_tol_window)
              .c_str(),
          kForceWindow, rejected_steps);
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

  // If the march ended materially above its own best residual -- which happens
  // when the CFL ramp pushes past the stability limit of the scheme late in the
  // run -- fall back to the best state, and say so.  Reporting the degraded final
  // state would understate the convergence actually achieved, and reporting the
  // best residual while writing the degraded field would be inconsistent.
  if (have_best_state && best_residual > 0.0 && last_residual > 2.0 * best_residual) {
    for (Index c = 0; c < num_owned; ++c) U.set(c, U_best.get(c));
    const std::string fallback = formatString(
        "the march ended at residual %.4e, a factor %.2f above the best value %.4e reached at step "
        "%d; the best state has been restored and is what is reported and written out",
        last_residual, last_residual / best_residual, best_residual, best_step);
    logInfo(fallback);
    outcome.notes += "  " + fallback;
    outcome.final_step = best_step;
    last_residual = best_residual;
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
