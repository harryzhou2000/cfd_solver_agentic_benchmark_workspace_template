#include "cfd/Reconstruction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace cfd {
namespace {

std::array<double, 4> components(const Primitive& value) {
  return {value.rho, value.u, value.v, value.p};
}

Primitive with_components(const std::array<double, 4>& q, double gamma, double gas_constant) {
  Primitive value{};
  value.rho = q[0];
  value.u = q[1];
  value.v = q[2];
  value.p = q[3];
  value.temperature = value.p / (value.rho * gas_constant);
  value.sound_speed = std::sqrt(gamma * value.p / value.rho);
  value.mach = std::hypot(value.u, value.v) / value.sound_speed;
  return value;
}

double limiter_factor(double center, double minimum, double maximum, double extrapolation) {
  constexpr double epsilon = 1.0e-14;
  if (extrapolation > epsilon) return std::min(1.0, (maximum - center) / extrapolation);
  if (extrapolation < -epsilon) return std::min(1.0, (minimum - center) / extrapolation);
  return 1.0;
}

}  // namespace

Reconstructor::Reconstructor(const LocalMesh& mesh, const CaseConfig& config,
                             MPI_Comm communicator)
    : mesh_(mesh), config_(config), halo_(mesh, communicator) {}

Primitive Reconstructor::freestream() const {
  const double angle = config_.freestream.aoa_degrees * std::acos(-1.0) / 180.0;
  Primitive value{};
  value.rho = config_.freestream.rho;
  value.u = config_.freestream.velocity_magnitude * std::cos(angle);
  value.v = config_.freestream.velocity_magnitude * std::sin(angle);
  value.p = config_.freestream.pressure;
  value.temperature = value.p / (value.rho * config_.gas.gas_constant);
  value.sound_speed = std::sqrt(config_.gas.gamma * value.p / value.rho);
  value.mach = std::hypot(value.u, value.v) / value.sound_speed;
  return value;
}

Primitive Reconstructor::characteristic_farfield(const LocalFace& face,
                                                  const Primitive& interior) const {
  const Primitive infinity = freestream();
  const double gamma = config_.gas.gamma;
  const Vec2 tangent{-face.normal.y, face.normal.x};
  const double interior_normal = interior.u * face.normal.x + interior.v * face.normal.y;
  const double infinity_normal = infinity.u * face.normal.x + infinity.v * face.normal.y;
  if (interior_normal >= interior.sound_speed) return interior;
  if (interior_normal <= -interior.sound_speed) return infinity;

  const double outgoing = interior_normal + 2.0 * interior.sound_speed / (gamma - 1.0);
  const double incoming = infinity_normal - 2.0 * infinity.sound_speed / (gamma - 1.0);
  const double normal_velocity = 0.5 * (outgoing + incoming);
  const double sound_speed = 0.25 * (gamma - 1.0) * (outgoing - incoming);
  const Primitive& entropy_source = normal_velocity < 0.0 ? infinity : interior;
  const double tangential_velocity = entropy_source.u * tangent.x + entropy_source.v * tangent.y;
  const double entropy = entropy_source.p / std::pow(entropy_source.rho, gamma);
  const double rho = std::pow(sound_speed * sound_speed / (gamma * entropy), 1.0 / (gamma - 1.0));
  Primitive boundary{};
  boundary.rho = rho;
  boundary.u = normal_velocity * face.normal.x + tangential_velocity * tangent.x;
  boundary.v = normal_velocity * face.normal.y + tangential_velocity * tangent.y;
  boundary.p = entropy * std::pow(rho, gamma);
  return complete_primitive(boundary, gas_properties(config_));
}

