#pragma once
// Calorically perfect gas model. Kept as a small struct so that alternative
// equations of state can be introduced behind the same interface later.
#include "common.hpp"

namespace fv {

struct Gas {
  double gamma = 1.4;
  double R = 1.0;
  double Pr = 0.72;
  double cp() const { return gamma * R / (gamma - 1.0); }
};

// Primitive variables W = [rho, u, v, p]
inline double consPressure(const State& U, const Gas& g) {
  double rho = U[0];
  double ke = 0.5 * (U[1] * U[1] + U[2] * U[2]) / rho;
  return (g.gamma - 1.0) * (U[3] - ke);
}

inline State primToCons(double rho, double u, double v, double p, const Gas& g) {
  State U;
  U[0] = rho;
  U[1] = rho * u;
  U[2] = rho * v;
  U[3] = p / (g.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  return U;
}

inline void consToPrim(const State& U, const Gas& g, double& rho, double& u, double& v, double& p) {
  rho = U[0];
  u = U[1] / rho;
  v = U[2] / rho;
  p = consPressure(U, g);
}

inline double soundSpeed(double rho, double p, const Gas& g) {
  return std::sqrt(g.gamma * p / rho);
}

}  // namespace fv
