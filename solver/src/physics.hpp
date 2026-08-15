#pragma once
// Compressible Navier-Stokes physics: gas model, conservative/primitive
// conversion, Roe inviscid flux with Harten entropy fix, Rusanov flux, viscous
// flux, and boundary-condition ghost states.  All functions are header-only
// inline for performance and clarity.
#include <algorithm>
#include <cmath>
#include "types.hpp"

namespace cfd {

struct Gas {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double cp() const { return gamma * R / (gamma - 1.0); }
  double cv() const { return R / (gamma - 1.0); }
  double gm1() const { return gamma - 1.0; }
};

inline Prim toPrim(const Cons& U, const Gas& g) {
  Prim W;
  double rho = U.r();
  W.r() = rho;
  double u = U.ru() / rho;
  double v = U.rv() / rho;
  double E = U.rE() / rho;
  double p = g.gm1() * (U.rE() - 0.5 * rho * (u * u + v * v));
  W.u() = u; W.v() = v; W.p() = p;
  return W;
}

inline Cons toCons(const Prim& W, const Gas& g) {
  Cons U;
  double rho = W.r(), u = W.u(), v = W.v(), p = W.p();
  U.r() = rho;
  U.ru() = rho * u;
  U.rv() = rho * v;
  double E = p / g.gm1() + 0.5 * rho * (u * u + v * v);
  U.rE() = rho * E;
  return U;
}

inline double soundSpeed(double rho, double p, const Gas& g) {
  return std::sqrt(g.gamma * p / std::max(rho, 1e-30));
}

inline double temperature(double rho, double p, const Gas& g) {
  return p / (std::max(rho, 1e-30) * g.R);
}

// Inviscid flux F(U) . n  (4-vector) for a primitive state and unit normal.
inline void inviscidFluxDotN(const Prim& W, double nx, double ny, const Gas& g,
                             double F[4]) {
  double rho = W.r(), u = W.u(), v = W.v(), p = W.p();
  double un = u * nx + v * ny;
  double rhoE = p / g.gm1() + 0.5 * rho * (u * u + v * v);
  F[0] = rho * un;
  F[1] = rho * u * un + p * nx;
  F[2] = rho * v * un + p * ny;
  F[3] = (rhoE + p) * un;
}

// Roe approximate Riemann solver with Harten entropy fix.
// Returns the numerical inviscid flux F.n given left/right conservative
// states and the unit outward normal of the left cell.
inline double efixMag(double lam, double delta) {
  double al = std::fabs(lam);
  if (al < delta) return 0.5 * (lam * lam / delta + delta);
  return al;
}
inline void roeFlux(const Cons& UL, const Cons& UR, double nx, double ny,
                    const Gas& g, double entropyCoeff, double F[4]) {
  Prim WL = toPrim(UL, g);
  Prim WR = toPrim(UR, g);
  double FL[4], FR[4];
  inviscidFluxDotN(WL, nx, ny, g, FL);
  inviscidFluxDotN(WR, nx, ny, g, FR);

  double rhoL = std::max(WL.r(), 1e-12), rhoR = std::max(WR.r(), 1e-12);
  double uL = WL.u(), vL = WL.v(), pL = std::max(WL.p(), 1e-12);
  double uR = WR.u(), vR = WR.v(), pR = std::max(WR.p(), 1e-12);
  double HL = (UL.rE() + pL) / rhoL;
  double HR = (UR.rE() + pR) / rhoR;
  double sqL = std::sqrt(rhoL), sqR = std::sqrt(rhoR);
  double inv = 1.0 / (sqL + sqR);
  double rho = sqL * sqR;
  double u = (sqL * uL + sqR * uR) * inv;
  double v = (sqL * vL + sqR * vR) * inv;
  double H = (sqL * HL + sqR * HR) * inv;
  double a = std::sqrt(std::max(g.gm1() * (H - 0.5 * (u * u + v * v)), 1e-12));
  double un = u * nx + v * ny;

  // Wave strengths.
  double drho = rhoR - rhoL;
  double du = uR - uL;
  double dv = vR - vL;
  double dp = pR - pL;
  double dun = du * nx + dv * ny;
  double dvt = -du * ny + dv * nx;
  double a2 = a * a;
  double a1 = (dp - rho * a * dun) / (2.0 * a2);   // un - a
  double a2w = drho - dp / a2;                       // un (entropy)
  double a3w = rho * dvt;                            // un (shear)
  double a4 = (dp + rho * a * dun) / (2.0 * a2);    // un + a

  // Entropy-fixed eigenvalue magnitudes.
  double delta = entropyCoeff * a;
  double l1 = efixMag(un - a, delta);
  double l2 = std::fabs(un);  // shear/entropy: linear, no fix needed (or small)
  double l3 = l2;
  double l4 = efixMag(un + a, delta);

  // Dissipation = sum |lambda_k| alpha_k r_k.
  double d0 = l1 * a1 * 1.0 + l2 * a2w * 1.0 + l4 * a4 * 1.0;
  double d1 = l1 * a1 * (u - a * nx) + l2 * a2w * u + l3 * a3w * (-ny) + l4 * a4 * (u + a * nx);
  double d2 = l1 * a1 * (v - a * ny) + l2 * a2w * v + l3 * a3w * (nx) + l4 * a4 * (v + a * ny);
  double d3 = l1 * a1 * (H - a * un) + l2 * a2w * 0.5 * (u * u + v * v)
            + l3 * a3w * (-u * ny + v * nx) + l4 * a4 * (H + a * un);

  F[0] = 0.5 * (FL[0] + FR[0]) - 0.5 * d0;
  F[1] = 0.5 * (FL[1] + FR[1]) - 0.5 * d1;
  F[2] = 0.5 * (FL[2] + FR[2]) - 0.5 * d2;
  F[3] = 0.5 * (FL[3] + FR[3]) - 0.5 * d3;
}

// Rusanov / local Lax-Friedrichs flux (more dissipative, very robust).
inline void rusanovFlux(const Cons& UL, const Cons& UR, double nx, double ny,
                        const Gas& g, double dissScale, double F[4]) {
  Prim WL = toPrim(UL, g);
  Prim WR = toPrim(UR, g);
  double FL[4], FR[4];
  inviscidFluxDotN(WL, nx, ny, g, FL);
  inviscidFluxDotN(WR, nx, ny, g, FR);
  double aL = soundSpeed(WL.r(), WL.p(), g);
  double aR = soundSpeed(WR.r(), WR.p(), g);
  double smax = dissScale * std::max(std::fabs(WL.u() * nx + WL.v() * ny) + aL,
                                     std::fabs(WR.u() * nx + WR.v() * ny) + aR);
  for (int i = 0; i < 4; ++i)
    F[i] = 0.5 * (FL[i] + FR[i]) - 0.5 * smax * (UR.q[i] - UL.q[i]);
}

// Viscous flux Fv . n (4-vector) given face primitive state and primitive
// gradients (du/dx, du/dy, dv/dx, dv/dy, dT/dx, dT/dy).
inline void viscousFluxDotN(const Prim& W, double ux, double uy, double vx,
                            double vy, double Tx, double Ty, double mu, double k,
                            double nx, double ny, double Fv[4]) {
  double div = ux + vy;
  double txx = 2.0 * mu * ux - 2.0 / 3.0 * mu * div;
  double tyy = 2.0 * mu * vy - 2.0 / 3.0 * mu * div;
  double txy = mu * (uy + vx);
  double qx = -k * Tx;
  double qy = -k * Ty;
  double u = W.u(), v = W.v();
  Fv[0] = 0.0;
  Fv[1] = txx * nx + txy * ny;
  Fv[2] = txy * nx + tyy * ny;
  Fv[3] = (u * txx + v * txy - qx) * nx + (u * txy + v * tyy - qy) * ny;
}

// Build a farfield ghost state from the interior state and freestream.
// Inflow (un<0): ghost = freestream.  Outflow: ghost = interior (zero gradient).
inline Cons farfieldGhost(const Cons& U, const Prim& fs, double nx, double ny,
                          const Gas& g) {
  Prim Wi = toPrim(U, g);
  double un = Wi.u() * nx + Wi.v() * ny;
  if (un < 0.0) {
    return toCons(fs, g);
  } else {
    return U;  // zero-gradient outflow
  }
}

}  // namespace cfd
