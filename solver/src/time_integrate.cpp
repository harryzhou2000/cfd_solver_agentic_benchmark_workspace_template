// Time integration: steady pseudo-time march (CFL-ramped LU-SGS) and the
// BDF2/trapezoidal dual-time transient for the cylinder Re 200 case.
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cfd {

// admissibility floors/caps shared by the update limiter and state repair
static inline void admissParams(const Physics& ph,
    double& rho_floor, double& p_floor, double& V_cap) {
  rho_floor = 0.05 * ph.W_inf.rho;
  p_floor   = 0.05 * ph.W_inf.p;
  double Vinf = std::sqrt(ph.W_inf.u*ph.W_inf.u + ph.W_inf.v*ph.W_inf.v);
  double a_inf = ph.gas.soundSpeed(ph.W_inf);
  V_cap = 4.0 * (Vinf + a_inf);   // generous velocity cap (catches runaway)
}

void Solver::limitUpdatePositivity() {
  const int no = lm.n_owned;
  double rho_floor, p_floor, V_cap;
  admissParams(phys, rho_floor, p_floor, V_cap);
  for (int c = 0; c < no; ++c) {
    double rho = U[c*NEQ+0], drho = dU[c*NEQ+0];
    double rhou = U[c*NEQ+1], drhou = dU[c*NEQ+1];
    double rhov = U[c*NEQ+2], drvov = dU[c*NEQ+2];
    double rhoE = U[c*NEQ+3], drhoE = dU[c*NEQ+3];
    double theta = 1.0;
    // 1) density floor (analytic)
    if (drho < 0.0) {
      double rho_new = rho + drho;
      if (rho_new < rho_floor) {
        double t = (rho_floor - rho) / drho;   // drho<0 so t>=0
        if (t < theta) theta = t;
      }
    }
    // 2) velocity cap (bisection on theta)
    auto Vsq = [&](double th){
      double rN = rho + th*drho; if (rN < 1e-12) rN = 1e-12;
      double ruN = rhou + th*drhou, rvN = rhov + th*drvov;
      return (ruN*ruN + rvN*rvN) / (rN*rN);
    };
    if (Vsq(theta) > V_cap*V_cap) {
      double lo = 0.0, hi = theta;
      for (int it = 0; it < 24; ++it) {
        double mid = 0.5*(lo+hi);
        if (Vsq(mid) > V_cap*V_cap) hi = mid; else lo = mid;
      }
      theta = lo;
    }
    // 3) pressure floor (bisection on theta)
    auto pAt = [&](double th) -> double {
      Cons Uc{ {rho + th*drho, rhou + th*drhou, rhov + th*drvov, rhoE + th*drhoE} };
      return phys.gas.primFromCons(Uc).p;
    };
    if (pAt(theta) < p_floor) {
      double lo = 0.0, hi = theta;
      for (int it = 0; it < 24; ++it) {
        double mid = 0.5*(lo+hi);
        if (pAt(mid) < p_floor) hi = mid; else lo = mid;
      }
      theta = lo;
    }
    theta = std::max(0.0, std::min(1.0, theta));
    for (int k = 0; k < NEQ; ++k) dU[c*NEQ+k] *= theta;
  }
}

void Solver::repairState() {
  const int no = lm.n_owned;
  double rho_floor, p_floor, V_cap;
  admissParams(phys, rho_floor, p_floor, V_cap);
  for (int c = 0; c < no; ++c) {
    double rho = U[c*NEQ+0];
    double rhou = U[c*NEQ+1], rhov = U[c*NEQ+2], rhoE = U[c*NEQ+3];
    bool changed = false;
    if (rho < rho_floor) { rho = rho_floor; changed = true; }
    rho = std::max(rho, 1e-12);
    double V2 = (rhou*rhou + rhov*rhov) / (rho*rho);
    if (V2 > V_cap*V_cap) {
      double sc = V_cap / std::sqrt(V2);
      rhou *= sc; rhov *= sc; changed = true;
    }
    Cons Uc{ {rho, rhou, rhov, rhoE} };
    double p = phys.gas.primFromCons(Uc).p;
    if (!(p > p_floor)) { p = p_floor; changed = true; }
    if (changed) {
      double ke = 0.5*(rhou*rhou + rhov*rhov) / rho;
      U[c*NEQ+0] = rho;
      U[c*NEQ+1] = rhou; U[c*NEQ+2] = rhov;
      U[c*NEQ+3] = p / phys.gas.gm1() + rho * ke;
    }
  }
}

double Solver::steadyStep(int step, int n_inner) {
  // Nonlinear inner iteration per pseudo-step (Newton-like): re-evaluate the
  // upwind residual at the updated state each inner pass and relax with the
  // full-spectral LU-SGS/SGS implicit. With the capped limiter + clamp
  // positivity safeguard this converges R->0 for the pseudo-step instead of
  // oscillating (which a single stale-linearization update does at high CFL).
  // The reported residual is R(U^n) from the first pass (the convergence
  // metric); n_inner is the number of nonlinear inner iterations.
  double c0 = cd.rc.cfl_initial, cmax = cd.rc.cfl_max;
  int ramp = std::max(1, cd.rc.pseudo_cfl_ramp_steps);
  double frac = std::min(1.0, double(step) / double(ramp));
  cfl_current = c0 + (cmax - c0) * frac;
  double rl2_first = -1.0;
  int no = lm.n_owned;
  for (int it = 0; it < n_inner; ++it) {
    exchangeHalo();
    computePrimitive();
    computeGradients();
    computeLimiters();
    computeResidual(false, 0.0);
    computeResidualNorms();
    if (it == 0) rl2_first = residualL2();
    int sw = 3;
    if (const char* e = std::getenv("CFD2D_SGS_SWEEPS")) sw = std::atoi(e);
    if (sw < 1) sw = 1;
    implicitSolve(sw);
    limitUpdatePositivity();
    for (int c = 0; c < no; ++c)
      for (int k = 0; k < NEQ; ++k)
        U[c*NEQ + k] += dU[c*NEQ + k];
    repairState();
  }
  return rl2_first;
}

void Solver::bdf2Step(int step, double dt) {
  // histories: Unm1 = U^{n-1}, Un = U^n (frozen during inner iterations)
  int nloc = lm.n_owned + lm.n_ghost;
  if (step == 0) {
    Unm1.assign(nloc * NEQ, 0.0);
    Un = U;   // U^{n-1} = U^n for startup
  }
  // U currently holds U^n; iterate toward U^{n+1}
  double r0 = -1.0;
  bool first_order = (step == 0);  // BDF1 startup
  int min_inner = std::max(5, cd.rc.min_inner);
  int max_inner = std::max(min_inner, cd.rc.max_inner);
  double target = cd.rc.inner_residual_target;  // 1e-3
  int niter = 0;
  bool converged = false;
  double last_ratio = 1.0;
  for (int k = 0; k < max_inner; ++k) {
    exchangeHalo();
    computePrimitive();
    computeGradients();
    computeLimiters();
    // dual-time residual: R_spatial - BDF2 source
    computeResidual(true, dt);
    computeResidualNorms();
    double rl2 = residualL2();
    if (k == 0) r0 = rl2;
    double ratio = (r0 > 1e-30) ? rl2 / r0 : 0.0;
    last_ratio = ratio;
    niter = k + 1;
    last_inner_iter = niter;
    if (k >= min_inner - 1 && ratio < target) { converged = true; break; }
    if (k == max_inner - 1) break;  // will be a target miss
    cfl_current = cd.rc.cfl_initial;  // fixed ~1.0
    int sw_t = 2;
    if (const char* e = std::getenv("CFD2D_SGS_SWEEPS")) sw_t = std::atoi(e);
    if (sw_t < 1) sw_t = 1;
    implicitSolve(sw_t);  // SGS sweeps per inner (nonlinear) iteration
    int no = lm.n_owned;
    limitUpdatePositivity();
    for (int c = 0; c < no; ++c)
      for (int e = 0; e < NEQ; ++e)
        U[c*NEQ + e] += dU[c*NEQ + e];
    repairState();
  }
  (void)first_order;  // BDF1 startup handled by Unm1==Un in computeResidual BDF2 term
  // update inner-iteration statistics
  inner_stats.n_steps++;
  inner_stats.total_inner += niter;
  inner_stats.min_inner = std::min(inner_stats.min_inner, niter);
  inner_stats.max_inner = std::max(inner_stats.max_inner, niter);
  if (!converged) inner_stats.target_misses++;
  inner_stats.last_ratio = last_ratio;
  // advance physical-time histories (after inner solve accepted)
  Unm1 = Un;
  Un = U;   // U^{n+1} becomes U^n for the next physical step
  final_phys_time += dt;
}

}  // namespace cfd
