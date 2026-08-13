// Phase 3: inviscid numerical flux implementation (see flux.h).

#include "flux.h"

#include <algorithm>
#include <cmath>

#include "physics.h"

namespace cfd {

namespace {

inline ConsState make_flux(double f0, double f1, double f2, double f3) {
  ConsState f;
  f.rho = f0;
  f.rhou = f1;
  f.rhov = f2;
  f.rhoE = f3;
  return f;
}

}  // namespace

ConsState inviscid_flux_x(const ConsState& U, const GasConfig& gas) {
  const double p = pressure_from_cons(U, gas);
  const double u = U.rhou / U.rho;
  const double v = U.rhov / U.rho;
  const double H = (U.rhoE + p) / U.rho;  // total enthalpy
  return make_flux(U.rhou, U.rhou * u + p, U.rhou * v, U.rhou * H);
}

ConsState inviscid_flux_y(const ConsState& U, const GasConfig& gas) {
  const double p = pressure_from_cons(U, gas);
  const double u = U.rhou / U.rho;
  const double v = U.rhov / U.rho;
  const double H = (U.rhoE + p) / U.rho;  // total enthalpy
  return make_flux(U.rhov, U.rhov * u, U.rhov * v + p, U.rhov * H);
}

ConsState inviscid_flux_dot_normal(const ConsState& U, const Vec2& normal,
                                   const GasConfig& gas) {
  double f[4];
  inviscid_flux_dot_normal(U, normal, gas, f);
  return make_flux(f[0], f[1], f[2], f[3]);
}

void inviscid_flux_dot_normal(const ConsState& U, const Vec2& normal,
                              const GasConfig& gas, double result[4]) {
  const double rho = U.rho;
  const double u = U.rhou / rho;
  const double v = U.rhov / rho;
  // Single EOS evaluation for the whole flux.
  const double p = (gas.gamma - 1.0) * (U.rhoE - 0.5 * rho * (u * u + v * v));
  const double H = (U.rhoE + p) / rho;  // total enthalpy

  result[0] = (rho * u) * normal.x + (rho * v) * normal.y;
  result[1] = (U.rhou * u + p) * normal.x + (U.rhou * v) * normal.y;
  result[2] = (U.rhov * u) * normal.x + (U.rhov * v + p) * normal.y;
  result[3] = (U.rhou * H) * normal.x + (U.rhov * H) * normal.y;
}

ConsState rusanov_flux(const ConsState& UL, const ConsState& UR,
                       const Vec2& normal, const GasConfig& gas) {
  return rusanov_flux(UL, UR, normal, gas, 1.0);
}

ConsState rusanov_flux(const ConsState& UL, const ConsState& UR,
                       const Vec2& normal, const GasConfig& gas,
                       double dissipation_scale) {
  const double area = normal.norm();
  if (!(area > 0.0)) {
    // Degenerate face (zero length): no flux through it.
    return ConsState{};
  }
  const Vec2 n_hat = normal / area;

  const double uL = UL.rhou / UL.rho;
  const double vL = UL.rhov / UL.rho;
  const double uR = UR.rhou / UR.rho;
  const double vR = UR.rhov / UR.rho;

  const double aL = speed_of_sound(UL, gas);
  const double aR = speed_of_sound(UR, gas);

  const double vnL = uL * n_hat.x + vL * n_hat.y;
  const double vnR = uR * n_hat.x + vR * n_hat.y;

  // Maximum wave speed in the face-normal direction.
  const double lambda = std::max(std::abs(vnL) + aL, std::abs(vnR) + aR);

  // Central part: average of the left/right flux dotted with the area vector
  // (single-pass primitive evaluation per side — the residual hot path).
  double FL[4];
  double FR[4];
  inviscid_flux_dot_normal(UL, normal, gas, FL);
  inviscid_flux_dot_normal(UR, normal, gas, FR);

  // Dissipation: 0.5 * lambda * |n| * (UR - UL). lambda is per unit normal,
  // so the face-area factor comes from |n| = area.
  const double coeff = 0.5 * dissipation_scale * lambda * area;
  return make_flux(0.5 * (FL[0] + FR[0]) - coeff * (UR.rho - UL.rho),
                   0.5 * (FL[1] + FR[1]) - coeff * (UR.rhou - UL.rhou),
                   0.5 * (FL[2] + FR[2]) - coeff * (UR.rhov - UL.rhov),
                   0.5 * (FL[3] + FR[3]) - coeff * (UR.rhoE - UL.rhoE));
}

}  // namespace cfd
