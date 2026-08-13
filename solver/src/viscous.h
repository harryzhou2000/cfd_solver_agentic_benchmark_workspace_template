#pragma once
// Phase 4: laminar viscous fluxes for the residual and the wall shear stress
// for the force integration.
//
// For a laminar case (constant viscosity mu from the freestream Reynolds
// number) the viscous flux through a face is
//
//   F_v . n = -[ 0,
//                tau_xx*nx + tau_xy*ny,
//                tau_xy*nx + tau_yy*ny,
//                (tau_xx*u + tau_xy*v + k*dT/dx)*nx
//                + (tau_xy*u + tau_yy*v + k*dT/dy)*ny ]
//
// with the stress tensor
//   tau_xx = mu*(2 du/dx - 2/3 (du/dx + dv/dy))
//   tau_yy = mu*(2 dv/dy - 2/3 (du/dx + dv/dy))
//   tau_xy = mu*(du/dy + dv/dx),
// the thermal conductivity k = mu * cp / Pr (cp = gamma*R/(gamma-1)), and
// n the face AREA vector (unit normal times face length). The leading minus
// sign matches the residual convention R = sum (F_inviscid - F_viscous):
// the value returned here is ADDED to the left cell's residual and
// SUBTRACTED from the right cell's, exactly like the inviscid flux.
//
// For inviscid cases mu = 0 and the viscous flux vanishes identically.

#include "types.h"

namespace cfd {

// Viscous contribution to the residual at a face. All gradients and face
// values are supplied by the caller (face-averaged cell gradients, averaged
// face velocity/temperature, or a one-sided wall treatment):
//   grad_u[0..1] = du/dx, du/dy     grad_v[0..1] = dv/dx, dv/dy
//   grad_T[0..1] = dT/dx, dT/dy     u_face, v_face = face velocity
//   normal = face area vector (already scaled by the face length)
//   mu     = constant viscosity (0 for inviscid)
//   cp     = gamma * R / (gamma - 1)
//   prandtl= Prandtl number
// On return out[0..3] = rho, rhou, rhov, rhoE components of F_v . n.
void viscous_flux_dot_normal(const double grad_u[2], const double grad_v[2],
                             const double grad_T[2], double u_face,
                             double v_face, const Vec2& normal, double mu,
                             double cp, double prandtl, double out[4]);

}  // namespace cfd
