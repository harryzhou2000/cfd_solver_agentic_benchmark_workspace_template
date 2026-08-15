#pragma once

/// @file reconstruction.hpp
/// Gradient computation and reconstruction for 2nd-order FVM.

#include "common.hpp"

namespace cfd {

// ============================================================================
// Gradient computation (least-squares)
// ============================================================================

/// Least-squares gradient data needed per cell for reconstruction.
/// We recompute the LS matrix per call, or we can precompute the coefficients.
/// For simplicity and correctness, we rebuild LS gradients each step.

/// Compute least-squares gradient of a scalar field at all cells.
/// @param mesh          the mesh
/// @param phi           scalar field values at cell centers
/// @param grad_phi      output: gradient at each cell [dphi/dx, dphi/dy]
void compute_scalar_gradient(const Mesh& mesh,
                             const std::vector<Real>& phi,
                             std::vector<Vec2>& grad_phi);

/// Compute gradients for primitive variables at all cells.
/// Uses least-squares with inverse-distance weighting.
void compute_primitive_gradients(const Mesh& mesh,
                                 const std::vector<Vec4>& U,
                                 Real gamma,
                                 std::vector<Vec2>& grad_rho,
                                 std::vector<Vec2>& grad_u,
                                 std::vector<Vec2>& grad_v,
                                 std::vector<Vec2>& grad_p);

// ============================================================================
// Reconstruction (left/right states at a face)
// ============================================================================

/// Reconstruct the left state (from cell `cell`) at face `face`.
/// @param mesh          the mesh
/// @param cell          index of the left cell
/// @param face          index of the face
/// @param U             cell-center conservative states
/// @param grad_rho      density gradients at cells
/// @param grad_u        u-velocity gradients at cells
/// @param grad_v        v-velocity gradients at cells
/// @param grad_p        pressure gradients at cells
/// @param limiter       per-component Barth-Jespersen limiter values [rho,u,v,p]
/// @param gamma         specific heat ratio
/// @return              reconstructed conservative state at face from left
Vec4 reconstruct_left(const Mesh& mesh, std::size_t cell, std::size_t face,
                      const std::vector<Vec4>& U,
                      const std::vector<Vec2>& grad_rho,
                      const std::vector<Vec2>& grad_u,
                      const std::vector<Vec2>& grad_v,
                      const std::vector<Vec2>& grad_p,
                      const Vec4& limiter,
                      Real gamma);

/// Reconstruct the right state (from cell `cell`) at face `face`.
/// @param mesh          the mesh
/// @param cell          index of the right cell
/// @param face          index of the face
/// @param U             cell-center conservative states
/// @param grad_rho      density gradients at cells
/// @param grad_u        u-velocity gradients at cells
/// @param grad_v        v-velocity gradients at cells
/// @param grad_p        pressure gradients at cells
/// @param limiter       per-component Barth-Jespersen limiter values [rho,u,v,p]
/// @param gamma         specific heat ratio
/// @return              reconstructed conservative state at face from right
Vec4 reconstruct_right(const Mesh& mesh, std::size_t cell, std::size_t face,
                       const std::vector<Vec4>& U,
                       const std::vector<Vec2>& grad_rho,
                       const std::vector<Vec2>& grad_u,
                       const std::vector<Vec2>& grad_v,
                       const std::vector<Vec2>& grad_p,
                       const Vec4& limiter,
                       Real gamma);

// ============================================================================
// Barth-Jespersen limiter
// ============================================================================

/// Compute Barth-Jespersen limiter for a cell's primitive variables.
/// Limits all components (rho, u, v, p) to their min/max over neighbors.
/// Returns limiter factor in [0, 1] for each component.
///
/// @param mesh          the mesh
/// @param cell          index of the cell to limit
/// @param U             cell-center conservative states
/// @param grad_rho      density gradients
/// @param grad_u        u-velocity gradients
/// @param grad_v        v-velocity gradients
/// @param grad_p        pressure gradients
/// @param gamma         specific heat ratio
/// @return              Vec4 of limiter values for [rho, u, v, p]
Vec4 barth_jespersen_limiter(const Mesh& mesh, std::size_t cell,
                              const std::vector<Vec4>& U,
                              const std::vector<Vec2>& grad_rho,
                              const std::vector<Vec2>& grad_u,
                              const std::vector<Vec2>& grad_v,
                              const std::vector<Vec2>& grad_p,
                              Real gamma);

} // namespace cfd
