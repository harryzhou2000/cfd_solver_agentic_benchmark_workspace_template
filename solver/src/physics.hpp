#pragma once

#include "common.hpp"
#include <algorithm>
#include <cmath>

namespace cfd {

// ---------------------------------------------------------------------------
// Calorically-perfect-gas primitives and flux functions (nondimensional).
// Conservative state U = [rho, rho u, rho v, rho E].
// ---------------------------------------------------------------------------
struct Prim {
    double rho = 1.0;
    double u = 0.0;
    double v = 0.0;
    double p = 1.0;
    double T = 1.0;
    double a = 1.0;
    double H = 1.0;  // total enthalpy
    double e = 0.0;  // internal energy
};

struct GasModel {
    double gamma;
    double R;
    double prandtl;
    double cp;  // gamma R / (gamma-1)

    GasModel(double g = 1.4, double r = 1.0, double pr = 0.72)
        : gamma(g), R(r), prandtl(pr), cp(g * r / (g - 1.0)) {}

    double speed_of_sound(double rho, double p) const {
        return std::sqrt(gamma * p / rho);
    }
    double pressure(double rho, double e) const {
        return (gamma - 1.0) * rho * e;
    }
    double internal_energy(double rho, double p) const {
        return p / ((gamma - 1.0) * rho);
    }
    double temperature(double rho, double p) const { return p / (rho * R); }
};

inline Prim to_prim(const Vec4& U, const GasModel& gas) {
    Prim w;
    w.rho = U[0];
    w.u = U[1] / U[0];
    w.v = U[2] / U[0];
    double kin = 0.5 * (w.u * w.u + w.v * w.v);
    w.e = U[3] / U[0] - kin;
    w.p = gas.pressure(w.rho, w.e);
    w.T = gas.temperature(w.rho, w.p);
    w.a = gas.speed_of_sound(w.rho, w.p);
    w.H = (U[3] + w.p) / U[0];
    return w;
}

inline Vec4 to_conservative(double rho, double u, double v, double p,
                            const GasModel& gas) {
    double e = gas.internal_energy(rho, p);
    double E = e + 0.5 * (u * u + v * v);
    return {rho, rho * u, rho * v, rho * E};
}

// Inviscid flux through face with unit normal (nx, ny).
inline Vec4 inviscid_flux(const Prim& w, double nx, double ny) {
    double vn = w.u * nx + w.v * ny;
    return {w.rho * vn,
            w.rho * vn * w.u + w.p * nx,
            w.rho * vn * w.v + w.p * ny,
            w.rho * vn * w.H};
}

// Rusanov (local Lax-Friedrichs) numerical flux.
inline Vec4 rusanov_flux(const Prim& wL, const Prim& wR, double nx, double ny,
                         double dissipation_scale) {
    Vec4 FL = inviscid_flux(wL, nx, ny);
    Vec4 FR = inviscid_flux(wR, nx, ny);
    double vnL = wL.u * nx + wL.v * ny;
    double vnR = wR.u * nx + wR.v * ny;
    double lambda = std::max(std::abs(vnL) + wL.a, std::abs(vnR) + wR.a);
    Vec4 ULa = {wL.rho, wL.rho * wL.u, wL.rho * wL.v,
                wL.rho * (wL.e + 0.5 * (wL.u * wL.u + wL.v * wL.v))};
    Vec4 URa = {wR.rho, wR.rho * wR.u, wR.rho * wR.v,
                wR.rho * (wR.e + 0.5 * (wR.u * wR.u + wR.v * wR.v))};
    double s = dissipation_scale * 0.5 * lambda;
    return {0.5 * (FL[0] + FR[0]) - s * (URa[0] - ULa[0]),
            0.5 * (FL[1] + FR[1]) - s * (URa[1] - ULa[1]),
            0.5 * (FL[2] + FR[2]) - s * (URa[2] - ULa[2]),
            0.5 * (FL[3] + FR[3]) - s * (URa[3] - ULa[3])};
}

// Viscous flux through a face with unit normal n, given primitive gradients
// at the face (du/dx etc.), viscosity, and conductivity.
// Returns the *diffusive* flux vector F_v = [0, tau_xx nx + tau_xy ny,
// tau_xy nx + tau_yy ny, (u tau_xx + v tau_xy - qx) nx + (u tau_xy + v tau_yy - qy) ny].
inline Vec4 viscous_flux(double dux, double duy, double dvx, double dvy,
                         double dTx, double dTy, double u, double v,
                         double mu, double k, double nx, double ny) {
    double div = dux + dvy;
    double txx = 2.0 * mu * dux - (2.0 / 3.0) * mu * div;
    double tyy = 2.0 * mu * dvy - (2.0 / 3.0) * mu * div;
    double txy = mu * (duy + dvx);
    double qx = -k * dTx;
    double qy = -k * dTy;
    double fx = txx * nx + txy * ny;
    double fy = txy * nx + tyy * ny;
    double fe = (u * txx + v * txy - qx) * nx + (u * txy + v * tyy - qy) * ny;
    return {0.0, fx, fy, fe};
}

inline double viscous_spectral_radius(double mu, double k, double rho, double cp,
                                      double area, double volume) {
    // Estimate: 2*max(mu, k/cp)/rho * A^2 / V  (laminar diffusion)
    double nu = std::max(mu, k / cp) / std::max(rho, 1e-12);
    return 2.0 * nu * area * area / std::max(volume, 1e-30);
}

}  // namespace cfd
