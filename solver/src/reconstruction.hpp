#pragma once

#include <mpi.h>

#include <utility>
#include <vector>

#include "gas.hpp"
#include "mpi_utils.hpp"
#include "partition.hpp"
#include "types.hpp"

namespace cfd {

// Limiter selection (reported in metadata.json).
enum class Limiter { None, Venkatakrishnan, BarthJespersen };

// Cell-centered gradients of the primitive variables used by the
// second-order scheme. Density at faces is recovered from the reconstructed
// pressure and temperature through the ideal-gas EOS (rho = p / (R * T)),
// which keeps the face state thermodynamically consistent.
struct Gradients {
    double du_dx = 0.0, du_dy = 0.0;
    double dv_dx = 0.0, dv_dy = 0.0;
    double dp_dx = 0.0, dp_dy = 0.0;
    double dT_dx = 0.0, dT_dy = 0.0;
};

// Per-local-cell connectivity built once per solve. `cell_neighbors[i]`
// lists (neighbor local index, shared internal face index) pairs for every
// face of owned cell i; `cell_boundary_faces[i]` lists the boundary face
// indices (into the mesh bface_* arrays) of the cell. Sized n_owned (ghost
// cells do not need gradients, limiters or time steps).
struct CellConnectivity {
    std::vector<std::vector<std::pair<cgsize_t, cgsize_t>>> cell_neighbors;
    std::vector<std::vector<cgsize_t>> cell_boundary_faces;
    cgsize_t n_owned = 0;
};

// Builds the per-local-cell connectivity (neighbor cells with the shared
// internal face index, plus the boundary faces of each owned cell) from the
// mesh face connectivity and the rank-local cell map.
CellConnectivity build_cell_connectivity(const Mesh& mesh,
                                         const LocalMesh& local_mesh);

// Task-specified mesh-only neighbor list (global cell ids): for every cell
// of the full mesh, the ids of the cells sharing an internal face. Useful
// for standalone use and tests.
std::vector<std::vector<cgsize_t>> build_cell_neighbor_list(const Mesh& mesh);

// Computes cell-centered least-squares gradients of u, v, p, T for the
// owned cells from the neighbor stencil:
//   grad w = (A^T A)^{-1} A^T b,  A rows = (dx_j, dy_j),  b_j = w_j - w_i
// Falls back to a zero gradient when the 2x2 normal matrix is (near)
// singular (degenerate stencils). grad_local is sized n_owned + n_ghost:
// the owned entries are computed here, the ghost entries must be filled
// with exchange_halo_gradients before the gradients are used.
void compute_gradients_least_squares(
    const Mesh& mesh, const std::vector<PrimitiveState>& prim_local,
    const LocalMesh& local_mesh, const CellConnectivity& conn,
    const GasParams& gas, std::vector<Gradients>& grad_local);

// Exchanges the owned-cell gradients into the ghost slots (same plan as
// exchange_halo; the Gradients payload is 8 contiguous doubles).
void exchange_halo_gradients(const HaloExchangePlan& plan,
                             std::vector<Gradients>& grad_local,
                             MPI_Comm comm);

// Linear reconstruction of the primitive state at a point (fx, fy) from the
// cell-center state and gradient:
//   w_face = w_center + grad w . (r_face - r_center)
// with rho recovered from the reconstructed p and T. Positivity fallback:
// if the reconstructed density, pressure or temperature is non-physical
// (< 1e-10), the cell-center state is returned (first-order).
PrimitiveState reconstruct_state(const PrimitiveState& center,
                                 const Gradients& grad, double cx, double cy,
                                 double fx, double fy, const GasParams& gas);

// Reconstructed left/right primitive states at an internal face (local cell
// indices, face index into mesh.face_* arrays).
struct FaceRecon {
    PrimitiveState left, right;
};

FaceRecon reconstruct_face(
    cgsize_t cell_left, cgsize_t cell_right,
    const std::vector<PrimitiveState>& prim_local,
    const std::vector<Gradients>& grad_local, const Mesh& mesh,
    const LocalMesh& local_mesh, const GasParams& gas, cgsize_t face_idx);

// Reconstructed interior primitive state at a boundary face (local cell
// index, boundary face index into mesh.bface_* arrays), with the positivity
// fallback.
PrimitiveState reconstruct_boundary_state(
    cgsize_t cell_local, const std::vector<PrimitiveState>& prim_local,
    const std::vector<Gradients>& grad_local, const Mesh& mesh,
    const LocalMesh& local_mesh, const GasParams& gas, cgsize_t bface_idx);

// Barth-Jespersen slope limiter for one cell. For each variable w in
// {u, v, p, T}, the unlimited face values (from `unlimited_grad`) must stay
// within the min/max of the cell and its neighbors:
//   w_face > w_i : phi_face = min(1, (w_max - w_i) / (w_face - w_i))
//   w_face < w_i : phi_face = min(1, (w_i - w_min) / (w_i - w_face))
//   otherwise    : phi_face = 1
// The cell limiter is phi = min over all variables and all faces of the
// cell (internal faces evaluated at their midpoints, plus the boundary
// faces), clamped to [0, 1]; `limited_grad` = phi * unlimited_grad. Returns
// phi.
double limit_gradient(
    const PrimitiveState& cell_center, const Gradients& unlimited_grad,
    const std::vector<PrimitiveState>& prim_local,
    const CellConnectivity& conn, cgsize_t cell_local, const Mesh& mesh,
    const LocalMesh& local_mesh, const GasParams& gas,
    Gradients& limited_grad);

}  // namespace cfd
