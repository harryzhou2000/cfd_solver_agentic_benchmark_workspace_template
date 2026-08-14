#pragma once
// Calorically perfect gas model: conversions between conservative and
// primitive states, pressure, temperature, sound speed.
//
// Conservative state U = [rho, rho u, rho v, rho E]^T.
// Primitive state  W = [rho, u, v, p]^T (same array slots, different meaning).

#include <cmath>

#include "common.hpp"
#include "config.hpp"

namespace cfd2d {

struct Gas {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double cp = 0.0;
  double cv = 0.0;
  Gas() { update(); }
  explicit Gas(const GasModel& m) : gamma(m.gamma), R(m.R), prandtl(m.prandtl) {
    update();
  }
  void update() {
    cp = gamma * R / (gamma - 1.0);
    cv = R / (gamma - 1.0);
  }
};

inline double pressure(const Gas& g, const Vec4& U) {
  const double rho = U[IRHO];
  const double ke = 0.5 * (U[IRHOU] * U[IRHOU] + U[IRHOV] * U[IRHOV]) / rho;
  return (g.gamma - 1.0) * (U[IRHOE] - ke);
}

inline Vec4 conservedFromPrimitive(const Gas& g, double rho, double u, double v,
                                   double p) {
  Vec4 U;
  U[IRHO] = rho;
  U[IRHOU] = rho * u;
  U[IRHOV] = rho * v;
  U[IRHOE] = p / (g.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  return U;
}

inline Vec4 primitiveFromConserved(const Gas& g, const Vec4& U) {
  Vec4 W;
  const double rho = U[IRHO];
  W[0] = rho;
  W[1] = U[IRHOU] / rho;
  W[2] = U[IRHOV] / rho;
  W[3] = pressure(g, U);
  return W;
}

inline double soundSpeed(const Gas& g, double rho, double p) {
  return std::sqrt(g.gamma * p / rho);
}

inline double temperature(const Gas& g, double rho, double p) {
  return p / (rho * g.R);
}

// Small positive floors for positivity control (relative to freestream).
struct PositivityFloors {
  double rhoFloor = 1e-8;
  double pFloor = 1e-8;
};

}  // namespace cfd2d
