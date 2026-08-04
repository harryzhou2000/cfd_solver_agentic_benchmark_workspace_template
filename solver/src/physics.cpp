#include "cfd/physics.hpp"

#include <algorithm>
#include <limits>

namespace cfd {

Primitive PerfectGas::primitive(const Conserved& state) const {
  Primitive result;
  result.rho = std::max(state[0], kDensityFloor);
  result.u = state[1] / result.rho;
  result.v = state[2] / result.rho;
  const double kinetic = 0.5 * result.rho * (result.u * result.u + result.v * result.v);
  result.pressure = std::max((gas_.gamma - 1.0) * (state[3] - kinetic), kPressureFloor);
  result.temperature = result.pressure / (result.rho * gas_.gas_constant);
  result.sound_speed = std::sqrt(gas_.gamma * result.pressure / result.rho);
  return result;
}

Conserved PerfectGas::conserved(const Primitive& input) const {
  Primitive state = input;
  state.rho = std::max(state.rho, kDensityFloor);
  state.pressure = std::max(state.pressure, kPressureFloor);
  const double energy = state.pressure / (gas_.gamma - 1.0) +
                        0.5 * state.rho * (state.u * state.u + state.v * state.v);
  return {state.rho, state.rho * state.u, state.rho * state.v, energy};
}

Conserved PerfectGas::freestream_state(const Freestream& freestream) const {
  constexpr double pi = 3.141592653589793238462643383279502884;
  const double angle = freestream.aoa_degrees * pi / 180.0;
  Primitive state;
  state.rho = freestream.rho;
  state.u = freestream.velocity_magnitude * std::cos(angle);
  state.v = freestream.velocity_magnitude * std::sin(angle);
  state.pressure = freestream.pressure;
  return conserved(state);
}

bool PerfectGas::physical(const Conserved& state) const {
  if (!std::isfinite(state[0]) || !std::isfinite(state[1]) || !std::isfinite(state[2]) ||
      !std::isfinite(state[3]) || state[0] <= kDensityFloor) {
    return false;
  }
  const double u = state[1] / state[0];
  const double v = state[2] / state[0];
  const double pressure = (gas_.gamma - 1.0) *
                          (state[3] - 0.5 * state[0] * (u * u + v * v));
  return std::isfinite(pressure) && pressure > kPressureFloor;
}

Conserved PerfectGas::enforce_physical(const Conserved& candidate, const Conserved& fallback) const {
  if (physical(candidate)) {
    return candidate;
  }
  if (physical(fallback)) {
    return fallback;
  }
  Primitive safe;
  safe.rho = std::max(kDensityFloor * 10.0, 1.0);
  safe.pressure = std::max(kPressureFloor * 10.0, 1.0);
  return conserved(safe);
}

namespace {

Conserved normal_euler_flux(const PerfectGas& gas, const Primitive& state, const Vec2 normal) {
  const Conserved conserved_state = gas.conserved(state);
  const double normal_velocity = state.u * normal.x + state.v * normal.y;
  return {conserved_state[0] * normal_velocity,
          conserved_state[1] * normal_velocity + state.pressure * normal.x,
          conserved_state[2] * normal_velocity + state.pressure * normal.y,
          (conserved_state[3] + state.pressure) * normal_velocity};
}

}  // namespace

Conserved rusanov_flux(const PerfectGas& gas, const Primitive& left, const Primitive& right,
                       const Vec2 unit_normal, const double dissipation_scale) {
  const Conserved ul = gas.conserved(left);
  const Conserved ur = gas.conserved(right);
  const Conserved fl = normal_euler_flux(gas, left, unit_normal);
  const Conserved fr = normal_euler_flux(gas, right, unit_normal);
  const double un_left = left.u * unit_normal.x + left.v * unit_normal.y;
  const double un_right = right.u * unit_normal.x + right.v * unit_normal.y;
  // Use the conventional local Lax--Friedrichs spectral radius.  The same
  // acoustic radius is used by the point-implicit diagonal, keeping the
  // nonlinear residual and its stability estimate consistent at low Mach.
  const double signal = dissipation_scale *
                        std::max(std::abs(un_left) + left.sound_speed,
                                 std::abs(un_right) + right.sound_speed);
  Conserved result{};
  for (std::size_t component = 0; component < result.size(); ++component) {
    result[component] = 0.5 * (fl[component] + fr[component]) -
                        0.5 * signal * (ur[component] - ul[component]);
  }
  return result;
}

Conserved hllc_flux(const PerfectGas& gas, const Primitive& left, const Primitive& right,
                    const Vec2 unit_normal, const double dissipation_scale) {
  const Conserved ul = gas.conserved(left);
  const Conserved ur = gas.conserved(right);
  const Conserved fl = normal_euler_flux(gas, left, unit_normal);
  const Conserved fr = normal_euler_flux(gas, right, unit_normal);
  const double un_left = left.u * unit_normal.x + left.v * unit_normal.y;
  const double un_right = right.u * unit_normal.x + right.v * unit_normal.y;
  const double acoustic = std::max(dissipation_scale, 1.0);
  const double speed_left = std::min(un_left - acoustic * left.sound_speed,
                                     un_right - acoustic * right.sound_speed);
  const double speed_right = std::max(un_left + acoustic * left.sound_speed,
                                      un_right + acoustic * right.sound_speed);
  const double denominator = left.rho * (speed_left - un_left) -
                             right.rho * (speed_right - un_right);
  if (!std::isfinite(speed_left) || !std::isfinite(speed_right) ||
      std::abs(denominator) <= kPressureFloor) {
    return rusanov_flux(gas, left, right, unit_normal, dissipation_scale);
  }
  const double contact_speed =
      (right.pressure - left.pressure + left.rho * un_left * (speed_left - un_left) -
       right.rho * un_right * (speed_right - un_right)) /
      denominator;
  if (!std::isfinite(contact_speed)) {
    return rusanov_flux(gas, left, right, unit_normal, dissipation_scale);
  }
  if (speed_left >= 0.0) {
    return fl;
  }
  if (speed_right <= 0.0) {
    return fr;
  }

  const auto star_state = [&](const Primitive& state, const Conserved& conserved_state,
                              const double normal_velocity, const double wave_speed) {
    const double wave_gap = wave_speed - contact_speed;
    const double density_star = state.rho * (wave_speed - normal_velocity) / wave_gap;
    const double pressure_star =
        state.pressure + state.rho * (wave_speed - normal_velocity) * (contact_speed - normal_velocity);
    const double velocity_x = state.u + (contact_speed - normal_velocity) * unit_normal.x;
    const double velocity_y = state.v + (contact_speed - normal_velocity) * unit_normal.y;
    const double energy_star =
        ((wave_speed - normal_velocity) * conserved_state[3] - state.pressure * normal_velocity +
         pressure_star * contact_speed) /
        wave_gap;
    return Conserved{density_star, density_star * velocity_x, density_star * velocity_y, energy_star};
  };
  const auto valid_star = [&](const Conserved& value) { return gas.physical(value); };
  if (contact_speed >= 0.0) {
    const Conserved star = star_state(left, ul, un_left, speed_left);
    if (!valid_star(star)) {
      return rusanov_flux(gas, left, right, unit_normal, dissipation_scale);
    }
    Conserved result{};
    for (std::size_t component = 0; component < result.size(); ++component) {
      result[component] = fl[component] + speed_left * (star[component] - ul[component]);
    }
    return result;
  }
  const Conserved star = star_state(right, ur, un_right, speed_right);
  if (!valid_star(star)) {
    return rusanov_flux(gas, left, right, unit_normal, dissipation_scale);
  }
  Conserved result{};
  for (std::size_t component = 0; component < result.size(); ++component) {
    result[component] = fr[component] + speed_right * (star[component] - ur[component]);
  }
  return result;
}

Primitive reflected_slip_state(const Primitive& inside, const Vec2 unit_normal) {
  Primitive result = inside;
  const double normal_velocity = inside.u * unit_normal.x + inside.v * unit_normal.y;
  result.u = inside.u - 2.0 * normal_velocity * unit_normal.x;
  result.v = inside.v - 2.0 * normal_velocity * unit_normal.y;
  return result;
}

Primitive reflected_no_slip_adiabatic_state(const Primitive& inside) {
  Primitive result = inside;
  result.u = -inside.u;
  result.v = -inside.v;
  return result;
}

}  // namespace cfd
