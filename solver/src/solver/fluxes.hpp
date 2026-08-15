#pragma once

#include "types.hpp"
#include "solver/gas_model.hpp"
#include "solver/solver_state.hpp"
#include <vector>

namespace cfd {

// ============================================================================
// Flux functions
// ============================================================================

/// Rusanov (Local Lax-Friedrichs) inviscid flux at a face
/// @param UL, UR  Conservative states at left and right of face
/// @param nx, ny  Face unit normal (pointing left->right)
/// @param gas     Gas model
/// @param dissipation_scale  Multiplier for dissipation term (default 1.0)
/// @return Numerical flux vector F*nx + G*ny
Conserved rusanov_flux(const Conserved& UL, const Conserved& UR,
                        Real nx, Real ny,
                        const GasModel& gas,
                        Real dissipation_scale = 1.0);

/// Compute inviscid flux vector F*nx + G*ny (no dissipation)
/// @param U  Conservative state
/// @param nx, ny  Face unit normal
/// @param gm1  gamma - 1
Conserved inviscid_flux_vector(const Conserved& U, Real nx, Real ny, Real gm1);

/// Compute maximum wave speed at a face for local time step
/// @param UL, UR  Conservative states
/// @param nx, ny  Face unit normal
/// @param gas     Gas model
Real max_wave_speed(const Conserved& UL, const Conserved& UR,
                     Real nx, Real ny, const GasModel& gas);

// ============================================================================
// Gradient and reconstruction
// ============================================================================

/// Least-squares gradient reconstruction for cell-centered data
/// Computes gradients of a scalar field at each cell using neighbor data.
///
/// @param values  Scalar field values at cell centers (n_cells)
/// @param cell_centroids  Cell centroid positions
/// @param cell_neighbors  Cell-cell adjacency list (neighbor cell indices)
/// @return  Gradients [dx, dy] at each cell
std::vector<Vec2> least_squares_gradient(
    const std::vector<Real>& values,
    const std::vector<Vec2>& cell_centroids,
    const std::vector<std::vector<Int>>& cell_neighbors);

/// Compute gradients for all primitive variables
/// Returns per-cell array of [grad_rho, grad_u, grad_v, grad_p]
struct PrimitiveGradients {
    std::vector<Vec2> grad_rho, grad_u, grad_v, grad_p;
};

PrimitiveGradients compute_primitive_gradients(
    const std::vector<Primitive>& W,
    const std::vector<Vec2>& cell_centroids,
    const std::vector<std::vector<Int>>& cell_neighbors);

// ============================================================================
// Reconstruction and limiting
// ============================================================================

/// Reconstruct left and right states at a face using piecewise-linear reconstruction
/// @param W_cell  Primitive state at cell center
/// @param W_neighbor  Primitive state at neighbor cell center
/// @param grad_cell  Gradient at cell center
/// @param grad_neighbor  Gradient at neighbor center
/// @param face_centroid  Face centroid position
/// @param cell_centroid  Cell centroid position
/// @param neighbor_centroid  Neighbor centroid position
/// @param limiter_active  Whether to apply the limiter (default true)
/// @return  (W_left, W_right) pair at face
std::pair<Primitive, Primitive> reconstruct_face_states(
    const Primitive& W_cell,
    const Primitive& W_neighbor,
    const Vec2& grad_cell,
    const Vec2& grad_neighbor,
    const Vec2& face_centroid,
    const Vec2& cell_centroid,
    const Vec2& neighbor_centroid,
    bool limiter_active = true);

/// Barth-Jespersen slope limiter
/// @param W_cell  Cell-centered primitive state
/// @param grad  Cell gradient [dx, dy] of a scalar
/// @param value_cell  Cell-centered value of the scalar
/// @param neighbor_values  Values at neighbor cells
/// @param neighbor_centroids  Neighbor cell centroid positions
/// @param cell_centroid  This cell's centroid position
/// @return Limiter value phi in [0,1]
Real barth_jespersen_limiter(
    Real value_cell,
    const Vec2& grad,
    const std::vector<Real>& neighbor_values,
    const std::vector<Vec2>& neighbor_centroids,
    const Vec2& cell_centroid);

// ============================================================================
// Viscous flux
// ============================================================================

/// Compute viscous flux at a face
/// @param WL, WR  Primitive states at left and right of face
/// @param gradL, gradR  Primitive gradients at left and right cells
/// @param face_centroid  Face centroid
/// @param cellL, cellR  Left and right cell centroids
/// @param nx, ny  Face unit normal
/// @param face_area  Face area/length
/// @param gas  Gas model
/// @param viscosity  Dynamic viscosity
/// @return Viscous flux vector (added to RHS; sign convention matches inviscid)
Conserved viscous_flux(const Primitive& WL, const Primitive& WR,
                        const PrimitiveGradients& gradL,
                        const PrimitiveGradients& gradR,
                        const Vec2& face_centroid,
                        const Vec2& cellL, const Vec2& cellR,
                        Real nx, Real ny, Real face_area,
                        const GasModel& gas, Real viscosity);

// ============================================================================
// Boundary conditions
// ============================================================================

/// Apply farfield boundary condition: return ghost state given interior state
/// Uses characteristic-based farfield treatment
Conserved farfield_boundary_state(const Conserved& U_interior,
                                   const FreestreamConfig& freestream,
                                   const GasModel& gas);

/// Apply inviscid slip wall: mirror the tangential velocity, zero normal
Conserved slip_wall_boundary_state(const Conserved& U_interior,
                                    Real nx, Real ny,
                                    const GasModel& gas);

/// Apply no-slip adiabatic wall: zero velocity, zero normal temperature gradient
Conserved no_slip_wall_boundary_state(const Conserved& U_interior,
                                       Real nx, Real ny,
                                       const GasModel& gas);

// ============================================================================
// Force integration
// ============================================================================

/// Compute pressure and viscous forces on a boundary face, decomposed into drag/lift
struct FaceForce {
    Real pressure_force_x{0}, pressure_force_y{0};
    Real viscous_force_x{0}, viscous_force_y{0};
};

FaceForce compute_boundary_face_force(
    const Conserved& U_face,
    const Primitive& W_face,
    const PrimitiveGradients& grad_face,
    Real nx, Real ny, Real face_area,
    const GasModel& gas, Real viscosity,
    BcType bc_type);

} // namespace cfd