Primitive Reconstructor::boundary_value(const LocalFace& face, const Primitive& interior) const {
  const auto found = config_.boundary_conditions.find(face.boundary_tag);
  if (found == config_.boundary_conditions.end()) {
    throw std::runtime_error("unknown boundary family: " + face.boundary_tag);
  }
  if (found->second == "farfield") return characteristic_farfield(face, interior);
  Primitive value = interior;
  if (found->second == "slip_wall") {
    const double normal_velocity = interior.u * face.normal.x + interior.v * face.normal.y;
    value.u -= normal_velocity * face.normal.x;
    value.v -= normal_velocity * face.normal.y;
  } else if (found->second == "no_slip_adiabatic_wall") {
    value.u = 0.0;
    value.v = 0.0;
  } else {
    throw std::runtime_error("unsupported boundary type: " + found->second);
  }
  value.mach = std::hypot(value.u, value.v) / value.sound_speed;
  return value;
}

Primitive Reconstructor::boundary_ghost(const LocalFace& face, const Primitive& interior) const {
  const auto found = config_.boundary_conditions.find(face.boundary_tag);
  if (found == config_.boundary_conditions.end()) {
    throw std::runtime_error("unknown boundary family: " + face.boundary_tag);
  }
  if (found->second == "farfield") return characteristic_farfield(face, interior);
  Primitive ghost = interior;
  if (found->second == "slip_wall") {
    const double normal_velocity = interior.u * face.normal.x + interior.v * face.normal.y;
    ghost.u -= 2.0 * normal_velocity * face.normal.x;
    ghost.v -= 2.0 * normal_velocity * face.normal.y;
  } else if (found->second == "no_slip_adiabatic_wall") {
    ghost.u = -interior.u;
    ghost.v = -interior.v;
  } else {
    throw std::runtime_error("unsupported boundary type: " + found->second);
  }
  ghost.mach = std::hypot(ghost.u, ghost.v) / ghost.sound_speed;
  return ghost;
}

