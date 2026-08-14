#include "cfd/boundary.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {
namespace {

Primitive safe_fallback(const Primitive& interior, const Primitive& freestream,
                        const CaloricallyPerfectGas& gas) noexcept {
  if (gas.admissible(freestream)) return freestream;
  return interior;
}

}  // namespace

Primitive boundary_exterior_state(BoundaryCondition condition,
                                  const Primitive& interior,
                                  const Primitive& freestream,
                                  const Vec2& outward_fluid_normal,
                                  const CaloricallyPerfectGas& gas) noexcept {
  const double normal_length = norm(outward_fluid_normal);
  if (!gas.admissible(interior) || !gas.admissible(freestream) ||
      !(normal_length > 0.0) || !std::isfinite(normal_length)) {
    return safe_fallback(interior, freestream, gas);
  }
  const Vec2 normal = outward_fluid_normal / normal_length;
  const Vec2 tangent{-normal.y, normal.x};
  const double interior_normal = interior.u * normal.x + interior.v * normal.y;

  if (condition == BoundaryCondition::slip_wall) {
    try {
      return gas.complete(interior.rho,
                          interior.u - 2.0 * interior_normal * normal.x,
                          interior.v - 2.0 * interior_normal * normal.y,
                          interior.p);
    } catch (...) {
      return interior;
    }
  }
  if (condition == BoundaryCondition::no_slip_adiabatic_wall) {
    try {
      return gas.complete(interior.rho, -interior.u, -interior.v, interior.p);
    } catch (...) {
      return interior;
    }
  }

  // All characteristics enter at supersonic inflow and leave at supersonic
  // outflow. The local interior state determines which branch reaches the face.
  if (interior_normal <= -interior.a) return freestream;
  if (interior_normal >= interior.a) return interior;

  const double gamma = gas.gamma();
  const double gamma_minus_one = gamma - 1.0;
  const double freestream_normal =
      freestream.u * normal.x + freestream.v * normal.y;
  const double outgoing_plus =
      interior_normal + 2.0 * interior.a / gamma_minus_one;
  const double incoming_minus =
      freestream_normal - 2.0 * freestream.a / gamma_minus_one;
  const double normal_velocity = 0.5 * (outgoing_plus + incoming_minus);
  const double sound_speed =
      0.25 * gamma_minus_one * (outgoing_plus - incoming_minus);
  if (!(sound_speed > 0.0) || !std::isfinite(sound_speed) ||
      !std::isfinite(normal_velocity)) {
    return freestream;
  }

  // At subsonic outflow entropy and tangential velocity leave the domain. At
  // subsonic inflow they enter from the prescribed freestream.
  const Primitive& transported = normal_velocity >= 0.0 ? interior : freestream;
  const double entropy = transported.p / std::pow(transported.rho, gamma);
  if (!(entropy > 0.0) || !std::isfinite(entropy)) return freestream;
  const double density_base = sound_speed * sound_speed / (gamma * entropy);
  if (!(density_base > 0.0) || !std::isfinite(density_base)) return freestream;
  const double density = std::pow(density_base, 1.0 / gamma_minus_one);
  const double pressure = entropy * std::pow(density, gamma);
  const double tangential_velocity =
      transported.u * tangent.x + transported.v * tangent.y;
  const double u = normal_velocity * normal.x + tangential_velocity * tangent.x;
  const double v = normal_velocity * normal.y + tangential_velocity * tangent.y;
  try {
    const Primitive candidate = gas.complete(density, u, v, pressure);
    return gas.admissible(candidate) ? candidate : freestream;
  } catch (...) {
    return freestream;
  }
}

}  // namespace cfd
