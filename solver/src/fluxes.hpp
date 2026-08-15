#pragma once

#include "gas.hpp"
#include "types.hpp"

namespace cfd {

// Inviscid and viscous numerical flux functions.
//
// Sign conventions (all fluxes are the quantity to ADD to the cell
// residual, i.e. the net flux leaving the cell through a face whose
// geometric normal is `n`, |n| = face area):
//   * inviscid flux through the face: F_n(U) = (rho*vn, rho*u*vn + p*nx,
//     rho*v*vn + p*ny, (rho*E + p)*vn) with vn = u*nx + v*ny (u, v are the
//     velocity components, n the area-scaled normal).
//   * viscous flux through the face: G_n(U) = (0, -(tau*n), -(tau*n),
//     -(tau*n).v + q.n) where tau is the Newtonian stress tensor, q the
//     Fourier heat flux and n the area-scaled normal. The minus sign
//     follows from the conservation form dU/dt + div(F - Fv) = 0.

// Physical inviscid flux of the conservative state across a face with
// normal `n` (magnitude = face area).
Vector4 inviscid_flux(const Vector4& U, const Vector2& n, const GasParams& gas);

// Rusanov (local Lax-Friedrichs) numerical flux between states UL (left)
// and UR (right) across a face with normal (nx, ny), |(nx,ny)| = face area.
// dissipation_scale multiplies the maximum wave speed lambda_max =
// max(|vn_L| + a_L, |vn_R| + a_R) (default 1.0; scale > 1 adds dissipation).
Vector4 inviscid_flux_rusanov(const Vector4& UL, const Vector4& UR,
                              double nx, double ny, const GasParams& gas,
                              double dissipation_scale = 1.0);

// Roe numerical flux with a Harten-Yee entropy fix. Falls back to the
// Rusanov flux if either state is non-physical (rho <= 0 or a^2 <= 0).
Vector4 inviscid_flux_roe(const Vector4& UL, const Vector4& UR,
                          double nx, double ny, const GasParams& gas);

// Viscous (laminar) numerical flux through a face with normal (nx, ny)
// (magnitude = face area). First-order gradient approximation from the
// face jump: grad(phi) ~= (phi_R - phi_L) / d * n_hat, where
// (dx, dy) = center_R - center_L is the cell-center displacement and
// d = |(dx, dy)|. Applies the Newtonian stress tensor (2D planar form with
// the (2/3) bulk-viscosity term) and the Fourier heat flux q = -k grad T,
// k = mu * cp / Pr. UL/UR are the conservative states, primL/primR their
// primitive counterparts (passed to avoid re-deriving them).
Vector4 viscous_flux(const Vector4& UL, const Vector4& UR,
                     const PrimitiveState& primL, const PrimitiveState& primR,
                     double nx, double ny, double dx, double dy,
                     const GasParams& gas, double viscosity);

// Viscous flux evaluated from an explicit face gradient (used with
// second-order reconstruction: grad_face = 0.5 * (grad_L + grad_R)).
// Same sign convention as viscous_flux (the quantity to add to the
// residual). primL/primR are the (reconstructed) face primitive states.
Vector4 viscous_flux_from_gradient(
    const PrimitiveState& primL, const PrimitiveState& primR, double nx,
    double ny, double du_dx, double du_dy, double dv_dx, double dv_dy,
    double dT_dx, double dT_dy, const GasParams& gas, double viscosity);

// Backward-compatible wrappers over the signatures above.
inline Vector4 rusanov_flux(const Vector4& UL, const Vector4& UR,
                            const Vector2& n, const GasParams& gas) {
    return inviscid_flux_rusanov(UL, UR, n.x, n.y, gas);
}

inline Vector4 roe_flux(const Vector4& UL, const Vector4& UR,
                        const Vector2& n, const GasParams& gas) {
    return inviscid_flux_roe(UL, UR, n.x, n.y, gas);
}

}  // namespace cfd
