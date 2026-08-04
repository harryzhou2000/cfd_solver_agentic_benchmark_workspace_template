#include "cfd/solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace cfd {
namespace {

constexpr double kTiny = 1.0e-14;
constexpr double kPi = 3.141592653589793238462643383279502884;

double component(const Primitive& value, const int index) {
  switch (index) {
    case 0:
      return value.rho;
    case 1:
      return value.u;
    case 2:
      return value.v;
    case 3:
      return value.temperature;
    default:
      throw std::logic_error("invalid primitive component");
  }
}

void set_component(Primitive& value, const int index, const double number) {
  switch (index) {
    case 0:
      value.rho = number;
      return;
    case 1:
      value.u = number;
      return;
    case 2:
      value.v = number;
      return;
    case 3:
      value.temperature = number;
      return;
    default:
      throw std::logic_error("invalid primitive component");
  }
}

Vec2 scale(const Vec2 value, const double factor) { return {factor * value.x, factor * value.y}; }
Vec2 add(const Vec2 first, const Vec2 second) { return {first.x + second.x, first.y + second.y}; }
Vec2 subtract(const Vec2 first, const Vec2 second) { return {first.x - second.x, first.y - second.y}; }
double inner(const Vec2 first, const Vec2 second) { return first.x * second.x + first.y * second.y; }
Vec2 tangent(const Vec2 normal) { return {-normal.y, normal.x}; }

Primitive characteristic_farfield(const PerfectGas& gas, const Primitive& inside,
                                  const Primitive& freestream, const Vec2 outward_normal) {
  const GasModel& parameters = gas.parameters();
  const double inside_normal = inner({inside.u, inside.v}, outward_normal);
  const double free_normal = inner({freestream.u, freestream.v}, outward_normal);
  const Vec2 wall_tangent = tangent(outward_normal);
  if (inside_normal >= inside.sound_speed) {
    return inside;  // supersonic outflow
  }
  if (inside_normal <= -inside.sound_speed) {
    return freestream;  // supersonic inflow
  }
  const double invariant_out = inside_normal + 2.0 * inside.sound_speed / (parameters.gamma - 1.0);
  const double invariant_in = free_normal - 2.0 * freestream.sound_speed / (parameters.gamma - 1.0);
  const double normal_velocity = 0.5 * (invariant_out + invariant_in);
  const double sound_speed = 0.25 * (parameters.gamma - 1.0) * (invariant_out - invariant_in);
  if (!(sound_speed > kTiny) || !std::isfinite(sound_speed)) {
    return freestream;
  }
  // The entropy and tangential velocity are convected characteristics.  A
  // hard inflow/outflow switch at u_n=0 is numerically disruptive on the
  // nominally tangential top/bottom parts of an external farfield: tiny
  // acoustic perturbations would alternately select interior and freestream
  // data.  Blend only over a narrow convective-speed band; the acoustic
  // Riemann invariants above remain unchanged.
  const double inside_entropy = inside.pressure / std::pow(inside.rho, parameters.gamma);
  const double freestream_entropy = freestream.pressure / std::pow(freestream.rho, parameters.gamma);
  if (!(inside_entropy > kTiny) || !(freestream_entropy > kTiny) ||
      !std::isfinite(inside_entropy) || !std::isfinite(freestream_entropy)) {
    return freestream;
  }
  const double freestream_speed = std::hypot(freestream.u, freestream.v);
  const double blend_speed = std::max(0.02 * freestream_speed, 1.0e-3 * freestream.sound_speed);
  const double convective_weight =
      0.5 * (1.0 + std::tanh(normal_velocity / std::max(blend_speed, kTiny)));
  const double entropy = std::exp(convective_weight * std::log(inside_entropy) +
                                  (1.0 - convective_weight) * std::log(freestream_entropy));
  Primitive result;
  result.rho = std::pow(sound_speed * sound_speed / (parameters.gamma * entropy),
                        1.0 / (parameters.gamma - 1.0));
  result.pressure = entropy * std::pow(result.rho, parameters.gamma);
  const double tangential_velocity =
      convective_weight * inner({inside.u, inside.v}, wall_tangent) +
      (1.0 - convective_weight) * inner({freestream.u, freestream.v}, wall_tangent);
  result.u = normal_velocity * outward_normal.x + tangential_velocity * wall_tangent.x;
  result.v = normal_velocity * outward_normal.y + tangential_velocity * wall_tangent.y;
  result.temperature = result.pressure / (result.rho * parameters.gas_constant);
  result.sound_speed = sound_speed;
  if (!std::isfinite(result.rho) || !std::isfinite(result.pressure) || result.rho <= kDensityFloor ||
      result.pressure <= kPressureFloor) {
    return freestream;
  }
  return result;
}

std::array<double, 4> state_at(const std::vector<double>& state, const int local_cell) {
  const auto offset = static_cast<std::size_t>(local_cell) * 4U;
  return {state[offset], state[offset + 1], state[offset + 2], state[offset + 3]};
}

void add_scaled(std::vector<double>& values, const int local_cell, const std::array<double, 4>& increment,
                const double factor) {
  const auto offset = static_cast<std::size_t>(local_cell) * 4U;
  for (int component_index = 0; component_index < 4; ++component_index) {
    values[offset + static_cast<std::size_t>(component_index)] +=
        factor * increment[static_cast<std::size_t>(component_index)];
  }
}

}  // namespace

FlowSolver::FlowSolver(CaseConfig config, LocalMesh mesh, MPI_Comm communicator)
    : config_(std::move(config)), mesh_(std::move(mesh)), comm_(communicator), gas_(config_.gas), halo_(mesh_, communicator) {
  if (comm_ == MPI_COMM_NULL) {
    throw std::invalid_argument("FlowSolver requires a valid MPI communicator");
  }
  MPI_Comm_rank(comm_, &rank_);
  MPI_Comm_size(comm_, &ranks_);
  if (mesh_.rank != rank_ || mesh_.size != ranks_) {
    throw std::runtime_error("distributed mesh communicator metadata does not match the solver communicator");
  }
  if (mesh_.owned_cell_count <= 0 || mesh_.cells.empty()) {
    throw std::runtime_error("distributed mesh contains no owned cells on a rank");
  }
  validate_boundary_map();
}

void FlowSolver::validate_boundary_map() const {
  for (const LocalFace& face : mesh_.faces) {
    if (face.right_cell < 0 && face.boundary_type == BoundaryType::unspecified) {
      throw std::runtime_error("mesh has an unmapped physical boundary face named '" + face.boundary_name +
                               "'; add it to boundary_conditions in the case JSON");
    }
  }
}

void FlowSolver::initialize() {
  const Conserved freestream = gas_.freestream_state(config_.freestream);
  state_.assign(mesh_.cells.size() * 4U, 0.0);
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    Conserved value = freestream;
    if (config_.run.type == RunType::Transient) {
      // A small smooth, deterministic perturbation is an initial condition, not
      // a case-specific forcing.  It breaks exact discrete reflection symmetry
      // so an unstable wake can select a physical shedding phase.
      const Vec2 center = mesh_.cells[static_cast<std::size_t>(local_cell)].centroid;
      const double dx = (center.x - config_.reference.moment_center[0]) / config_.reference.length;
      const double dy = (center.y - config_.reference.moment_center[1]) / config_.reference.length;
      Primitive primitive = gas_.primitive(value);
      primitive.v += 1.0e-5 * config_.freestream.velocity_magnitude * std::sin(2.0 * kPi * dx) *
                     std::exp(-(dx * dx + dy * dy));
      value = gas_.conserved(primitive);
    }
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    std::copy(value.begin(), value.end(), state_.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  synchronize_state();
  gradients_.assign(mesh_.cells.size() * 8U, 0.0);
  initialized_ = true;
}

void FlowSolver::restore_owned_state(const std::vector<double>& owned_state) {
  const std::size_t expected = static_cast<std::size_t>(mesh_.owned_cell_count) * 4U;
  if (owned_state.size() != expected) {
    throw std::invalid_argument("restart state does not match local owned-cell count");
  }
  state_.assign(mesh_.cells.size() * 4U, 0.0);
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    const Conserved candidate{owned_state[offset], owned_state[offset + 1], owned_state[offset + 2], owned_state[offset + 3]};
    const Conserved safe = gas_.enforce_physical(candidate, gas_.freestream_state(config_.freestream));
    std::copy(safe.begin(), safe.end(), state_.begin() + static_cast<std::ptrdiff_t>(offset));
  }
  synchronize_state();
  gradients_.assign(mesh_.cells.size() * 8U, 0.0);
  initialized_ = true;
}

