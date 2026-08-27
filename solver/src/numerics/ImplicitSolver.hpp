// Matrix-free implicit relaxation (LU-SGS / symmetric Gauss-Seidel).
//
// The linear system solved at every nonlinear step is
//
//     [ V/dtau (+ a0 V/dt) + dR/dU ] dU = -R*
//
// with the Jacobian approximated by flux-vector splitting on each face,
//     dPhi_f/dU_i = 1/2 (A_i.n + lambda_f I) S_f,
//     dPhi_f/dU_j = 1/2 (A_j.n - lambda_f I) S_f,
// where lambda_f is the face spectral radius (convective plus viscous).  The
// diagonal collapses to a scalar because sum_f n_f S_f = 0 for a closed cell,
// and the off-diagonal products are evaluated matrix-free as true nonlinear
// flux differences F(U_j + dU_j).n - F(U_j).n.
//
// Between ranks the scheme degrades gracefully to block-Jacobi: halo dU values
// are frozen during a sweep and exchanged afterwards.
#pragma once

#include <vector>

#include "core/Types.hpp"
#include "numerics/SpatialOperator.hpp"

namespace cfd {

struct LinearSolveReport {
  int sweeps = 0;
  Real residual_ratio = 1.0;
  bool converged = false;
};

class ImplicitSolver {
 public:
  explicit ImplicitSolver(SpatialOperator& op);

  // Solve for `du` given the right-hand side `rhs` (= R*, the residual whose
  // norm we want to drive to zero).  `du` is sized owned+ghost and is
  // halo-exchanged between sweeps.
  LinearSolveReport solve(const std::vector<Real>& rhs, int min_sweeps, int max_sweeps,
                          Real target_ratio, std::vector<Real>& du);

 private:
  void sweep(const std::vector<Real>& rhs, std::vector<Real>& du, bool forward) const;
  Real linearResidualNorm(const std::vector<Real>& rhs, const std::vector<Real>& du) const;
  // 0.5 * (F(U_j + dU_j).n_out - F(U_j).n_out - lambda * dU_j) * area
  void offDiagonal(Index j, const Vec2& n_out, Real area, Real lambda, const Real* duj,
                   ConsVec& out) const;

  SpatialOperator& op_;
  const LocalMesh& mesh_;
  std::vector<Index> c2f_off_, c2f_face_, c2f_nb_;
  std::vector<signed char> c2f_sign_;
  mutable std::vector<Real> work_;
};

}  // namespace cfd
