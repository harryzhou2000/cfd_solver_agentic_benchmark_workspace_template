#include "flux.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {

State rusanovFlux(const State& UL, const State& UR, const GasModel& gas, double nx,
                  double ny, double dissScale, double& lambdaMax) {
  Prim wl = toPrimitive(UL, gas);
  Prim wr = toPrimitive(UR, gas);
  const double al = soundSpeed(wl, gas);
  const double ar = soundSpeed(wr, gas);
  const double vnl = wl[1] * nx + wl[2] * ny;
  const double vnr = wr[1] * nx + wr[2] * ny;
  lambdaMax = std::max(std::fabs(vnl) + al, std::fabs(vnr) + ar);
  State fl = eulerFlux(UL, gas, nx, ny);
  State fr = eulerFlux(UR, gas, nx, ny);
  const double d = 0.5 * dissScale * lambdaMax;
  return {0.5 * (fl[0] + fr[0]) - d * (UR[0] - UL[0]),
          0.5 * (fl[1] + fr[1]) - d * (UR[1] - UL[1]),
          0.5 * (fl[2] + fr[2]) - d * (UR[2] - UL[2]),
          0.5 * (fl[3] + fr[3]) - d * (UR[3] - UL[3])};
}

State roeFlux(const State& UL, const State& UR, const GasModel& gas, double nx,
              double ny, double dissScale, double& lambdaMax) {
  const double g = gas.gamma;
  const double gm1 = g - 1.0;
  const Prim wl = toPrimitive(UL, gas);
  const Prim wr = toPrimitive(UR, gas);
  const double rl = wl[0], rr = wr[0];
  const double srl = std::sqrt(rl), srr = std::sqrt(rr);
  const double inv = 1.0 / (srl + srr);
  // Roe-averaged state
  const double rho = srl * srr;
  const double u = (srl * wl[1] + srr * wr[1]) * inv;
  const double v = (srl * wl[2] + srr * wr[2]) * inv;
  const double q2 = u * u + v * v;
  const double hl = (UL[3] + wl[3]) / rl;
  const double hr = (UR[3] + wr[3]) / rr;
  const double H = (srl * hl + srr * hr) * inv;
  const double a = std::sqrt(std::max(gm1 * (H - 0.5 * q2), 1e-12));
  const double vn = u * nx + v * ny;
  const double vt = -u * ny + v * nx;

  // wave strengths
  const double drho = rr - rl;
  const double du = wr[1] - wl[1];
  const double dv = wr[2] - wl[2];
  const double dp = wr[3] - wl[3];
  const double dvn = du * nx + dv * ny;
  const double dvt = -du * ny + dv * nx;
  const double a1 = (dp - rho * a * dvn) / (2.0 * a * a);
  const double a2 = drho - dp / (a * a);
  const double a3 = (dp + rho * a * dvn) / (2.0 * a * a);
  const double a4 = rho * dvt;

  // Harten-Yee entropy fix
  const double delta = 0.1 * (std::fabs(vn) + a);
  auto efix = [delta](double lam) {
    const double al = std::fabs(lam);
    return al < delta ? (lam * lam + delta * delta) / (2.0 * delta) : al;
  };
  const double l1 = efix(vn - a);
  const double l2 = efix(vn);
  const double l3 = efix(vn + a);
  const double l4 = l2;
  lambdaMax = std::max(std::fabs(wl[1] * nx + wl[2] * ny) + soundSpeed(wl, gas),
                       std::fabs(wr[1] * nx + wr[2] * ny) + soundSpeed(wr, gas));

  const double hq = 0.5 * q2;
  const double d1 = a2 * l2 + a1 * l1 + a3 * l3;
  const double d2 = a2 * l2 * u + a1 * l1 * (u - a * nx) + a3 * l3 * (u + a * nx) +
                    a4 * l4 * (-ny);
  const double d3 = a2 * l2 * v + a1 * l1 * (v - a * ny) + a3 * l3 * (v + a * ny) +
                    a4 * l4 * nx;
  const double d4 = a2 * l2 * hq + a1 * l1 * (H - a * vn) + a3 * l3 * (H + a * vn) +
                    a4 * l4 * vt;

  State fl = eulerFlux(UL, gas, nx, ny);
  State fr = eulerFlux(UR, gas, nx, ny);
  const double d = 0.5 * dissScale;
  return {0.5 * (fl[0] + fr[0]) - d * d1, 0.5 * (fl[1] + fr[1]) - d * d2,
          0.5 * (fl[2] + fr[2]) - d * d3, 0.5 * (fl[3] + fr[3]) - d * d4};
}

