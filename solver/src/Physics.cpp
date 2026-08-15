#include "cfd/Physics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace cfd {
namespace {

constexpr double kTinyNormal = 1.0e-300;

bool finite(double value) { return std::isfinite(value); }

void require_finite(double value, const char* name) {
  if (!finite(value)) throw PhysicsError(std::string(name) + " must be finite");
}

void require_positive(double value, const char* name) {
  require_finite(value, name);
  if (!(value > 0.0)) throw PhysicsError(std::string(name) + " must be positive");
}

void validate_gas(const GasProperties& gas) {
  require_finite(gas.gamma, "gamma");
  if (!(gas.gamma > 1.0)) throw PhysicsError("gamma must be greater than one");
  require_positive(gas.gas_constant, "gas constant");
  require_positive(gas.prandtl, "Prandtl number");
}

void validate_normal(Vec2 normal) {
  require_finite(normal.x, "normal.x");
  require_finite(normal.y, "normal.y");
  if (!(std::hypot(normal.x, normal.y) > kTinyNormal)) {
    throw PhysicsError("normal must have nonzero length");
  }
}

void validate_primary_primitive(const Primitive& state, const GasProperties& gas) {
  validate_gas(gas);
  require_positive(state.rho, "density");
  require_finite(state.u, "u velocity");
  require_finite(state.v, "v velocity");
  require_positive(state.p, "pressure");
}

double pressure_from_conserved(const Conserved& state, const GasProperties& gas) {
  const double rho = state[0];
  if (!(rho > 0.0) || !finite(rho)) return std::numeric_limits<double>::quiet_NaN();
  const double kinetic = 0.5 * (state[1] * state[1] + state[2] * state[2]) / rho;
  return (gas.gamma - 1.0) * (state[3] - kinetic);
}

Conserved add_scaled(const Conserved& state, const Conserved& increment, double scale) {
  Conserved result{};
  for (std::size_t i = 0; i < result.size(); ++i) result[i] = state[i] + scale * increment[i];
  return result;
}

}  // namespace

GasProperties gas_properties(const CaseConfig& config) {
  GasProperties gas{config.gas.gamma, config.gas.gas_constant, config.gas.prandtl};
  validate_gas(gas);
  return gas;
}

Primitive complete_primitive(Primitive state, const GasProperties& gas) {
  validate_primary_primitive(state, gas);
  state.temperature = state.p / (state.rho * gas.gas_constant);
  state.sound_speed = std::sqrt(gas.gamma * state.p / state.rho);
  state.mach = std::hypot(state.u, state.v) / state.sound_speed;
  if (!finite(state.temperature) || !finite(state.sound_speed) || !finite(state.mach)) {
    throw PhysicsError("primitive thermodynamic completion produced a non-finite value");
  }
  return state;
}

Conserved conserved_from_primitive(const Primitive& state, const GasProperties& gas) {
  const Primitive completed = complete_primitive(state, gas);
  const double kinetic = 0.5 * (completed.u * completed.u + completed.v * completed.v);
  const double total_energy = completed.p / ((gas.gamma - 1.0) * completed.rho) + kinetic;
  Conserved result{completed.rho, completed.rho * completed.u, completed.rho * completed.v,
                   completed.rho * total_energy};
  for (double value : result) require_finite(value, "conservative variable");
  return result;
}

Primitive primitive_from_conserved(const Conserved& state, const GasProperties& gas) {
  validate_gas(gas);
  for (double value : state) require_finite(value, "conservative variable");
  require_positive(state[0], "density");
  const double rho = state[0];
  const double u = state[1] / rho;
  const double v = state[2] / rho;
  const double pressure = pressure_from_conserved(state, gas);
  return complete_primitive({rho, u, v, pressure}, gas);
}

Conserved primitive_to_conservative(const Primitive& state, double gamma, double gas_constant) {
  return conserved_from_primitive(state, {gamma, gas_constant, 0.72});
}

Primitive conservative_to_primitive(const Conserved& state, double gamma, double gas_constant) {
  return primitive_from_conserved(state, {gamma, gas_constant, 0.72});
}

Primitive freestream_primitive(const CaseConfig& config) {
  const GasProperties gas = gas_properties(config);
  require_positive(config.freestream.rho, "freestream density");
  require_positive(config.freestream.pressure, "freestream pressure");
  require_positive(config.freestream.velocity_magnitude, "freestream velocity magnitude");
  require_finite(config.freestream.aoa_degrees, "freestream angle of attack");
  const double angle = config.freestream.aoa_degrees * std::acos(-1.0) / 180.0;
  const double speed = config.freestream.velocity_magnitude;
  return complete_primitive({config.freestream.rho, speed * std::cos(angle),
                             speed * std::sin(angle), config.freestream.pressure}, gas);
}

Conserved freestream_conserved(const CaseConfig& config) {
  return conserved_from_primitive(freestream_primitive(config), gas_properties(config));
}

