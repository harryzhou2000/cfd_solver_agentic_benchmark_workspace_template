#include "physics.hpp"

#include <cmath>

namespace cfd {

double Gas::pressure(double rho, double rhou, double rhov, double rhoE) const {
  const double u = rhou / rho;
  const double v = rhov / rho;
  return (gamma - 1.0) * (rhoE - 0.5 * rho * (u * u + v * v));
}

void boundary_primitives(BcType bc, double rho, double u, double v, double p,
                         double nx, double ny,
                         const std::array<double, 4>& q_inf,
                         double gamma, std::array<double, 4>& qb) {
  (void)gamma;
  switch (bc) {
    case BcType::Farfield:
      qb = q_inf;
      break;
    case BcType::SlipWall: {
      // mirror: normal velocity reversed, tangential preserved
      const double vn = u * nx + v * ny;
      qb = {rho, u - 2.0 * vn * nx, v - 2.0 * vn * ny, p};
      break;
    }
    case BcType::NoSlipAdiabaticWall:
      // Wall-state stencil value: the no-slip wall itself has zero velocity
      // and (adiabatic) the wall temperature equals the cell temperature
      // (rho, p unchanged).  Using the wall value (rather than a velocity
      // mirror) gives the correct wall-normal velocity gradient scale for
      // the viscous stress.
      qb = {rho, 0.0, 0.0, p};
      break;
  }
}

void inviscid_flux(const std::array<double, 4>& UL, const std::array<double, 4>& UR,
                   double nx, double ny, double gamma, double diss_scale,
                   std::array<double, 4>& F) {
  const double rhoL = UL[0], rhoR = UR[0];
  const double uL = UL[1] / rhoL, vL = UL[2] / rhoL;
  const double uR = UR[1] / rhoR, vR = UR[2] / rhoR;
  const double vnL = uL * nx + vL * ny;
  const double vnR = uR * nx + vR * ny;
  const double pL = (gamma - 1.0) * (UL[3] - 0.5 * rhoL * (uL * uL + vL * vL));
  const double pR = (gamma - 1.0) * (UR[3] - 0.5 * rhoR * (uR * uR + vR * vR));
  const double aL = std::sqrt(gamma * pL / rhoL);
  const double aR = std::sqrt(gamma * pR / rhoR);
  const double HL = (UL[3] + pL) / rhoL;
  const double HR = (UR[3] + pR) / rhoR;

  // physical flux in direction n, averaged
  const double f0 = 0.5 * (rhoL * vnL + rhoR * vnR);
  const double f1 = 0.5 * (rhoL * vnL * uL + pL * nx + rhoR * vnR * uR + pR * nx);
  const double f2 = 0.5 * (rhoL * vnL * vL + pL * ny + rhoR * vnR * vR + pR * ny);
  const double f3 = 0.5 * (rhoL * vnL * HL + rhoR * vnR * HR);

  const double lam =
      std::max(rusanov_wavespeed(uL, vL, vnL, aL), rusanov_wavespeed(uR, vR, vnR, aR));
  const double d = 0.5 * diss_scale * lam;
  F[0] = f0 - d * (UR[0] - UL[0]);
  F[1] = f1 - d * (UR[1] - UL[1]);
  F[2] = f2 - d * (UR[2] - UL[2]);
  F[3] = f3 - d * (UR[3] - UL[3]);
}

void euler_flux_jacobian(const std::array<double, 4>& q, double nx, double ny,
                         double gamma, std::array<double, 16>& A) {
  const double rho = q[0], u = q[1], v = q[2], p = q[3];
  const double vn = u * nx + v * ny;
  const double q2 = u * u + v * v;
  const double g = gamma;
  const double g1 = g - 1.0;
  const double H = (g / g1) * (p / rho) + 0.5 * q2;   // total enthalpy
  // A = dF/dU, U = [rho, rhou, rhov, rhoE]
  A[0] = 0.0;         A[1] = nx;       A[2] = ny;      A[3] = 0.0;
  A[4] = -u * vn + 0.5 * g1 * q2 * nx;
  A[5] = vn + (2.0 - g) * u * nx;
  A[6] = u * ny - g1 * v * nx;
  A[7] = g1 * nx;
  A[8] = -v * vn + 0.5 * g1 * q2 * ny;
  A[9] = v * nx - g1 * u * ny;
  A[10] = vn + (2.0 - g) * v * ny;
  A[11] = g1 * ny;
  A[12] = 0.5 * g1 * q2 * vn - vn * H;
  A[13] = H * nx - g1 * u * vn;
  A[14] = H * ny - g1 * v * vn;
  A[15] = g * vn;
}

namespace {

// Contract the Newtonian stress tensor with the face normal.
inline void stress_dot_n(const double g[6], double nx, double ny, double mu,
                         double& tx, double& ty) {
  // g = [du/dx, du/dy, dv/dx, dv/dy, dT/dx, dT/dy]
  const double ux = g[0], uy = g[1];
  const double vx = g[2], vy = g[3];
  const double div = ux + vy;
  const double txx = mu * (2.0 * ux - (2.0 / 3.0) * div);
  const double tyy = mu * (2.0 * vy - (2.0 / 3.0) * div);
  const double txy = mu * (uy + vx);
  tx = txx * nx + txy * ny;
  ty = txy * nx + tyy * ny;
}

}  // namespace

void viscous_flux(const std::array<double, 4>& qL, const std::array<double, 4>& qR,
                  const std::array<double, 6>& gL, const std::array<double, 6>& gR,
                  double nx, double ny, double mu, double k, double gamma,
                  std::array<double, 4>& Fv) {
  (void)gamma;
  double g[6];
  for (int i = 0; i < 6; ++i) g[i] = 0.5 * (gL[i] + gR[i]);
  const double u = 0.5 * (qL[1] + qR[1]);
  const double v = 0.5 * (qL[2] + qR[2]);
  const double dTdn = g[4] * nx + g[5] * ny;
  double tx, ty;
  stress_dot_n(g, nx, ny, mu, tx, ty);
  Fv[0] = 0.0;
  Fv[1] = tx;
  Fv[2] = ty;
  Fv[3] = tx * u + ty * v + k * dTdn;
}

void viscous_wall_flux(const std::array<double, 4>& q,
                       const std::array<double, 6>& grad,
                       double nx, double ny, double mu, double gamma,
                       std::array<double, 4>& Fv, double& tau_tangential,
                       double* tx_out, double* ty_out) {
  (void)gamma;
  double g[6] = {grad[0], grad[1], grad[2], grad[3], grad[4], grad[5]};
  double tx, ty;
  stress_dot_n(g, nx, ny, mu, tx, ty);
  // wall velocity = 0; adiabatic wall: heat flux = 0
  Fv[0] = 0.0;
  Fv[1] = tx;
  Fv[2] = ty;
  Fv[3] = 0.0;
  // tangential shear for skin friction: tau . t with t = (-ny, nx)
  tau_tangential = tx * (-ny) + ty * nx;
  if (tx_out) *tx_out = tx;
  if (ty_out) *ty_out = ty;
}

}  // namespace cfd
