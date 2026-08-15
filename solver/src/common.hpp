#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace fv {

// Conservative state indices
enum { IRHO = 0, IRHOU = 1, IRHOV = 2, IRHOE = 3 };
constexpr int NVAR = 4;

using Vec4 = std::array<double, NVAR>;

// Perfect-gas model constants for one case
struct Gas {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;

  double cp() const { return gamma * R / (gamma - 1.0); }
  double cv() const { return R / (gamma - 1.0); }
};

// Primitive variable helpers. U = [rho, rho u, rho v, rho E]
inline double pressure(const Gas& g, const Vec4& U) {
  const double rho = U[IRHO];
  const double ke = 0.5 * (U[IRHOU] * U[IRHOU] + U[IRHOV] * U[IRHOV]) / rho;
  return (g.gamma - 1.0) * (U[IRHOE] - ke);
}

inline double soundSpeed(const Gas& g, double rho, double p) {
  return std::sqrt(g.gamma * p / rho);
}

inline Vec4 primitiveToCons(const Gas& g, double rho, double u, double v, double p) {
  Vec4 U;
  U[IRHO] = rho;
  U[IRHOU] = rho * u;
  U[IRHOV] = rho * v;
  U[IRHOE] = p / (g.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
  return U;
}

// Inviscid flux F(U)*nx + G(U)*ny (dot with face normal)
inline Vec4 inviscidFlux(const Vec4& U, double p, double nx, double ny) {
  const double rho = U[IRHO];
  const double u = U[IRHOU] / rho;
  const double v = U[IRHOV] / rho;
  const double un = u * nx + v * ny;
  Vec4 F;
  F[IRHO] = rho * un;
  F[IRHOU] = rho * u * un + p * nx;
  F[IRHOV] = rho * v * un + p * ny;
  F[IRHOE] = (U[IRHOE] + p) * un;
  return F;
}

}  // namespace fv
