// cns2d -- approximate Riemann solvers for the inviscid flux.
//
// Three fluxes are implemented so the scheme can be matched to the case:
//   * Roe with a Harten-Hyman entropy fix -- the default.  Lowest dissipation
//     of the three, which matters for the Re 5000 boundary layers, and the
//     entropy fix removes the expansion-shock admissibility problem that plain
//     Roe has at sonic points in the M=2 cases.
//   * HLLC -- positivity-friendly and robust through strong shocks, used as a
//     fallback when Roe reports a non-physical intermediate state.
//   * Rusanov / local Lax-Friedrichs -- the most dissipative, retained because
//     the benchmark names it as the minimum acceptable flux and specifies a
//     dissipation scale for it.
//
// All three are written directly from the conservative jump/eigenstructure
// relations for the 2-D Euler equations in rotated (face-normal) coordinates.
#pragma once

#include <string>

#include "core/types.h"
#include "physics/perfect_gas.h"

namespace cns2d {

enum class RiemannFluxType {
  kRoeEntropyFix,
  kHllc,
  kRusanov,
};

std::string riemannFluxName(RiemannFluxType t);
RiemannFluxType parseRiemannFluxType(const std::string &name);
std::string entropyFixName(RiemannFluxType t);

// Exact inviscid flux projected on a normal: F(U) . n.
ConsVec eulerNormalFlux(const PerfectGas &gas, const PrimVec &W, Vec2 n);

// Numerical flux across a face with unit normal n, from left state WL to right
// state WR (both primitive).  'dissipation_scale' multiplies the jump term of
// the Rusanov flux and is ignored by the other two.
// 'max_wave_speed' returns the largest characteristic speed, used by the local
// time step and the implicit diagonal.
ConsVec riemannFlux(RiemannFluxType type, const PerfectGas &gas, const PrimVec &WL,
                    const PrimVec &WR, Vec2 n, Real dissipation_scale, Real &max_wave_speed);

}  // namespace cns2d
