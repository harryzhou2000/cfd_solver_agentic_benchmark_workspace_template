#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

using Vec2 = std::array<double, 2>;
using Vec3 = std::array<double, 3>;

// Conservative state: [rho, rho u, rho v, rho E]
using State = std::array<double, 4>;

// Primitive state: [rho, u, v, p]
using Prim = std::array<double, 4>;

struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double cp() const { return gamma * R / (gamma - 1.0); }
};

inline double sq(double x) { return x * x; }

// ---- Conversions between conservative and primitive states ----
inline Prim toPrimitive(const State& U, const GasModel& gas) {
  double rho = U[0];
  double u = U[1] / rho;
  double v = U[2] / rho;
  double p = (gas.gamma - 1.0) * (U[3] - 0.5 * rho * (u * u + v * v));
  return {rho, u, v, p};
}

inline State toConservative(const Prim& W, const GasModel& gas) {
  double rho = W[0];
  double u = W[1];
  double v = W[2];
  double p = W[3];
  double e = p / ((gas.gamma - 1.0) * rho);
  double E = e + 0.5 * (u * u + v * v);
  return {rho, rho * u, rho * v, rho * E};
}

inline double soundSpeed(const Prim& W, const GasModel& gas) {
  return std::sqrt(gas.gamma * W[3] / W[0]);
}

inline double mach(const Prim& W, const GasModel& gas) {
  double a = soundSpeed(W, gas);
  return std::sqrt(W[1] * W[1] + W[2] * W[2]) / a;
}

inline double temperature(const Prim& W, const GasModel& gas) {
  return W[3] / (gas.R * W[0]);
}

// Euler flux through a face with unit normal (nx, ny).  Area weighting is
// applied by the caller.
inline State eulerFlux(const State& U, const GasModel& gas, double nx, double ny) {
  double rho = U[0];
  double u = U[1] / rho;
  double v = U[2] / rho;
  double p = (gas.gamma - 1.0) * (U[3] - 0.5 * rho * (u * u + v * v));
  double vn = u * nx + v * ny;
  double h = (U[3] + p) / rho;  // total enthalpy
  return {rho * vn, U[1] * vn + p * nx, U[2] * vn + p * ny, U[3] * vn + p * vn};
}

inline State eulerFluxPrim(const Prim& W, const GasModel& gas, double nx, double ny) {
  double rho = W[0];
  double u = W[1];
  double v = W[2];
  double p = W[3];
  double vn = u * nx + v * ny;
  double rhoE = p / (gas.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  return {rho * vn, (rho * u) * vn + p * nx, (rho * v) * vn + p * ny,
          (rhoE + p) * vn};
}

}  // namespace cfd
