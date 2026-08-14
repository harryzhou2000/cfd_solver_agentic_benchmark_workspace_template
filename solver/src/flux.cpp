#include "cfd/flux.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cfd {

Conservative euler_normal_flux(const Conservative& state, const Primitive& primitive,
                               const Vec2& unit_normal) {
  const double normal_velocity = primitive.u * unit_normal.x + primitive.v * unit_normal.y;
  return Conservative{state[0] * normal_velocity,
                      state[1] * normal_velocity + primitive.p * unit_normal.x,
                      state[2] * normal_velocity + primitive.p * unit_normal.y,
                      (state[3] + primitive.p) * normal_velocity};
}

ConservativeJacobian euler_normal_jacobian(
    const Primitive& state, const Vec2& unit_normal, double gamma) {
  ConservativeJacobian matrix{};
  const double velocity_squared = state.u * state.u + state.v * state.v;
  const double normal_velocity =
      state.u * unit_normal.x + state.v * unit_normal.y;
  const double enthalpy = gamma * state.p / ((gamma - 1.0) * state.rho) +
                          0.5 * velocity_squared;
  matrix[1] = unit_normal.x;
  matrix[2] = unit_normal.y;
  matrix[4] = -state.u * normal_velocity +
              0.5 * (gamma - 1.0) * velocity_squared * unit_normal.x;
  matrix[5] = normal_velocity + (2.0 - gamma) * state.u * unit_normal.x;
  matrix[6] = state.u * unit_normal.y -
              (gamma - 1.0) * state.v * unit_normal.x;
  matrix[7] = (gamma - 1.0) * unit_normal.x;
  matrix[8] = -state.v * normal_velocity +
              0.5 * (gamma - 1.0) * velocity_squared * unit_normal.y;
  matrix[9] = state.v * unit_normal.x -
              (gamma - 1.0) * state.u * unit_normal.y;
  matrix[10] = normal_velocity + (2.0 - gamma) * state.v * unit_normal.y;
  matrix[11] = (gamma - 1.0) * unit_normal.y;
  matrix[12] = normal_velocity *
               (0.5 * (gamma - 1.0) * velocity_squared - enthalpy);
  matrix[13] = enthalpy * unit_normal.x -
               (gamma - 1.0) * state.u * normal_velocity;
  matrix[14] = enthalpy * unit_normal.y -
               (gamma - 1.0) * state.v * normal_velocity;
  matrix[15] = gamma * normal_velocity;
  return matrix;
}

RusanovFaceJacobian frozen_rusanov_face_jacobian(
    const Conservative& left, const Conservative& right,
    const Vec2& unit_normal, double face_length,
    const CaloricallyPerfectGas& gas, double dissipation_scale) {
  if (!(face_length > 0.0) || !std::isfinite(face_length)) {
    throw std::invalid_argument("Rusanov face length must be finite and positive");
  }
  if (!(dissipation_scale >= 0.0) || !std::isfinite(dissipation_scale)) {
    throw std::invalid_argument("Rusanov dissipation scale must be finite and nonnegative");
  }
  const Primitive wl = gas.primitive(left);
  const Primitive wr = gas.primitive(right);
  const double sl = std::abs(wl.u * unit_normal.x + wl.v * unit_normal.y) + wl.a;
  const double sr = std::abs(wr.u * unit_normal.x + wr.v * unit_normal.y) + wr.a;
  RusanovFaceJacobian result;
  result.frozen_wave_speed = std::max(sl, sr);
  const ConservativeJacobian al =
      euler_normal_jacobian(wl, unit_normal, gas.gamma());
  const ConservativeJacobian ar =
      euler_normal_jacobian(wr, unit_normal, gas.gamma());
  const double dissipative = dissipation_scale * result.frozen_wave_speed;
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      const std::size_t entry = row * 4U + column;
      const double identity = row == column ? dissipative : 0.0;
      result.left_left[entry] = 0.5 * face_length * (al[entry] + identity);
      result.left_right[entry] = 0.5 * face_length * (ar[entry] - identity);
      result.right_left[entry] = -result.left_left[entry];
      result.right_right[entry] = -result.left_right[entry];
    }
  }
  return result;
}