Primitive FlowSolver::primitive_at(const int local_cell) const {
  return gas_.primitive(state_at(state_, local_cell));
}

std::array<Vec2, 4> FlowSolver::primitive_gradients(const int local_cell) const {
  std::array<Vec2, 4> result{};
  const auto offset = static_cast<std::size_t>(local_cell) * 8U;
  for (int variable = 0; variable < 4; ++variable) {
    result[static_cast<std::size_t>(variable)] = {
        gradients_[offset + static_cast<std::size_t>(2 * variable)],
        gradients_[offset + static_cast<std::size_t>(2 * variable + 1)]};
  }
  return result;
}

Primitive FlowSolver::reconstructed_primitive(const int local_cell, const Vec2 point) const {
  Primitive center = primitive_at(local_cell);
  if (gradients_.empty()) {
    return center;
  }
  const Vec2 delta = subtract(point, mesh_.cells[static_cast<std::size_t>(local_cell)].centroid);
  const auto gradient = primitive_gradients(local_cell);
  Primitive reconstructed = center;
  for (int variable = 0; variable < 4; ++variable) {
    set_component(reconstructed, variable,
                  component(center, variable) + inner(gradient[static_cast<std::size_t>(variable)], delta));
  }
  reconstructed.rho = std::max(reconstructed.rho, kDensityFloor);
  reconstructed.temperature = std::max(reconstructed.temperature, kPressureFloor / config_.gas.gas_constant);
  reconstructed.pressure = reconstructed.rho * config_.gas.gas_constant * reconstructed.temperature;
  reconstructed.sound_speed = std::sqrt(config_.gas.gamma * reconstructed.pressure / reconstructed.rho);
  if (!std::isfinite(reconstructed.rho) || !std::isfinite(reconstructed.u) || !std::isfinite(reconstructed.v) ||
      !std::isfinite(reconstructed.pressure) || reconstructed.pressure <= kPressureFloor) {
    return center;
  }
  return reconstructed;
}

void FlowSolver::synchronize_state() { halo_.exchange(state_, 4); }

void FlowSolver::reconstruct_gradients_and_limit() {
  const std::size_t local_count = mesh_.cells.size();
  std::vector<double> raw(local_count * 8U, 0.0);

  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const Cell& cell = mesh_.cells[static_cast<std::size_t>(local_cell)];
    const Primitive base = primitive_at(local_cell);
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    std::array<double, 4> bx{0.0, 0.0, 0.0, 0.0};
    std::array<double, 4> by{0.0, 0.0, 0.0, 0.0};
    for (const int neighbor : cell.neighbors) {
      if (neighbor < 0) {
        continue;
      }
      const Primitive other = primitive_at(neighbor);
      const Vec2 delta = subtract(mesh_.cells[static_cast<std::size_t>(neighbor)].centroid, cell.centroid);
      xx += delta.x * delta.x;
      xy += delta.x * delta.y;
      yy += delta.y * delta.y;
      for (int variable = 0; variable < 4; ++variable) {
        const double difference = component(other, variable) - component(base, variable);
        bx[static_cast<std::size_t>(variable)] += delta.x * difference;
        by[static_cast<std::size_t>(variable)] += delta.y * difference;
      }
    }
    const double determinant = xx * yy - xy * xy;
    if (determinant <= kTiny * std::max(1.0, xx * yy)) {
      continue;
    }
    const auto offset = static_cast<std::size_t>(local_cell) * 8U;
    for (int variable = 0; variable < 4; ++variable) {
      raw[offset + static_cast<std::size_t>(2 * variable)] =
          (yy * bx[static_cast<std::size_t>(variable)] - xy * by[static_cast<std::size_t>(variable)]) /
          determinant;
      raw[offset + static_cast<std::size_t>(2 * variable + 1)] =
          (xx * by[static_cast<std::size_t>(variable)] - xy * bx[static_cast<std::size_t>(variable)]) /
          determinant;
    }
  }

  std::vector<std::array<double, 4>> minima(static_cast<std::size_t>(mesh_.owned_cell_count));
  std::vector<std::array<double, 4>> maxima(static_cast<std::size_t>(mesh_.owned_cell_count));
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const Primitive base = primitive_at(local_cell);
    for (int variable = 0; variable < 4; ++variable) {
      minima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] = component(base, variable);
      maxima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] = component(base, variable);
    }
    for (const int neighbor : mesh_.cells[static_cast<std::size_t>(local_cell)].neighbors) {
      if (neighbor < 0) {
        continue;
      }
      const Primitive other = primitive_at(neighbor);
      for (int variable = 0; variable < 4; ++variable) {
        minima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] =
            std::min(minima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)],
                     component(other, variable));
        maxima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] =
            std::max(maxima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)],
                     component(other, variable));
      }
    }
  }

  std::vector<std::array<double, 4>> limiter(static_cast<std::size_t>(mesh_.owned_cell_count));
  for (auto& values : limiter) {
    values.fill(1.0);
  }
  const auto limit_at_face = [&](const int local_cell, const Vec2 point) {
    if (!mesh_.is_owned(local_cell)) {
      return;
    }
    const Primitive base = primitive_at(local_cell);
    const Vec2 delta = subtract(point, mesh_.cells[static_cast<std::size_t>(local_cell)].centroid);
    const auto offset = static_cast<std::size_t>(local_cell) * 8U;
    for (int variable = 0; variable < 4; ++variable) {
      const double increment = raw[offset + static_cast<std::size_t>(2 * variable)] * delta.x +
                               raw[offset + static_cast<std::size_t>(2 * variable + 1)] * delta.y;
      if (increment > kTiny) {
        limiter[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] =
            std::min(limiter[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)],
                     (maxima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] -
                      component(base, variable)) /
                         increment);
      } else if (increment < -kTiny) {
        limiter[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] =
            std::min(limiter[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)],
                     (minima[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)] -
                      component(base, variable)) /
                         increment);
      }
    }
  };
  for (const LocalFace& face : mesh_.faces) {
    limit_at_face(face.left_cell, face.centroid);
    limit_at_face(face.right_cell, face.centroid);
  }
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const auto offset = static_cast<std::size_t>(local_cell) * 8U;
    for (int variable = 0; variable < 4; ++variable) {
      const double theta = std::clamp(limiter[static_cast<std::size_t>(local_cell)][static_cast<std::size_t>(variable)],
                                      0.0, 1.0);
      raw[offset + static_cast<std::size_t>(2 * variable)] *= theta;
      raw[offset + static_cast<std::size_t>(2 * variable + 1)] *= theta;
    }
  }
  gradients_ = std::move(raw);
  halo_.exchange(gradients_, 8);
}

double FlowSolver::local_viscosity() const {
  if (config_.physics_mode == PhysicsMode::Inviscid) {
    return 0.0;
  }
  return config_.freestream.rho * config_.freestream.velocity_magnitude * config_.reference.reynolds_length /
         config_.reynolds;
}

