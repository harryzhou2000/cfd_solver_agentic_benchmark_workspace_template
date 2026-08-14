#pragma once

#include "cfd/config.hpp"

#include <array>

namespace cfd {

// Freestream Mach is redundant with rho, p, and velocity.  Allow ordinary
// decimal input rounding while rejecting physically meaningful disagreement.
inline constexpr double freestream_mach_relative_tolerance = 1.0e-6;

using Conservative = std::array<double, 4>;

struct Primitive {
  double rho{};
  double u{};
  double v{};
  double p{};
  double T{};
  double a{};
};

struct VelocityTemperatureGradients {
  Vec2 u{};
  Vec2 v{};
  Vec2 temperature{};
};

class CaloricallyPerfectGas {
 public:
  explicit CaloricallyPerfectGas(const GasConfig& config);
  CaloricallyPerfectGas(double gamma, double gas_constant, double prandtl);

  double gamma() const noexcept { return gamma_; }
  double gas_constant() const noexcept { return gas_constant_; }
  double prandtl() const noexcept { return prandtl_; }
  double cv() const noexcept { return gas_constant_ / (gamma_ - 1.0); }
  double cp() const noexcept { return gamma_ * gas_constant_ / (gamma_ - 1.0); }

  Primitive primitive(const Conservative& state) const;
  Conservative conservative(const Primitive& state) const;
  bool admissible(const Conservative& state) const noexcept;
  bool admissible(const Primitive& state) const noexcept;
  Primitive complete(double rho, double u, double v, double pressure) const;
  Conservative freestream(const FreestreamConfig& freestream) const;

  // Constant dynamic viscosity implied by the reference Reynolds number.
  double constant_viscosity(const CaseConfig& config) const;

 private:
  double gamma_{};
  double gas_constant_{};
  double prandtl_{};
};

}  // namespace cfd
