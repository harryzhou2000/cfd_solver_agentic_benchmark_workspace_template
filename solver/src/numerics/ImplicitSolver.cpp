#include "numerics/ImplicitSolver.hpp"

#include <algorithm>

#include "core/Exception.hpp"

namespace cfd {

ImplicitSolver::ImplicitSolver(SpatialOperator& op) : op_(op), mesh_(op.mesh()) {
  const Index no = mesh_.num_owned;
  std::vector<Index> counts(static_cast<std::size_t>(no), 0);
  for (Index f = 0; f < mesh_.numFaces(); ++f) {
    if (mesh_.face_l[f] < no) counts[mesh_.face_l[f]]++;
    if (mesh_.face_r[f] < no) counts[mesh_.face_r[f]]++;
  }
  c2f_off_.assign(static_cast<std::size_t>(no) + 1, 0);
  for (Index c = 0; c < no; ++c) c2f_off_[c + 1] = c2f_off_[c] + counts[c];
  c2f_face_.assign(c2f_off_[no], -1);
  c2f_nb_.assign(c2f_off_[no], -1);
  c2f_sign_.assign(c2f_off_[no], 0);
  std::vector<Index> fill(c2f_off_.begin(), c2f_off_.end() - 1);
  for (Index f = 0; f < mesh_.numFaces(); ++f) {
    const Index l = mesh_.face_l[f], r = mesh_.face_r[f];
    if (l < no) {
      c2f_face_[fill[l]] = f; c2f_nb_[fill[l]] = r; c2f_sign_[fill[l]] = 1; fill[l]++;
    }
    if (r < no) {
      c2f_face_[fill[r]] = f; c2f_nb_[fill[r]] = l; c2f_sign_[fill[r]] = -1; fill[r]++;
    }
  }
}

void ImplicitSolver::offDiagonal(Index j, const Vec2& n_out, Real area, Real lambda,
                                 const Real* duj, ConsVec& out) const {
  const PerfectGas& gas = op_.gas();
  const std::vector<Real>& w = op_.W();
  const std::vector<Real>& u = op_.U();
  const PrimVec wj{w[j * kNVar], w[j * kNVar + 1], w[j * kNVar + 2], w[j * kNVar + 3]};

  ConsVec un{};
  for (int k = 0; k < kNVar; ++k) un[k] = u[j * kNVar + k] + duj[k];
  bool valid = un[0] > 0.0;
  Real pn = 0.0;
  if (valid) {
    pn = (gas.gamma() - 1.0) * (un[3] - 0.5 * (un[1] * un[1] + un[2] * un[2]) / un[0]);
    valid = pn > 0.0 && std::isfinite(pn);
  }
  const Real sc = op_.spatialJacobianScale();
  if (valid) {
    const PrimVec wn{un[0], un[1] / un[0], un[2] / un[0], pn};
    const ConsVec f0 = gas.normalFlux(wj, n_out);
    const ConsVec f1 = gas.normalFlux(wn, n_out);
    for (int k = 0; k < kNVar; ++k)
      out[k] = 0.5 * sc * (f1[k] - f0[k] - lambda * duj[k]) * area;
  } else {
    // Reconstructed neighbour update is not physical: drop the convective part
    // and keep the (dissipative, diagonally dominant) spectral-radius term.
    for (int k = 0; k < kNVar; ++k) out[k] = -0.5 * sc * lambda * duj[k] * area;
  }
}

void ImplicitSolver::sweep(const std::vector<Real>& rhs, std::vector<Real>& du,
                           bool forward) const {
  const Index no = mesh_.num_owned;
  const std::vector<Real>& diag = op_.diagonal();
  const std::vector<Real>& flam = op_.faceLambda();
  for (Index idx = 0; idx < no; ++idx) {
    const Index i = forward ? idx : (no - 1 - idx);
    ConsVec sum{};
    for (Index k = c2f_off_[i]; k < c2f_off_[i + 1]; ++k) {
      const Index f = c2f_face_[k];
      const Index j = c2f_nb_[k];
      const Real sgn = static_cast<Real>(c2f_sign_[k]);
      const Vec2 n_out{sgn * mesh_.face_normal[f][0], sgn * mesh_.face_normal[f][1]};
      ConsVec contrib{};
      offDiagonal(j, n_out, mesh_.face_area[f], flam[f], &du[j * kNVar], contrib);
      for (int v = 0; v < kNVar; ++v) sum[v] += contrib[v];
    }
    const Real inv_d = 1.0 / diag[i];
    for (int v = 0; v < kNVar; ++v) du[i * kNVar + v] = (-rhs[i * kNVar + v] - sum[v]) * inv_d;
  }
}

Real ImplicitSolver::linearResidualNorm(const std::vector<Real>& rhs,
                                        const std::vector<Real>& du) const {
  const Index no = mesh_.num_owned;
  const std::vector<Real>& diag = op_.diagonal();
  const std::vector<Real>& flam = op_.faceLambda();
  work_.assign(static_cast<std::size_t>(mesh_.numTotalCells()) * kNVar, 0.0);
  for (Index i = 0; i < no; ++i) {
    ConsVec sum{};
    for (Index k = c2f_off_[i]; k < c2f_off_[i + 1]; ++k) {
      const Index f = c2f_face_[k];
      const Index j = c2f_nb_[k];
      const Real sgn = static_cast<Real>(c2f_sign_[k]);
      const Vec2 n_out{sgn * mesh_.face_normal[f][0], sgn * mesh_.face_normal[f][1]};
      ConsVec contrib{};
      offDiagonal(j, n_out, mesh_.face_area[f], flam[f], &du[j * kNVar], contrib);
      for (int v = 0; v < kNVar; ++v) sum[v] += contrib[v];
    }
    for (int v = 0; v < kNVar; ++v)
      work_[i * kNVar + v] = diag[i] * du[i * kNVar + v] + sum[v] + rhs[i * kNVar + v];
  }
  return op_.computeNorms(work_).l2;
}

LinearSolveReport ImplicitSolver::solve(const std::vector<Real>& rhs, int min_sweeps,
                                        int max_sweeps, Real target_ratio,
                                        std::vector<Real>& du) {
  std::fill(du.begin(), du.end(), 0.0);
  LinearSolveReport rep;
  const Real rhs_norm = op_.computeNorms(rhs).l2;
  if (!(rhs_norm > 0.0)) { rep.converged = true; rep.residual_ratio = 0.0; return rep; }

  for (int s = 1; s <= max_sweeps; ++s) {
    sweep(rhs, du, true);
    op_.halo().exchange(du.data(), kNVar);
    sweep(rhs, du, false);
    op_.halo().exchange(du.data(), kNVar);
    rep.sweeps = s;
    if (s >= min_sweeps) {
      rep.residual_ratio = linearResidualNorm(rhs, du) / rhs_norm;
      if (rep.residual_ratio <= target_ratio) { rep.converged = true; break; }
    }
  }
  if (rep.sweeps > 0 && !rep.converged && rep.residual_ratio == 1.0) {
    rep.residual_ratio = linearResidualNorm(rhs, du) / rhs_norm;
  }
  return rep;
}

}  // namespace cfd
