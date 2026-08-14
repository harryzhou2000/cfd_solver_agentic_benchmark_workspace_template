#pragma once
// Piecewise-linear reconstruction on unstructured meshes:
//  - inverse-distance-weighted least-squares gradients over face neighbors
//  - Barth-Jespersen and Venkatakrishnan slope limiters
//  - positivity fallback at reconstructed face states

#include <vector>

#include "common.hpp"
#include "partition.hpp"

namespace cfd2d {

enum class LimiterType { None, BarthJespersen, Venkatakrishnan };

// Per-cell least-squares coefficient data: grad_phi = A * sum_j w_j dphi_j.
// For cell i, for each neighbor j: contribution w_j * (phi_j - phi_i) to
// (dphi/dx, dphi/dy). Precomputed once from the mesh.
struct LsqStencil {
  // For cell i (owned only), neighbor list and coefficients.
  std::vector<std::vector<int>> neighbors;            // local cell ids (incl. ghosts)
  std::vector<std::vector<std::array<double, 2>>> coef;  // [i][k] = (cx, cy)
};

LsqStencil buildLsqStencil(const LocalMesh& lm);

// Compute gradients of a scalar field (size nLocal) on owned cells.
// Output grad has size nOwned, two doubles per cell (dphi/dx, dphi/dy).
void computeScalarGradient(const LsqStencil& ls, const std::vector<double>& phi,
                           std::vector<double>& grad);

// Compute gradients for all 4 primitive variables at once.
// W has nLocal * 4 entries; grad has nOwned * 8 entries.
void computePrimitiveGradients(const LsqStencil& ls, const std::vector<double>& W,
                               std::vector<double>& grad);

// Slope limiter on owned cells.
//   W: primitive field, nLocal*4.
//   grad: gradients, nOwned*gradStride, with variable q at
//         [i*gradStride + 2q, i*gradStride + 2q+1].
//   limiter out: nOwned*4, one factor per variable.
void computeLimiter(const LocalMesh& lm, const LsqStencil& ls, LimiterType type,
                    const std::vector<double>& W, const std::vector<double>& grad,
                    int gradStride, std::vector<double>& limiter);

}  // namespace cfd2d
