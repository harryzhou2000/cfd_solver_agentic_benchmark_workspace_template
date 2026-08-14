#include "fluxes.h"
#include <cmath>
#include <algorithm>

namespace cfd2d {

void GasPhysics::initFromCase(const CaseInput& ci) {
  gamma = ci.gamma;
  R = ci.R;
  prandtl = ci.prandtl;
  rusanovScale = ci.rusanovDissipationScale;
  viscous = (ci.physicsMode == "laminar");
  rhoInf = ci.rhoInf;
  velMag = ci.velMag;
  uInf = ci.velMag * std::cos(ci.aoaDeg * M_PI / 180.0);
  vInf = ci.velMag * std::sin(ci.aoaDeg * M_PI / 180.0);
  pInf = ci.pressureInf;
  machInf = ci.mach;
  if (viscous && ci.reynolds > 0.0) {
    // mu = rho_inf * U_inf * L_ref / Re
    mu = rhoInf * ci.velMag * ci.reynoldsLength / ci.reynolds;
  } else {
    mu = 0.0;
  }
}

PrimState GasPhysics::consToPrim(const ConsState& U) const {
  PrimState P;
  P[0] = U[0]; // rho
  double rho = std::max(P[0], 1e-12);
  P[1] = U[1] / rho; // u
  P[2] = U[2] / rho; // v
  // E = U[3], e = E - 0.5*(u^2+v^2), p = (gamma-1)*rho*e
  double e = U[3] / rho - 0.5 * (P[1] * P[1] + P[2] * P[2]);
  P[3] = (gamma - 1.0) * rho * e; // p
  return P;
}

ConsState GasPhysics::primToCons(const PrimState& P) const {
  ConsState U;
  U[0] = P[0];
  U[1] = P[0] * P[1];
  U[2] = P[0] * P[2];
  double e = P[3] / ((gamma - 1.0) * P[0]);
  U[3] = P[0] * (e + 0.5 * (P[1] * P[1] + P[2] * P[2]));
  return U;
}

double GasPhysics::soundSpeed(const PrimState& P) const {
  return std::sqrt(gamma * std::max(P[3], 1e-12) / std::max(P[0], 1e-12));
}

double GasPhysics::temperature(const PrimState& P) const {
  return P[3] / (std::max(P[0], 1e-12) * R);
}

void GasPhysics::inviscidFlux(const ConsState& U, ConsState& F, ConsState& G) const {
  PrimState P = consToPrim(U);
  double rho = P[0], u = P[1], v = P[2], p = P[3];
  double un = u; // for x-flux
  F[0] = rho * u;
  F[1] = rho * u * u + p;
  F[2] = rho * u * v;
  F[3] = u * (U[3] + p);
  G[0] = rho * v;
  G[1] = rho * v * u;
  G[2] = rho * v * v + p;
  G[3] = v * (U[3] + p);
}

ConsState GasPhysics::rusanovFlux(const ConsState& UL, const ConsState& UR,
                                   double nx, double ny) const {
  PrimState PL = consToPrim(UL);
  PrimState PR = consToPrim(UR);
  // Flux from left and right
  ConsState FL, GL, FR, GR;
  inviscidFlux(UL, FL, GL);
  inviscidFlux(UR, FR, GR);
  // Normal flux
  ConsState FnL, FnR;
  for (int i = 0; i < NEQ; ++i) {
    FnL[i] = FL[i] * nx + GL[i] * ny;
    FnR[i] = FR[i] * nx + GR[i] * ny;
  }
  // Spectral radius: max wave speed * |n|
  double aL = soundSpeed(PL);
  double aR = soundSpeed(PR);
  double unL = PL[1] * nx + PL[2] * ny;
  double unR = PR[1] * nx + PR[2] * ny;
  double smax = std::max(std::abs(unL) + aL, std::abs(unR) + aR);
  ConsState F;
  for (int i = 0; i < NEQ; ++i) {
    F[i] = 0.5 * (FnL[i] + FnR[i]) - 0.5 * rusanovScale * smax * (UR[i] - UL[i]);
  }
  return F;
}

void GasPhysics::viscousFlux(const PrimState& PL, const PrimState& PR,
                             const std::array<double,4>& dWdx,
                             const std::array<double,4>& dWdy,
                             double nx, double ny,
                             ConsState& Fvisc) const {
  // dW = [drho, du, dv, dT] gradients (we use T = p/(rho*R))
  // Average primitive
  double rho = 0.5 * (PL[0] + PR[0]);
  double u = 0.5 * (PL[1] + PR[1]);
  double v = 0.5 * (PL[2] + PR[2]);
  double p = 0.5 * (PL[3] + PR[3]);
  double T = p / (rho * R);
  double ux = dWdx[1], uy = dWdy[1];
  double vx = dWdx[2], vy = dWdy[2];
  double Tx = dWdx[3], Ty = dWdy[3];

  // Stress tensor (Newtonian)
  double div = ux + vy;
  double txx = mu * (2.0 * ux - 2.0 / 3.0 * div);
  double tyy = mu * (2.0 * vy - 2.0 / 3.0 * div);
  double txy = mu * (uy + vx);

  // Heat flux q = -k grad T, k = mu * cp / Pr, cp = gamma*R/(gamma-1)
  double cp = gamma * R / (gamma - 1.0);
  double k = mu * cp / prandtl;
  double qx = -k * Tx;
  double qy = -k * Ty;

  // Viscous flux in x: Fv = [0, txx, txy, u*txx + v*txy - qx]
  // Viscous flux in y: Gv = [0, txy, tyy, u*txy + v*tyy - qy]
  // Normal viscous flux = Fv*nx + Gv*ny
  Fvisc[0] = 0.0;
  Fvisc[1] = txx * nx + txy * ny;
  Fvisc[2] = txy * nx + tyy * ny;
  Fvisc[3] = (u * txx + v * txy - qx) * nx + (u * txy + v * tyy - qy) * ny;
}

} // namespace cfd2d
