#pragma once
// Phase 4: gradient reconstruction and second-order state reconstruction.
//
// compute_gradients() computes cell-center gradients of the four conserved
// variables by weighted least-squares over the face-neighbor stencil (the
// per-cell face CSR), with a Green-Gauss fallback for degenerate (poorly
// conditioned) stencils. The gradients are then halo-exchanged for ghost
// cells (see halo.h) and clipped by the Barth-Jespersen limiter (limiter.h)
// before being used for second-order face reconstruction.
//
// The reconstruct_* helpers build a second-order face state from a cell
// center value, its (limited) gradient, and the offset vector to the face
// centroid. The positivity fallback (first order when the reconstructed
// state is non-physical) is applied by the caller (solver.cpp).

#include <vector>

#include "limiter.h"
#include "partition.h"
#include "types.h"

namespace cfd {

// ---------------------------------------------------------------------------
// Gradient storage
// ---------------------------------------------------------------------------

// Cell-center gradients of the four conserved variables for the OWNED cells
// of this rank (one entry per owned cell, index-aligned with LocalMesh cells
// 0..n_owned-1). Component order: 0 = rho, 1 = rhou, 2 = rhov, 3 = rhoE.
struct CellGradients {
  std::vector<Vec2> grad_rho;
  std::vector<Vec2> grad_rhou;
  std::vector<Vec2> grad_rhov;
  std::vector<Vec2> grad_rhoE;

  void resize(size_t n) {
    grad_rho.resize(n);
    grad_rhou.resize(n);
    grad_rhov.resize(n);
    grad_rhoE.resize(n);
  }
};

// Gradients of the GHOST cells, filled by exchange_gradient_halo() (halo.h).
// One entry per ghost cell, indexed by ghost_local_index - n_owned.
struct GhostGradients {
  std::vector<Vec2> grad_rho;
  std::vector<Vec2> grad_rhou;
  std::vector<Vec2> grad_rhov;
  std::vector<Vec2> grad_rhoE;

  void resize(size_t n) {
    grad_rho.resize(n);
    grad_rhou.resize(n);
    grad_rhov.resize(n);
    grad_rhoE.resize(n);
  }
};

// Compute the cell-center gradients of the conserved variables for every
// owned cell. U_local must hold fresh owned + ghost states (the caller
// exchanges the halo first); ghost states enter the least-squares stencil of
// boundary-adjacent owned cells.
//
// Algorithm per cell i: accumulate the 2x2 least-squares matrix
//   M = sum_f w * dr dr^T,  rhs = sum_f w * dr * du,  w = 1/|dr|
// over the face-neighbor cells j (dr = centroid_j - centroid_i), solve the
// 2x2 system per variable, and fall back to a Green-Gauss average over the
// cell's faces when the matrix is singular or the stencil is empty.
void compute_gradients(const std::vector<ConsState>& U_local,
                       const LocalMesh& lm, CellGradients& grads);

// ---------------------------------------------------------------------------
// Gradient accessors (component index 0..3: rho, rhou, rhov, rhoE)
// ---------------------------------------------------------------------------

// Raw (unlimited) gradient of owned cell i.
inline Vec2 raw_grad(const CellGradients& g, int k, int i) {
  switch (k) {
    case 0: return g.grad_rho[static_cast<size_t>(i)];
    case 1: return g.grad_rhou[static_cast<size_t>(i)];
    case 2: return g.grad_rhov[static_cast<size_t>(i)];
    default: return g.grad_rhoE[static_cast<size_t>(i)];
  }
}

// Limited gradient of owned cell i: limiter phi times the raw gradient.
inline Vec2 limited_grad(const CellGradients& g, const Limiters& l, int k,
                         int i) {
  switch (k) {
    case 0: return g.grad_rho[i] * l.phi_rho[i];
    case 1: return g.grad_rhou[i] * l.phi_rhou[i];
    case 2: return g.grad_rhov[i] * l.phi_rhov[i];
    default: return g.grad_rhoE[i] * l.phi_rhoE[i];
  }
}

// Raw (unlimited) gradient of ghost cell gi (gi = ghost local index minus
// n_owned), as received from the owning rank.
inline Vec2 ghost_grad(const GhostGradients& g, int k, int gi) {
  switch (k) {
    case 0: return g.grad_rho[static_cast<size_t>(gi)];
    case 1: return g.grad_rhou[static_cast<size_t>(gi)];
    case 2: return g.grad_rhov[static_cast<size_t>(gi)];
    default: return g.grad_rhoE[static_cast<size_t>(gi)];
  }
}

// ---------------------------------------------------------------------------
// Second-order face reconstruction
// ---------------------------------------------------------------------------

// Reconstruct the state of owned cell i at offset dr from its centroid using
// the limited gradient: U_face = U_i + (phi * grad_i) . dr.
inline void reconstruct_limited(const ConsState& U, const CellGradients& g,
                                const Limiters& l, int i, const Vec2& dr,
                                ConsState& out) {
  out.rho = U.rho + limited_grad(g, l, 0, i).dot(dr);
  out.rhou = U.rhou + limited_grad(g, l, 1, i).dot(dr);
  out.rhov = U.rhov + limited_grad(g, l, 2, i).dot(dr);
  out.rhoE = U.rhoE + limited_grad(g, l, 3, i).dot(dr);
}

// Reconstruct the state of ghost cell gi at offset dr using its (exchanged,
// unlimited) gradient.
inline void reconstruct_ghost(const ConsState& U, const GhostGradients& g,
                              int gi, const Vec2& dr, ConsState& out) {
  out.rho = U.rho + ghost_grad(g, 0, gi).dot(dr);
  out.rhou = U.rhou + ghost_grad(g, 1, gi).dot(dr);
  out.rhov = U.rhov + ghost_grad(g, 2, gi).dot(dr);
  out.rhoE = U.rhoE + ghost_grad(g, 3, gi).dot(dr);
}

// ---------------------------------------------------------------------------
// Primitive gradients (for the viscous flux)
// ---------------------------------------------------------------------------

// Velocity and temperature gradients derived from the conservative gradients
// and the cell-center state:
//   grad u = (grad(rhou) - u * grad(rho)) / rho            (same for v)
//   grad p = (gamma-1) * (grad(rhoE) - grad(ke)),  ke = kinetic energy
//   grad T = (grad p - (p/rho) * grad rho) / (rho * R)
struct PrimGradients {
  Vec2 grad_u;
  Vec2 grad_v;
  Vec2 grad_T;
};

// grad[0..3] must be grad_rho, grad_rhou, grad_rhov, grad_rhoE of the same
// cell (raw or limited — the caller decides).
PrimGradients cons_grad_to_prim(const ConsState& U, const Vec2 grad[4],
                                const GasConfig& gas);

}  // namespace cfd
