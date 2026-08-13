#pragma once
// Phase 4: Barth-Jespersen-type limiter for the second-order reconstruction.
//
// compute_limiters() computes, for every owned cell and each of the four
// conserved variables, a scalar phi in [0, 1] that scales the cell gradient
// so that the reconstructed face values never exceed the local min/max of
// the cell and its face neighbors (monotonicity-preserving reconstruction).
// The limited gradient phi * grad is then used by reconstruct_limited()
// (gradient.h).
//
// The per-face ratio uses the smooth Venkatakrishnan form (the raw
// Barth-Jespersen ratio y/delta with the min over faces is non-smooth in the
// state and stalls implicit iterations at a residual floor; the smooth form
// bounds the reconstruction the same way but lets the implicit solver
// converge to the steady state of the limited operator).

#include <vector>

#include "partition.h"
#include "types.h"

namespace cfd {

struct CellGradients;  // defined in gradient.h (used by reference only)

// Per-cell limiter values, one entry per owned cell (index-aligned with
// LocalMesh cells 0..n_owned-1). Component order: rho, rhou, rhov, rhoE.
struct Limiters {
  std::vector<double> phi_rho;
  std::vector<double> phi_rhou;
  std::vector<double> phi_rhov;
  std::vector<double> phi_rhoE;

  void resize(size_t n) {
    phi_rho.resize(n);
    phi_rhou.resize(n);
    phi_rhov.resize(n);
    phi_rhoE.resize(n);
  }
};

// Compute the limiter for every owned cell.
//
// U_local must hold fresh owned + ghost states and `grads` the corresponding
// cell-center gradients (both computed before this call). Per cell i and
// variable k:
//   - U_min/U_max over cell i and all its face neighbors (owned or ghost),
//   - for every face with a neighbor j: reconstruct the value at the neighbor
//     centroid, U_face = U_i + grad_i . (centroid_j - centroid_i), and bound
//     the ratio with the smooth Venkatakrishnan function of the overshoot
//     delta = U_face - U_i against the available range U_max - U_i
//     [resp. U_min - U_i],
//   - phi_i = min over faces, clamped to [0, 1].
// Boundary faces (no neighbor) do not enter the min.
void compute_limiters(const std::vector<ConsState>& U_local,
                      const LocalMesh& lm, const CellGradients& grads,
                      Limiters& limiters);

}  // namespace cfd
