#pragma once

#include <array>
#include <vector>

#include "common.hpp"
#include "partition.hpp"

namespace cfd {

// Weighted least-squares gradient stencil, precomputed once from geometry.
struct LsqStencil {
  // For each local cell: list of (neighbor local id, dx, dy, weight)
  struct Entry {
    int nbr;
    double dx, dy, w;
  };
  std::vector<std::vector<Entry>> entries;
  std::vector<std::array<double, 2>> a11a12;  // {A11, A12}
  std::vector<std::array<double, 2>> a21a22;  // {A21, A22}
  std::vector<double> det;
};

// Build LSQ stencils from the rank-local mesh (neighbors via interior/halo
// faces).  Inverse-distance-squared weighting w = 1/|d|^2.
LsqStencil buildLsqStencil(const LocalMesh& mesh);

// Compute primitive gradients for owned cells.  gradDx[var] and gradDy[var]
// hold the x/y derivatives of (rho, u, v, p); ghost entries are left
// untouched and must be filled by halo exchange.
void computeGradients(const LocalMesh& mesh, const LsqStencil& stencil,
                      const std::vector<Prim>& W, std::vector<Prim>& gradDx,
                      std::vector<Prim>& gradDy, int nOwned);

// Barth-Jespersen limiter for owned cells (single scalar per cell, applied to
// all primitives).  Reconstructed primitive at each face and min/max over
// face-neighbor cells.
std::vector<double> computeLimiter(const LocalMesh& mesh, const std::vector<Prim>& W,
                                   const std::vector<Prim>& gradDx,
                                   const std::vector<Prim>& gradDy, int nOwned);

}  // namespace cfd