Conserved euler_normal_flux(const Primitive& state, Vec2 normal, const GasProperties& gas) {
  const Primitive completed = complete_primitive(state, gas);
  validate_normal(normal);
  const double normal_velocity = completed.u * normal.x + completed.v * normal.y;
  const double total_energy = completed.p / (gas.gamma - 1.0) +
                              0.5 * completed.rho *
                                  (completed.u * completed.u + completed.v * completed.v);
  return {completed.rho * normal_velocity,
          completed.rho * completed.u * normal_velocity + completed.p * normal.x,
          completed.rho * completed.v * normal_velocity + completed.p * normal.y,
          (total_energy + completed.p) * normal_velocity};
}

double normal_wave_speed(const Primitive& state, Vec2 normal, const GasProperties& gas) {
  const Primitive completed = complete_primitive(state, gas);
  validate_normal(normal);
  return std::abs(completed.u * normal.x + completed.v * normal.y) +
         completed.a * std::hypot(normal.x, normal.y);
}

Conserved rusanov_flux(const Primitive& left, const Primitive& right, Vec2 normal,
                       const GasProperties& gas, double dissipation_scale) {
  require_positive(dissipation_scale, "Rusanov dissipation scale");
  const Conserved left_conserved = conserved_from_primitive(left, gas);
  const Conserved right_conserved = conserved_from_primitive(right, gas);
  const Conserved left_flux = euler_normal_flux(left, normal, gas);
  const Conserved right_flux = euler_normal_flux(right, normal, gas);
  const double lambda = dissipation_scale *
                        std::max(normal_wave_speed(left, normal, gas),
                                 normal_wave_speed(right, normal, gas));
  Conserved result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = 0.5 * (left_flux[i] + right_flux[i]) -
                0.5 * lambda * (right_conserved[i] - left_conserved[i]);
  }
  return result;
}

Conserved viscous_normal_flux(const Primitive& state, const PrimitiveGradients& gradients,
                              Vec2 normal, const GasProperties& gas, double mu) {
  const Primitive completed = complete_primitive(state, gas);
  validate_normal(normal);
  require_finite(mu, "dynamic viscosity");
  if (mu < 0.0) throw PhysicsError("dynamic viscosity must be nonnegative");
  require_finite(gradients.u.x, "du/dx");
  require_finite(gradients.u.y, "du/dy");
  require_finite(gradients.v.x, "dv/dx");
  require_finite(gradients.v.y, "dv/dy");
  require_finite(gradients.T.x, "dT/dx");
  require_finite(gradients.T.y, "dT/dy");

  const double divergence = gradients.u.x + gradients.v.y;
  const double tau_xx = 2.0 * mu * (gradients.u.x - divergence / 3.0);
  const double tau_yy = 2.0 * mu * (gradients.v.y - divergence / 3.0);
  const double tau_xy = mu * (gradients.u.y + gradients.v.x);
  const double conductivity = mu * gas.gamma * gas.gas_constant /
                              ((gas.gamma - 1.0) * gas.prandtl);
  const double traction_x = tau_xx * normal.x + tau_xy * normal.y;
  const double traction_y = tau_xy * normal.x + tau_yy * normal.y;
  const double heat_term = conductivity * (gradients.T.x * normal.x + gradients.T.y * normal.y);
  return {0.0, traction_x, traction_y, completed.u * traction_x + completed.v * traction_y + heat_term};
}

bool is_physical(const Conserved& state, const GasProperties& gas, double density_floor,
                 double pressure_floor) {
  try {
    validate_gas(gas);
    require_positive(density_floor, "density floor");
    require_positive(pressure_floor, "pressure floor");
    for (double value : state) {
      if (!finite(value)) return false;
    }
    return state[0] >= density_floor && pressure_from_conserved(state, gas) >= pressure_floor;
  } catch (const PhysicsError&) {
    return false;
  }
}

Conserved positivity_safe_update(const Conserved& state, const Conserved& increment,
                                 const GasProperties& gas, double density_floor,
                                 double pressure_floor) {
  validate_gas(gas);
  require_positive(density_floor, "density floor");
  require_positive(pressure_floor, "pressure floor");
  if (!is_physical(state, gas, density_floor, pressure_floor)) {
    throw PhysicsError("positivity-limited update requires a physical starting state");
  }
  for (double value : increment) require_finite(value, "update increment");
  const Conserved candidate = add_scaled(state, increment, 1.0);
  if (is_physical(candidate, gas, density_floor, pressure_floor)) return candidate;

  double low = 0.0;
  double high = 1.0;
  // Pressure is continuous along a finite conservative update wherever rho > 0.
  // Bisection gives the largest safe prefix without relying on a fragile closed form.
  for (int iteration = 0; iteration < 80; ++iteration) {
    const double middle = 0.5 * (low + high);
    if (is_physical(add_scaled(state, increment, middle), gas, density_floor, pressure_floor)) {
      low = middle;
    } else {
      high = middle;
    }
  }
  Conserved result = add_scaled(state, increment, low);
  // Roundoff can place a state infinitesimally beneath a requested floor.
  while (!is_physical(result, gas, density_floor, pressure_floor) && low > 0.0) {
    low *= 0.5;
    result = add_scaled(state, increment, low);
  }
  return result;
}

}  // namespace cfd