FlowSolver::Assembly FlowSolver::assemble_spatial_residual() {
  synchronize_state();
  reconstruct_gradients_and_limit();
  Assembly result;
  result.residual.assign(mesh_.cells.size() * 4U, 0.0);
  result.spectral_radius.assign(static_cast<std::size_t>(mesh_.owned_cell_count), 0.0);
  const double viscosity = local_viscosity();
  const double conductivity = viscosity * gas_.cp() / config_.gas.prandtl;
  const Primitive farfield = gas_.primitive(gas_.freestream_state(config_.freestream));

  for (const LocalFace& face : mesh_.faces) {
    const int left_index = face.left_cell;
    Primitive left = reconstructed_primitive(left_index, face.centroid);
    Primitive right = left;
    bool no_slip_wall = false;
    bool impermeable_wall = false;
    bool characteristic_boundary = false;
    if (face.right_cell >= 0) {
      right = reconstructed_primitive(face.right_cell, face.centroid);
    } else {
      switch (face.boundary_type) {
        case BoundaryType::farfield:
          right = characteristic_farfield(gas_, left, farfield, face.unit_normal);
          characteristic_boundary = true;
          break;
        case BoundaryType::slip_wall: {
          // Constrain the reconstructed face velocity itself.  Reflecting an
          // unconstrained state and feeding it to LLF/Rusanov adds an
          // artificial O(a rho u_n) wall traction at low Mach.
          const double normal_velocity = left.u * face.unit_normal.x + left.v * face.unit_normal.y;
          left.u -= normal_velocity * face.unit_normal.x;
          left.v -= normal_velocity * face.unit_normal.y;
          right = reflected_slip_state(left, face.unit_normal);
          impermeable_wall = true;
          break;
        }
        case BoundaryType::no_slip_adiabatic_wall:
          right = reflected_no_slip_adiabatic_state(left);
          no_slip_wall = true;
          impermeable_wall = true;
          break;
        case BoundaryType::interior:
        case BoundaryType::unspecified:
          throw std::runtime_error("invalid boundary classification during residual assembly");
      }
    }
    // For impermeable walls, the inviscid boundary flux is exactly the wall
    // pressure traction.  For farfield boundaries, the characteristic helper
    // returns the boundary trace itself, so evaluate its physical Euler flux
    // directly rather than applying a second, reflective Rusanov interface.
    const Conserved inviscid = impermeable_wall
                                   ? Conserved{0.0, left.pressure * face.unit_normal.x,
                                               left.pressure * face.unit_normal.y, 0.0}
                                   : characteristic_boundary
                                         ? euler_flux(gas_, right, face.unit_normal)
                                         : rusanov_flux(gas_, left, right, face.unit_normal,
                                                        config_.run.rusanov_dissipation_scale);
    std::array<double, 4> total = inviscid;

    if (viscosity > 0.0) {
      auto gradient_left = primitive_gradients(left_index);
      auto gradient_right = face.right_cell >= 0 ? primitive_gradients(face.right_cell) : gradient_left;
      Vec2 grad_u = scale(add(gradient_left[1], gradient_right[1]), 0.5);
      Vec2 grad_v = scale(add(gradient_left[2], gradient_right[2]), 0.5);
      Vec2 grad_t = scale(add(gradient_left[3], gradient_right[3]), 0.5);
      Primitive viscous_state = left;
      if (face.right_cell >= 0) {
        viscous_state.rho = 0.5 * (left.rho + right.rho);
        viscous_state.u = 0.5 * (left.u + right.u);
        viscous_state.v = 0.5 * (left.v + right.v);
        viscous_state.pressure = 0.5 * (left.pressure + right.pressure);
        viscous_state.temperature = 0.5 * (left.temperature + right.temperature);
      } else if (no_slip_wall) {
        const Cell& left_cell = mesh_.cells[static_cast<std::size_t>(left_index)];
        const double normal_distance =
            std::max(std::abs(inner(subtract(face.centroid, left_cell.centroid), face.unit_normal)),
                     0.15 * std::sqrt(left_cell.area));
        const Vec2 wall_tangent = tangent(face.unit_normal);
        const auto wall_gradient = [&](const Vec2 center_gradient, const double center_value) {
          const double tangential_derivative = inner(center_gradient, wall_tangent);
          const double normal_derivative = -center_value / normal_distance;
          return add(scale(wall_tangent, tangential_derivative), scale(face.unit_normal, normal_derivative));
        };
        grad_u = wall_gradient(gradient_left[1], primitive_at(left_index).u);
        grad_v = wall_gradient(gradient_left[2], primitive_at(left_index).v);
        // Adiabatic wall: preserve the reconstructed tangential derivative but
        // impose dT/dn = 0 directly at the face.
        grad_t = scale(wall_tangent, inner(gradient_left[3], wall_tangent));
        viscous_state.u = 0.0;
        viscous_state.v = 0.0;
      }
      const double divergence = grad_u.x + grad_v.y;
      const double tau_xx = 2.0 * viscosity * grad_u.x - (2.0 / 3.0) * viscosity * divergence;
      const double tau_yy = 2.0 * viscosity * grad_v.y - (2.0 / 3.0) * viscosity * divergence;
      const double tau_xy = viscosity * (grad_u.y + grad_v.x);
      const double traction_x = tau_xx * face.unit_normal.x + tau_xy * face.unit_normal.y;
      const double traction_y = tau_xy * face.unit_normal.x + tau_yy * face.unit_normal.y;
      const double heat_flux_term = conductivity * inner(grad_t, face.unit_normal);
      const std::array<double, 4> viscous{0.0, traction_x, traction_y,
                                           viscous_state.u * traction_x + viscous_state.v * traction_y + heat_flux_term};
      for (int component_index = 0; component_index < 4; ++component_index) {
        total[static_cast<std::size_t>(component_index)] -= viscous[static_cast<std::size_t>(component_index)];
      }
    }

    const double length = face.length;
    add_scaled(result.residual, left_index, total, length);
    if (face.right_cell >= 0 && mesh_.is_owned(face.right_cell)) {
      add_scaled(result.residual, face.right_cell, total, -length);
    }

    const auto add_spectral = [&](const int local_cell, const Primitive& state) {
      if (!mesh_.is_owned(local_cell)) {
        return;
      }
      const double convective = config_.run.rusanov_dissipation_scale *
                                (std::abs(state.u * face.unit_normal.x + state.v * face.unit_normal.y) +
                                 state.sound_speed);
      double normal_distance = 0.0;
      if (face.right_cell >= 0) {
        const int neighbor = local_cell == face.left_cell ? face.right_cell : face.left_cell;
        normal_distance = std::abs(inner(
            subtract(mesh_.cells[static_cast<std::size_t>(neighbor)].centroid,
                     mesh_.cells[static_cast<std::size_t>(local_cell)].centroid),
            face.unit_normal));
      } else {
        normal_distance = std::abs(inner(
            subtract(face.centroid, mesh_.cells[static_cast<std::size_t>(local_cell)].centroid),
            face.unit_normal));
      }
      normal_distance = std::max(
          normal_distance, 0.15 * std::sqrt(mesh_.cells[static_cast<std::size_t>(local_cell)].area));
      // A diffusion Jacobian scales as mu * face_length / (rho * d_n).
      // Keeping the normal distance explicit gives it the same units as the
      // convective face spectral radius, including on stretched wall cells.
      const double viscous = viscosity > 0.0
                                 ? 4.0 * viscosity * face.length /
                                       std::max(state.rho * normal_distance, kTiny)
                                 : 0.0;
      result.spectral_radius[static_cast<std::size_t>(local_cell)] += face.length * convective + viscous;
    };
    add_spectral(left_index, left);
    if (face.right_cell >= 0) {
      add_spectral(face.right_cell, right);
    }
  }
  return result;
}

