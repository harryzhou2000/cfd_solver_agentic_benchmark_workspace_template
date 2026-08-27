// cns2d -- laminar viscous flux.
//
// The Newtonian stress tensor and Fourier heat flux are assembled from
// primitive-variable gradients:
//   tau_xx = 2 mu u_x - (2/3) mu (u_x + v_y)
//   tau_yy = 2 mu v_y - (2/3) mu (u_x + v_y)
//   tau_xy = mu (u_y + v_x)
//   q      = -k grad(T),   k = mu cp / Pr
//
// Face gradients use the averaged cell gradients corrected along the
// cell-to-cell direction.  That correction is what keeps the viscous operator
// coercive on stretched boundary-layer cells: a plain average of the two cell
// gradients has a near-null mode in the wall-normal direction on high-aspect
// ratio cells and produces an odd-even oscillation in the boundary layer.
#pragma once

#include "core/types.h"
#include "physics/perfect_gas.h"

namespace cns2d {

// Gradients needed by the viscous flux, in primitive-variable form.
struct ViscousGradients {
  Vec2 grad_u{};
  Vec2 grad_v{};
  Vec2 grad_T{};
};

// Viscous flux (as it appears on the right-hand side, i.e. the flux to be
// ADDED to the residual with the same sign convention as the inviscid flux
// subtraction) projected on the unit normal n.
ConsVec viscousNormalFlux(const PerfectGas &gas, Real mu, const PrimVec &W_face,
                          const ViscousGradients &g, Vec2 n);

// Wall shear stress vector (tangential traction) and heat flux magnitude at a
// wall face.  Used by force integration so skin friction comes from the
// tangential shear rather than from the full viscous normal traction.
Vec2 wallShearTraction(Real mu, const ViscousGradients &g, Vec2 n);

}  // namespace cns2d
