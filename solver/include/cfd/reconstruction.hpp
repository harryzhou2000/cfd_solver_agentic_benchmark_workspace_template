#pragma once

#include <array>
#include <vector>

#include "cfd/physics.hpp"

namespace cfd {

/// A geometric sample used by a cell-local reconstruction.  The sample may
/// represent an owned neighbor, a ghost cell, or a boundary ghost value.
struct PrimitiveSample {
    Vec2 location{};
    Primitive value{};
};

struct ReconstructionBounds {
    Primitive minimum{};
    Primitive maximum{};
};

struct ReconstructionResult {
    PrimitiveGradients gradients{};
    /// Barth--Jespersen factor for [rho, u, v, p].
    std::array<Real, kStateVariables> limiter{{1.0, 1.0, 1.0, 1.0}};
    bool used_singular_fallback{};
};

struct FaceReconstruction {
    ThermodynamicState primitive{};
    ConservativeState conservative{};
    Real positivity_scale{1.0};
    bool used_positivity_fallback{};
};

/// Computes weighted least-squares gradients of [rho,u,v,p].  Weights are
/// inverse squared distance; rank-deficient stencils use a bounded one-
/// dimensional projection fallback rather than an unstable matrix inverse.
[[nodiscard]] PrimitiveGradients weighted_least_squares_gradients(
    const Vec2& cell_center, const Primitive& cell_value,
    const std::vector<PrimitiveSample>& samples, bool* used_singular_fallback = nullptr);

/// Bounds every primitive component by the center, neighbor/ghost samples,
/// and supplied physical-boundary values.
[[nodiscard]] ReconstructionBounds primitive_bounds(
    const Primitive& cell_value, const std::vector<PrimitiveSample>& samples,
    const std::vector<Primitive>& boundary_values = {});

/// Applies the multidimensional Barth--Jespersen limiter at all supplied face
/// locations.  Boundary values participate in the extrema, so the caller can
/// use the same routine for owned, ghost, and physical-boundary faces.
[[nodiscard]] ReconstructionResult reconstruct_limited_primitive(
    const Vec2& cell_center, const Primitive& cell_value,
    const std::vector<PrimitiveSample>& samples, const std::vector<Vec2>& face_locations,
    const std::vector<Primitive>& boundary_values = {});

/// Reconstructs at a face, scales the full primitive increment when rho or p
/// would cross its gas-model floor, and encodes a conservative state.  An
/// inadmissible/non-finite candidate falls back to the cell-center state.
[[nodiscard]] FaceReconstruction reconstruct_face_state(
    const Vec2& cell_center, const Primitive& cell_value,
    const PrimitiveGradients& gradients, const Vec2& face_location, const GasModel& gas);

/// Detects a strong compressive pressure jump across an oriented interior
/// face.  Such faces may use cell-centre inviscid states to prevent a limited
/// second-order shock from oscillating between adjacent cells; smooth and
/// expansive faces remain second order.
[[nodiscard]] bool compressive_shock_face(const Primitive& left,
                                          const Primitive& right,
                                          const Vec2& left_to_right_normal,
                                          Real relative_pressure_threshold = 0.15);

/// Converts primitive density/pressure gradients to grad(T) for T=p/(rho R).
[[nodiscard]] Vec2 temperature_gradient(const Primitive& primitive,
                                         const PrimitiveGradients& gradients,
                                         const GasModel& gas);

/// Corrects the arithmetic face gradient along the cell-center connector.  It
/// preserves exact linear gradients and enforces the exact center-to-center
/// derivative from the two cell values.
[[nodiscard]] Vec2 corrected_central_face_gradient(Real left_value, Real right_value,
                                                    const Vec2& left_center,
                                                    const Vec2& right_center,
                                                    const Vec2& left_gradient,
                                                    const Vec2& right_gradient);

[[nodiscard]] PrimitiveGradients corrected_central_face_gradients(
    const Primitive& left_value, const Primitive& right_value, const Vec2& left_center,
    const Vec2& right_center, const PrimitiveGradients& left_gradients,
    const PrimitiveGradients& right_gradients);

/// Returns a wall primitive with the no-slip wall velocity imposed exactly.
[[nodiscard]] Primitive no_slip_wall_primitive(const Primitive& extrapolated) noexcept;

/// Removes the normal temperature derivative to impose an adiabatic wall.  A
/// zero/invalid normal leaves the supplied gradient unchanged.
[[nodiscard]] Vec2 adiabatic_temperature_gradient(const Vec2& temperature_gradient,
                                                   const Vec2& wall_normal) noexcept;

}  // namespace cfd