Mat44 eulerJacobian(const State& U, const GasModel& gas, double nx, double ny) {
  const double rho = U[0];
  const double u = U[1] / rho;
  const double v = U[2] / rho;
  const double q2 = u * u + v * v;
  const double p = (gas.gamma - 1.0) * (U[3] - 0.5 * rho * q2);
  const double H = (U[3] + p) / rho;
  const double vn = u * nx + v * ny;
  const double gm1 = gas.gamma - 1.0;
  const double c = 0.5 * gm1 * q2;

  Mat44 A;
  // row 1
  A[0] = {0.0, nx, ny, 0.0};
  // row 2
  A[1] = {-u * vn + c * nx,
          vn - (gas.gamma - 2.0) * u * nx,
          v * nx - gm1 * u * ny,
          gm1 * nx};
  // row 3
  A[2] = {-v * vn + c * ny,
          u * ny - gm1 * v * nx,
          vn - (gas.gamma - 2.0) * v * ny,
          gm1 * ny};
  // row 4
  A[3] = {vn * (-H + c),
          H * nx - gm1 * u * vn,
          H * ny - gm1 * v * vn,
          gas.gamma * vn};
  return A;
}

Prim farfieldGhost(const Prim& Wint, const Freestream& fs, const GasModel& gas,
                   double nx, double ny) {
  const double g = gas.gamma;
  const double gm1 = g - 1.0;
  // freestream primitives
  const double rhoF = fs.rho;
  const double uF = fs.u;
  const double vF = fs.v;
  const double pF = fs.pressure;
  const double aF = std::sqrt(g * pF / rhoF);

  const double vnI = Wint[1] * nx + Wint[2] * ny;
  const double vnF = uF * nx + vF * ny;
  const double aI = soundSpeed(Wint, gas);

  // tangential velocity and entropy source
  double vt, pS, aS, rhoS;
  bool fromInterior;
  if (vnI >= 0.0) {
    // outflow: entropy/tangential from interior
    vt = Wint[1] * (-ny) + Wint[2] * nx;
    pS = Wint[3];
    aS = aI;
    rhoS = Wint[0];
    fromInterior = true;
  } else {
    vt = uF * (-ny) + vF * nx;
    pS = pF;
    aS = aF;
    rhoS = rhoF;
    fromInterior = false;
  }

  double vnG, aG, pG, rhoG;
  if (vnI > aI) {
    // supersonic outflow: fully extrapolated
    return Wint;
  }
  if (vnI < -aI) {
    // supersonic inflow: fully freestream
    return {rhoF, uF, vF, pF};
  }
  // subsonic: Riemann invariants
  const double Rp = fromInterior ? (vnI + 2.0 * aI / gm1) : (vnF + 2.0 * aF / gm1);
  const double Rm = fromInterior ? (vnF - 2.0 * aF / gm1) : (vnI - 2.0 * aI / gm1);
  vnG = 0.5 * (Rp + Rm);
  aG = 0.25 * gm1 * (Rp - Rm);
  if (aG <= 0.0) aG = aS * 1e-6;
  // isentropic relation from the entropy source state
  pG = pS * std::pow(aG / aS, 2.0 * g / gm1);
  rhoG = g * pG / (aG * aG);
  // tangential component back to Cartesian
  const double uG = vnG * nx - vt * ny;
  const double vG = vnG * ny + vt * nx;
  return {rhoG, uG, vG, pG};
}

State viscousFlux(double u, double v, double ux, double uy, double vx, double vy,
                  double Tx, double Ty, double mu, double kappa, double nx, double ny) {
  double txx, txy, tyy;
  stress(ux, uy, vx, vy, mu, txx, txy, tyy);
  const double qx = -kappa * Tx;
  const double qy = -kappa * Ty;
  const double fx = txx * nx + txy * ny;
  const double fy = txy * nx + tyy * ny;
  return {0.0, fx, fy, (u * fx + v * fy) - (qx * nx + qy * ny)};
}

State solve44(const Mat44& A, const State& b) {
  Mat44 M = A;
  State rhs = b;
  for (int col = 0; col < 4; ++col) {
    // pivot
    int piv = col;
    for (int r = col + 1; r < 4; ++r)
      if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
    if (M[piv][col] == 0.0) return {0.0, 0.0, 0.0, 0.0};
    if (piv != col) {
      std::swap(M[piv], M[col]);
      std::swap(rhs[piv], rhs[col]);
    }
    const double d = M[col][col];
    for (int r = col + 1; r < 4; ++r) {
      const double f = M[r][col] / d;
      if (f == 0.0) continue;
      for (int c = col; c < 4; ++c) M[r][c] -= f * M[col][c];
      rhs[r] -= f * rhs[col];
    }
  }
  State x;
  for (int r = 3; r >= 0; --r) {
    double s = rhs[r];
    for (int c = r + 1; c < 4; ++c) s -= M[r][c] * x[c];
    x[r] = s / M[r][r];
  }
  return x;
}

}  // namespace cfd
