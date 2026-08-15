#include "cfd/SpatialOperator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace cfd {
namespace {

Conserved subtract(const Conserved& inviscid, const Conserved& viscous) {
  Conserved result{};
  for (int variable = 0; variable < 4; ++variable) result[variable] = inviscid[variable] - viscous[variable];
  return result;
}

Vec2 corrected_gradient(Vec2 average, Vec2 displacement, double difference) {
  const double distance_squared = std::max(dot(displacement, displacement), 1.0e-30);
  return average + ((difference - dot(average, displacement)) / distance_squared) * displacement;
}

Vec2 temperature_gradient(const Primitive& value, const ReconstructionData& data,
                          double gas_constant) {
  return (1.0 / (value.rho * gas_constant)) * data.gradient[3] -
         (value.p / (value.rho * value.rho * gas_constant)) * data.gradient[0];
}

}  // namespace

Vec2 tangential_wall_shear_force(Vec2 velocity, Vec2 normal, double wall_distance,
                                 double viscosity, double area) {
  const double normal_length = std::hypot(normal.x, normal.y);
  if (!(normal_length > 0.0) || !(wall_distance > 0.0) || !(area > 0.0) ||
      !(viscosity >= 0.0) || !std::isfinite(area) || !std::isfinite(viscosity)) {
    throw std::runtime_error("invalid wall geometry or viscosity for skin-friction force");
  }
  const Vec2 tangent{-normal.y / normal_length, normal.x / normal_length};
  const double wall_shear = viscosity * dot(velocity, tangent) / wall_distance;
  return (area * wall_shear) * tangent;
}

SpatialOperator::SpatialOperator(const LocalMesh& mesh, const CaseConfig& config,
                                 MPI_Comm communicator)
    : mesh_(mesh),
      config_(config),
      gas_(gas_properties(config)),
      rusanov_scale_(config.run_control.rusanov_dissipation_scale.value_or(1.0)),
      reconstructor_(mesh, config, communicator) {
  if (config.physics.mode == "laminar") {
    viscosity_ = config.freestream.rho * config.freestream.velocity_magnitude *
                 config.reference.reynolds_length / config.physics.reynolds.value();
  }
}

bool SpatialOperator::is_wall(const LocalFace& face) const {
  if (face.right >= 0) return false;
  const auto found = config_.boundary_conditions.find(face.boundary_tag);
  return found != config_.boundary_conditions.end() && found->second != "farfield";
}

PrimitiveGradients SpatialOperator::face_gradients(
    const LocalFace& face, const FaceStates& face_state,
    const std::vector<Conserved>& state,
    const std::vector<ReconstructionData>& data) const {
  const ReconstructionData& left_data = data.at(static_cast<std::size_t>(face.left));
  const Primitive left_center = primitive_from_conserved(state.at(static_cast<std::size_t>(face.left)), gas_);
  Vec2 grad_u = left_data.gradient[1];
  Vec2 grad_v = left_data.gradient[2];
  Vec2 grad_t = temperature_gradient(left_center, left_data, gas_.gas_constant);

  if (face.right >= 0) {
    const ReconstructionData& right_data = data.at(static_cast<std::size_t>(face.right));
    const Primitive right_center = primitive_from_conserved(state.at(static_cast<std::size_t>(face.right)), gas_);
    grad_u = 0.5 * (grad_u + right_data.gradient[1]);
    grad_v = 0.5 * (grad_v + right_data.gradient[2]);
    grad_t = 0.5 * (grad_t + temperature_gradient(right_center, right_data, gas_.gas_constant));
    const Vec2 displacement = mesh_.cells.at(static_cast<std::size_t>(face.right)).center -
                              mesh_.cells.at(static_cast<std::size_t>(face.left)).center;
    grad_u = corrected_gradient(grad_u, displacement, right_center.u - left_center.u);
    grad_v = corrected_gradient(grad_v, displacement, right_center.v - left_center.v);
    grad_t = corrected_gradient(grad_t, displacement, right_center.temperature - left_center.temperature);
  } else {
    const Primitive boundary = reconstructor_.boundary_value(face, left_center);
    const Vec2 displacement = face.center - mesh_.cells.at(static_cast<std::size_t>(face.left)).center;
    const std::string& type = config_.boundary_conditions.at(face.boundary_tag);
    if (type == "no_slip_adiabatic_wall") {
      grad_u = corrected_gradient(grad_u, displacement, boundary.u - left_center.u);
      grad_v = corrected_gradient(grad_v, displacement, boundary.v - left_center.v);
      grad_t = grad_t - dot(grad_t, face.normal) * face.normal;
    } else if (type == "farfield") {
      grad_u = corrected_gradient(grad_u, displacement, boundary.u - left_center.u);
      grad_v = corrected_gradient(grad_v, displacement, boundary.v - left_center.v);
      grad_t = corrected_gradient(grad_t, displacement, boundary.temperature - left_center.temperature);
    }
  }
  return {grad_u, grad_v, grad_t};
}

