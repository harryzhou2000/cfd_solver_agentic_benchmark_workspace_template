#pragma once

/// @file boundary.hpp
/// Boundary condition application for 2-D compressible Navier-Stokes.

#include "common.hpp"

namespace cfd {

/// Apply a boundary condition to get the ghost/exterior state at a face.
///
/// @param bc_type         type of boundary condition
/// @param U_interior      interior cell state (conservative)
/// @param face_normal     face unit normal (points outward from domain)
/// @param freestream      freestream reference state
/// @param gas             gas properties (gamma, R)
/// @param inviscid        true if the solver is inviscid (affects wall BCs)
/// @return                ghost/exterior conservative state at face
Vec4 apply_boundary_condition(BoundaryType bc_type, const Vec4& U_interior,
                               const Vec2& face_normal,
                               const FreestreamConfig& freestream,
                               const GasConfig& gas, bool inviscid);

} // namespace cfd
