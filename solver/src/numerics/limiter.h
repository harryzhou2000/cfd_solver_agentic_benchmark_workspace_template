// cns2d -- slope limiting for the piecewise-linear reconstruction.
//
// Two limiter families are provided:
//   * Barth-Jespersen: the classical monotone multidimensional limiter.  It is
//     the minimum acceptable limiter for this benchmark and is the most
//     restrictive of the two.
//   * Venkatakrishnan: a differentiable variant of Barth-Jespersen that avoids
//     the limiter chattering which stalls steady convergence, at the cost of a
//     small amount of admissible overshoot.
//
// Both are applied per primitive variable using the min/max of the cell's own
// value and its face neighbours, and both are followed by a positivity guard on
// the reconstructed density and pressure.
#pragma once

#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "physics/perfect_gas.h"

namespace cns2d {

enum class LimiterType {
  kNone,               // unlimited linear reconstruction (verification only)
  kBarthJespersen,
  kVenkatakrishnan,
};

std::string limiterName(LimiterType t);
LimiterType parseLimiterType(const std::string &name);

// Per-cell, per-variable limiter factors in [0, 1].
class LimiterField {
 public:
  void resize(Index num_cells) {
    data_.assign(static_cast<std::size_t>(num_cells) * kNumVars, 1.0);
  }
  Real *cell(Index c) { return data_.data() + static_cast<std::size_t>(c) * kNumVars; }
  const Real *cell(Index c) const { return data_.data() + static_cast<std::size_t>(c) * kNumVars; }
  Real *data() { return data_.data(); }
  void fill(Real value) { std::fill(data_.begin(), data_.end(), value); }

 private:
  std::vector<Real> data_;
};

// Compute limiter factors for owned cells.
// 'venkat_k' is the Venkatakrishnan smoothing constant (K in the standard
// formulation); the smoothing threshold scales as (K * h)^3.
void computeLimiter(const DistributedMesh &mesh, const FlowContext &ctx, LimiterType type,
                    const StateField &W, const GradientField &grad, Real venkat_k,
                    LimiterField &phi);

// Reconstruct the primitive state from cell 'c' to point 'x' using the limited
// gradient, then enforce positivity of density and pressure.  Returns true if
// the full second-order value was used, false if the positivity fallback
// clipped it back towards the cell average.
bool reconstructPrimitive(const Real *w_cell, const Real *grad_cell, const Real *phi_cell,
                          Vec2 delta, PrimVec &out);

}  // namespace cns2d
