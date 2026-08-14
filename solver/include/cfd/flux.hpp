#pragma once

#include "cfd/gas.hpp"
#include "cfd/types.hpp"

namespace cfd {

using ConservativeJacobian = std::array<double, 16>;

struct RusanovFaceJacobian {
  ConservativeJacobian left_left{};
  ConservativeJacobian left_right{};
  ConservativeJacobian right_left{};
  ConservativeJacobian right_right{};
  double frozen_wave_speed{};
};

struct NumericalFlux {
  Conservative value{};
  double spectral_radius{};
};

Conservative euler_normal_flux(const Conservative& state, const Primitive& primitive,
                               const Vec2& unit_normal);
ConservativeJacobian euler_normal_jacobian(
    const Primitive& state, const Vec2& unit_normal, double gamma);
// Jacobian of the integrated first-order Rusanov face residual with the
// spectral radius frozen at the supplied left/right states. Rows correspond
// to (left residual, right residual), columns to (left state, right state).
RusanovFaceJacobian frozen_rusanov_face_jacobian(
    const Conservative& left, const Conservative& right,
    const Vec2& unit_normal, double face_length,
    const CaloricallyPerfectGas& gas, double dissipation_scale = 1.0);
NumericalFlux rusanov_flux(const Conservative& left, const Conservative& right,
                           const Vec2& unit_normal, const CaloricallyPerfectGas& gas,
                           double dissipation_scale = 1.0);
Conservative pressure_wall_flux(double pressure, const Vec2& outward_fluid_normal);

struct ViscousFaceFlux {
  Conservative value{};
  Vec2 traction{};            // tau dot outward-fluid normal
  Vec2 tangential_traction{}; // normal stress deliberately removed
  double conductivity{};
};

ViscousFaceFlux viscous_flux(const Primitive& face_state,
                             const VelocityTemperatureGradients& gradient,
                             const Vec2& unit_normal, double viscosity,
                             const CaloricallyPerfectGas& gas);
ViscousFaceFlux no_slip_adiabatic_wall_flux(
    const Primitive& cell_state, VelocityTemperatureGradients cell_gradient,
    const Vec2& outward_fluid_normal, double center_to_wall_normal_distance,
    double viscosity, const CaloricallyPerfectGas& gas);

}  // namespace cfd
