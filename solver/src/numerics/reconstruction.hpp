#pragma once

// Second-order piecewise-linear reconstruction using inverse-distance
// weighted least-squares gradients, computed on PRIMITIVE variables
// (rho, u, v, p). Ghost cells receive their owner's gradients via the same
// halo exchange used for the conservative state (NVARS*2 components).

#include <vector>

#include "common/types.hpp"
#include "partition/partition.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// Unified local face index space.
// The DistributedMesh stores faces in four lists; a "unified" face index
// covers all of them in order: interior [0, n_int), boundary
// [n_int, n_int+n_bnd), send [...), recv [...). This lets per-cell face
// maps and the LU-SGS sweeps address every face of a cell uniformly.
// ---------------------------------------------------------------------------
int n_unified_faces(const DistributedMesh& dmesh);

const Face2D& get_local_face(const DistributedMesh& dmesh, int unified_face);

// The OTHER local cell across this face (the neighbor): for interior faces
// the other owned cell, for send/recv faces the ghost cell; -1 for boundary
// faces (no neighbor).
int face_other_cell(const DistributedMesh& dmesh, int unified_face, int cell);

// Rebuilds per-cell face adjacency for OWNED cells (Cell2D::faces was
// cleared during partitioning). Returns, for each owned cell, the unified
// local face indices of all faces touching it.
std::vector<std::vector<int>> build_cell_face_map(const DistributedMesh& dmesh);

// ---------------------------------------------------------------------------
// Gradients
// ---------------------------------------------------------------------------
// Computes inverse-distance-weighted least-squares gradients of the
// PRIMITIVE variables for all OWNED cells.
//   gradients: output, size NVARS*2 * n_local (owned + ghosts, zeroed for
//     ghosts), variable-major layout:
//     [dRho/dx, dRho/dy, du/dx, du/dy, dv/dx, dv/dy, dp/dx, dp/dy] per cell.
//   Ghost entries are zero here; the caller MUST exchange the array
//   (halo_exchange with NVARS*2 components) after limiting so every MPI
//   face sees the same limited gradients on both sides.
// The stencil includes the neighbor cell across every non-boundary face
// (interior/send/recv). Boundary faces contribute no point: a ghosted
// boundary point would bias the fit at O(|grad|*h).
void compute_gradients(const std::vector<double>& U,
                       const DistributedMesh& dmesh,
                       const std::vector<std::vector<int>>& cell_face_map,
                       std::vector<double>& gradients, double gamma);

// Piecewise-linear reconstruction of the face state from the cell state and
// its (already limited) primitive gradient. If the reconstructed density or
// pressure is non-positive, falls back to the cell-center state.
//   U_int:   conservative cell state
//   grad_int: primitive gradient, NVARS*2 = [dRho/dx, dRho/dy, du/dx, ...]
//   dx:      face_center - cell_center
//   U_face:  output conservative state at the face
void reconstruct_face(const double* U_int, const double* grad_int,
                      const Vector3& dx, double gamma, double* U_face);

}  // namespace cfd