NumericalFlux rusanov_flux(const Conservative& left, const Conservative& right,
                           const Vec2& unit_normal, const CaloricallyPerfectGas& gas,
                           double dissipation_scale) {
  if (!(dissipation_scale >= 0.0) || !std::isfinite(dissipation_scale)) {
    throw std::invalid_argument("Rusanov dissipation scale must be finite and nonnegative");
  }
  const Primitive wl = gas.primitive(left);
  const Primitive wr = gas.primitive(right);
  const Conservative fl = euler_normal_flux(left, wl, unit_normal);
  const Conservative fr = euler_normal_flux(right, wr, unit_normal);
  const double sl = std::abs(wl.u * unit_normal.x + wl.v * unit_normal.y) + wl.a;
  const double sr = std::abs(wr.u * unit_normal.x + wr.v * unit_normal.y) + wr.a;
  const double wave = std::max(sl, sr);
  NumericalFlux result;
  result.spectral_radius = wave;
  for (std::size_t component = 0; component < result.value.size(); ++component) {
    result.value[component] = 0.5 * (fl[component] + fr[component]) -
                              0.5 * dissipation_scale * wave *
                                  (right[component] - left[component]);
  }
  return result;
}

Conservative pressure_wall_flux(double pressure, const Vec2& outward_fluid_normal) {
  if (!(pressure > 0.0) || !std::isfinite(pressure)) {
    throw std::domain_error("wall pressure must be finite and positive");
  }
  return Conservative{0.0, pressure * outward_fluid_normal.x,
                      pressure * outward_fluid_normal.y, 0.0};
}

ViscousFaceFlux viscous_flux(const Primitive& face_state,
                             const VelocityTemperatureGradients& gradient,
                             const Vec2& unit_normal, double viscosity,
                             const CaloricallyPerfectGas& gas) {
  if (!(viscosity >= 0.0) || !std::isfinite(viscosity)) {
    throw std::invalid_argument("viscosity must be finite and nonnegative");
  }
  const double divergence = gradient.u.x + gradient.v.y;
  const double tau_xx = 2.0 * viscosity * gradient.u.x -
                        (2.0 / 3.0) * viscosity * divergence;
  const double tau_yy = 2.0 * viscosity * gradient.v.y -
                        (2.0 / 3.0) * viscosity * divergence;
  const double tau_xy = viscosity * (gradient.u.y + gradient.v.x);
  const Vec2 traction{tau_xx * unit_normal.x + tau_xy * unit_normal.y,
                      tau_xy * unit_normal.x + tau_yy * unit_normal.y};
  const double normal_stress = dot(traction, unit_normal);
  const Vec2 tangential = traction - normal_stress * unit_normal;
  const double conductivity = viscosity * gas.cp() / gas.prandtl();
  const double conducted_heat = conductivity * dot(gradient.temperature, unit_normal);
  ViscousFaceFlux result;
  result.traction = traction;
  result.tangential_traction = tangential;
  result.conductivity = conductivity;
  result.value = Conservative{0.0, traction.x, traction.y,
                              face_state.u * traction.x + face_state.v * traction.y +
                                  conducted_heat};
  return result;
}

ViscousFaceFlux no_slip_adiabatic_wall_flux(
    const Primitive& cell_state, VelocityTemperatureGradients cell_gradient,
    const Vec2& outward_fluid_normal, double center_to_wall_normal_distance,
    double viscosity, const CaloricallyPerfectGas& gas) {
  if (!(center_to_wall_normal_distance > 0.0) ||
      !std::isfinite(center_to_wall_normal_distance)) {
    throw std::invalid_argument("center-to-wall normal distance must be positive");
  }
  const double du_dn = -cell_state.u / center_to_wall_normal_distance;
  const double dv_dn = -cell_state.v / center_to_wall_normal_distance;
  cell_gradient.u += (du_dn - dot(cell_gradient.u, outward_fluid_normal)) *
                     outward_fluid_normal;
  cell_gradient.v += (dv_dn - dot(cell_gradient.v, outward_fluid_normal)) *
                     outward_fluid_normal;
  cell_gradient.temperature -= dot(cell_gradient.temperature, outward_fluid_normal) *
                               outward_fluid_normal;
  Primitive wall = cell_state;
  wall.u = 0.0;
  wall.v = 0.0;
  ViscousFaceFlux result =
      viscous_flux(wall, cell_gradient, outward_fluid_normal, viscosity, gas);
  // The stationary adiabatic wall can exchange neither work nor heat.
  result.value[3] = 0.0;
  return result;
}

}  // namespace cfd
