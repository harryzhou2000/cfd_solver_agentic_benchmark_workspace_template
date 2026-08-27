// Matrix-free (Newton-Krylov) GMRES inner solver with scalar LU-SGS
// preconditioning. The Jacobian action is evaluated with a Frechet
// derivative of the full second-order residual, so the inner solve matches
// the true nonlinear operator.
#include "solver.hpp"

namespace fv {

double Solver::dotGlobal(const vector<State>& a, const vector<State>& b) const {
  double loc = 0.0;
  for (int i = 0; i < mesh_.nOwn; ++i)
    for (int v = 0; v < 4; ++v) loc += a[i][v] * b[i][v];
  double glob;
  MPI_Allreduce(&loc, &glob, 1, MPI_DOUBLE, MPI_SUM, comm_);
  return glob;
}

double Solver::normGlobal(const vector<State>& a) const {
  return std::sqrt(std::max(dotGlobal(a, a), 0.0));
}

// Preconditioner: a few symmetric Gauss-Seidel (scalar LU-SGS) sweep pairs
// approximating the inverse of the frozen scalar-Jacobian implicit operator.
void Solver::applyPrec(const vector<State>& z, vector<State>& y, long sweepPairs) {
  y.assign(mesh_.nAll, State{0, 0, 0, 0});
  for (long s = 0; s < sweepPairs; ++s) {
    sgsSweepPair(y, z);
    syncDU(y);
  }
}

// Frechet Jacobian-vector product: J z ~ (R(U + eps z) - R(U)) / eps
void Solver::frechetMatvec(const vector<State>& z, vector<State>& w) {
  int nOwn = mesh_.nOwn;
  // scale for the perturbation
  double nrmz = normGlobal(z);
  double nrmU = normGlobal(U_);
  double eps = 1e-7 * std::max(nrmU, 1.0) / std::max(nrmz, 1e-300);
  vector<State> Usave(U_.begin(), U_.begin() + nOwn);
  for (int i = 0; i < nOwn; ++i)
    for (int v = 0; v < 4; ++v) U_[i][v] = Usave[i][v] + eps * z[i][v];
  vector<State> Rtmp(mesh_.nAll, State{});
  computeResidual(Rtmp, false, 0, 0, 0, 0);
  for (int i = 0; i < nOwn; ++i)
    for (int v = 0; v < 4; ++v) {
      w[i][v] = (Rtmp[i][v] - R_[i][v]) / eps;
      U_[i][v] = Usave[i][v];
    }
  if (std::getenv("FV2D_DEBUG_FRECHET")) {
    double nrmR = normGlobal(R_);
    double nrmRt = normGlobal(Rtmp);
    double nrmw = normGlobal(w);
    if (rank_ == 0)
      std::printf("  [frechet] eps %.3e nrmz %.3e nrmU %.3e nrmR %.3e nrmRt %.3e nrmw %.3e\n", eps,
                  nrmz, nrmU, nrmR, nrmRt, nrmw);
  }
}

long Solver::gmresSolve(const vector<State>& rhs, vector<State>& dU, double tol, long m,
                        long maxIt) {
  int nOwn = mesh_.nOwn;
  dU.assign(mesh_.nAll, State{0, 0, 0, 0});
  double beta = normGlobal(rhs);
  if (beta == 0.0) return 0;
  m = std::min(m, maxIt);
  vector<vector<State>> V(m + 1, vector<State>(nOwn, State{0, 0, 0, 0}));
  vector<vector<State>> Z(m, vector<State>(nOwn, State{0, 0, 0, 0}));
  vector<vector<double>> H(m + 1, vector<double>(m, 0.0));
  vector<double> cs(m), sn(m), g(m + 1, 0.0);
  for (int i = 0; i < nOwn; ++i)
    for (int v = 0; v < 4; ++v) V[0][i][v] = rhs[i][v] / beta;
  g[0] = beta;
  long itUsed = 0;
  vector<State> w(nOwn), zvec;
  for (long j = 0; j < m; ++j) {
    itUsed = j + 1;
    applyPrec(V[j], Z[j], 2);  // two SGS sweep pairs as preconditioner
    frechetMatvec(Z[j], w);
    // modified Gram-Schmidt
    for (long i = 0; i <= j; ++i) {
      H[i][j] = dotGlobal(w, V[i]);
      for (int c = 0; c < nOwn; ++c)
        for (int v = 0; v < 4; ++v) w[c][v] -= H[i][j] * V[i][c][v];
    }
    H[j + 1][j] = normGlobal(w);
    if (H[j + 1][j] > 0.0)
      for (int c = 0; c < nOwn; ++c)
        for (int v = 0; v < 4; ++v) V[j + 1][c][v] = w[c][v] / H[j + 1][j];
    // apply previous Givens rotations to the new column
    for (long i = 0; i < j; ++i) {
      double h0 = H[i][j], h1 = H[i + 1][j];
      H[i][j] = cs[i] * h0 + sn[i] * h1;
      H[i + 1][j] = -sn[i] * h0 + cs[i] * h1;
    }
    // new Givens rotation
    double denom = std::hypot(H[j][j], H[j + 1][j]);
    if (denom == 0.0) { itUsed = j + 1; break; }
    cs[j] = H[j][j] / denom;
    sn[j] = H[j + 1][j] / denom;
    H[j][j] = cs[j] * H[j][j] + sn[j] * H[j + 1][j];
    H[j + 1][j] = 0.0;
    double g0 = g[j];
    g[j] = cs[j] * g0;
    g[j + 1] = -sn[j] * g0;
  double resid = std::fabs(g[j + 1]);
    if (resid <= tol * beta) break;
  }
  lastGmresRelResid_ = (beta > 0.0) ? std::fabs(g[itUsed]) / beta : 0.0;
  // back substitution for y
  long n = itUsed;
  vector<double> ycoef(n, 0.0);
  for (long i = n - 1; i >= 0; --i) {
    double s = g[i];
    for (long k = i + 1; k < n; ++k) s -= H[i][k] * ycoef[k];
    ycoef[i] = s / H[i][i];
  }
  for (int c = 0; c < nOwn; ++c)
    for (int v = 0; v < 4; ++v) {
      double s = 0.0;
      for (long j = 0; j < n; ++j) s += ycoef[j] * Z[j][c][v];
      dU[c][v] = s;
    }
  syncDU(dU);
  return itUsed;
}

}  // namespace fv
