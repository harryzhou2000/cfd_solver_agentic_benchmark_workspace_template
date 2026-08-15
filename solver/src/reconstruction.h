#pragma once

#include "common.h"
#include "partition.h"
#include "physics.h"
#include "case.h"

namespace cfd {

struct PrimGrad {
  double rx = 0.0, ry = 0.0;
  double ux = 0.0, uy = 0.0;
  double vx = 0.0, vy = 0.0;
  double px = 0.0, py = 0.0;
};

// Per-cell least-squares weights aligned with lm.cell_faces[i].
struct GradWeights {
  std::vector<std::vector<double>> wx;
  std::vector<std::vector<double>> wy;
};

// Boundary stencil value for a boundary face (freestream / wall mirror /
// no-slip wall value), given the cell state and the unit wall normal
// (pointing from the body into the fluid).
Prim boundary_stencil_value(BCType bc, const Prim& w_cell,
                            double nwx, double nwy, const CaseConfig& cfg);

// Computes the inverse-distance-weighted least-squares weights for all owned
// cells. Ghost-cell weight arrays are left empty.
void compute_gradient_weights(const LocalMesh& lm, const CaseConfig& cfg,
                              GradWeights& gw);

// Unlimited primitive gradient at a cell (owned cells only).
PrimGrad compute_gradient(const LocalMesh& lm, const GradWeights& gw,
                          const std::vector<Prim>& W, int cell,
                          const CaseConfig& cfg);

// Barth-Jespersen limiting of the primitive gradients for one cell. Applies
// the limiter to rho, u, v, p and enforces the positivity fallback (all
// limiters forced to zero if any reconstructed face state has rho<=0 or p<=0).
// Returns the limiter values; increments fallback_count when the fallback
// fires for this cell.
std::array<double, 4> limit_gradient(const LocalMesh& lm, const CaseConfig& cfg,
                                     const std::vector<Prim>& W, int cell,
                                     const PrimGrad& grad, long long& fallback_count);

// Reconstructed primitive at a face midpoint for cell `cell` (owned or ghost),
// using limited gradients and limiters.
Prim face_state(const LocalMesh& lm, const std::vector<Prim>& W,
                const std::vector<PrimGrad>& grads,
                int cell, int face);

// Wall pressure extrapolated to a boundary face (limited).
double wall_pressure(const LocalMesh& lm, const std::vector<Prim>& W,
                     const std::vector<PrimGrad>& grads,
                     int cell, int face);

}  // namespace cfd
