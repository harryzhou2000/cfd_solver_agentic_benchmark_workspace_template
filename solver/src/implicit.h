#pragma once

#include "types.h"
#include "mesh.h"
#include "partition.h"
#include "fluxes.h"
#include "reconstruction.h"
#include <vector>

namespace cfd2d {

// LU-SGS implicit solver.
// Solves: (I/dt + A) dU = -R
// where A is the implicit Jacobian (diagonal + lower/upper from neighbors).
// We use a simplified Jacobian: diagonal = (1/dt + spectral_radius/vol) per cell,
// off-diagonal = -spectral_radius_face/vol for each neighbor.
struct LUSGSSolver {
  // Per-cell diagonal
  std::vector<double> diag;
  // Lower and upper triangular sweeps
  // For each cell, list of (neighbor_local_idx, coefficient)
  std::vector<std::vector<std::pair<int,double>>> lower, upper;

  void setup(const LocalMesh& lm, double dt, const GasPhysics& gas,
             const std::vector<PrimState>& P);
  // Solve dU = -R using LU-SGS forward/backward sweep
  // R is the residual (already computed), dU is the update
  // relax controls under-relaxation (1.0 = full, 0.7 = damped)
  void solve(const LocalMesh& lm, const std::vector<ConsState>& R,
             std::vector<ConsState>& dU, int nSweeps = 3, double relax = 0.7);
};

} // namespace cfd2d
