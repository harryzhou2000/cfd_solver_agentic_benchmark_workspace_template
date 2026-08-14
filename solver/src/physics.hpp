#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "case_config.hpp"

// Conservative state U = [rho, rho*u, rho*v, rho*E]^T.
using Vec4 = std::array<double, 4>;

struct Gas {
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;

    double cp() const { return gamma * R / (gamma - 1.0); }
};

inline double pressure_of(const Vec4& U, const Gas& g) {
    double ke = 0.5 * (U[1] * U[1] + U[2] * U[2]) / U[0];
    return (g.gamma - 1.0) * (U[3] - ke);
}

inline Vec4 cons_from_prim(double rho, double u, double v, double p, const Gas& g) {
    Vec4 U;
    U[0] = rho;
    U[1] = rho * u;
    U[2] = rho * v;
    U[3] = p / (g.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
    return U;
}

inline void prim_from_cons(const Vec4& U, const Gas& g, double& rho, double& u, double& v,
                           double& p) {
    rho = U[0];
    u = U[1] / rho;
    v = U[2] / rho;
    p = pressure_of(U, g);
}

inline double sound_speed(double rho, double p, const Gas& g) {
    return std::sqrt(g.gamma * p / rho);
}

// Normal inviscid flux F·n with unit normal (nx, ny).
inline Vec4 inviscid_flux(const Vec4& U, double nx, double ny, const Gas& g) {
    double rho, u, v, p;
    prim_from_cons(U, g, rho, u, v, p);
    double un = u * nx + v * ny;
    double H = (U[3] + p) / rho;
    Vec4 F;
    F[0] = rho * un;
    F[1] = rho * u * un + p * nx;
    F[2] = rho * v * un + p * ny;
    F[3] = rho * H * un;
    return F;
}

// Rusanov (local Lax-Friedrichs) flux. Returns the flux; also outputs the
// scalar dissipation coefficient (spectral radius) for implicit use.
// When low_mach_mfloor > 0, the dissipation coefficient is replaced by the
// Turkel-preconditioned spectral radius, removing the low-Mach acoustic
// stiffness/accuracy loss (recovers the standard flux for M >= 1).
inline Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, double nx, double ny, const Gas& g,
                         double diss_scale, double& lambda_out,
                         double low_mach_mfloor = 0.0) {
    double rL, uL, vL, pL, rR, uR, vR, pR;
    prim_from_cons(UL, g, rL, uL, vL, pL);
    prim_from_cons(UR, g, rR, uR, vR, pR);
    double unL = uL * nx + vL * ny;
    double unR = uR * nx + vR * ny;
    double aL = sound_speed(rL, pL, g);
    double aR = sound_speed(rR, pR, g);
    double lambda = std::max(std::abs(unL) + aL, std::abs(unR) + aR);
    if (low_mach_mfloor > 0.0) {
        double un = std::max(std::abs(unL), std::abs(unR));
        double a = std::max(aL, aR);
        double vmag = std::max(std::sqrt(uL * uL + vL * vL), std::sqrt(uR * uR + vR * vR));
        double m_eff = std::max(vmag / a, low_mach_mfloor);
        double alpha = std::min(1.0, m_eff * m_eff);
        lambda = 0.5 * ((1.0 + alpha) * un +
                        std::sqrt((1.0 - alpha) * (1.0 - alpha) * un * un +
                                  4.0 * alpha * a * a));
    }
    lambda_out = lambda;
    Vec4 FL = inviscid_flux(UL, nx, ny, g);
    Vec4 FR = inviscid_flux(UR, nx, ny, g);
    Vec4 F;
    for (int k = 0; k < 4; k++)
        F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * diss_scale * lambda * (UR[k] - UL[k]);
    return F;
}

// HLLC flux (Toro) for the 2-D Euler equations.
inline Vec4 hllc_flux(const Vec4& UL, const Vec4& UR, double nx, double ny, const Gas& g,
                      double& lambda_out) {
    double rL, uL, vL, pL, rR, uR, vR, pR;
    prim_from_cons(UL, g, rL, uL, vL, pL);
    prim_from_cons(UR, g, rR, uR, vR, pR);
    double unL = uL * nx + vL * ny;
    double unR = uR * nx + vR * ny;
    double aL = sound_speed(rL, pL, g);
    double aR = sound_speed(rR, pR, g);
    double EL = UL[3] / rL, ER = UR[3] / rR;
    double HL = EL + pL / rL, HR = ER + pR / rR;

    // Roe-averaged wave-speed estimates
    double sqL = std::sqrt(rL), sqR = std::sqrt(rR);
    double denom = sqL + sqR;
    double un_roe = (sqL * unL + sqR * unR) / denom;
    double H_roe = (sqL * HL + sqR * HR) / denom;
    double a_roe2 = (g.gamma - 1.0) * (H_roe - 0.5 * un_roe * un_roe);
    double a_roe = std::sqrt(std::max(a_roe2, 1e-12));
    double SL = std::min(unL - aL, un_roe - a_roe);
    double SR = std::max(unR + aR, un_roe + a_roe);
    lambda_out = std::max(std::abs(SL), std::abs(SR));

    Vec4 FL = inviscid_flux(UL, nx, ny, g);
    Vec4 FR = inviscid_flux(UR, nx, ny, g);
    if (SL >= 0.0) return FL;
    if (SR <= 0.0) return FR;

    double SM = (rR * unR * (SR - unR) - rL * unL * (SL - unL) + pL - pR) /
                (rR * (SR - unR) - rL * (SL - unL));
    double pstar = pL + rL * (SL - unL) * (SM - unL);

    Vec4 F;
    if (SM >= 0.0) {
        double coef = rL * (SL - unL) / (SL - SM);
        Vec4 Ustar;
        Ustar[0] = coef;
        Ustar[1] = coef * SM;
        Ustar[2] = coef * SM;
        Ustar[3] = coef * (EL + (SM - unL) * (SM + pL / (rL * (SL - unL))));
        // tangential momentum: keep u_t
        double utL = -uL * ny + vL * nx;
        Ustar[1] = coef * (SM * nx - utL * ny);
        Ustar[2] = coef * (SM * ny + utL * nx);
        for (int k = 0; k < 4; k++) F[k] = FL[k] + SL * (Ustar[k] - UL[k]);
    } else {
        double coef = rR * (SR - unR) / (SR - SM);
        Vec4 Ustar;
        Ustar[0] = coef;
        Ustar[3] = coef * (ER + (SM - unR) * (SM + pR / (rR * (SR - unR))));
        double utR = -uR * ny + vR * nx;
        Ustar[1] = coef * (SM * nx - utR * ny);
        Ustar[2] = coef * (SM * ny + utR * nx);
        for (int k = 0; k < 4; k++) F[k] = FR[k] + SR * (Ustar[k] - UR[k]);
    }
    return F;
}

// Reflect normal velocity for inviscid slip walls (mirrored ghost state).
inline Vec4 slip_wall_mirror(const Vec4& Uin, double nx, double ny) {
    Vec4 Ug = Uin;
    double un = (Uin[1] * nx + Uin[2] * ny);
    Ug[1] = Uin[1] - 2.0 * un * nx;
    Ug[2] = Uin[2] - 2.0 * un * ny;
    return Ug;
}
