// Boundary-condition states.
//
// Two states are produced for each boundary face:
//   * ghostState()    -- the exterior state handed to the approximate Riemann
//                        solver, so boundary fluxes use exactly the same flux
//                        function as interior faces (weak imposition);
//   * boundaryValue() -- the physical value ON the boundary, used for the
//                        least-squares gradient stencil, the viscous wall flux
//                        and the surface output required by OUTPUT_CONTRACT.md.
//
// Adding a new boundary type means adding one enumerator in CaseConfig and one
// branch in each of the two functions below.
#pragma once

#include "core/CaseConfig.hpp"
#include "core/Types.hpp"
#include "physics/PerfectGas.hpp"

namespace cfd {

// Exterior ("ghost") state for the numerical flux.
inline PrimVec ghostState(BcType type, const PrimVec& wi, const Vec2& n, const PrimVec& winf,
                          const PerfectGas& gas) {
  switch (type) {
    case BcType::kSlipWall: {
      // Mirror the normal velocity: the Riemann solver then returns exactly
      // zero mass flux and the wall pressure plus an acoustic correction.
      const Real un = wi[1] * n[0] + wi[2] * n[1];
      return {wi[0], wi[1] - 2.0 * un * n[0], wi[2] - 2.0 * un * n[1], wi[3]};
    }
    case BcType::kNoSlipAdiabaticWall: {
      // The *convective* part of a solid-wall flux carries no mass, so the
      // mirrored state reverses the normal velocity only.  With rho, p and the
      // tangential velocity continuous the Riemann solver returns exactly
      //     F = [0, p_w n_x, p_w n_y, 0],   p_w = p + rho*a*u_n + rho*u_n^2,
      // i.e. zero mass and energy flux and a purely normal momentum flux with
      // an acoustic correction that drives u_n to zero.  Reversing the
      // tangential velocity as well would leave a spurious convective
      // tangential momentum flux rho*u_t*u_n at the wall.  The no-slip
      // condition itself is imposed through the viscous stress, which is built
      // from the zero-velocity wall state returned by boundaryValue().
      const Real un = wi[1] * n[0] + wi[2] * n[1];
      return {wi[0], wi[1] - 2.0 * un * n[0], wi[2] - 2.0 * un * n[1], wi[3]};
    }
    case BcType::kFarfield: {
      const Real g = gas.gamma();
      const Real gm1 = g - 1.0;
      const Real ai = gas.soundSpeed(wi[0], wi[3]);
      const Real ainf = gas.soundSpeed(winf[0], winf[3]);
      const Real uni = wi[1] * n[0] + wi[2] * n[1];
      const Real uninf = winf[1] * n[0] + winf[2] * n[1];
      const Real mn = uni / ai;
      if (mn <= -1.0) return winf;             // supersonic inflow
      if (mn >= 1.0) return wi;                // supersonic outflow
      // Subsonic: combine the incoming and outgoing Riemann invariants.
      const Real rp = uni + 2.0 * ai / gm1;    // from the interior
      const Real rm = uninf - 2.0 * ainf / gm1;  // from the exterior
      const Real unb = 0.5 * (rp + rm);
      const Real ab = 0.25 * gm1 * (rp - rm);
      const bool inflow = unb <= 0.0;
      const PrimVec& wref = inflow ? winf : wi;
      const Real unref = inflow ? uninf : uni;
      // Tangential velocity and entropy come from the upwind side.
      const Real utx = wref[1] - unref * n[0];
      const Real uty = wref[2] - unref * n[1];
      const Real sref = wref[3] / std::pow(wref[0], g);   // p / rho^gamma
      const Real rhob = std::pow(ab * ab / (g * sref), 1.0 / gm1);
      const Real pb = sref * std::pow(rhob, g);
      return {rhob, utx + unb * n[0], uty + unb * n[1], pb};
    }
  }
  return wi;
}

// Physical state on the boundary itself.
inline PrimVec boundaryValue(BcType type, const PrimVec& wi, const Vec2& n, const PrimVec& winf,
                             const PerfectGas& gas) {
  switch (type) {
    case BcType::kSlipWall: {
      const Real un = wi[1] * n[0] + wi[2] * n[1];
      return {wi[0], wi[1] - un * n[0], wi[2] - un * n[1], wi[3]};
    }
    case BcType::kNoSlipAdiabaticWall:
      // Zero velocity; adiabatic wall keeps the adjacent temperature, and the
      // boundary-layer approximation keeps the adjacent pressure.
      return {wi[0], 0.0, 0.0, wi[3]};
    case BcType::kFarfield:
      return ghostState(type, wi, n, winf, gas);
  }
  return wi;
}

inline bool isWall(BcType t) {
  return t == BcType::kSlipWall || t == BcType::kNoSlipAdiabaticWall;
}

}  // namespace cfd
