// physics.hpp — Gas model, state conversions, flux functions for 2D compressible NS
#pragma once
#include <cmath>
#include <array>
#include <algorithm>

namespace cfd2d {

constexpr int NEQ = 4;  // rho, rhou, rhov, rhoE

// Conservative state indices
enum { RHO = 0, RHOU = 1, RHOV = 2, RHOE = 3 };

struct GasModel {
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;
    double gammaMinus1() const { return gamma - 1.0; }
    double cp() const { return gamma * R / (gamma - 1.0); }
    double cv() const { return R / (gamma - 1.0); }
};

// Extract primitive variables from conservative state
// Returns [rho, u, v, p, T, a, mach, E]
struct Primitive {
    double rho, u, v, p, T, a, mach, E;
};

inline Primitive toPrimitive(const double U[NEQ], const GasModel& gas) {
    Primitive p;
    p.rho = U[RHO];
    double rhou = U[RHOU], rhov = U[RHOV], rhoE = U[RHOE];
    p.u = rhou / p.rho;
    p.v = rhov / p.rho;
    p.E = rhoE / p.rho;
    double kinetic = 0.5 * (p.u * p.u + p.v * p.v);
    double e = p.E - kinetic;
    p.p = gas.gammaMinus1() * p.rho * e;
    p.p = std::max(p.p, 1e-14);
    p.T = p.p / (p.rho * gas.R);
    p.a = std::sqrt(gas.gamma * p.p / p.rho);
    p.mach = std::sqrt(p.u * p.u + p.v * p.v) / std::max(p.a, 1e-14);
    return p;
}

inline double pressureFromConservative(const double U[NEQ], const GasModel& gas) {
    double rho = U[RHO];
    double kinetic = 0.5 * (U[RHOU]*U[RHOU] + U[RHOV]*U[RHOV]) / rho;
    double e = U[RHOE] / rho - kinetic;
    return gas.gammaMinus1() * rho * e;
}

inline double internalEnergyFromP(double rho, double p, const GasModel& gas) {
    return p / (gas.gammaMinus1() * rho);
}

// Build conservative state from primitive
inline void fromPrimitive(double rho, double u, double v, double p, const GasModel& gas, double U[NEQ]) {
    U[RHO] = rho;
    U[RHOU] = rho * u;
    U[RHOV] = rho * v;
    double e = p / (gas.gammaMinus1() * rho);
    double E = e + 0.5 * (u*u + v*v);
    U[RHOE] = rho * E;
}

// Inviscid flux in x-direction: F(U)
inline void inviscidFluxX(const double U[NEQ], const GasModel& gas, double F[NEQ]) {
    Primitive p = toPrimitive(U, gas);
    F[0] = U[RHOU];
    F[1] = U[RHOU] * p.u + p.p;
    F[2] = U[RHOU] * p.v;
    F[3] = (U[RHOE] + p.p) * p.u;
}

// Inviscid flux in y-direction: G(U)
inline void inviscidFluxY(const double U[NEQ], const GasModel& gas, double G[NEQ]) {
    Primitive p = toPrimitive(U, gas);
    G[0] = U[RHOV];
    G[1] = U[RHOV] * p.u;
    G[2] = U[RHOV] * p.v + p.p;
    G[3] = (U[RHOE] + p.p) * p.v;
}

// Inviscid flux through a face with unit normal (nx, ny) and area |S|
// F_n = F*nx + G*ny (flux normal to face)
inline void inviscidFluxNormal(const double U[NEQ], double nx, double ny, const GasModel& gas, double Fn[NEQ]) {
    Primitive p = toPrimitive(U, gas);
    double un = p.u * nx + p.v * ny;
    double rho = p.rho;
    Fn[0] = rho * un;
    Fn[1] = rho * un * p.u + p.p * nx;
    Fn[2] = rho * un * p.v + p.p * ny;
    Fn[3] = (U[RHOE] + p.p) * un;
}

// Roe approximate Riemann solver with Harten-Yee entropy fix
// UL, UR: left/right conservative states
// nx, ny: face unit normal (pointing from left to right)
// Returns: flux through face (per unit area)
inline void roeFlux(const double UL[NEQ], const double UR[NEQ],
                    double nx, double ny, const GasModel& gas, double flux[NEQ]) {
    Primitive pL = toPrimitive(UL, gas);
    Primitive pR = toPrimitive(UR, gas);

    // Roe averages
    double sqrtRhoL = std::sqrt(std::max(pL.rho, 1e-14));
    double sqrtRhoR = std::sqrt(std::max(pR.rho, 1e-14));
    double invSum = 1.0 / (sqrtRhoL + sqrtRhoR);

    double rhoRoe = sqrtRhoL * sqrtRhoR;
    double uRoe = (sqrtRhoL * pL.u + sqrtRhoR * pR.u) * invSum;
    double vRoe = (sqrtRhoL * pL.v + sqrtRhoR * pR.v) * invSum;
    double hL = (pL.p / gas.gammaMinus1() + 0.5 * pL.rho * (pL.u*pL.u + pL.v*pL.v) + pL.p) / std::max(pL.rho, 1e-14);
    double hR = (pR.p / gas.gammaMinus1() + 0.5 * pR.rho * (pR.u*pR.u + pR.v*pR.v) + pR.p) / std::max(pR.rho, 1e-14);
    double hRoe = (sqrtRhoL * hL + sqrtRhoR * hR) * invSum;
    double aRoe = std::sqrt(std::max(gas.gammaMinus1() * (hRoe - 0.5 * (uRoe*uRoe + vRoe*vRoe)), 1e-14));

    double unRoe = uRoe * nx + vRoe * ny;
    double unL = pL.u * nx + pL.v * ny;
    double unR = pR.u * nx + pR.v * ny;

    // Flux from left and right states (normal flux)
    double FL[NEQ], FR[NEQ];
    inviscidFluxNormal(UL, nx, ny, gas, FL);
    inviscidFluxNormal(UR, nx, ny, gas, FR);

    // Eigenvalues
    double lam1 = unRoe;          // u, v eigenvalue (doubly degenerate)
    double lam2 = unRoe + aRoe;   // |
    double lam3 = unRoe - aRoe;   // |

    // Harten-Yee entropy fix
    double delta = 0.1 * aRoe;
    auto entropyFix = [&](double lam) {
        double al = std::abs(lam);
        if (al < delta) return 0.5 * (lam*lam / delta + delta);
        return al;
    };

    double absLam1 = entropyFix(lam1);
    double absLam2 = entropyFix(lam2);
    double absLam3 = entropyFix(lam3);

    // Wave strengths
    double drho = pR.rho - pL.rho;
    double du = pR.u - pL.u;
    double dv = pR.v - pL.v;
    double dp = pR.p - pL.p;

    // Roe decomposition: dU = sum of waves
    double drho_star = drho - dp / (aRoe * aRoe);
    double r1 = drho_star;                           // acoustic+entropy combined
    double r2 = (dp + rhoRoe * aRoe * (du*nx + dv*ny)) / (2.0 * aRoe * aRoe);
    double r3 = (dp - rhoRoe * aRoe * (du*nx + dv*ny)) / (2.0 * aRoe * aRoe);

    // Dissipation: |A| * dU = sum |lambda_k| * r_k * e_k
    double diss[NEQ] = {0, 0, 0, 0};
    // Entropy/shear wave (lambda = un)
    diss[0] += absLam1 * r1;
    diss[1] += absLam1 * (r1 * uRoe);
    diss[2] += absLam1 * (r1 * vRoe);
    diss[3] += absLam1 * (r1 * 0.5 * (uRoe*uRoe + vRoe*vRoe));

    // Right acoustic wave (lambda = un + a)
    diss[0] += absLam2 * r2;
    diss[1] += absLam2 * r2 * (uRoe + aRoe * nx);
    diss[2] += absLam2 * r2 * (vRoe + aRoe * ny);
    diss[3] += absLam2 * r2 * (hRoe + aRoe * unRoe);

    // Left acoustic wave (lambda = un - a)
    diss[0] += absLam3 * r3;
    diss[1] += absLam3 * r3 * (uRoe - aRoe * nx);
    diss[2] += absLam3 * r3 * (vRoe - aRoe * ny);
    diss[3] += absLam3 * r3 * (hRoe - aRoe * unRoe);

    // Roe flux: F = 0.5*(FL + FR) - 0.5*diss
    for (int i = 0; i < NEQ; i++)
        flux[i] = 0.5 * (FL[i] + FR[i]) - 0.5 * diss[i];
}

// Rusanov / local Lax-Friedrichs flux (fallback / for viscous cases)
inline void rusanovFlux(const double UL[NEQ], const double UR[NEQ],
                        double nx, double ny, double dissScale,
                        const GasModel& gas, double flux[NEQ]) {
    Primitive pL = toPrimitive(UL, gas);
    Primitive pR = toPrimitive(UR, gas);

    double FL[NEQ], FR[NEQ];
    inviscidFluxNormal(UL, nx, ny, gas, FL);
    inviscidFluxNormal(UR, nx, ny, gas, FR);

    double aL = pL.a, aR = pR.a;
    double unL = pL.u * nx + pL.v * ny;
    double unR = pR.u * nx + pR.v * ny;
    double maxSpeed = std::max(std::abs(unL) + aL, std::abs(unR) + aR);

    for (int i = 0; i < NEQ; i++)
        flux[i] = 0.5 * (FL[i] + FR[i]) - 0.5 * dissScale * maxSpeed * (UR[i] - UL[i]);
}

// Spectral radius of the inviscid flux Jacobian for a face
inline double inviscidSpectralRadius(const double UL[NEQ], const double UR[NEQ],
                                     double nx, double ny, const GasModel& gas) {
    Primitive pL = toPrimitive(UL, gas);
    Primitive pR = toPrimitive(UR, gas);
    double unL = std::abs(pL.u * nx + pL.v * ny) + pL.a;
    double unR = std::abs(pR.u * nx + pR.v * ny) + pR.a;
    return std::max(unL, unR);
}

// Viscous flux through a face with normal (nx, ny)
// gradU: gradients of primitive variables [rho, u, v, T] x [dx, dy]
// mu: dynamic viscosity, k: thermal conductivity
struct ViscousGradients {
    double drho_dx, drho_dy;
    double du_dx, du_dy;
    double dv_dx, dv_dy;
    double dT_dx, dT_dy;
};

inline void viscousFluxNormal(double rho, double u, double v, double T,
                              const ViscousGradients& grad,
                              double mu, double k,
                              const GasModel& gas,
                              double nx, double ny, double Fv[NEQ]) {
    double div = grad.du_dx + grad.dv_dy;
    double txx = mu * (2.0 * grad.du_dx - 2.0/3.0 * div);
    double tyy = mu * (2.0 * grad.dv_dy - 2.0/3.0 * div);
    double txy = mu * (grad.du_dy + grad.dv_dx);

    // Heat flux: q = -k * grad(T)
    double qx = -k * grad.dT_dx;
    double qy = -k * grad.dT_dy;

    // Viscous flux in normal direction (Fv*nx + Gv*ny)
    Fv[0] = 0.0;
    Fv[1] = txx * nx + txy * ny;
    Fv[2] = txy * nx + tyy * ny;
    Fv[3] = (txx * u + txy * v) * nx + (txy * u + tyy * v) * ny - qx * nx - qy * ny;
}

// Viscous spectral radius estimate (for implicit diagonal)
inline double viscousSpectralRadius(double mu, double rho, double area, double dist, const GasModel& gas) {
    // Approximate: 4*mu/(3*rho) * |S|^2 / d
    double d2 = std::max(dist * dist, 1e-14);
    return 4.0 * mu / (3.0 * std::max(rho, 1e-14)) * area * area / d2;
}

} // namespace cfd2d
