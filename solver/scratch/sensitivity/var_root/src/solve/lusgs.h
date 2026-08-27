// cns2d -- implicit inner solver: LU-SGS with a simplified (scalar-dissipation)
// Jacobian, plus a symmetric-Gauss-Seidel / Jacobi fallback.
//
// The linear system solved at each nonlinear step is
//   ( V_i/dtau_i * beta * I + D_i ) dU_i + sum_j O_ij dU_j = R_i^*
// where D_i is the diagonal block from the face flux linearisation, O_ij are the
// off-diagonal blocks, and R_i^* is the (possibly dual-time) right-hand side.
//
// The off-diagonal blocks use the cheap "scalar dissipation" approximation
//   O_ij dU_j ~ 0.5 * ( A_j.n - |lambda_j| I ) dU_j * |face|
// with A_j.n the exact flux Jacobian of the neighbour state projected on the
// face normal, evaluated as a matrix-free product.  This is the standard
// approximate-factorisation choice for LU-SGS: it converges well while never
// forming or storing a 4x4 block per face, so memory stays O(cells).
//
// Sweep structure: a forward sweep over owned cells using already-updated lower
// neighbours, then a backward sweep using updated upper neighbours.  Off-rank
// (ghost) contributions are frozen at the value received by the halo exchange
// at the start of the inner iteration, which makes the method a block-Jacobi
// coupling between ranks and an LU-SGS solve within each rank.
#pragma once

#include <vector>

#include "core/types.h"
#include "numerics/residual.h"
#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "parallel/halo_exchange.h"
#include "physics/perfect_gas.h"

namespace cns2d {

enum class ImplicitSolverType {
  kLuSgs,
  kJacobi,
};

std::string implicitSolverName(ImplicitSolverType t);
ImplicitSolverType parseImplicitSolverType(const std::string &name);

// Matrix-free product of the exact inviscid flux Jacobian with a state
// increment: returns (dF(U)/dU . n) * dU.
ConsVec fluxJacobianTimesVector(const PerfectGas &gas, const ConsVec &U, Vec2 n,
                                const ConsVec &dU);

class ImplicitSolver {
 public:
  ImplicitSolver(const DistributedMesh &mesh, const FlowContext &flow, ImplicitSolverType type);

  // Perform 'num_sweeps' LU-SGS (or Jacobi) sweeps on the linear system with
  // diagonal scaling 'diag_scale' (= V_i/dtau_i * beta), right-hand side 'rhs',
  // and current state 'U'.
  //
  // 'reset_increment' controls whether the sweeps start from dU = 0 or continue
  // from the increment already stored in 'dU'.  Continuing is what lets the
  // driver add sweeps until the linear system is solved to the requested
  // tolerance: restarting from zero on every call would discard all previous
  // work and cap the achievable accuracy at whatever one block of sweeps can
  // deliver.
  void solve(const StateField &U, const StateField &rhs, const std::vector<Real> &diag_scale,
             const std::vector<Real> &conv_radius, const std::vector<Real> &visc_radius,
             int num_sweeps, StateField &dU, HaloExchange &halo, bool reset_increment = true);

  // Globally reduced relative linear residual of the system the sweeps solve:
  //   || rhs - ( diag_scale*I + dR/dU ) dU || / || rhs || .
  // Evaluated with the same matrix-free operator used by the sweeps, so it is a
  // faithful measure of how well the inner solve has converged.
  Real linearResidualRatio(const StateField &U, const StateField &rhs,
                           const std::vector<Real> &diag_scale,
                           const std::vector<Real> &conv_radius,
                           const std::vector<Real> &visc_radius, StateField &dU,
                           HaloExchange &halo);

  ImplicitSolverType type() const { return type_; }

 private:
  void buildDiagonal(const StateField &U, const std::vector<Real> &diag_scale,
                     const std::vector<Real> &conv_radius, const std::vector<Real> &visc_radius);
  // Off-diagonal action of neighbour 'other' on cell 'c' across face 'face'.
  ConsVec offDiagonalAction(const StateField &U, const StateField &dU, Index face, Index c,
                            Index other) const;

  const DistributedMesh &mesh_;
  const FlowContext &flow_;
  ImplicitSolverType type_;

  // Scalar diagonal per cell (one value per cell: the simplified Jacobian uses
  // a scalar diagonal plus the identity, which keeps the inversion trivial and
  // the memory footprint at one double per cell).
  std::vector<Real> diagonal_;
  StateField work_;
};

}  // namespace cns2d