ResidualRecord FlowSolver::global_residual_record(const int step, const double time, const int inner_iter,
                                                  const double cfl, const double dt,
                                                  const std::vector<double>& residual) const {
  std::array<double, 4> local_squares{0.0, 0.0, 0.0, 0.0};
  double local_infinity = 0.0;
  double local_volume = 0.0;
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const double area = mesh_.cells[static_cast<std::size_t>(local_cell)].area;
    local_volume += area;
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    for (int component_index = 0; component_index < 4; ++component_index) {
      const double normalized = residual[offset + static_cast<std::size_t>(component_index)] / area;
      // Finite-volume residuals are cell-volume integrals.  Weighting their
      // density by cell area gives the physical L2 norm and prevents a single
      // vanishing-area sharp trailing-edge cell from dominating convergence.
      local_squares[static_cast<std::size_t>(component_index)] += normalized * normalized * area;
      local_infinity = std::max(local_infinity, std::abs(normalized));
    }
  }
  std::array<double, 4> global_squares{};
  double global_infinity = 0.0;
  double global_volume = 0.0;
  MPI_Allreduce(local_squares.data(), global_squares.data(), 4, MPI_DOUBLE, MPI_SUM, comm_);
  MPI_Allreduce(&local_infinity, &global_infinity, 1, MPI_DOUBLE, MPI_MAX, comm_);
  MPI_Allreduce(&local_volume, &global_volume, 1, MPI_DOUBLE, MPI_SUM, comm_);
  ResidualRecord record;
  record.step = step;
  record.physical_time = time;
  record.inner_iter = inner_iter;
  record.cfl = cfl;
  record.dt = dt;
  double combined = 0.0;
  for (int component_index = 0; component_index < 4; ++component_index) {
    record.components[static_cast<std::size_t>(component_index)] =
        std::sqrt(global_squares[static_cast<std::size_t>(component_index)] / std::max(global_volume, kTiny));
    combined += record.components[static_cast<std::size_t>(component_index)] *
                record.components[static_cast<std::size_t>(component_index)];
  }
  record.l2 = std::sqrt(combined);
  record.linf = global_infinity;
  return record;
}

double FlowSolver::global_norm(const std::vector<double>& residual) const {
  return global_residual_record(0, 0.0, 0, 0.0, 0.0, residual).l2;
}

std::vector<double> FlowSolver::local_time_steps(const std::vector<double>& spectral, const double cfl) const {
  std::vector<double> result(static_cast<std::size_t>(mesh_.owned_cell_count), 0.0);
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    result[static_cast<std::size_t>(local_cell)] =
        cfl * mesh_.cells[static_cast<std::size_t>(local_cell)].area /
        std::max(spectral[static_cast<std::size_t>(local_cell)], kTiny);
  }
  return result;
}

void FlowSolver::implicit_update(const std::vector<double>& total_residual,
                                 const std::vector<double>& spectral,
                                 const std::vector<double>& time_diagonal,
                                 const double relaxation) {
  if (total_residual.size() != state_.size() || spectral.size() != static_cast<std::size_t>(mesh_.owned_cell_count) ||
      time_diagonal.size() != static_cast<std::size_t>(mesh_.owned_cell_count)) {
    throw std::invalid_argument("invalid implicit update arrays");
  }
  // This is a rank-local LU-SGS update of a frozen nonlinear residual.  MPI
  // interfaces remain block-Jacobi (ghost corrections are lagged), while
  // owned-owned Rusanov couplings are swept in both local directions.  The
  // four conservative components are updated together and a local positivity
  // line search prevents a rejected high-CFL correction from freezing a cell.
  std::vector<double> diagonal(static_cast<std::size_t>(mesh_.owned_cell_count), 0.0);
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    diagonal[static_cast<std::size_t>(local_cell)] =
        std::max(time_diagonal[static_cast<std::size_t>(local_cell)] +
                     spectral[static_cast<std::size_t>(local_cell)],
                 kTiny);
  }

  // Retain the positive, scalar Rusanov diffusion couplings in the frozen
  // approximate Jacobian.  The original point update discarded every
  // off-diagonal contribution, which makes a low-Mach acoustic mode converge
  // only through very small CFL steps.  A local LU-SGS application keeps the
  // block-Jacobi MPI boundary (ghost corrections remain lagged) while coupling
  // cells owned by this rank.  The diagonal deliberately remains the more
  // conservative full spectral radius: it is at least twice the sum of these
  // Rusanov half-couplings on an interior-only row.
  std::vector<std::vector<std::pair<int, double>>> couplings(
      static_cast<std::size_t>(mesh_.owned_cell_count));
  for (const LocalFace& face : mesh_.faces) {
    if (face.right_cell < 0 || !mesh_.is_owned(face.right_cell)) {
      continue;
    }
    const Primitive left = reconstructed_primitive(face.left_cell, face.centroid);
    const Primitive right = reconstructed_primitive(face.right_cell, face.centroid);
    const double left_signal = std::abs(left.u * face.unit_normal.x + left.v * face.unit_normal.y) +
                               left.sound_speed;
    const double right_signal = std::abs(right.u * face.unit_normal.x + right.v * face.unit_normal.y) +
                                right.sound_speed;
    const double coupling = 0.5 * config_.run.rusanov_dissipation_scale *
                            std::max(left_signal, right_signal) * face.length;
    if (!(coupling > 0.0) || !std::isfinite(coupling)) {
      continue;
    }
    couplings[static_cast<std::size_t>(face.left_cell)].emplace_back(face.right_cell, coupling);
    couplings[static_cast<std::size_t>(face.right_cell)].emplace_back(face.left_cell, coupling);
  }

  std::vector<double> forward(static_cast<std::size_t>(mesh_.owned_cell_count) * 4U, 0.0);
  std::vector<double> correction(static_cast<std::size_t>(mesh_.owned_cell_count) * 4U, 0.0);
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    for (int component_index = 0; component_index < 4; ++component_index) {
      double right_hand_side = -total_residual[offset + static_cast<std::size_t>(component_index)];
      for (const auto& [neighbor, coupling] : couplings[static_cast<std::size_t>(local_cell)]) {
        if (neighbor < local_cell) {
          right_hand_side += coupling * forward[static_cast<std::size_t>(neighbor) * 4U +
                                                static_cast<std::size_t>(component_index)];
        }
      }
      forward[offset + static_cast<std::size_t>(component_index)] =
          right_hand_side / diagonal[static_cast<std::size_t>(local_cell)];
    }
  }
  for (int local_cell = mesh_.owned_cell_count - 1; local_cell >= 0; --local_cell) {
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    for (int component_index = 0; component_index < 4; ++component_index) {
      double value = forward[offset + static_cast<std::size_t>(component_index)];
      for (const auto& [neighbor, coupling] : couplings[static_cast<std::size_t>(local_cell)]) {
        if (neighbor > local_cell) {
          value += coupling * correction[static_cast<std::size_t>(neighbor) * 4U +
                                         static_cast<std::size_t>(component_index)] /
                   diagonal[static_cast<std::size_t>(local_cell)];
        }
      }
      correction[offset + static_cast<std::size_t>(component_index)] = value;
    }
  }
  for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
    const auto offset = static_cast<std::size_t>(local_cell) * 4U;
    const Conserved before = state_at(state_, local_cell);
    double accepted_relaxation = relaxation;
    Conserved candidate = before;
    bool accepted = false;
    for (int attempt = 0; attempt < 12; ++attempt) {
      candidate = before;
      for (int component_index = 0; component_index < 4; ++component_index) {
        candidate[static_cast<std::size_t>(component_index)] +=
            accepted_relaxation * correction[offset + static_cast<std::size_t>(component_index)];
      }
      if (gas_.physical(candidate)) {
        accepted = true;
        break;
      }
      accepted_relaxation *= 0.5;
    }
    const Conserved safe = accepted ? candidate : before;
    std::copy(safe.begin(), safe.end(), state_.begin() + static_cast<std::ptrdiff_t>(offset));
  }
}

