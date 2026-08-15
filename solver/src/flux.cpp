/// @file flux.cpp
/// Implementation of inviscid and viscous flux routines.

#include "flux.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

// ============================================================================
// Inviscid flux (Euler)
// ============================================================================

Vec4 compute_inviscid_flux(const Vec4& U, const Vec2& normal, Real gamma) {
    const Real rho  = U(0);
    const Real rhou = U(1);
    const Real rhov = U(2);
    const Real rhoE = U(3);

    const Real u = rhou / rho;
    const Real v = rhov / rho;
    const Real ke = 0.5 * rho * (u * u + v * v);
    const Real p  = (gamma - 1.0) * (rhoE - ke);

    const Real Vn = u * normal(0) + v * normal(1);

    Vec4 flux;
    flux(0) = rho * Vn;
    flux(1) = rhou * Vn + p * normal(0);
    flux(2) = rhov * Vn + p * normal(1);
    flux(3) = (rhoE + p) * Vn;
    return flux;
}

// ============================================================================
// Rusanov (LLF) flux
// ============================================================================

Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
                  Real gamma, Real dissipation_scale) {
    const Real rhoL = UL(0);
    const Real uL   = UL(1) / rhoL;
    const Real vL   = UL(2) / rhoL;
    const Real keL  = 0.5 * rhoL * (uL * uL + vL * vL);
    const Real pL   = (gamma - 1.0) * (UL(3) - keL);

    const Real rhoR = UR(0);
    const Real uR   = UR(1) / rhoR;
    const Real vR   = UR(2) / rhoR;
    const Real keR  = 0.5 * rhoR * (uR * uR + vR * vR);
    const Real pR   = (gamma - 1.0) * (UR(3) - keR);

    const Real aL = std::sqrt(std::max(gamma * pL / rhoL, EPS));
    const Real aR = std::sqrt(std::max(gamma * pR / rhoR, EPS));

    const Real VnL = uL * normal(0) + vL * normal(1);
    const Real VnR = uR * normal(0) + vR * normal(1);

    const Real lambda = std::max(std::abs(VnL) + aL, std::abs(VnR) + aR);

    const Vec4 FL = compute_inviscid_flux(UL, normal, gamma);
    const Vec4 FR = compute_inviscid_flux(UR, normal, gamma);

    return 0.5 * (FL + FR) - 0.5 * dissipation_scale * lambda * (UR - UL);
}

// ============================================================================
// Roe flux with Harten-Yee entropy fix
// ============================================================================

