#pragma once

#include "cfd/gas.hpp"

namespace cfd {

// Construct the primitive state immediately outside a boundary face. The
// normal points out of the fluid. Wall states are exact mirror states; the
// farfield state is characteristic and falls back to an admissible state.
Primitive boundary_exterior_state(BoundaryCondition condition,
                                  const Primitive& interior,
                                  const Primitive& freestream,
                                  const Vec2& outward_fluid_normal,
                                  const CaloricallyPerfectGas& gas) noexcept;

}  // namespace cfd
