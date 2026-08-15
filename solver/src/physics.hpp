#pragma once
#include "types.hpp"
#include <cmath>

namespace cfd {

struct Gas {
    Real gamma = 1.4, R = 1.0, prandtl = 0.72;
    Real gm1() const { return gamma - 1.0; }
    Real cp() const { return gamma * R / gm1(); }
};

struct CasePhysics {
    Gas gas;
    bool viscous = false;
    Real mu = 0.0;          // dynamic viscosity
    Real kcond = 0.0;       // thermal conductivity
    Real rusanovScale = 1.0;
    int fluxType = 1;       // 0=Rusanov, 1=Roe
    Real entropyCoeff = 0.1;
    // freestream primitive
    Prim fs;                // [rho, u, v, p]
    Real machInf = 0.0;
};

inline Prim cons2prim(const Cons& U, const Gas& g) {
    Prim p;
    p[0] = U[0];
    Real rho = std::max(p[0], 1e-12);
    p[1] = U[1] / rho;
    p[2] = U[2] / rho;
    Real E = U[3] / rho;
    Real e = E - 0.5 * (p[1] * p[1] + p[2] * p[2]);
    p[3] = g.gm1() * rho * e;
    if (!(p[3] > 0.0)) p[3] = 1e-12;
    return p;
}
inline Cons prim2cons(const Prim& p, const Gas& g) {
    Cons U;
    U[0] = p[0];
    U[1] = p[0] * p[1];
    U[2] = p[0] * p[2];
    Real e = p[3] / (g.gm1() * p[0]);
    U[3] = p[0] * (e + 0.5 * (p[1] * p[1] + p[2] * p[2]));
    return U;
}
inline Real soundSpeed(const Prim& p, const Gas& g) {
    return std::sqrt(std::max(g.gamma * p[3] / std::max(p[0], 1e-12), 1e-12));
}
inline Real temperature(const Prim& p, const Gas& g) { return p[3] / (p[0] * g.R); }

// Inviscid flux vector in direction n=(nx,ny) for primitive state
inline Cons inviscidFlux(const Prim& p, Real nx, Real ny, const Gas& g) {
    Real rho = p[0], u = p[1], v = p[2], P = p[3];
    Real un = u * nx + v * ny;
    Real rhoE = P / g.gm1() + 0.5 * rho * (u * u + v * v);
    Cons F;
    F[0] = rho * un;
    F[1] = rho * u * un + P * nx;
    F[2] = rho * v * un + P * ny;
    F[3] = (rhoE + P) * un;
    return F;
}

// Rusanov (local Lax-Friedrichs) flux
// Uses reconstructed pL,pR for central flux but cell-center pLc,pRc for dissipation.
inline Cons rusanovFlux(const Prim& pL, const Prim& pR, Real nx, Real ny,
                        const Gas& g, Real scale) {
    Cons FL = inviscidFlux(pL, nx, ny, g);
    Cons FR = inviscidFlux(pR, nx, ny, g);
    Real aL = soundSpeed(pL, g), aR = soundSpeed(pR, g);
    Real unL = pL[1] * nx + pL[2] * ny;
    Real unR = pR[1] * nx + pR[2] * ny;
    Real smax = std::max(std::fabs(unL) + aL, std::fabs(unR) + aR) * scale;
    Cons UL = prim2cons(pL, g), UR = prim2cons(pR, g);
    Cons F;
    for (int k = 0; k < NEQ; ++k)
        F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * smax * (UR[k] - UL[k]);
    return F;
}
// Rusanov flux with separate cell-center states for dissipation
inline Cons rusanovFlux2(const Prim& pL, const Prim& pR,
                         const Prim& pLc, const Prim& pRc,
                         Real nx, Real ny, const Gas& g, Real scale) {
    Cons FL = inviscidFlux(pL, nx, ny, g);
    Cons FR = inviscidFlux(pR, nx, ny, g);
    Real aL = soundSpeed(pLc, g), aR = soundSpeed(pRc, g);
    Real unL = pLc[1] * nx + pLc[2] * ny;
    Real unR = pRc[1] * nx + pRc[2] * ny;
    Real smax = std::max(std::fabs(unL) + aL, std::fabs(unR) + aR) * scale;
    Cons ULc = prim2cons(pLc, g), URc = prim2cons(pRc, g);
    Cons F;
    for (int k = 0; k < NEQ; ++k)
        F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * smax * (URc[k] - ULc[k]);
    return F;
}

// Roe flux with Harten entropy fix
inline Cons roeFlux(const Prim& pL, const Prim& pR, Real nx, Real ny,
                    const Gas& g, Real entropyCoeff) {
    Cons FL = inviscidFlux(pL, nx, ny, g);
    Cons FR = inviscidFlux(pR, nx, ny, g);
    Real srL = std::sqrt(std::max(pL[0], 1e-12));
    Real srR = std::sqrt(std::max(pR[0], 1e-12));
    Real sr = srL + srR;
    Real rho = srL * srR;
    Real u = (srL * pL[1] + srR * pR[1]) / sr;
    Real v = (srL * pL[2] + srR * pR[2]) / sr;
    Real HL = (prim2cons(pL, g)[3] + pL[3]) / std::max(pL[0], 1e-12);
    Real HR = (prim2cons(pR, g)[3] + pR[3]) / std::max(pR[0], 1e-12);
    Real H = (srL * HL + srR * HR) / sr;
    Real a2 = g.gm1() * (H - 0.5 * (u * u + v * v));
    Real a = std::sqrt(std::max(a2, 1e-12));
    Real unt = u * nx + v * ny;
    Real tx = -ny, ty = nx;
    // primitive jumps
    Real drho = pR[0] - pL[0];
    Real du = pR[1] - pL[1];
    Real dv = pR[2] - pL[2];
    Real dp = pR[3] - pL[3];
    Real dun = du * nx + dv * ny;
    Real dvt = du * tx + dv * ty;
    Real l1 = unt - a, l2 = unt, l3 = unt, l4 = unt + a;
    Real a1 = (dp - rho * a * dun) / (2.0 * a2);
    Real a2w = drho - dp / a2;
    Real a3 = rho * dvt;
    Real a4 = (dp + rho * a * dun) / (2.0 * a2);
    // Harten entropy fix
    Real delta = entropyCoeff * a;
    auto efix = [delta](Real lam) -> Real {
        Real al = std::fabs(lam);
        if (al < delta) return (lam * lam + delta * delta) / (2.0 * delta);
        return al;
    };
    Real e1 = efix(l1), e2 = efix(l2), e3 = efix(l3), e4 = efix(l4);
    Real V2 = u * u + v * v;
    // dissipation = sum |lambda_k| alpha_k r_k
    Cons D{};
    // r1 = [1, u-a*nx, v-a*ny, H-a*unt]
    { Real w = e1 * a1; D[0] += w * 1.0; D[1] += w * (u - a * nx); D[2] += w * (v - a * ny); D[3] += w * (H - a * unt); }
    // r2 = [1, u, v, V2/2]
    { Real w = e2 * a2w; D[0] += w * 1.0; D[1] += w * u; D[2] += w * v; D[3] += w * (0.5 * V2); }
    // r3 = [0, tx, ty, u*tx+v*ty]
    { Real w = e3 * a3; D[0] += 0.0; D[1] += w * tx; D[2] += w * ty; D[3] += w * (u * tx + v * ty); }
    // r4 = [1, u+a*nx, v+a*ny, H+a*unt]
    { Real w = e4 * a4; D[0] += w * 1.0; D[1] += w * (u + a * nx); D[2] += w * (v + a * ny); D[3] += w * (H + a * unt); }
    Cons F;
    for (int k = 0; k < NEQ; ++k) F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * D[k];
    return F;
}

inline Cons numFlux(const Prim& pL, const Prim& pR, Real nx, Real ny,
                    const CasePhysics& ph) {
    // For second-order, pL/pR are reconstructed; use first-order fallback
    if (ph.fluxType == 1)
        return roeFlux(pL, pR, nx, ny, ph.gas, ph.entropyCoeff);
    return rusanovFlux(pL, pR, nx, ny, ph.gas, ph.rusanovScale);
}
// Flux with separate cell-center states for dissipation (second-order safe)
inline Cons numFlux2(const Prim& pL, const Prim& pR,
                     const Prim& pLc, const Prim& pRc,
                     Real nx, Real ny, const CasePhysics& ph) {
    return rusanovFlux2(pL, pR, pLc, pRc, nx, ny, ph.gas, ph.rusanovScale);
}

// Spectral radius of the inviscid flux Jacobian (for LU-SGS diagonal)
inline Real inviscidSpectralRadius(const Prim& pL, const Prim& pR, Real nx, Real ny,
                                   const Gas& g, Real scale) {
    Real aL = soundSpeed(pL, g), aR = soundSpeed(pR, g);
    Real unL = pL[1] * nx + pL[2] * ny;
    Real unR = pR[1] * nx + pR[2] * ny;
    return std::max(std::fabs(unL) + aL, std::fabs(unR) + aR) * scale;
}

// Boundary ghost state (primitive) for a face with normal n pointing out of cell L.
inline Prim boundaryGhost(const Prim& pL, Real nx, Real ny, BCType bc,
                          const CasePhysics& ph) {
    Prim g = pL;
    switch (bc) {
        case BCType::SlipWall: {
            // reflect normal velocity, keep tangential; keep rho,p
            Real un = pL[1] * nx + pL[2] * ny;
            g[1] = pL[1] - 2.0 * un * nx;
            g[2] = pL[2] - 2.0 * un * ny;
            g[0] = pL[0]; g[3] = pL[3];
            break;
        }
        case BCType::NoSlipWall: {
            // wall velocity = 0: mirror velocity; adiabatic => T_g=T_L, p_g=p_L, rho_g=rho_L
            g[0] = pL[0];
            g[1] = -pL[1];
            g[2] = -pL[2];
            g[3] = pL[3];
            break;
        }
        case BCType::Farfield:
        default:
            g = ph.fs;
            break;
    }
    return g;
}

// Viscous flux in direction n given primitive state p and gradients (ux,uy,vx,vy,Tx,Ty)
inline Cons viscousFlux(const Prim& p, Real ux, Real uy, Real vx, Real vy,
                        Real Tx, Real Ty, Real nx, Real ny, Real mu, Real kcond,
                        const Gas& g) {
    Real div = ux + vy;
    Real txx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
    Real tyy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
    Real txy = mu * (uy + vx);
    Real qx = -kcond * Tx;
    Real qy = -kcond * Ty;
    Real u = p[1], v = p[2];
    Cons F;
    F[0] = 0.0;
    F[1] = txx * nx + txy * ny;
    F[2] = txy * nx + tyy * ny;
    F[3] = (u * txx + v * txy) * nx + (u * txy + v * tyy) * ny - (qx * nx + qy * ny);
    return F;
}

// Temperature gradient from density & pressure gradients: T = p/(rho R)
inline Vec2 gradT(const Vec2& grho, const Vec2& gp, const Prim& p, const Gas& g) {
    Real rho = std::max(p[0], 1e-12);
    Real inv = 1.0 / (rho * rho * g.R);
    return { (gp.x * rho - p[3] * grho.x) * inv, (gp.y * rho - p[3] * grho.y) * inv };
}

} // namespace cfd
