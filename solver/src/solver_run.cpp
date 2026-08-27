#include "solver.hpp"
#include <filesystem>

namespace fv {

void Solver::steadyPhase(const string& tag, long maxSteps, double resTargetOrders, double cfl0,
                         double cfl1, long rampSteps, long minIt, long maxIt, double linTarget,
                         bool logCsv, long stepBase) {
  const int nOwn = mesh_.nOwn;
  vector<State> rhs(nOwn), dU(mesh_.nAll);
  // one-step diagnostic: FV2D_ONESTEP=cfl does a single sweep and reports the
  // actual residual change, then exits
  if (const char* os = std::getenv("FV2D_ONESTEP")) {
    double cfl = std::atof(os);
    computeResidual(R_, false, 0, 0, 0, 0);
    ResidualNorms r0 = residualNorms(R_);
    computeLocalDt(cfl);
    buildDiag(cfl, 0.0, 0.0);
    for (int i = 0; i < nOwn; ++i)
      for (int v = 0; v < 4; ++v) rhs[i][v] = -R_[i][v];
    for (auto& d : dU) d = State{0, 0, 0, 0};
    syncDU(dU);
    sgsSweepPair(dU, rhs);
    syncDU(dU);
    applyUpdate(dU, U_);
    computeResidual(R_, false, 0, 0, 0, 0);
    ResidualNorms r1 = residualNorms(R_);
    if (rank_ == 0)
      std::printf("[onestep] cfl %.3g  res %.6e -> %.6e  ratio %.4f\n", cfl, r0.l2, r1.l2,
                  r1.l2 / r0.l2);
    convergenceStatus_ = "converged";
    finalStep_ = step_;
    return;
  }
  // number of LU-SGS inner relaxations per nonlinear step (bounded by the
  // case inner-iteration limits; each inner iteration relinearizes)
  long nInner = std::max(3L, std::min(minIt, maxIt));
  const char* ni = std::getenv("FV2D_INNER");
  if (ni) nInner = std::atol(ni);
  computeResidual(R_, false, 0, 0, 0, 0);
  ResidualNorms rn = residualNorms(R_);
  double resBase = rn.l2;
  if (logCsv && res0_ <= 0.0) res0_ = rn.l2;
  if (resBase <= 0.0) resBase = 1e-300;
  bool conv = false;
  long s = 0;
  // adaptive CFL (residual-driven): shrinks on residual rise, recovers slowly
  const bool adaptOn = std::getenv("FV2D_ADAPT") != nullptr;
  double cflAdapt = 1e30;
  double rnPrev = rn.l2;
  for (s = 1; s <= maxSteps; ++s) {
    long gstep = stepBase + s;
    double cflRamp = cflAt(s, cfl0, cfl1, rampSteps);
    double cfl = std::min(cflRamp, cflAdapt);
    double rnInner0 = 0.0;
    // inner nonlinear relaxation: K LU-SGS sub-steps, each relinearized
    for (long k = 1; k <= nInner; ++k) {
      computeResidual(R_, false, 0, 0, 0, 0);
      if (k == 1) rnInner0 = residualNorms(R_).l2;
      computeLocalDt(cfl);
      buildDiag(cfl, 0.0, 0.0);
      for (int i = 0; i < nOwn; ++i)
        for (int v = 0; v < 4; ++v) rhs[i][v] = -R_[i][v];
      for (auto& d : dU) d = State{0, 0, 0, 0};
      syncDU(dU);
      sgsSweepPair(dU, rhs);
      syncDU(dU);
      applyUpdate(dU, U_);
    }
    inner_.lastRatio = residualNorms(R_).l2 / std::max(rnInner0, 1e-300);
    computeResidual(R_, false, 0, 0, 0, 0);
    rn = residualNorms(R_);
    step_ = gstep;
    // CFL adaptation from residual trend (opt-in)
    if (adaptOn) {
      if (rn.l2 > 1.05 * rnPrev) {
        cflAdapt = std::max(cfl0, std::min(cflAdapt, cflRamp) * 0.5);
      } else {
        cflAdapt = std::min(cflAdapt * 1.02, 1e30);
      }
    }
    rnPrev = rn.l2;
    // ---- two-phase switch: first-order startup -> 2nd-order -> frozen limiter
    if (!secondOrderEnabled_ && !firstOrder_ && gstep >= switchStep_ &&
        std::log10(resBase / rn.l2) >= switchOrders_) {
      secondOrderEnabled_ = true;
      logLine("[" + tag + "] switching to 2nd-order at step " + std::to_string(gstep));
    } else if (secondOrderEnabled_ && !limFrozen_ && !firstOrder_ &&
               (std::log10(resBase / rn.l2) >= switchOrders_ + freezeOrders_ ||
                gstep >= freezeStep_)) {
      limFrozen_ = true;
      limFrozenStore_ = lim_;
      resAtFreeze_ = rn.l2;
      logLine("[" + tag + "] limiter frozen at step " + std::to_string(gstep));
    } else if (limFrozen_ && resAtFreeze_ == 0.0) {
      resAtFreeze_ = rn.l2;
    } else if (limFrozen_ && resAtFreeze_ > 0.0 && rn.l2 > 3.0 * resAtFreeze_) {
      limFrozen_ = false;
      logLine("[" + tag + "] limiter unfrozen (residual rise) at step " + std::to_string(gstep));
    }
    // mean local pseudo time step (for the dt column)
    double dtl = 0.0;
    for (int i = 0; i < nOwn; ++i) dtl += dtau_[i];
    double dtg = 0.0;
    MPI_Allreduce(&dtl, &dtg, 1, MPI_DOUBLE, MPI_SUM, comm_);
    dtg /= mesh_.nCellsGlobal;
    if (logCsv) {
      long ev = std::max(1L, cfg_.outputs.write_residuals_every);
      if (s % ev == 0) logResidualCsv(gstep, 0.0, nInner, cfl, dtg, rn);
      long fv = std::max(1L, cfg_.outputs.write_forces_every);
      if (s % fv == 0) logForcesCsv(gstep, 0.0, computeForces());
      // track inner stats for metadata
      inner_.steps++;
      inner_.totalIt += nInner;
      inner_.maxIt = std::max(inner_.maxIt, nInner);
      inner_.minIt = (inner_.minIt < 0) ? nInner : std::min(inner_.minIt, nInner);
      inner_.lastRatio = rn.l2 / resBase;
    }
    if (s % 500 == 0 || s == 1) {
      char line[256];
      std::snprintf(line, sizeof(line),
                    "[%s] step %ld cfl %.3g res_l2 %.6e (drop %.2f orders) linf %.3e inner %ld",
                    tag.c_str(), gstep, cfl, rn.l2, std::log10(resBase / rn.l2), rn.linf, nInner);
      logLine(line);
    }
    if (std::getenv("FV2D_DEBUG_GMRES") && rank_ == 0 && s % 5 == 0) {
      char line[200];
      std::snprintf(line, sizeof(line), "  [gmres] step %ld inner %ld relres %.3e res %.6e", gstep,
                    nInner, lastGmresRelResid_, rn.l2);
      logLine(line);
    }
    if (std::getenv("FV2D_DEBUG_MAX") && s % 10 == 0 && rank_ == 0) {
      double best = 0;
      int bc = -1;
      for (int i = 0; i < nOwn; ++i) {
        for (int v = 0; v < 4; ++v) {
          double r = std::fabs(R_[i][v]) / mesh_.vol[i];
          if (r > best) { best = r; bc = i; }
        }
      }
      if (bc >= 0) {
        const Prim& w = W_[bc];
        char line[256];
        std::snprintf(line, sizeof(line),
                      "  [dbg] step %ld maxres cell (% .4f,% .4f) vol %.2e rho %.4f u %.4f v %.4f p %.4f",
                      gstep, mesh_.xc[bc], mesh_.yc[bc], mesh_.vol[bc], w.rho, w.u, w.v, w.p);
        logLine(line);
      }
    }
    if (rn.l2 / resBase < std::pow(10.0, -resTargetOrders)) { conv = true; break; }
    if (!std::isfinite(rn.l2)) { logLine("[" + tag + "] ERROR: non-finite residual"); break; }
    // debug: periodic field dumps
    if (const char* fe = std::getenv("FV2D_FIELD_EVERY")) {
      long every = std::atol(fe);
      if (every > 0 && s % every == 0) {
        char nm[256];
        std::snprintf(nm, sizeof(nm), "fields/step%06ld.vtk", gstep);
        writeFieldVtk(outdir_ + "/" + nm);
      }
    }
  }
  if (logCsv) {
    // ensure final row present
    logResidualCsv(step_, 0.0, 0, cflAt(s, cfl0, cfl1, rampSteps), 0.0, rn);
    logForcesCsv(step_, 0.0, computeForces());
    finalStep_ = step_;
    if (conv) {
      convergenceStatus_ = "converged";
      converged_ = true;
      notes_ = "steady residual reduced by " + std::to_string(std::log10(resBase / rn.l2)) +
               " orders of magnitude";
      residualReductionOrders_ = std::log10(resBase / rn.l2);
    } else {
      convergenceStatus_ = "failed";
      converged_ = false;
      residualReductionOrders_ = std::log10(resBase / std::max(rn.l2, 1e-300));
      notes_ = "steady run did not reach the requested residual reduction (only " +
               std::to_string(std::log10(resBase / rn.l2)) + " orders)";
    }
    if (rank_ == 0) { std::fflush(fRes_); std::fflush(fForce_); }
  } else {
    char line[256];
    std::snprintf(line, sizeof(line), "[%s] done after %ld steps, res_l2 %.6e (drop %.2f orders)",
                  tag.c_str(), s, rn.l2, std::log10(resBase / std::max(rn.l2, 1e-300)));
    logLine(line);
  }
}

void Solver::transientPhase() {
  const int nOwn = mesh_.nOwn;
  // ---- optional steady pseudo-time initialization to establish the base flow
  if (cfg_.rc.steady_init_steps > 0 && step_ == 0) {
    logLine("[transient] running steady initialization phase (" +
            std::to_string(cfg_.rc.steady_init_steps) + " steps)");
    steadyPhase("steady-init", cfg_.rc.steady_init_steps, 1e30, 1.0, cfg_.rc.steady_init_cfl,
                std::min(2000L, cfg_.rc.steady_init_steps / 2), 3, 8, 0.05, false, 0);
    step_ = 0;
    time_ = 0.0;
  }
  Un_ = U_;
  Unm1_ = U_;
  secondOrderEnabled_ = true;  // transient runs always use the 2nd-order operator
  const double dt = cfg_.rc.time_step;
  const long nsteps = std::lround(cfg_.rc.final_time / dt);
  const double cfl = std::max(cfg_.rc.cfl_initial, 1e-12);  // fixed near 1.0 per case contract
  vector<State> rhs(nOwn), dU(mesh_.nAll);
  vector<double> clHist;  // rank-0 lift history for periodicity assessment
  inner_ = InnerStats{};
  nextFieldTime_ = (cfg_.outputs.write_field_every_time > 0)
                       ? std::floor(time_ / cfg_.outputs.write_field_every_time + 1e-9) *
                                 cfg_.outputs.write_field_every_time +
                             cfg_.outputs.write_field_every_time
                       : 1e300;
  long stepStart = step_;
  for (long s = stepStart + 1; s <= nsteps; ++s) {
    bool bdf1 = (s == 1);
    double c0 = bdf1 ? 1.0 : 1.5;
    double c1 = bdf1 ? -1.0 : -2.0;
    double c2 = bdf1 ? 0.0 : 0.5;
    computeResidual(R_, true, c0, c1, c2, dt);
    ResidualNorms rn0 = residualNorms(R_);
    double T0 = std::max(rn0.l2, 1e-300);
    computeLocalDt(cfl);
    buildDiag(cfl, c0, dt);
    long k = 0;
    double ratio = 1.0;
    ResidualNorms rn = rn0;
    for (k = 1; k <= cfg_.rc.max_inner_iterations; ++k) {
      for (int i = 0; i < nOwn; ++i)
        for (int v = 0; v < 4; ++v) rhs[i][v] = -R_[i][v];
      for (auto& d : dU) d = State{0, 0, 0, 0};
      syncDU(dU);
      sgsSweepPair(dU, rhs);
      syncDU(dU);
      applyUpdate(dU, U_);
      computeResidual(R_, true, c0, c1, c2, dt);
      rn = residualNorms(R_);
      ratio = rn.l2 / T0;
      if (k >= cfg_.rc.min_inner_iterations && ratio <= cfg_.rc.inner_residual_reduction_target)
        break;
    }
    // ---- inner-solve statistics
    inner_.steps++;
    inner_.totalIt += k;
    inner_.maxIt = std::max(inner_.maxIt, k);
    inner_.minIt = (inner_.minIt < 0) ? k : std::min(inner_.minIt, k);
    inner_.lastRatio = ratio;
    if (ratio > cfg_.rc.inner_residual_reduction_target) inner_.misses++;
    // ---- accept physical step; update BDF2 histories only now
    Unm1_ = Un_;
    Un_ = U_;
    step_ = s;
    time_ = s * dt;
    ForceRecord fr = computeForces();
    long ev = std::max(1L, cfg_.outputs.write_residuals_every);
    if (s % ev == 0) logResidualCsv(s, time_, k, cfl, dt, rn);
    long fv = std::max(1L, cfg_.outputs.write_forces_every);
    if (s % fv == 0) logForcesCsv(s, time_, fr);
    if (rank_ == 0) clHist.push_back(fr.cl);
    if (time_ >= nextFieldTime_ - 1e-12) {
      writeIntermediateField();
      nextFieldTime_ += cfg_.outputs.write_field_every_time;
    }
    if (s % 5000 == 0) writeRestart("restart_final.bin");
    if (s % 200 == 0 || s == 1) {
      char line[256];
      std::snprintf(line, sizeof(line),
                    "[transient] step %ld t %.3f inner %ld ratio %.2e cl %.5f cd %.5f", s, time_,
                    k, ratio, fr.cl, fr.cd);
      logLine(line);
    }
    if (!std::isfinite(rn.l2)) {
      logLine("[transient] ERROR: non-finite residual, aborting");
      break;
    }
  }
  // ensure final rows exist
  {
    computeResidual(R_, true, 1.5, -2.0, 0.5, dt);
    ResidualNorms rn = residualNorms(R_);
    logResidualCsv(step_, time_, 0, cfl, dt, rn);
    logForcesCsv(step_, time_, computeForces());
  }
  finalStep_ = step_;
  // ---- periodicity assessment from the last quartile of the lift history
  long nstepsDone = step_;
  int periodic = 0;
  double clPP = 0.0;
  if (rank_ == 0 && clHist.size() > 100) {
    size_t n0 = clHist.size() * 3 / 4;
    double lo = 1e300, hi = -1e300;
    for (size_t i = n0; i < clHist.size(); ++i) {
      lo = std::min(lo, clHist[i]);
      hi = std::max(hi, clHist[i]);
    }
    clPP = hi - lo;
    periodic = (nstepsDone >= nsteps && clPP > 0.01) ? 1 : 0;
  }
  MPI_Bcast(&periodic, 1, MPI_INT, 0, comm_);
  MPI_Bcast(&clPP, 1, MPI_DOUBLE, 0, comm_);
  bool completedRun = (nstepsDone >= nsteps);
  if (completedRun && periodic) {
    convergenceStatus_ = "statistically_periodic";
    converged_ = true;
    residualReductionOrders_ = -std::log10(std::max(inner_.lastRatio, 1e-300));
    notes_ = "transient completed to t=" + std::to_string(time_) +
             "; post-transient lift peak-to-peak amplitude " + std::to_string(clPP);
  } else if (completedRun) {
    convergenceStatus_ = "failed";
    converged_ = false;
    residualReductionOrders_ = -std::log10(std::max(inner_.lastRatio, 1e-300));
    notes_ = "transient completed but no significant lift oscillation detected (pp=" +
             std::to_string(clPP) + ")";
  } else {
    convergenceStatus_ = "failed";
    converged_ = false;
    residualReductionOrders_ = 0.0;
    notes_ = "transient aborted at step " + std::to_string(step_);
  }
  if (rank_ == 0) { std::fflush(fRes_); std::fflush(fForce_); }
}

void Solver::run(const string& outdir, const string& restartFile) {
  outdir_ = outdir;
  wallStart_ = MPI_Wtime();
  {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    startTimeUtc_ = buf;
  }
  if (rank_ == 0) {
    std::filesystem::create_directories(outdir_);
    std::filesystem::create_directories(outdir_ + "/fields");
    fLog_ = std::fopen((outdir_ + "/stdout.log").c_str(), "w");
    fRes_ = std::fopen((outdir_ + "/residuals.csv").c_str(), "w");
    std::fprintf(fRes_, "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n");
    fForce_ = std::fopen((outdir_ + "/forces.csv").c_str(), "w");
    std::fprintf(fForce_, "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n");
  }
  {
    char line[512];
    std::snprintf(line, sizeof(line),
                  "fv2d: case %s | ranks %d | cells global %ld (owned %d ghost %d) | flux %s | %s",
                  cfg_.case_id.c_str(), nranks_, mesh_.nCellsGlobal, mesh_.nOwn, mesh_.nGhost,
                  useRoe_ ? "roe(harten-fix)" : "rusanov",
                  cfg_.viscous() ? "laminar" : "inviscid");
    logLine(line);
  }
  // ---- initial state
  if (!restartFile.empty()) {
    readRestart(restartFile);
    logLine("restarted from " + restartFile + " at step " + std::to_string(step_));
  } else {
    State ufs = primToCons(fs_.rho, fs_.u, fs_.v, fs_.p, cfg_.gas);
    for (auto& u : U_) u = ufs;
  }
  if (cfg_.transient()) {
    transientPhase();
  } else {
    steadyPhase("steady", cfg_.rc.max_steps, cfg_.rc.residual_reduction_target,
                cfg_.rc.cfl_initial, cfg_.rc.cfl_max, cfg_.rc.pseudo_cfl_ramp_steps,
                cfg_.rc.min_inner_iterations, cfg_.rc.max_inner_iterations,
                cfg_.rc.inner_residual_reduction_target, true, 0);
  }
  finalizeOutputs();
  if (rank_ == 0) {
    if (fRes_) std::fclose(fRes_);
    if (fForce_) std::fclose(fForce_);
    if (fLog_) std::fclose(fLog_);
  }
}

void Solver::writeIntermediateField() {
  char name[256];
  std::snprintf(name, sizeof(name), "fields/field_t%08.3f.vtk", time_);
  writeFieldVtk(outdir_ + "/" + name);
}

}  // namespace fv
