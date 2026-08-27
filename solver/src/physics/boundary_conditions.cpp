#include "physics/boundary_conditions.h"

#include <algorithm>
#include <cmath>

#include "core/exceptions.h"

namespace cns2d {

ConsVec slipWallState(const ConsVec &interior, Vec2 normal) {
  // Reflect the normal momentum component: u_ghost = u - 2 (u.n) n.
  // Density and pressure are unchanged, so the reflected state has the same
  // total energy and the resulting face flux carries only pressure through the
  // wall, which is the correct inviscid wall condition.
  const Real inv_rho = 1.0 / interior[kRho];
  const Real u = interior[kRhoU] * inv_rho;
  const Real v = interior[kRhoV] * inv_rho;
  const Real un = u * normal.x + v * normal.y;
  const Real ur = u - 2.0 * un * normal.x;
  const Real vr = v - 2.0 * un * normal.y;
  ConsVec out{};
  out[kRho] = interior[kRho];
  out[kRhoU] = interior[kRho] * ur;
  out[kRhoV] = interior[kRho] * vr;
  out[kRhoE] = interior[kRhoE];  // |u| is unchanged by reflection
  return out;
}

ConsVec slipWallFaceState(const ConsVec &interior, Vec2 normal) {
  // Physical state on the wall: remove the normal velocity component entirely
  // (u_n = 0) and keep the tangential component.  Density and pressure are
  // unchanged; the total energy is corrected for the reduced kinetic energy.
  const Real inv_rho = 1.0 / interior[kRho];
  const Real u = interior[kRhoU] * inv_rho;
  const Real v = interior[kRhoV] * inv_rho;
  const Real un = u * normal.x + v * normal.y;
  const Real ut_x = u - un * normal.x;
  const Real ut_y = v - un * normal.y;
  // Internal energy is preserved (pressure and density unchanged).
  const Real kinetic_interior = 0.5 * (u * u + v * v);
  const Real internal = interior[kRhoE] * inv_rho - kinetic_interior;
  const Real kinetic_wall = 0.5 * (ut_x * ut_x + ut_y * ut_y);
  ConsVec out{};
  out[kRho] = interior[kRho];
  out[kRhoU] = interior[kRho] * ut_x;
  out[kRhoV] = interior[kRho] * ut_y;
  out[kRhoE] = interior[kRho] * (internal + kinetic_wall);
  return out;
}

ConsVec noSlipWallGhostState(const ConsVec &interior) {
  // Reverse the whole velocity vector; |u| and therefore the total energy are
  // unchanged, so the face-average velocity is exactly zero.
  ConsVec out{};
  out[kRho] = interior[kRho];
  out[kRhoU] = -interior[kRhoU];
  out[kRhoV] = -interior[kRhoV];
  out[kRhoE] = interior[kRhoE];
  return out;
}

ConsVec noSlipAdiabaticWallState(const ConsVec &interior, const FlowContext &ctx) {
  // Wall state: zero velocity, interior pressure (dp/dn = 0), and the interior
  // temperature (adiabatic wall, dT/dn = 0).  Density then follows from the
  // equation of state, so the wall state is thermodynamically consistent rather
  // than simply copying the interior density.
  const Real p = ctx.gas.pressureFromCons(interior);
  const Real t = ctx.gas.temperatureFromRhoP(interior[kRho], p);
  const Real rho_wall = p / (ctx.gas.R() * t);  // == interior density for an adiabatic wall
  ConsVec out{};
  out[kRho] = rho_wall;
  out[kRhoU] = 0.0;
  out[kRhoV] = 0.0;
  out[kRhoE] = p / (ctx.gas.gamma() - 1.0);  // zero kinetic energy at the wall
  return out;
}

ConsVec farfieldState(const ConsVec &interior, Vec2 normal, const FlowContext &ctx) {
  // Characteristic farfield based on locally one-dimensional Riemann
  // invariants along the face normal.
  const PerfectGas &gas = ctx.gas;
  const Real gamma = gas.gamma();
  const Real gm1 = gamma - 1.0;

  const Real rho_i = interior[kRho];
  const Real inv_rho_i = 1.0 / rho_i;
  const Real u_i = interior[kRhoU] * inv_rho_i;
  const Real v_i = interior[kRhoV] * inv_rho_i;
  const Real p_i = gas.pressureFromCons(interior);
  const Real a_i = gas.soundSpeed(rho_i, p_i);
  const Real un_i = u_i * normal.x + v_i * normal.y;

  const Real rho_o = ctx.freestream_prim[kPrimRho];
  const Real u_o = ctx.freestream_prim[kPrimU];
  const Real v_o = ctx.freestream_prim[kPrimV];
  const Real p_o = ctx.freestream_prim[kPrimP];
  const Real a_o = gas.soundSpeed(rho_o, p_o);
  const Real un_o = u_o * normal.x + v_o * normal.y;

  const Real mach_n = un_i / a_i;

  // Supersonic outflow: everything is determined by the interior.
  if (mach_n >= 1.0) {
    return interior;
  }
  // Supersonic inflow: everything is determined by the freestream.
  if (mach_n <= -1.0) {
    return ctx.freestream_cons;
  }

  // Subsonic: outgoing invariant from inside, incoming from the freestream.
  const Real r_plus = un_i + 2.0 * a_i / gm1;   // travels outward
  const Real r_minus = un_o - 2.0 * a_o / gm1;  // travels inward
  const Real un_b = 0.5 * (r_plus + r_minus);
  const Real a_b = 0.25 * gm1 * (r_plus - r_minus);
  if (!(a_b > 0.0)) {
    // Degenerate/unphysical invariant combination: fall back to the freestream
    // rather than producing a negative sound speed.
    return ctx.freestream_cons;
  }

  // Entropy and tangential velocity come from whichever side the flow arrives
  // from.
  const bool outflow = un_b > 0.0;
  const Real rho_ref = outflow ? rho_i : rho_o;
  const Real p_ref = outflow ? p_i : p_o;
  const Real u_ref = outflow ? u_i : u_o;
  const Real v_ref = outflow ? v_i : v_o;
  const Real un_ref = outflow ? un_i : un_o;

  // s = p / rho^gamma is constant along the streamline.
  const Real entropy = p_ref / std::pow(rho_ref, gamma);
  const Real rho_b = std::pow(a_b * a_b / (gamma * entropy), 1.0 / gm1);
  const Real p_b = entropy * std::pow(rho_b, gamma);

  // Tangential velocity is carried unchanged from the upwind side.
  const Real ut_x = u_ref - un_ref * normal.x;
  const Real ut_y = v_ref - un_ref * normal.y;
  const Real u_b = ut_x + un_b * normal.x;
  const Real v_b = ut_y + un_b * normal.y;

  if (!(rho_b > 0.0) || !(p_b > 0.0)) {
    return ctx.freestream_cons;
  }
  return gas.consFromPrim({rho_b, u_b, v_b, p_b});
}

ConsVec boundaryGhostState(BCType type, const ConsVec &interior, Vec2 normal,
                           const FlowContext &ctx) {
  switch (type) {
    case BCType::kFarfield:
      return farfieldState(interior, normal, ctx);
    case BCType::kSlipWall:
      return slipWallState(interior, normal);
    case BCType::kNoSlipAdiabaticWall:
      return noSlipWallGhostState(interior);
  }
  throw CnsError("internal error: unhandled boundary condition type");
}

ConsVec boundaryFaceState(BCType type, const ConsVec &interior, Vec2 normal,
                          const FlowContext &ctx) {
  switch (type) {
    case BCType::kFarfield:
      return farfieldState(interior, normal, ctx);
    case BCType::kSlipWall:
      return slipWallFaceState(interior, normal);
    case BCType::kNoSlipAdiabaticWall:
      return noSlipAdiabaticWallState(interior, ctx);
  }
  throw CnsError("internal error: unhandled boundary condition type");
}

}  // namespace cns2d
