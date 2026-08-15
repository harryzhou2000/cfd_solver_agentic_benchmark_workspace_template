#include "flux.hpp"

#include <algorithm>
#include <cmath>

namespace fv {

double convectiveSpectralRadius(const Gas& g, const Vec4& U, double nx, double ny) {
  const double rho = U[IRHO];
  const double u = U[IRHOU] / rho, v = U[IRHOV] / rho;
  const double p = pressure(g, U);
  const double a = soundSpeed(g, rho, p);
  return std::abs(u * nx + v * ny) + a;
}

Vec4 hllcFlux(const Gas& g, const Vec4& UL, const Vec4& UR, double nx, double ny,
              double& smax) {
  const double rhoL = UL[IRHO], rhoR = UR[IRHO];
  const double uL = UL[IRHOU] / rhoL, vL = UL[IRHOV] / rhoL;
  const double uR = UR[IRHOU] / rhoR, vR = UR[IRHOV] / rhoR;
  const double pL = pressure(g, UL), pR = pressure(g, UR);
  const double unL = uL * nx + vL * ny, unR = uR * nx + vR * ny;
  const double aL = soundSpeed(g, rhoL, pL), aR = soundSpeed(g, rhoR, pR);
  const double HL = (UL[IRHOE] + pL) / rhoL, HR = (UR[IRHOE] + pR) / rhoR;

  // Roe averages for contact wave speed estimate
  const double sqL = std::sqrt(rhoL), sqR = std::sqrt(rhoR);
  const double invSum = 1.0 / (sqL + sqR);
  const double uRoe = (sqL * uL + sqR * uR) * invSum;
  const double vRoe = (sqL * vL + sqR * vR) * invSum;
  const double unRoe = uRoe * nx + vRoe * ny;
  const double HRoe = (sqL * HL + sqR * HR) * invSum;
  const double aRoe2 =
      (g.gamma - 1.0) * (HRoe - 0.5 * (uRoe * uRoe + vRoe * vRoe));
  const double aRoe = std::sqrt(std::max(aRoe2, 1e-12));

  const double SL = std::min(unL - aL, unRoe - aRoe);
  const double SR = std::max(unR + aR, unRoe + aRoe);
  smax = std::max(std::abs(SL), std::abs(SR));

  const Vec4 FL = inviscidFlux(UL, pL, nx, ny);
  if (SL >= 0.0) return FL;
  const Vec4 FR = inviscidFlux(UR, pR, nx, ny);
  if (SR <= 0.0) return FR;

  // contact (star) wave speed
  const double rhoLun = rhoL * (SL - unL), rhoRun = rhoR * (SR - unR);
  const double SM =
      (pR - pL + rhoL * unL * (SL - unL) - rhoR * unR * (SR - unR)) / (rhoLun - rhoRun);

  Vec4 F;
  if (SM >= 0.0) {
    const double coef = rhoL * (SL - unL) / (SL - SM);
    Vec4 UStar;
    UStar[IRHO] = coef;
    UStar[IRHOU] = coef * (uL + (SM - unL) * nx);
    UStar[IRHOV] = coef * (vL + (SM - unL) * ny);
    UStar[IRHOE] = coef * (UL[IRHOE] / rhoL + (SM - unL) * (SM + pL / (rhoL * (SL - unL))));
    for (int m = 0; m < NVAR; ++m) F[m] = FL[m] + SL * (UStar[m] - UL[m]);
  } else {
    const double coef = rhoR * (SR - unR) / (SR - SM);
    Vec4 UStar;
    UStar[IRHO] = coef;
    UStar[IRHOU] = coef * (uR + (SM - unR) * nx);
    UStar[IRHOV] = coef * (vR + (SM - unR) * ny);
    UStar[IRHOE] = coef * (UR[IRHOE] / rhoR + (SM - unR) * (SM + pR / (rhoR * (SR - unR))));
    for (int m = 0; m < NVAR; ++m) F[m] = FR[m] + SR * (UStar[m] - UR[m]);
  }
  return F;
}

Vec4 rusanovFlux(const Gas& g, const Vec4& UL, const Vec4& UR, double nx, double ny,
                 double dissipationScale, double& smax) {
  const double pL = pressure(g, UL), pR = pressure(g, UR);
  const Vec4 FL = inviscidFlux(UL, pL, nx, ny);
  const Vec4 FR = inviscidFlux(UR, pR, nx, ny);
  const double sL = convectiveSpectralRadius(g, UL, nx, ny);
  const double sR = convectiveSpectralRadius(g, UR, nx, ny);
  smax = std::max(sL, sR);
  Vec4 F;
  const double d = dissipationScale * smax;
  for (int m = 0; m < NVAR; ++m)
    F[m] = 0.5 * (FL[m] + FR[m]) - 0.5 * d * (UR[m] - UL[m]);
  return F;
}

Vec4 viscousFlux(double uF, double vF, double duFdx, double duFdy, double dvFdx,
                 double dvFdy, double dTFdx, double dTFdy, double mu, double kCond,
                 double nx, double ny) {
  const double div = duFdx + dvFdy;
  const double tauXX = 2.0 * mu * duFdx - (2.0 / 3.0) * mu * div;
  const double tauYY = 2.0 * mu * dvFdy - (2.0 / 3.0) * mu * div;
  const double tauXY = mu * (duFdy + dvFdx);
  Vec4 F;
  F[IRHO] = 0.0;
  F[IRHOU] = tauXX * nx + tauXY * ny;
  F[IRHOV] = tauXY * nx + tauYY * ny;
  F[IRHOE] = (uF * tauXX + vF * tauXY + kCond * dTFdx) * nx +
             (uF * tauXY + vF * tauYY + kCond * dTFdy) * ny;
  return F;
}

}  // namespace fv
