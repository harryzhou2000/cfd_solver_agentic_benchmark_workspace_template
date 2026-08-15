#pragma once

#include "common.hpp"

namespace cfd {

// Rusanov (local Lax-Friedrichs) numerical flux at an interior face.
// Returns the flux vector multiplied by the face area S.
State rusanov_flux(const State& UL, const State& UR,
                   Real nx, Real ny, Real S,
                   Real gamma, Real R, Real diss_scale);

// Pressure-only wall flux: (0, p*nx, p*ny, 0) * S.
State pressure_flux(Real p, Real nx, Real ny, Real S);

// Viscous flux at a face using face-averaged primitive gradients.
// gu, gv, gT are the face-averaged gradients of u, v, T.
State viscous_flux(const Prims& qL, const Prims& qR,
                   const Vec2& gu, const Vec2& gv, const Vec2& gT,
                   Real nx, Real ny, Real S,
                   Real mu, Real gamma, Real R, Real prandtl);

} // namespace cfd
