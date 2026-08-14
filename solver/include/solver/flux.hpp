#pragma once
#include "mesh_types.hpp"
#include "case_config.hpp"

namespace solver {

// Convert conservative [rho, rhou, rhov, rhoE] to primitive [rho, u, v, p]
Vec4 conservative_to_primitive(const Vec4& U, double gamma);

// Convert primitive [rho, u, v, p] to conservative [rho, rhou, rhov, rhoE]
Vec4 primitive_to_conservative(const Vec4& W, double gamma);

// Speed of sound: a = sqrt(gamma * p / rho)
double speed_of_sound(double rho, double p, double gamma);

// Inviscid flux F(U) * n (flux in face-normal direction)
// F[0] = rho*Vn, F[1] = rho*u*Vn + p*nx, F[2] = rho*v*Vn + p*ny, F[3] = (rho*E+p)*Vn
Vec4 inviscid_flux_term(const Vec4& U, const Vec2& normal, double gamma);

// Rusanov / Local Lax-Friedrichs flux
// lambda = max(|Vn_L| + a_L, |Vn_R| + a_R)
// F = 0.5*(F_L + F_R) - 0.5*lambda*dissipation_scale*(U_R - U_L)
Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
                  double gamma, double dissipation_scale = 1.0);

// Roe flux with Harten-Yee entropy fix (2D unstructured)
// Computes Roe-averaged state, eigenvalues, wave strengths, and characteristic flux
// delta is the entropy fix threshold (recommended: 0.05 * reference_sound_speed)
Vec4 roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
              double gamma, double delta = 0.1);

// Viscous flux in face-normal direction
// Uses face-averaged velocity gradients, stress tensor, and heat flux
// mu: dynamic viscosity, cp: specific heat at const pressure, Pr: Prandtl number
// grad_U[var][face] = face-averaged gradient component from left/right cells
// T: face-averaged temperature, grad_T: face-averaged temperature gradient
Vec4 viscous_flux_term(const Vec4& U_face,   // face-averaged conservative state
                        const Vec4& grad_rho, const Vec4& grad_rhou,
                        const Vec4& grad_rhov, const Vec4& grad_rhoE,
                        double T_face, const Vec2& grad_T,
                        const Vec2& normal,
                        double mu, double Pr, double gamma, double R);

} // namespace solver
