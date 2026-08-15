#pragma once

/// @file flux.hpp
/// Inviscid and viscous flux computation for 2-D compressible Navier-Stokes.

#include "common.hpp"

namespace cfd {

// ============================================================================
// Inviscid (Euler) fluxes
// ============================================================================

/// Compute the inviscid flux through a face with given unit normal.
/// U = [rho, rho*u, rho*v, rho*E] (conservative variables)
/// Returns F·n (dimensional, not per-area).
Vec4 compute_inviscid_flux(const Vec4& U, const Vec2& normal, Real gamma);

/// Rusanov (Local Lax-Friedrichs) flux.
/// @param dissipation_scale  multiplicative factor on the dissipation term
Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal, Real gamma,
                  Real dissipation_scale = 1.0);

/// Roe flux with Harten-Yee entropy fix.
/// @param entropy_fix_delta  δ parameter (fraction of reference speed of sound)
Vec4 roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal, Real gamma,
              Real entropy_fix_delta = 0.1);

// ============================================================================
// Viscous flux
// ============================================================================

/// Compute the viscous flux through a face.
/// grad_vel = [du/dx du/dy; dv/dx dv/dy] (2x2 velocity gradient at face)
/// grad_T   = [dT/dx, dT/dy] (temperature gradient at face)
/// normal   = face unit normal
Vec4 compute_viscous_flux(const Vec4& U, const Mat2& grad_vel, const Vec2& grad_T,
                          const Vec2& normal, Real mu, Real gamma, Real Pr, Real R);

// ============================================================================
// Utility: primitive ↔ conservative
// ============================================================================

/// Extract primitive (rho, u, v, p) from conservative state.
inline Vec4 conservative_to_primitive(const Vec4& U, Real gamma) {
    Real rho  = U(0);
    Real rhou = U(1);
    Real rhov = U(2);
    Real rhoE = U(3);
    Real u    = rhou / rho;
    Real v    = rhov / rho;
    Real ke   = 0.5 * rho * (u * u + v * v);
    Real p    = (gamma - 1.0) * (rhoE - ke);
    Vec4 prim;
    prim << rho, u, v, p;
    return prim;
}

/// Build conservative state from primitive variables.
inline Vec4 primitive_to_conservative(const Vec4& prim, Real gamma) {
    Real rho = prim(0);
    Real u   = prim(1);
    Real v   = prim(2);
    Real p   = prim(3);
    Real rhoE = p / (gamma - 1.0) + 0.5 * rho * (u * u + v * v);
    Vec4 U;
    U << rho, rho * u, rho * v, rhoE;
    return U;
}

/// Compute speed of sound from primitive variables.
inline Real speed_of_sound(Real rho, Real p, Real gamma) {
    return std::sqrt(std::max(gamma * p / rho, EPS));
}

} // namespace cfd