double FlowSolver::cfl_for_step(const int step) const {
  if (config_.run.pseudo_cfl_ramp_steps <= 0) {
    return config_.run.cfl_max;
  }
  const double fraction = std::clamp(static_cast<double>(step) /
                                         static_cast<double>(config_.run.pseudo_cfl_ramp_steps),
                                     0.0, 1.0);
  return config_.run.cfl_initial + fraction * (config_.run.cfl_max - config_.run.cfl_initial);
}

ForceRecord FlowSolver::integrated_forces(const int step, const double time) const {
  std::array<double, 5> local{0.0, 0.0, 0.0, 0.0, 0.0};  // pressure x/y, viscous x/y, moment
  const double viscosity = local_viscosity();
  const double conductivity_unused = viscosity * gas_.cp() / config_.gas.prandtl;
  static_cast<void>(conductivity_unused);
  const double q_inf = 0.5 * config_.freestream.rho *
                       config_.freestream.velocity_magnitude * config_.freestream.velocity_magnitude;
  const double alpha = config_.freestream.aoa_degrees * kPi / 180.0;
  const Vec2 drag_direction{std::cos(alpha), std::sin(alpha)};
  const Vec2 lift_direction{-std::sin(alpha), std::cos(alpha)};

  for (const LocalFace& face : mesh_.faces) {
    if (face.right_cell >= 0 ||
        (face.boundary_type != BoundaryType::slip_wall && face.boundary_type != BoundaryType::no_slip_adiabatic_wall)) {
      continue;
    }
    const Primitive fluid = reconstructed_primitive(face.left_cell, face.centroid);
    const Vec2 pressure_force = scale(face.unit_normal, fluid.pressure * face.length);
    Vec2 viscous_force{0.0, 0.0};
    if (viscosity > 0.0) {
      const auto gradients = primitive_gradients(face.left_cell);
      const Cell& cell = mesh_.cells[static_cast<std::size_t>(face.left_cell)];
      const double normal_distance =
          std::max(std::abs(inner(subtract(face.centroid, cell.centroid), face.unit_normal)), 0.15 * std::sqrt(cell.area));
      const Vec2 wall_tangent = tangent(face.unit_normal);
      const auto wall_gradient = [&](const Vec2 center_gradient, const double center_value) {
        return add(scale(wall_tangent, inner(center_gradient, wall_tangent)),
                   scale(face.unit_normal, -center_value / normal_distance));
      };
      const Vec2 grad_u = wall_gradient(gradients[1], primitive_at(face.left_cell).u);
      const Vec2 grad_v = wall_gradient(gradients[2], primitive_at(face.left_cell).v);
      const double divergence = grad_u.x + grad_v.y;
      const double tau_xx = 2.0 * viscosity * grad_u.x - (2.0 / 3.0) * viscosity * divergence;
      const double tau_yy = 2.0 * viscosity * grad_v.y - (2.0 / 3.0) * viscosity * divergence;
      const double tau_xy = viscosity * (grad_u.y + grad_v.x);
      const Vec2 traction{tau_xx * face.unit_normal.x + tau_xy * face.unit_normal.y,
                          tau_xy * face.unit_normal.x + tau_yy * face.unit_normal.y};
      // Only tangential traction is reported as skin-friction force.
      const double tangential_traction = -inner(traction, wall_tangent);
      viscous_force = scale(wall_tangent, tangential_traction * face.length);
    }
    const Vec2 total_force = add(pressure_force, viscous_force);
    local[0] += pressure_force.x;
    local[1] += pressure_force.y;
    local[2] += viscous_force.x;
    local[3] += viscous_force.y;
    const Vec2 arm{subtract(face.centroid, {config_.reference.moment_center[0], config_.reference.moment_center[1]})};
    local[4] += arm.x * total_force.y - arm.y * total_force.x;
  }
  std::array<double, 5> global{};
  MPI_Allreduce(local.data(), global.data(), static_cast<int>(global.size()), MPI_DOUBLE, MPI_SUM, comm_);
  const Vec2 pressure_force{global[0], global[1]};
  const Vec2 viscous_force{global[2], global[3]};
  const double scale_coefficient = 1.0 / std::max(q_inf * config_.reference.area, kTiny);
  ForceRecord record;
  record.step = step;
  record.physical_time = time;
  record.pressure_drag = inner(pressure_force, drag_direction) * scale_coefficient;
  record.viscous_drag = inner(viscous_force, drag_direction) * scale_coefficient;
  record.pressure_lift = inner(pressure_force, lift_direction) * scale_coefficient;
  record.viscous_lift = inner(viscous_force, lift_direction) * scale_coefficient;
  if (config_.physics_mode == PhysicsMode::Inviscid) {
    record.viscous_drag = 0.0;
    record.viscous_lift = 0.0;
  }
  record.cd = record.pressure_drag + record.viscous_drag;
  record.cl = record.pressure_lift + record.viscous_lift;
  record.cmz = global[4] * scale_coefficient / config_.reference.length;
  return record;
}