SpatialEvaluation SpatialOperator::evaluate(
    const std::vector<Conserved>& state,
    std::vector<ReconstructionData>& reconstruction,
    double reconstruction_factor,
    bool refresh_reconstruction) const {
  if (refresh_reconstruction) {
    reconstructor_.compute(state, reconstruction, reconstruction_factor);
  } else if (reconstruction.size() != state.size()) {
    throw std::runtime_error("cannot reuse unavailable reconstruction data");
  }
  SpatialEvaluation result;
  result.residual.assign(static_cast<std::size_t>(mesh_.owned_count), Conserved{});
  result.spectral_radius.assign(static_cast<std::size_t>(mesh_.owned_count), 0.0);

  for (const LocalFace& face : mesh_.faces) {
    const FaceStates values = reconstructor_.face_states(face, state, reconstruction);
    Conserved inviscid =
        rusanov_flux(values.left, values.right, face.normal, gas_, rusanov_scale_);
    Conserved viscous{};
    PrimitiveGradients gradients{};
    Primitive face_primitive = complete_primitive(
        {0.5 * (values.left.rho + values.right.rho),
         0.5 * (values.left.u + values.right.u),
         0.5 * (values.left.v + values.right.v),
         0.5 * (values.left.p + values.right.p)}, gas_);
    if (is_wall(face)) {
      const Primitive wall = reconstructor_.boundary_value(face, values.left);
      inviscid = {0.0, wall.p * face.normal.x, wall.p * face.normal.y, 0.0};
      face_primitive = wall;
    }
    if (viscosity_ > 0.0) {
      gradients = face_gradients(face, values, state, reconstruction);
      if (face.right < 0) face_primitive = reconstructor_.boundary_value(face, values.left);
      viscous = viscous_normal_flux(face_primitive, gradients, face.normal, gas_, viscosity_);
    }
    const Conserved flux = subtract(inviscid, viscous);
    if (face.left < mesh_.owned_count) {
      for (int variable = 0; variable < 4; ++variable) {
        result.residual[static_cast<std::size_t>(face.left)][variable] += flux[variable] * face.area;
      }
      result.spectral_radius[static_cast<std::size_t>(face.left)] +=
          normal_wave_speed(values.left, face.normal, gas_) * face.area;
      if (viscosity_ > 0.0) {
        const LocalCell& cell = mesh_.cells[static_cast<std::size_t>(face.left)];
        result.spectral_radius[static_cast<std::size_t>(face.left)] +=
            4.0 * viscosity_ * face.area * face.area / (values.left.rho * cell.volume);
      }
    }
    if (face.right >= 0 && face.right < mesh_.owned_count) {
      for (int variable = 0; variable < 4; ++variable) {
        result.residual[static_cast<std::size_t>(face.right)][variable] -= flux[variable] * face.area;
      }
      result.spectral_radius[static_cast<std::size_t>(face.right)] +=
          normal_wave_speed(values.right, face.normal, gas_) * face.area;
      if (viscosity_ > 0.0) {
        const LocalCell& cell = mesh_.cells[static_cast<std::size_t>(face.right)];
        result.spectral_radius[static_cast<std::size_t>(face.right)] +=
            4.0 * viscosity_ * face.area * face.area / (values.right.rho * cell.volume);
      }
    }

    if (is_wall(face) && face.left < mesh_.owned_count) {
      const double pressure_x = face_primitive.p * face.normal.x * face.area;
      const double pressure_y = face_primitive.p * face.normal.y * face.area;
      const Primitive cell_value = primitive_from_conserved(
          state.at(static_cast<std::size_t>(face.left)), gas_);
      const Vec2 wall_offset =
          face.center - mesh_.cells.at(static_cast<std::size_t>(face.left)).center;
      const double wall_distance = std::max(std::abs(dot(wall_offset, face.normal)), 1.0e-12);
      // Use the same one-sided tangential wall-normal gradient as surface Cf.
      // This excludes normal viscous stress and keeps integrated skin friction
      // consistent with the submitted wall distribution.
      const Vec2 viscous_force = tangential_wall_shear_force(
          {cell_value.u, cell_value.v}, face.normal, wall_distance, viscosity_, face.area);
      const double viscous_x = viscous_force.x;
      const double viscous_y = viscous_force.y;
      result.forces.pressure_drag += pressure_x;
      result.forces.pressure_lift += pressure_y;
      result.forces.viscous_drag += viscous_x;
      result.forces.viscous_lift += viscous_y;
      const double rx = face.center.x - config_.reference.moment_center[0];
      const double ry = face.center.y - config_.reference.moment_center[1];
      result.forces.moment_z += rx * (pressure_y + viscous_y) - ry * (pressure_x + viscous_x);
    }
  }
  return result;
}

}  // namespace cfd
