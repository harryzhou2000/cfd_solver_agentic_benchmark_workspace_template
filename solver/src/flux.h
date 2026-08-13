#pragma once
// Phase 3: inviscid numerical flux — Rusanov / Local Lax-Friedrichs.
//
// The face normal passed to the flux functions is the AREA vector: unit
// normal scaled by the face length (2D area), oriented left -> right for
// internal faces and outward for boundary faces. The returned flux is
// therefore the face-integrated flux (per unit depth).

#include "types.h"

namespace cfd {

// Euler flux in the x-direction:
//   F_x = [rho*u, rho*u^2 + p, rho*u*v, u*(rho*E + p)]
ConsState inviscid_flux_x(const ConsState& U, const GasConfig& gas);

// Euler flux in the y-direction:
//   F_y = [rho*v, rho*u*v, rho*v^2 + p, v*(rho*E + p)]
ConsState inviscid_flux_y(const ConsState& U, const GasConfig& gas);

// F(U) . n = F_x * nx + F_y * ny, where `normal` is the area vector.
ConsState inviscid_flux_dot_normal(const ConsState& U, const Vec2& normal,
                                   const GasConfig& gas);

// Same flux written into a caller-provided 4-element array
// (result[0..3] = rho, rhou, rhov, rhoE). Single-pass primitive evaluation;
// this is the hot path of the residual assembly.
void inviscid_flux_dot_normal(const ConsState& U, const Vec2& normal,
                              const GasConfig& gas, double result[4]);

// Rusanov / Local Lax-Friedrichs face flux (dissipation scale 1.0).
ConsState rusanov_flux(const ConsState& UL, const ConsState& UR,
                       const Vec2& normal, const GasConfig& gas);

// Rusanov flux with an explicit dissipation scale (the case JSON's
// rusanov_dissipation_scale; multiplies the dissipation term only).
ConsState rusanov_flux(const ConsState& UL, const ConsState& UR,
                       const Vec2& normal, const GasConfig& gas,
                       double dissipation_scale);

}  // namespace cfd