std::vector<SurfaceRecord> FlowSolver::build_surface_records() const {
  std::vector<SurfaceRecord> rows;
  const double viscosity = local_viscosity();
  const double q_inf = 0.5 * config_.freestream.rho *
                       config_.freestream.velocity_magnitude * config_.freestream.velocity_magnitude;
  for (const LocalFace& face : mesh_.faces) {
    if (face.right_cell >= 0 ||
        (face.boundary_type != BoundaryType::slip_wall && face.boundary_type != BoundaryType::no_slip_adiabatic_wall)) {
      continue;
    }
    const Primitive fluid = reconstructed_primitive(face.left_cell, face.centroid);
    SurfaceRecord row;
    row.x = face.centroid.x;
    row.y = face.centroid.y;
    row.nx = face.unit_normal.x;
    row.ny = face.unit_normal.y;
    row.pressure = fluid.pressure;
    row.cp = (fluid.pressure - config_.freestream.pressure) / std::max(q_inf, kTiny);
    row.rho = fluid.rho;
    row.tag = face.boundary_name;
    if (face.boundary_type == BoundaryType::no_slip_adiabatic_wall) {
      row.u = 0.0;
      row.v = 0.0;
      row.mach = 0.0;
      if (viscosity > 0.0) {
        const auto gradients = primitive_gradients(face.left_cell);
        const Cell& cell = mesh_.cells[static_cast<std::size_t>(face.left_cell)];
        const double distance = std::max(std::abs(inner(subtract(face.centroid, cell.centroid), face.unit_normal)),
                                         0.15 * std::sqrt(cell.area));
        const Vec2 wall_tangent = tangent(face.unit_normal);
        const auto wall_gradient = [&](const Vec2 center_gradient, const double center_value) {
          return add(scale(wall_tangent, inner(center_gradient, wall_tangent)),
                     scale(face.unit_normal, -center_value / distance));
        };
        const Vec2 gu = wall_gradient(gradients[1], primitive_at(face.left_cell).u);
        const Vec2 gv = wall_gradient(gradients[2], primitive_at(face.left_cell).v);
        const double div = gu.x + gv.y;
        const double tx = 2.0 * viscosity * gu.x - (2.0 / 3.0) * viscosity * div;
        const double ty = 2.0 * viscosity * gv.y - (2.0 / 3.0) * viscosity * div;
        const double txy = viscosity * (gu.y + gv.x);
        const Vec2 traction{tx * face.unit_normal.x + txy * face.unit_normal.y,
                            txy * face.unit_normal.x + ty * face.unit_normal.y};
        row.cf = -inner(traction, wall_tangent) / std::max(q_inf, kTiny);
      }
    } else {
      const double normal_velocity = fluid.u * face.unit_normal.x + fluid.v * face.unit_normal.y;
      row.u = fluid.u - normal_velocity * face.unit_normal.x;
      row.v = fluid.v - normal_velocity * face.unit_normal.y;
      row.mach = std::sqrt(row.u * row.u + row.v * row.v) / std::max(fluid.sound_speed, kTiny);
      row.cf = 0.0;
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

bool FlowSolver::transient_force_is_periodic() const {
  if (summary_.forces.size() < 100) {
    return false;
  }
  const std::size_t begin = summary_.forces.size() * 3U / 4U;
  double mean = 0.0;
  for (std::size_t index = begin; index < summary_.forces.size(); ++index) {
    mean += summary_.forces[index].cl;
  }
  const double count = static_cast<double>(summary_.forces.size() - begin);
  mean /= count;
  double variance = 0.0;
  for (std::size_t index = begin; index < summary_.forces.size(); ++index) {
    const double delta = summary_.forces[index].cl - mean;
    variance += delta * delta;
  }
  const double amplitude = std::sqrt(variance / count);
  return std::isfinite(amplitude) && amplitude > 1.0e-7;
}

RunSummary FlowSolver::solve() {
  if (!initialized_) {
    initialize();
  }
  summary_ = RunSummary{};
  if (config_.run.type == RunType::Steady) {
    double initial_norm = 0.0;
    double pseudo_time = 0.0;
    ResidualRecord final_record;
    // A residual target reached after a sustained interval is a legitimate
    // convergence stop; otherwise we honor the full supplied maximum.  The
    // force history is also required to be flat over the trailing window.
    const int minimum_steps_before_early_exit = std::min(config_.run.max_steps, 200);
    long long total_inner_iterations = 0;
    int observed_min_inner = std::numeric_limits<int>::max();
    int observed_max_inner = 0;
    int inner_target_misses = 0;
    double last_inner_ratio = 1.0;
    // The supplied ramp is the target continuation schedule.  We only back it
    // off when the current implicit solve or outer residual rejects the next
    // increase; the actual local CFL is retained in residuals.csv.
    const double steady_cfl_floor = std::min(config_.run.cfl_initial, 0.05);
    const double steady_cfl_ceiling = std::min(config_.run.cfl_max, 1.0);
    double adaptive_cfl = config_.run.cfl_initial;
    // At the CFL floor, lowering CFL can no longer damp a nonlinear outer
    // oscillation.  Keep a separate correction relaxation that can back off
    // locally without changing the requested continuation schedule or the
    // physical discretization.
    double floor_relaxation = 0.5;
    int floor_decline_streak = 0;
    int floor_retry_count = 0;
    constexpr double minimum_floor_relaxation = 0.0625;
    // A steady nonlinear solve can make small, bounded residual excursions
    // while still converging.  Keep the trust region local to the recent
    // accepted trajectory: an all-time residual minimum turns a harmless
    // trough into a permanent rejection threshold and repeatedly re-solves
    // the same floor-CFL correction without changing the state.
    std::vector<double> accepted_outer_norms;
    accepted_outer_norms.reserve(static_cast<std::size_t>(config_.run.max_steps));
    // The controller separately reduces the next CFL after a 10% accepted
    // step-to-step increase below.  Reserve this wider envelope for rejecting
    // genuinely unstable trials rather than normal nonlinear ringing.
    constexpr double residual_trust_factor = 1.20;
    double previous_outer_norm = std::numeric_limits<double>::infinity();
    for (int step = 1; step <= config_.run.max_steps; ++step) {
      const double requested_cfl = cfl_for_step(step);
      const double cfl = std::min(requested_cfl, adaptive_cfl);
      // Once the controller has reached its permitted CFL floor, a further
      // CFL backoff cannot damp a nonlinear oscillation.  Use a more
      // conservative implicit correction in that regime instead of allowing
      // a high-Reynolds-number steady solve to repeatedly amplify it.
      const bool at_steady_cfl_floor = cfl <= steady_cfl_floor * (1.0 + 1.0e-12);
      const double steady_relaxation = at_steady_cfl_floor ? floor_relaxation : 0.8;
      const std::vector<double> outer_state = state_;
      double first_inner_norm = 0.0;
      int used_inner = 0;
      // Solve the frozen pseudo-time residual to the case-controlled inner
      // target, using all configured iterations when required.  The previous
      // implementation stopped at the minimum count, leaving most of the
      // prescribed implicit work unused.
      const int steady_inner_limit = config_.run.max_inner_iterations;
      bool inner_target_reached = false;
      for (int inner_iteration = 1; inner_iteration <= steady_inner_limit; ++inner_iteration) {
        Assembly assembly = assemble_spatial_residual();
        const std::vector<double> dtau = local_time_steps(assembly.spectral_radius, cfl);
        std::vector<double> total = assembly.residual;
        std::vector<double> diagonal(static_cast<std::size_t>(mesh_.owned_cell_count), 0.0);
        for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
          const double pseudo_diagonal = mesh_.cells[static_cast<std::size_t>(local_cell)].area /
                                         std::max(dtau[static_cast<std::size_t>(local_cell)], kTiny);
          diagonal[static_cast<std::size_t>(local_cell)] = pseudo_diagonal;
          const auto offset = static_cast<std::size_t>(local_cell) * 4U;
          for (int component_index = 0; component_index < 4; ++component_index) {
            total[offset + static_cast<std::size_t>(component_index)] += pseudo_diagonal *
                (state_[offset + static_cast<std::size_t>(component_index)] -
                 outer_state[offset + static_cast<std::size_t>(component_index)]);
          }
        }
        const ResidualRecord record = global_residual_record(step, pseudo_time, inner_iteration, cfl, 0.0, total);
        const double norm_value = record.l2;
        if (initial_norm == 0.0) {
          initial_norm = std::max(norm_value, kTiny);
        }
        if (first_inner_norm == 0.0) {
          first_inner_norm = std::max(norm_value, kTiny);
        }
        used_inner = inner_iteration;
        final_record = record;
        const bool inner_converged = inner_iteration >= config_.run.min_inner_iterations &&
                                     norm_value / first_inner_norm <= config_.run.inner_residual_reduction_target;
        last_inner_ratio = norm_value / first_inner_norm;
        if (inner_converged || inner_iteration == steady_inner_limit) {
          inner_target_reached = inner_converged;
          break;
        }
        implicit_update(total, assembly.spectral_radius, diagonal, steady_relaxation);
      }
      Assembly diagnostic = assemble_spatial_residual();
      final_record = global_residual_record(step, pseudo_time, used_inner, cfl, 0.0, diagnostic.residual);
      const std::size_t rolling_count = std::min<std::size_t>(25U, accepted_outer_norms.size());
      const double rolling_outer_norm = rolling_count == 0
                                            ? std::numeric_limits<double>::infinity()
                                            : *std::min_element(accepted_outer_norms.end() -
                                                                    static_cast<std::ptrdiff_t>(rolling_count),
                                                                accepted_outer_norms.end());
      const bool reject_high_cfl_correction =
          !at_steady_cfl_floor && std::isfinite(rolling_outer_norm) &&
          final_record.l2 > residual_trust_factor * rolling_outer_norm;
      const bool reject_floor_correction =
          at_steady_cfl_floor && std::isfinite(rolling_outer_norm) &&
          final_record.l2 > residual_trust_factor * rolling_outer_norm;
      bool accepted_by_outer_line_search = false;
      if ((reject_high_cfl_correction || reject_floor_correction) && std::isfinite(rolling_outer_norm)) {
        // The inner solve may produce a useful correction whose full nonlinear
        // amplitude is outside the residual trust region.  Backtracking that
        // outer correction is both more direct and much cheaper than accepting
        // a rebound after several identical floor-CFL re-solves.  Interpolate
        // only owned cells, then refresh ghosts before measuring the trial.
        const std::vector<double> full_correction_state = state_;
        const double trust_limit = residual_trust_factor * rolling_outer_norm;
        for (int line_search_step = 1; line_search_step <= 8; ++line_search_step) {
          const double fraction = std::ldexp(1.0, -line_search_step);
          for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
            const auto offset = static_cast<std::size_t>(local_cell) * 4U;
            for (int component_index = 0; component_index < 4; ++component_index) {
              const auto component = static_cast<std::size_t>(component_index);
              state_[offset + component] = outer_state[offset + component] +
                                           fraction * (full_correction_state[offset + component] -
                                                       outer_state[offset + component]);
            }
          }
          synchronize_state();
          diagnostic = assemble_spatial_residual();
          const ResidualRecord line_record =
              global_residual_record(step, pseudo_time, used_inner, cfl, 0.0, diagnostic.residual);
          if (std::isfinite(line_record.l2) && line_record.l2 <= trust_limit) {
            final_record = line_record;
            // A damped outer correction has not completed the requested
            // implicit solve, so prevent the CFL controller from immediately
            // increasing the next trial.
            inner_target_reached = false;
            accepted_by_outer_line_search = true;
            break;
          }
        }
        if (!accepted_by_outer_line_search) {
          state_ = outer_state;
          synchronize_state();
        }
      }
      if (reject_high_cfl_correction && !accepted_by_outer_line_search) {
        // A growing correction away from the CFL floor is recoverable by
        // returning to the accepted outer state and retrying at lower CFL.
        // Do this before recording force/residual output so the controller
        // never advances from a knowingly rejected nonlinear state.
        state_ = outer_state;
        synchronize_state();
        adaptive_cfl = std::max(steady_cfl_floor, 0.5 * cfl);
        floor_decline_streak = 0;
        --step;
        continue;
      }
      if (reject_floor_correction && !accepted_by_outer_line_search && floor_retry_count < 4) {
        // Reuse the same outer state with a smaller correction, so a failed
        // trial cannot contaminate the accepted residual/force history.  A
        // bounded retry count avoids indefinitely re-solving an identical
        // floor-CFL correction once the local relaxation reaches its limit.
        state_ = outer_state;
        synchronize_state();
        floor_relaxation = std::max(minimum_floor_relaxation, 0.5 * floor_relaxation);
        floor_decline_streak = 0;
        ++floor_retry_count;
        --step;
        continue;
      }
      floor_retry_count = 0;
      accepted_outer_norms.push_back(final_record.l2);
      summary_.residuals.push_back(final_record);
      const std::vector<double> dtau = local_time_steps(diagnostic.spectral_radius, cfl);
      double local_dt_sum = std::accumulate(dtau.begin(), dtau.end(), 0.0);
      double global_dt_sum = 0.0;
      MPI_Allreduce(&local_dt_sum, &global_dt_sum, 1, MPI_DOUBLE, MPI_SUM, comm_);
      pseudo_time += global_dt_sum / static_cast<double>(mesh_.global_cell_count);
      summary_.forces.push_back(integrated_forces(step, pseudo_time));
      total_inner_iterations += used_inner;
      observed_min_inner = std::min(observed_min_inner, used_inner);
      observed_max_inner = std::max(observed_max_inner, used_inner);
      if (!inner_target_reached) {
        ++inner_target_misses;
      }
      summary_.final_step = step;
      summary_.final_physical_time = pseudo_time;
      const double orders = std::log10(initial_norm / std::max(final_record.l2, kTiny));
      summary_.residual_reduction_orders = orders;
      if (rank_ == 0 && (step % 25 == 0 || step == 1)) {
        std::cout << "steady step=" << step << " cfl=" << cfl << " residual_l2=" << final_record.l2
                  << " residual_orders=" << orders << " inner_sweeps=" << used_inner << '\n' << std::flush;
      }
      bool forces_flat = summary_.forces.size() >= 30;
      if (forces_flat) {
        const std::size_t first = summary_.forces.size() - 30U;
        double cd_min = summary_.forces[first].cd;
        double cd_max = cd_min;
        for (std::size_t index = first; index < summary_.forces.size(); ++index) {
          cd_min = std::min(cd_min, summary_.forces[index].cd);
          cd_max = std::max(cd_max, summary_.forces[index].cd);
        }
        forces_flat = std::abs(cd_max - cd_min) <= 1.0e-4 * std::max(1.0, std::abs(summary_.forces.back().cd));
      }
      if (step >= minimum_steps_before_early_exit && orders >= config_.run.residual_reduction_target && forces_flat) {
        summary_.converged = true;
        summary_.diagnostic = "global residual target reached by CFL-controlled block-Jacobi pseudo-time solve";
        break;
      }
      if (at_steady_cfl_floor && std::isfinite(previous_outer_norm)) {
        // Rejected growing trials above already reduce the correction and
        // restore the outer state.  Recover it slowly only after sustained
        // improvement, so an isolated good step cannot re-excite a
        // high-Reynolds-number mode.
        if (final_record.l2 < previous_outer_norm) {
          ++floor_decline_streak;
          if (floor_decline_streak >= 5) {
            floor_relaxation = std::min(0.5, 1.25 * floor_relaxation);
            floor_decline_streak = 0;
          }
        } else {
          floor_decline_streak = 0;
        }
      } else {
        floor_decline_streak = 0;
      }
      if (inner_target_reached && final_record.l2 <= 1.02 * previous_outer_norm) {
        adaptive_cfl = std::min(steady_cfl_ceiling, cfl * 1.15);
      } else if (!inner_target_reached || final_record.l2 > 1.10 * previous_outer_norm) {
        adaptive_cfl = std::max(steady_cfl_floor, cfl * 0.5);
      } else {
        adaptive_cfl = std::max(steady_cfl_floor, cfl * 0.9);
      }
      previous_outer_norm = final_record.l2;
    }
    if (!summary_.converged) {
      summary_.diagnostic = "steady residual target was not reached before the supplied maximum pseudo steps";
    }
    summary_.inner_statistics.minimum = observed_min_inner == std::numeric_limits<int>::max() ? 0 : observed_min_inner;
    summary_.inner_statistics.maximum = observed_max_inner;
    summary_.inner_statistics.mean = summary_.final_step > 0
                                         ? static_cast<double>(total_inner_iterations) / static_cast<double>(summary_.final_step)
                                         : 0.0;
    summary_.inner_statistics.target_misses = inner_target_misses;
    summary_.inner_statistics.converged_fraction = summary_.final_step > 0
                                                        ? 1.0 - static_cast<double>(inner_target_misses) /
                                                                    static_cast<double>(summary_.final_step)
                                                        : 0.0;
    summary_.inner_statistics.last_ratio = last_inner_ratio;
  } else {
    const int physical_steps = static_cast<int>(std::llround(config_.run.final_time / config_.run.time_step));
    if (physical_steps <= 0 || std::abs(physical_steps * config_.run.time_step - config_.run.final_time) > 1.0e-9) {
      throw std::runtime_error("transient final_time must be an integer multiple of time_step");
    }
    std::vector<double> previous = state_;
    std::vector<double> previous_previous = state_;
    long long total_inner_iterations = 0;
    int min_inner_observed = std::numeric_limits<int>::max();
    int max_inner_observed = 0;
    // Count attempts rather than silently accepting an under-converged physical
    // state.  A retry keeps the same BDF history frozen, so an unsuccessful
    // pseudo-time solve cannot contaminate U^n or U^{n-1}.
    int attempted_inner_solves = 0;
    int accepted_physical_steps = 0;
    int target_misses = 0;
    double final_ratio = 1.0;
    for (int step = 1; step <= physical_steps; ++step) {
      const double physical_time = static_cast<double>(step) * config_.run.time_step;
      const double requested_cfl = cfl_for_step(step);
      bool target_reached = false;
      ResidualRecord final_record;
      double accepted_cfl = requested_cfl;
      int used_inner = 0;
      // Pseudo-CFL backoff is a controlled retry of the same physical step.
      // The external BDF states remain immutable until an attempt reaches the
      // full spatial-plus-physical residual target.
      constexpr int kMaximumStepRetries = 4;
      for (int retry = 0; retry < kMaximumStepRetries && !target_reached; ++retry) {
        const double cfl = requested_cfl * std::pow(0.5, retry);
        state_ = previous;
        if (step > 1) {
          // A second-order extrapolation is only an initial nonlinear guess;
          // U^n and U^{n-1} below remain frozen BDF history states.
          for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
            const auto offset = static_cast<std::size_t>(local_cell) * 4U;
            Conserved predicted{};
            for (int component_index = 0; component_index < 4; ++component_index) {
              const std::size_t index = offset + static_cast<std::size_t>(component_index);
              predicted[static_cast<std::size_t>(component_index)] =
                  2.0 * previous[index] - previous_previous[index];
            }
            const Conserved safe = gas_.enforce_physical(predicted, state_at(previous, local_cell));
            std::copy(safe.begin(), safe.end(), state_.begin() + static_cast<std::ptrdiff_t>(offset));
          }
        }
        synchronize_state();
        double first_inner_norm = 0.0;
        used_inner = 0;
        ++attempted_inner_solves;
        for (int inner_iteration = 1; inner_iteration <= config_.run.max_inner_iterations; ++inner_iteration) {
          Assembly assembly = assemble_spatial_residual();
          std::vector<double> total = assembly.residual;
          std::vector<double> diagonal(static_cast<std::size_t>(mesh_.owned_cell_count), 0.0);
          const bool first_order_start = step == 1;
          const double physical_coefficient = first_order_start ? 1.0 / config_.run.time_step
                                                                 : 1.5 / config_.run.time_step;
          for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
            const double area = mesh_.cells[static_cast<std::size_t>(local_cell)].area;
            // The BDF defect contains the physical mass term and the spatial
            // residual only.  implicit_update() supplies its frozen spatial
            // spectral Jacobian; adding A/dtau here would add a second,
            // unmatched pseudo-time damping term and severely slow the
            // nonlinear BDF correction at the required CFL=1.
            diagonal[static_cast<std::size_t>(local_cell)] = physical_coefficient * area;
            const auto offset = static_cast<std::size_t>(local_cell) * 4U;
            for (int component_index = 0; component_index < 4; ++component_index) {
              const std::size_t index = offset + static_cast<std::size_t>(component_index);
              if (first_order_start) {
                total[index] += area / config_.run.time_step * (state_[index] - previous[index]);
              } else {
                total[index] += area / (2.0 * config_.run.time_step) *
                                (3.0 * state_[index] - 4.0 * previous[index] + previous_previous[index]);
              }
            }
          }
          const ResidualRecord record =
              global_residual_record(step, physical_time, inner_iteration, cfl, config_.run.time_step, total);
          if (first_inner_norm == 0.0) {
            first_inner_norm = std::max(record.l2, kTiny);
          }
          final_ratio = record.l2 / first_inner_norm;
          used_inner = inner_iteration;
          final_record = record;
          if (inner_iteration >= config_.run.min_inner_iterations &&
              final_ratio <= config_.run.inner_residual_reduction_target) {
            target_reached = true;
            break;
          }
          if (inner_iteration == config_.run.max_inner_iterations) {
            break;
          }
          // Retrying a difficult physical step at a lower pseudo-CFL also
          // reduces the nonlinear correction relaxation, while the production
          // CFL=1 path uses the tested full 1.2 relaxation.
          implicit_update(total, assembly.spectral_radius, diagonal, 1.2 * std::min(1.0, cfl));
        }
        // Make the candidate state and reported force/surface state use the
        // same synchronized reconstruction before deciding whether to accept.
        const Assembly final_assembly = assemble_spatial_residual();
        std::vector<double> final_total = final_assembly.residual;
        const bool first_order_start = step == 1;
        for (int local_cell = 0; local_cell < mesh_.owned_cell_count; ++local_cell) {
          const double area = mesh_.cells[static_cast<std::size_t>(local_cell)].area;
          const auto offset = static_cast<std::size_t>(local_cell) * 4U;
          for (int component_index = 0; component_index < 4; ++component_index) {
            const std::size_t index = offset + static_cast<std::size_t>(component_index);
            if (first_order_start) {
              final_total[index] += area / config_.run.time_step * (state_[index] - previous[index]);
            } else {
              final_total[index] += area / (2.0 * config_.run.time_step) *
                                    (3.0 * state_[index] - 4.0 * previous[index] + previous_previous[index]);
            }
          }
        }
        final_record = global_residual_record(step, physical_time, used_inner, cfl, config_.run.time_step, final_total);
        final_ratio = final_record.l2 / std::max(first_inner_norm, kTiny);
        target_reached = used_inner >= config_.run.min_inner_iterations &&
                         final_ratio <= config_.run.inner_residual_reduction_target;
        if (target_reached) {
          accepted_cfl = cfl;
          break;
        }
        ++target_misses;
        // Reject the candidate.  In particular, do not append output or move
        // BDF history forward from an under-converged physical solve.
        state_ = previous;
        synchronize_state();
      }
      if (!target_reached) {
        summary_.diagnostic = "transient inner target was not reached after controlled pseudo-CFL retries; BDF history was not advanced";
        break;
      }
      summary_.residuals.push_back(final_record);
      summary_.forces.push_back(integrated_forces(step, physical_time));
      total_inner_iterations += used_inner;
      min_inner_observed = std::min(min_inner_observed, used_inner);
      max_inner_observed = std::max(max_inner_observed, used_inner);
      previous_previous = previous;
      previous = state_;  // history updates only after the complete inner loop is accepted.
      summary_.final_step = step;
      summary_.final_physical_time = physical_time;
      ++accepted_physical_steps;
      if (rank_ == 0 && (step % 1000 == 0 || step == 1)) {
        std::cout << "transient step=" << step << " t=" << physical_time << " cfl=" << accepted_cfl
                  << " residual_ratio=" << final_ratio
                  << " inner_sweeps=" << used_inner << '\n' << std::flush;
      }
    }
    summary_.inner_statistics.minimum = min_inner_observed == std::numeric_limits<int>::max() ? 0 : min_inner_observed;
    summary_.inner_statistics.maximum = max_inner_observed;
    summary_.inner_statistics.mean = accepted_physical_steps > 0
                                         ? static_cast<double>(total_inner_iterations) /
                                               static_cast<double>(accepted_physical_steps)
                                         : 0.0;
    summary_.inner_statistics.target_misses = target_misses;
    summary_.inner_statistics.converged_fraction = attempted_inner_solves > 0
                                                        ? static_cast<double>(accepted_physical_steps) /
                                                              static_cast<double>(attempted_inner_solves)
                                                        : 0.0;
    summary_.inner_statistics.last_ratio = final_ratio;
    summary_.residual_reduction_orders = final_ratio > 0.0 ? -std::log10(final_ratio) : 0.0;
    const bool reached_requested_horizon = accepted_physical_steps == physical_steps;
    summary_.statistically_periodic = reached_requested_horizon && summary_.inner_statistics.converged_fraction >= 0.95 &&
                                      transient_force_is_periodic();
    summary_.converged = summary_.statistically_periodic;
    if (summary_.statistically_periodic) {
      summary_.diagnostic = "BDF2 outer physical-time loop reached the requested horizon with settled lift oscillations";
    } else if (reached_requested_horizon) {
      summary_.diagnostic = "transient run reached the requested horizon without satisfying periodicity or inner convergence criteria";
    }
  }
  synchronize_state();
  reconstruct_gradients_and_limit();
  summary_.local_surface = build_surface_records();
  return summary_;
}

}  // namespace cfd
