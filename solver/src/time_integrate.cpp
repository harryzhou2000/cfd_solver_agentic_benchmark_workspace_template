// Time integration: steady pseudo-time march (CFL-ramped LU-SGS) and the
// BDF2/trapezoidal dual-time transient for the cylinder Re 200 case.
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cfd {

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
    implicitSolve(3);
    for (int c = 0; c < no; ++c)
      for (int k = 0; k < NEQ; ++k)
        U[c*NEQ + k] += dU[c*NEQ + k];
    for (int c = 0; c < no; ++c) {
      Prim w = phys.gas.primFromCons(Cons{ {U[c*NEQ+0],U[c*NEQ+1],U[c*NEQ+2],U[c*NEQ+3]} });
      double rho = std::max(U[c*NEQ+0], 0.1 * phys.W_inf.rho);
      double p = std::max(w.p, 0.1 * phys.W_inf.p);
      if (rho != U[c*NEQ+0] || p != w.p) {
        U[c*NEQ+0] = rho;
        double ke = 0.5 * (U[c*NEQ+1]*U[c*NEQ+1] + U[c*NEQ+2]*U[c*NEQ+2]) / std::max(rho,1e-30);
        U[c*NEQ+3] = p / phys.gas.gm1() + rho * ke;
      }
    }
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
    implicitSolve(2);  // a couple of SGS sweeps per inner (nonlinear) iteration
    int no = lm.n_owned;
    for (int c = 0; c < no; ++c)
      for (int e = 0; e < NEQ; ++e)
        U[c*NEQ + e] += dU[c*NEQ + e];
    // positivity safeguard
    for (int c = 0; c < no; ++c) {
      Prim w = phys.gas.primFromCons(Cons{ {U[c*NEQ+0],U[c*NEQ+1],U[c*NEQ+2],U[c*NEQ+3]} });
      double rho = std::max(U[c*NEQ+0], 0.1 * phys.W_inf.rho);
      double p = std::max(w.p, 0.1 * phys.W_inf.p);
      if (rho != U[c*NEQ+0] || p != w.p) {
        U[c*NEQ+0] = rho;
        double ke = 0.5 * (U[c*NEQ+1]*U[c*NEQ+1] + U[c*NEQ+2]*U[c*NEQ+2]) / std::max(rho,1e-30);
        U[c*NEQ+3] = p / phys.gas.gm1() + rho * ke;
      }
    }
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
