#pragma once
#include "types.hpp"

namespace cfd {

// Primitive variable set extracted from the conservative state.
struct Prims {
    real_t rho, u, v, p, T, a, e;
};

inline Prims prims_from_conservative(const StateVec& U, real_t gamma, real_t R) {
    Prims q;
    q.rho = U[0];
    q.u = U[1] / U[0];
    q.v = U[2] / U[0];
    real_t ke = 0.5 * (q.u * q.u + q.v * q.v);
    q.p = (gamma - 1.0) * (U[3] - U[0] * ke);
    if (q.p < 0) q.p = 0; // clamped for robustness; positivity fallback elsewhere
    q.T = q.p / (q.rho * R);
    q.a = std::sqrt(gamma * q.p / q.rho);
    q.e = q.p / ((gamma - 1.0) * q.rho);
    return q;
}

inline StateVec conservative_from_prims(const Prims& q, real_t gamma) {
    StateVec U;
    real_t ke = 0.5 * (q.u * q.u + q.v * q.v);
    U << q.rho, q.rho * q.u, q.rho * q.v, q.rho * (q.e + ke);
    return U;
}

// Exact inviscid flux vector at a face with unit normal (nx, ny).
inline StateVec inviscid_flux(const Prims& q, real_t nx, real_t ny) {
    StateVec F;
    real_t un = q.u * nx + q.v * ny;
    F[0] = q.rho * un;
    F[1] = q.rho * q.u * un + q.p * nx;
    F[2] = q.rho * q.v * un + q.p * ny;
    real_t H = (q.e + q.p / q.rho) + 0.5 * (q.u * q.u + q.v * q.v);
    F[3] = q.rho * H * un;
    return F;
}

// Rusanov (local Lax-Friedrichs) numerical flux with dissipation scale d.
// The returned flux is already scaled by the face length S.
StateVec rusanov_flux(const StateVec& UL, const StateVec& UR,
                      real_t nx, real_t ny, real_t S,
                      real_t gamma, real_t R, real_t diss_scale);

// Pressure-only wall flux: (0, p nx, p ny, 0) * S.
StateVec pressure_only_flux(real_t p, real_t nx, real_t ny, real_t S);

// Viscous flux at an interior face using face-averaged states and gradients.
// gradient components: gu, gv, gT are vectors (d/dx, d/dy) of u, v, T.
StateVec viscous_flux_face(const Prims& qL, const Prims& qR,
                           const Vec2& gu, const Vec2& gv, const Vec2& gT,
                           real_t nx, real_t ny, real_t S,
                           real_t mu, real_t gamma, real_t R, real_t prandtl);

} // namespace cfd