Vec4 roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
              Real gamma, Real entropy_fix_delta) {
    // --- Primitive states ---
    const Real rhoL = UL(0);
    const Real uL   = UL(1) / rhoL;
    const Real vL   = UL(2) / rhoL;
    const Real keL  = 0.5 * rhoL * (uL * uL + vL * vL);
    const Real pL   = (gamma - 1.0) * (UL(3) - keL);
    const Real hL   = (UL(3) + pL) / rhoL;  // total enthalpy

    const Real rhoR = UR(0);
    const Real uR   = UR(1) / rhoR;
    const Real vR   = UR(2) / rhoR;
    const Real keR  = 0.5 * rhoR * (uR * uR + vR * vR);
    const Real pR   = (gamma - 1.0) * (UR(3) - keR);
    const Real hR   = (UR(3) + pR) / rhoR;

    // --- Roe averages ---
    const Real Rfac = std::sqrt(std::max(rhoR / rhoL, EPS));
    const Real rho_roe = Rfac * rhoL;
    const Real u_roe   = (uL + Rfac * uR) / (1.0 + Rfac);
    const Real v_roe   = (vL + Rfac * vR) / (1.0 + Rfac);
    const Real h_roe   = (hL + Rfac * hR) / (1.0 + Rfac);
    const Real Vn_roe  = u_roe * normal(0) + v_roe * normal(1);
    const Real vel2_roe = u_roe * u_roe + v_roe * v_roe;
    const Real a_roe   = std::sqrt(std::max((gamma - 1.0) * (h_roe - 0.5 * vel2_roe), EPS));

    // --- Wave speeds (eigenvalues) ---
    const Real lam1 = Vn_roe - a_roe;
    const Real lam2 = Vn_roe;          // repeated (entropy + shear wave)
    const Real lam4 = Vn_roe + a_roe;

    // --- Harten-Yee entropy fix ---
    auto entropy_fix = [entropy_fix_delta](Real lambda, Real a_ref) -> Real {
        const Real delta = entropy_fix_delta * a_ref;
        if (std::abs(lambda) < delta) {
            return (lambda * lambda + delta * delta) / (2.0 * delta);
        }
        return std::abs(lambda);
    };

    const Real a_ref = a_roe;  // use Roe-averaged speed of sound as reference
    const Real lam1_abs = entropy_fix(lam1, a_ref);
    const Real lam2_abs = entropy_fix(lam2, a_ref);
    const Real lam4_abs = entropy_fix(lam4, a_ref);

    // --- Delta U = UR - UL ---
    const Real drho   = UR(0) - UL(0);
    const Real drhou  = UR(1) - UL(1);
    const Real drhov  = UR(2) - UL(2);

    // --- Wave strengths (α) in the direction of right eigenvectors ---
    // Tangent vector (rotate normal by 90° counter-clockwise)
    const Real tx = -normal(1);
    const Real ty =  normal(0);

    // Velocity jump in normal and tangential directions
    const Real dVn = (drhou - u_roe * drho) * normal(0) + (drhov - v_roe * drho) * normal(1);
    const Real dVt = (drhou - u_roe * drho) * tx + (drhov - v_roe * drho) * ty;

    // Standard Roe wave strengths for ideal gas:
    // dVn/dVt here are momentum jumps (they already contain ρ̃ implicitly:
    // dVn = ρ̃·ΔVn, dVt = ρ̃·ΔVt), so the acoustic strengths use dVn directly:
    // α1 = (Δp - a_roe*ΔVn) / (2*a_roe²)   (acoustic: λ = Vn - a)
    // α2 = Δρ - Δp / a_roe²                (entropy:  λ = Vn)
    // α3 = ρ_roe * ΔVt                      (shear:    λ = Vn)
    // α4 = (Δp + a_roe*ΔVn) / (2*a_roe²)   (acoustic: λ = Vn + a)

    const Real dp = pR - pL;

    const Real a1 = (dp - a_roe * dVn) / (2.0 * a_roe * a_roe);
    const Real a2 = drho - dp / (a_roe * a_roe);
    const Real a3 = rho_roe * dVt;
    const Real a4 = (dp + a_roe * dVn) / (2.0 * a_roe * a_roe);

    // --- Right eigenvectors (at Roe state) ---
    // K1 = [1, u-a*nx, v-a*ny, h-a*Vn]
    // K2 = [1, u, v, 0.5*(u²+v²)]
    // K3 = [0, tx, ty, Vt]   (Vt = u*tx + v*ty)
    // K4 = [1, u+a*nx, v+a*ny, h+a*Vn]

    const Real Vt_roe = u_roe * tx + v_roe * ty;

    Vec4 K1, K2, K3, K4;
    K1 << 1.0, u_roe - a_roe * normal(0), v_roe - a_roe * normal(1), h_roe - a_roe * Vn_roe;
    K2 << 1.0, u_roe, v_roe, 0.5 * vel2_roe;
    K3 << 0.0, tx, ty, Vt_roe;
    K4 << 1.0, u_roe + a_roe * normal(0), v_roe + a_roe * normal(1), h_roe + a_roe * Vn_roe;

    // Dissipation term: Σ |λ_k| α_k K_k
    Vec4 dissipation = lam1_abs * a1 * K1 +
                       lam2_abs * a2 * K2 +
                       lam2_abs * a3 * K3 +
                       lam4_abs * a4 * K4;

    // Central flux
    const Vec4 FL = compute_inviscid_flux(UL, normal, gamma);
    const Vec4 FR = compute_inviscid_flux(UR, normal, gamma);

    return 0.5 * (FL + FR) - 0.5 * dissipation;
}

// ============================================================================
// Viscous flux
// ============================================================================

Vec4 compute_viscous_flux(const Vec4& U, const Mat2& grad_vel,
                          const Vec2& grad_T, const Vec2& normal,
                          Real mu, Real gamma, Real Pr, Real R) {
    const Real rho  = U(0);
    const Real rhou = U(1);
    const Real rhov = U(2);
    const Real u    = rhou / rho;
    const Real v    = rhov / rho;

    const Real dudx = grad_vel(0, 0);
    const Real dudy = grad_vel(0, 1);
    const Real dvdx = grad_vel(1, 0);
    const Real dvdy = grad_vel(1, 1);

    const Real divV = dudx + dvdy;

    // Newtonian stress tensor (with Stokes hypothesis)
    const Real txx = 2.0 * mu * dudx - (2.0 / 3.0) * mu * divV;
    const Real tyy = 2.0 * mu * dvdy - (2.0 / 3.0) * mu * divV;
    const Real txy = mu * (dudy + dvdx);

    // Thermal conductivity: k = μ * cp / Pr, cp = γR/(γ−1)
    const Real cp = gamma * R / (gamma - 1.0);
    const Real k  = mu * cp / Pr;

    // Heat flux vector: q = -k ∇T
    const Real qx = -k * grad_T(0);
    const Real qy = -k * grad_T(1);

    // Viscous flux through face: Fv·n
    Vec4 flux;
    flux(0) = 0.0;
    flux(1) = txx * normal(0) + txy * normal(1);
    flux(2) = txy * normal(0) + tyy * normal(1);
    flux(3) = (txx * u + txy * v - qx) * normal(0) +
              (txy * u + tyy * v - qy) * normal(1);
    return flux;
}

} // namespace cfd
