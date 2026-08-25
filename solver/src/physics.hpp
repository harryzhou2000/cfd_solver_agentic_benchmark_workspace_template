#pragma once
// Calorically-perfect-gas state conversions, inviscid numerical fluxes
// (Rusanov/LLF and Roe with Harten entropy fix), viscous stress and heat
// flux. All formulas implemented directly from the governing equations.

#include <algorithm>
#include <cmath>
#include "common.hpp"

namespace cfd {

constexpr double RHO_FLOOR = 1e-10;
constexpr double P_FLOOR = 1e-10;

// Conserved U = [rho, rho u, rho v, rho E] -> primitive [rho, u, v, p].
// Applies positivity floors; returns the (possibly floored) primitive state.
inline Vec4 cons_to_prim(const Vec4& U, const GasModel& gas) {
  Vec4 W;
  double rho = std::max(U[0], RHO_FLOOR);
  double u = U[1] / rho;
  double v = U[2] / rho;
  double ke = 0.5 * (u * u + v * v);
  double p = (gas.gamma - 1.0) * (U[3] - rho * ke);
  p = std::max(p, P_FLOOR);
  W[0] = rho; W[1] = u; W[2] = v; W[3] = p;
  return W;
}

inline Vec4 prim_to_cons(const Vec4& W, const GasModel& gas) {
  Vec4 U;
  double rho = std::max(W[0], RHO_FLOOR);
  double p = std::max(W[3], P_FLOOR);
  U[0] = rho;
  U[1] = rho * W[1];
  U[2] = rho * W[2];
  U[3] = p / (gas.gamma - 1.0) + 0.5 * rho * (W[1] * W[1] + W[2] * W[2]);
  return U;
}

inline double sound_speed(double rho, double p, const GasModel& gas) {
  return std::sqrt(gas.gamma * std::max(p, P_FLOOR) / std::max(rho, RHO_FLOOR));
}

// Physical inviscid flux in direction n=(nx,ny) from a primitive state.
// Returns F_i * nx + G_i * ny (per unit area).
inline Vec4 inviscid_flux_phys(const Vec4& W, double nx, double ny,
                               const GasModel& gas) {
  double rho = W[0], u = W[1], v = W[2], p = W[3];
  double un = u * nx + v * ny;
  double rhoE = p / (gas.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  Vec4 F;
  F[0] = rho * un;
  F[1] = rho * u * un + p * nx;
  F[2] = rho * v * un + p * ny;
  F[3] = un * (rhoE + p);
  return F;
}

// Rusanov / local Lax--Friedrichs flux. diss_scale multiplies the jump term.
inline Vec4 rusanov_flux(const Vec4& WL, const Vec4& WR, double nx, double ny,
                         const GasModel& gas, double diss_scale) {
  Vec4 UL = prim_to_cons(WL, gas), UR = prim_to_cons(WR, gas);
  Vec4 FL = inviscid_flux_phys(WL, nx, ny, gas);
  Vec4 FR = inviscid_flux_phys(WR, nx, ny, gas);
  double aL = sound_speed(WL[0], WL[3], gas);
  double aR = sound_speed(WR[0], WR[3], gas);
  double unL = WL[1] * nx + WL[2] * ny;
  double unR = WR[1] * nx + WR[2] * ny;
  double s = std::max(std::fabs(unL) + aL, std::fabs(unR) + aR);
  Vec4 F;
  for (int k = 0; k < 4; ++k)
    F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * diss_scale * s * (UR[k] - UL[k]);
  return F;
}

// Harten entropy fix for a wave speed.
inline double roe_fix(double lam, double delta) {
  double a = std::fabs(lam);
  if (a < delta) return 0.5 * (lam * lam / delta + delta);
  return a;
}

// Roe approximate Riemann flux with Harten entropy fix (2-D Euler).
inline Vec4 roe_flux(const Vec4& WL, const Vec4& WR, double nx, double ny,
                     const GasModel& gas) {
  const double gm1 = gas.gamma - 1.0;
  double rhoL = std::max(WL[0], RHO_FLOOR), rhoR = std::max(WR[0], RHO_FLOOR);
  double uL = WL[1], vL = WL[2], pL = std::max(WL[3], P_FLOOR);
  double uR = WR[1], vR = WR[2], pR = std::max(WR[3], P_FLOOR);
  double hL = gas.gamma / gm1 * pL / rhoL + 0.5 * (uL * uL + vL * vL);  // H
  double hR = gas.gamma / gm1 * pR / rhoR + 0.5 * (uR * uR + vR * vR);

  double sr = std::sqrt(rhoR / rhoL);
  double rhoT = sr * rhoL;                       // Roe-averaged density
  double uT = (uL + sr * uR) / (1.0 + sr);
  double vT = (vL + sr * vR) / (1.0 + sr);
  double hT = (hL + sr * hR) / (1.0 + sr);
  double q2 = uT * uT + vT * vT;
  double aT2 = std::max(gm1 * (hT - 0.5 * q2), 1e-12);
  double aT = std::sqrt(aT2);
  double unT = uT * nx + vT * ny;

  double dp = pR - pL;
  double dun = (uR - uL) * nx + (vR - vL) * ny;
  double dut = (uR - uL) * (-ny) + (vR - vL) * nx;
  double drho = rhoR - rhoL;

  // Wave strengths (characteristic decomposition of the jump).
  double a1 = 0.5 * (dp - rhoT * aT * dun) / aT2;   // acoustic, lambda = un - a
  double a2 = drho - dp / aT2;                      // entropy
  double a3 = rhoT * dut;                           // shear
  double a4 = 0.5 * (dp + rhoT * aT * dun) / aT2;   // acoustic, lambda = un + a

  double delta = 0.1 * aT;  // Harten entropy-fix threshold
  double l1 = roe_fix(unT - aT, delta);
  double l2 = roe_fix(unT, delta);
  double l4 = roe_fix(unT + aT, delta);

  // |A| dU = sum_k |lambda_k| * alpha_k * R_k
  double tx = -ny, ty = nx;
  double utT = uT * tx + vT * ty;
  double c1 = l1 * a1, c2 = l2 * a2, c3 = l2 * a3, c4 = l4 * a4;
  Vec4 diss;
  diss[0] = c1 + c2 + c4;
  diss[1] = c1 * (uT - aT * nx) + c2 * uT + c3 * tx + c4 * (uT + aT * nx);
  diss[2] = c1 * (vT - aT * ny) + c2 * vT + c3 * ty + c4 * (vT + aT * ny);
  diss[3] = c1 * (hT - aT * unT) + c2 * 0.5 * q2 + c3 * utT + c4 * (hT + aT * unT);

  Vec4 FL = inviscid_flux_phys(WL, nx, ny, gas);
  Vec4 FR = inviscid_flux_phys(WR, nx, ny, gas);
  Vec4 F;
  for (int k = 0; k < 4; ++k) F[k] = 0.5 * (FL[k] + FR[k]) - 0.5 * diss[k];
  return F;
}

// Jacobian of the inviscid normal flux times a conservative increment:
// (d(F_n)/dU) * dU, evaluated at primitive state W.
inline Vec4 flux_jac_times(const Vec4& W, double nx, double ny, const Vec4& dU,
                           const GasModel& gas) {
  const double gm1 = gas.gamma - 1.0;
  double rho = std::max(W[0], RHO_FLOOR);
  double u = W[1], v = W[2];
  double un = u * nx + v * ny;
  double drho = dU[0];
  double du = (dU[1] - u * drho) / rho;
  double dv = (dU[2] - v * drho) / rho;
  double dun = du * nx + dv * ny;
  double dp = gm1 * (dU[3] - u * dU[1] - v * dU[2] + 0.5 * (u * u + v * v) * drho);
  double drho_un = rho * dun + un * drho;   // d(rho un)
  double drhoE_p = dU[3] + dp;              // d(rho E + p)
  double rhoE_p = std::max(W[3], P_FLOOR) / gm1 * gas.gamma +
                  0.5 * rho * (u * u + v * v);
  Vec4 out;
  out[0] = drho_un;
  out[1] = u * drho_un + rho * un * du + dp * nx;
  out[2] = v * drho_un + rho * un * dv + dp * ny;
  out[3] = un * drhoE_p + rhoE_p * dun;
  return out;
}

// Viscous stress (Newtonian) and heat flux contributions in direction n.
// Given face values of u, v and face gradients of u, v, T.
// Returns F_v . n (to be SUBTRACTED from the inviscid flux in the residual).
inline Vec4 viscous_flux_phys(double u, double v, double ux, double uy, double vx,
                              double vy, double Tx, double Ty, double mu,
                              double k_cond, double nx, double ny) {
  double div = ux + vy;
  double txx = 2.0 * mu * ux - 2.0 / 3.0 * mu * div;
  double tyy = 2.0 * mu * vy - 2.0 / 3.0 * mu * div;
  double txy = mu * (uy + vx);
  Vec4 F;
  F[0] = 0.0;
  F[1] = txx * nx + txy * ny;
  F[2] = txy * nx + tyy * ny;
  F[3] = (u * txx + v * txy + k_cond * Tx) * nx +
         (u * txy + v * tyy + k_cond * Ty) * ny;
  return F;
}

}  // namespace cfd
