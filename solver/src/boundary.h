#pragma once
// Phase 3/4: boundary conditions — ghost/boundary state construction.
//
// Each BC helper maps the interior state U_int to the boundary (ghost) state
// U_b at a boundary face. The face flux is then computed with the Riemann
// solver: flux = rusanov_flux(U_int, U_b, normal, gas).
//
// Phase 4 (second order): the caller passes the RECONSTRUCTED interior face
// state (U_i + limited_grad_i . dr_to_face) as U_int — the same API — so the
// ghost state is built from the reconstructed value, giving second-order
// boundary treatment. The first-order call (U_int = cell center) remains
// valid and unchanged.

#include "types.h"

namespace cfd {

// Farfield boundary state (characteristic-based).
//
// Supersonic outflow (vn_int >= a): everything from the interior.
// Supersonic inflow  (vn_int <= -a): everything from the freestream.
// Subsonic: combine the outgoing Riemann invariant (R+ = vn + 2a/(gamma-1),
// carried by the interior) with the incoming invariant (R- = vn - 2a/(gamma-1),
// carried by the freestream); entropy and tangential velocity come from the
// interior on outflow and from the freestream on inflow.
ConsState farfield_state(const ConsState& U_int, const Freestream& fs,
                         const Vec2& normal, const GasConfig& gas);

// Slip wall: mirror the interior normal velocity (so the wall sees zero
// normal velocity), preserve the tangential component; density and pressure
// from the interior.
ConsState slip_wall_state(const ConsState& U_int, const Vec2& normal,
                          const GasConfig& gas);

// No-slip adiabatic wall: zero velocity at the wall; density and pressure
// from the interior (zero-gradient extrapolation at first order).
ConsState no_slip_wall_state(const ConsState& U_int, const GasConfig& gas);

// BC dispatch: boundary state for the given interior state and face normal.
// Throws std::runtime_error for an unsupported BC type.
ConsState boundary_state(const ConsState& U_int, BCType bc,
                         const Vec2& normal, const Freestream& fs,
                         const GasConfig& gas);

}  // namespace cfd
