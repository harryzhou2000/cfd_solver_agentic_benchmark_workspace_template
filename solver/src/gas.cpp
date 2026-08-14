#include "cfd/gas.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cfd {
namespace {

void require_positive_finite(double value, const char* name) {
  if (!(value > 0.0) || !std::isfinite(value)) {
    throw std::invalid_argument(std::string(name) + " must be finite and positive");
  }
}

}  // namespace

CaloricallyPerfectGas::CaloricallyPerfectGas(const GasConfig& config)
    : CaloricallyPerfectGas(config.gamma, config.gas_constant, config.prandtl) {}

CaloricallyPerfectGas::CaloricallyPerfectGas(double gamma, double gas_constant,
                                             double prandtl)
    : gamma_(gamma), gas_constant_(gas_constant), prandtl_(prandtl) {
  if (!(gamma_ > 1.0) || !std::isfinite(gamma_)) {
    throw std::invalid_argument("gamma must be finite and greater than one");
  }
  require_positive_finite(gas_constant_, "gas constant");
  require_positive_finite(prandtl_, "Prandtl number");
}

Primitive CaloricallyPerfectGas::primitive(const Conservative& state) const {
  for (double value : state) {
    if (!std::isfinite(value)) throw std::domain_error("conservative state is not finite");
  }
  require_positive_finite(state[0], "density");
  const double inverse_rho = 1.0 / state[0];
  const double u = state[1] * inverse_rho;
  const double v = state[2] * inverse_rho;
  const double kinetic = 0.5 * (state[1] * u + state[2] * v);
  const double pressure = (gamma_ - 1.0) * (state[3] - kinetic);
  return complete(state[0], u, v, pressure);
}

Conservative CaloricallyPerfectGas::conservative(const Primitive& state) const {
  if (!admissible(state)) throw std::domain_error("primitive state is not finite and positive");
  return Conservative{state.rho, state.rho * state.u, state.rho * state.v,
                      state.p / (gamma_ - 1.0) +
                          0.5 * state.rho * (state.u * state.u + state.v * state.v)};
}

bool CaloricallyPerfectGas::admissible(const Conservative& state) const noexcept {
  for (double value : state) {
    if (!std::isfinite(value)) return false;
  }
  if (!(state[0] > 0.0)) return false;
  const double kinetic = 0.5 * (state[1] * state[1] + state[2] * state[2]) / state[0];
  return (gamma_ - 1.0) * (state[3] - kinetic) > 0.0;
}

bool CaloricallyPerfectGas::admissible(const Primitive& state) const noexcept {
  return state.rho > 0.0 && state.p > 0.0 && state.T > 0.0 && state.a > 0.0 &&
         std::isfinite(state.rho) && std::isfinite(state.u) && std::isfinite(state.v) &&
         std::isfinite(state.p) && std::isfinite(state.T) && std::isfinite(state.a);
}

Primitive CaloricallyPerfectGas::complete(double rho, double u, double v,
                                          double pressure) const {
  require_positive_finite(rho, "density");
  require_positive_finite(pressure, "pressure");
  if (!std::isfinite(u) || !std::isfinite(v)) {
    throw std::domain_error("velocity must be finite");
  }
  const double temperature = pressure / (rho * gas_constant_);
  const double sound_speed = std::sqrt(gamma_ * pressure / rho);
  Primitive result{rho, u, v, pressure, temperature, sound_speed};
  if (!admissible(result)) throw std::domain_error("derived primitive state is inadmissible");
  return result;
}

Conservative CaloricallyPerfectGas::freestream(const FreestreamConfig& freestream) const {
  constexpr double pi = 3.141592653589793238462643383279502884;
  const Primitive thermodynamic_state =
      complete(freestream.rho, 0.0, 0.0, freestream.pressure);
  const double implied_mach = freestream.velocity_magnitude / thermodynamic_state.a;
  const double mach_scale =
      std::max({std::abs(implied_mach), std::abs(freestream.mach), 1.0e-12});
  const double tolerance = freestream_mach_relative_tolerance * mach_scale;
  if (!std::isfinite(freestream.mach) ||
      std::abs(freestream.mach - implied_mach) > tolerance) {
    throw std::invalid_argument("freestream Mach is inconsistent with velocity and sound speed");
  }
  const double angle = freestream.aoa_degrees * pi / 180.0;
  return conservative(complete(freestream.rho,
                               freestream.velocity_magnitude * std::cos(angle),
                               freestream.velocity_magnitude * std::sin(angle),
                               freestream.pressure));
}

double CaloricallyPerfectGas::constant_viscosity(const CaseConfig& config) const {
  if (config.physics.mode == PhysicsMode::inviscid) return 0.0;
  if (!config.physics.reynolds.has_value()) {
    throw std::invalid_argument("laminar case has no Reynolds number");
  }
  const double length = config.reference.reynolds_length;
  const double viscosity = config.freestream.rho * config.freestream.velocity_magnitude *
                           length / *config.physics.reynolds;
  require_positive_finite(viscosity, "constant viscosity");
  return viscosity;
}

}  // namespace cfd