void Reconstructor::compute(const std::vector<Conserved>& state,
                            std::vector<ReconstructionData>& data,
                            double reconstruction_factor) const {
  if (state.size() != mesh_.cells.size()) throw std::runtime_error("state/mesh size mismatch");
  if (reconstruction_factor < 0.0 || reconstruction_factor > 1.0) {
    throw std::runtime_error("reconstruction factor must be in [0,1]");
  }
  data.assign(state.size(), ReconstructionData{});
  std::vector<Primitive> primitive(state.size());
  for (std::size_t i = 0; i < state.size(); ++i) {
    primitive[i] = conservative_to_primitive(state[i], config_.gas.gamma, config_.gas.gas_constant);
  }

  struct LeastSquares {
    double xx = 0.0, xy = 0.0, yy = 0.0;
    std::array<double, 4> bx{}, by{};
  };
  std::vector<LeastSquares> systems(static_cast<std::size_t>(mesh_.owned_count));
  std::vector<std::array<double, 4>> minima(static_cast<std::size_t>(mesh_.owned_count));
  std::vector<std::array<double, 4>> maxima(static_cast<std::size_t>(mesh_.owned_count));
  for (int cell = 0; cell < mesh_.owned_count; ++cell) {
    minima[static_cast<std::size_t>(cell)] = components(primitive[static_cast<std::size_t>(cell)]);
    maxima[static_cast<std::size_t>(cell)] = minima[static_cast<std::size_t>(cell)];
  }

  auto add_neighbor = [&](int cell, Vec2 displacement, const Primitive& neighbor) {
    if (cell < 0 || cell >= mesh_.owned_count) return;
    LeastSquares& system = systems[static_cast<std::size_t>(cell)];
    const double distance_squared = std::max(dot(displacement, displacement), 1.0e-30);
    const double weight = 1.0 / distance_squared;
    system.xx += weight * displacement.x * displacement.x;
    system.xy += weight * displacement.x * displacement.y;
    system.yy += weight * displacement.y * displacement.y;
    const auto center = components(primitive[static_cast<std::size_t>(cell)]);
    const auto other = components(neighbor);
    for (int variable = 0; variable < 4; ++variable) {
      const double difference = other[variable] - center[variable];
      system.bx[variable] += weight * displacement.x * difference;
      system.by[variable] += weight * displacement.y * difference;
      minima[static_cast<std::size_t>(cell)][variable] =
          std::min(minima[static_cast<std::size_t>(cell)][variable], other[variable]);
      maxima[static_cast<std::size_t>(cell)][variable] =
          std::max(maxima[static_cast<std::size_t>(cell)][variable], other[variable]);
    }
  };

  for (const LocalFace& face : mesh_.faces) {
    if (face.right >= 0) {
      const Vec2 left_to_right = mesh_.cells[static_cast<std::size_t>(face.right)].center -
                                 mesh_.cells[static_cast<std::size_t>(face.left)].center;
      add_neighbor(face.left, left_to_right, primitive[static_cast<std::size_t>(face.right)]);
      add_neighbor(face.right, -1.0 * left_to_right, primitive[static_cast<std::size_t>(face.left)]);
    } else {
      const Vec2 displacement = 2.0 * (face.center - mesh_.cells[static_cast<std::size_t>(face.left)].center);
      add_neighbor(face.left, displacement,
                   boundary_ghost(face, primitive[static_cast<std::size_t>(face.left)]));
    }
  }

  for (int cell = 0; cell < mesh_.owned_count; ++cell) {
    const LeastSquares& system = systems[static_cast<std::size_t>(cell)];
    const double determinant = system.xx * system.yy - system.xy * system.xy;
    if (std::abs(determinant) <= 1.0e-20) continue;
    for (int variable = 0; variable < 4; ++variable) {
      data[static_cast<std::size_t>(cell)].gradient[variable] =
          {(system.yy * system.bx[variable] - system.xy * system.by[variable]) / determinant,
           (system.xx * system.by[variable] - system.xy * system.bx[variable]) / determinant};
    }
  }

  for (const LocalFace& face : mesh_.faces) {
    const std::array<int, 2> sides{face.left, face.right};
    for (const int cell : sides) {
      if (cell < 0 || cell >= mesh_.owned_count) continue;
      const Vec2 displacement = face.center - mesh_.cells[static_cast<std::size_t>(cell)].center;
      const auto center = components(primitive[static_cast<std::size_t>(cell)]);
      for (int variable = 0; variable < 4; ++variable) {
        const double extrapolation =
            dot(data[static_cast<std::size_t>(cell)].gradient[variable], displacement);
        data[static_cast<std::size_t>(cell)].limiter[variable] =
            std::min(data[static_cast<std::size_t>(cell)].limiter[variable],
                     limiter_factor(center[variable], minima[static_cast<std::size_t>(cell)][variable],
                                    maxima[static_cast<std::size_t>(cell)][variable], extrapolation));
      }
    }
  }
  for (int cell = 0; cell < mesh_.owned_count; ++cell) {
    for (double& limiter : data[static_cast<std::size_t>(cell)].limiter) {
      limiter *= reconstruction_factor;
    }
  }
  halo_.exchange(data, 5202);
}

FaceStates Reconstructor::face_states(const LocalFace& face, const std::vector<Conserved>& state,
                                      const std::vector<ReconstructionData>& data) const {
  auto extrapolate = [&](int cell) {
    const Primitive center = conservative_to_primitive(
        state.at(static_cast<std::size_t>(cell)), config_.gas.gamma, config_.gas.gas_constant);
    auto q = components(center);
    const Vec2 displacement = face.center - mesh_.cells.at(static_cast<std::size_t>(cell)).center;
    for (int variable = 0; variable < 4; ++variable) {
      q[variable] += data.at(static_cast<std::size_t>(cell)).limiter[variable] *
                     dot(data.at(static_cast<std::size_t>(cell)).gradient[variable], displacement);
    }
    if (!(q[0] > 1.0e-12) || !(q[3] > 1.0e-12)) q = components(center);
    return with_components(q, config_.gas.gamma, config_.gas.gas_constant);
  };

  FaceStates result;
  result.left = extrapolate(face.left);
  result.right = face.right >= 0 ? extrapolate(face.right) : boundary_ghost(face, result.left);
  return result;
}

}  // namespace cfd
