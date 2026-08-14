#include "cfd/residual.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace cfd {
namespace {

void add_flux(std::vector<CompensatedSum>& residual, std::size_t owned_cell,
              const Conservative& flux, double signed_length) {
  const std::size_t offset = owned_cell * 4U;
  for (std::size_t k = 0; k < 4U; ++k) {
    residual[offset + k].add(signed_length * flux[k]);
  }
}

ResidualNorms global_residual_norms(const std::vector<double>& residual,
                                    std::size_t owned_cells,
                                    std::size_t global_cells,
                                    MPI_Comm communicator) {
  std::array<long double, 4> local_squared{};
  double local_maximum = 0.0;
  for (std::size_t cell = 0; cell < owned_cells; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const double value = residual[cell * 4U + component];
      const long double extended = static_cast<long double>(value);
      local_squared[component] += extended * extended;
      local_maximum = std::max(local_maximum, std::abs(value));
    }
  }
  std::array<long double, 4> global_squared{};
  MPI_Allreduce(local_squared.data(), global_squared.data(), 4,
                MPI_LONG_DOUBLE, MPI_SUM, communicator);
  ResidualNorms norms;
  MPI_Allreduce(&local_maximum, &norms.linf, 1, MPI_DOUBLE, MPI_MAX,
                communicator);
  const long double cell_count = static_cast<long double>(global_cells);
  long double total_squared = 0.0L;
  for (std::size_t component = 0; component < 4U; ++component) {
    norms.component_l2[component] = static_cast<double>(
        std::sqrt(std::max(0.0L, global_squared[component]) / cell_count));
    total_squared += global_squared[component];
  }
  norms.total_l2 = static_cast<double>(
      std::sqrt(std::max(0.0L, total_squared) / cell_count));
  return norms;
}

Primitive average_primitive(const Primitive& left, const Primitive& right,
                            const CaloricallyPerfectGas& gas) {
  return gas.complete(0.5 * (left.rho + right.rho), 0.5 * (left.u + right.u),
                      0.5 * (left.v + right.v), 0.5 * (left.p + right.p));
}

void collective_state_preflight(const std::vector<double>& U, std::size_t expected_size,
                                std::size_t owned_cells,
                                const CaloricallyPerfectGas& gas, double cfl,
                                MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  int local_code = 0;
  if (U.size() != expected_size) {
    local_code = 1;
  } else if (!(cfl > 0.0) || !std::isfinite(cfl)) {
    local_code = 2;
  } else {
    for (std::size_t cell = 0; cell < owned_cells; ++cell) {
      const Conservative state{U[cell * 4U], U[cell * 4U + 1U], U[cell * 4U + 2U],
                               U[cell * 4U + 3U]};
      if (!gas.admissible(state)) {
        local_code = 3;
        break;
      }
    }
  }
  const int candidate_rank = local_code == 0 ? size : rank;
  int failure_rank = size;
  MPI_Allreduce(&candidate_rank, &failure_rank, 1, MPI_INT, MPI_MIN, communicator);
  if (failure_rank == size) return;
  int failure_code = rank == failure_rank ? local_code : 0;
  MPI_Bcast(&failure_code, 1, MPI_INT, failure_rank, communicator);
  const char* reason = failure_code == 1 ? "state vector dimension mismatch"
                       : failure_code == 2 ? "invalid CFL"
                                           : "inadmissible owned conservative state";
  throw std::invalid_argument("collective residual preflight failed on rank " +
                              std::to_string(failure_rank) + ": " + reason);
}

}  // namespace

Primitive blend_reconstructed_primitive(
    const Primitive& cell_center, const Primitive& reconstructed,
    double reconstruction_blend, const CaloricallyPerfectGas& gas) {
  if (!std::isfinite(reconstruction_blend) || reconstruction_blend < 0.0 ||
      reconstruction_blend > 1.0) {
    throw std::invalid_argument("reconstruction_blend must be finite and in [0,1]");
  }
  if (!gas.admissible(cell_center) || !gas.admissible(reconstructed)) {
    throw std::invalid_argument(
        "reconstruction blend endpoints must be finite and admissible");
  }
  if (reconstruction_blend == 0.0) return cell_center;
  if (reconstruction_blend == 1.0) return reconstructed;
  const double complement = 1.0 - reconstruction_blend;
  return gas.complete(
      complement * cell_center.rho + reconstruction_blend * reconstructed.rho,
      complement * cell_center.u + reconstruction_blend * reconstructed.u,
      complement * cell_center.v + reconstruction_blend * reconstructed.v,
      complement * cell_center.p + reconstruction_blend * reconstructed.p);
}

void validate_boundary_mapping(const DistributedMesh& mesh, const CaseConfig& config,
                               MPI_Comm communicator) {
  int local_missing = 0;
  std::string missing_name;
  for (const LocalFace& face : mesh.faces) {
    if (face.right_cell >= 0) continue;
    const auto found = config.boundary_conditions.find(face.boundary);
    if (found == config.boundary_conditions.end()) {
      local_missing = 1;
      missing_name = face.boundary;
      break;
    }
    switch (found->second) {
      case BoundaryCondition::farfield:
      case BoundaryCondition::slip_wall:
      case BoundaryCondition::no_slip_adiabatic_wall: break;
      default: local_missing = 1; break;
    }
  }
  int global_missing = 0;
  MPI_Allreduce(&local_missing, &global_missing, 1, MPI_INT, MPI_MAX, communicator);
  if (global_missing != 0) {
    throw std::invalid_argument("mesh boundary family is missing or unsupported in CaseConfig" +
                                (missing_name.empty() ? std::string{} : ": " + missing_name));
  }
}

ResidualOperator::ResidualOperator(const DistributedMesh& mesh, const CaseConfig& config,
                                   MPI_Comm communicator)
    : mesh_(mesh),
      config_(config),
      communicator_(communicator),
      gas_(config.gas),
      freestream_(gas_.freestream(config.freestream)),
      freestream_primitive_(gas_.primitive(freestream_)),
      viscosity_(gas_.constant_viscosity(config)),
      dissipation_scale_(config.run_control.rusanov_dissipation_scale.value_or(1.0)),
      reconstruction_(mesh, gas_, config.boundary_conditions,
                      freestream_primitive_, communicator,
                      config.reference.length) {
  boundary_face_count_ = static_cast<std::size_t>(std::count_if(
      mesh_.faces.begin(), mesh_.faces.end(),
      [](const LocalFace& face) { return face.right_cell < 0; }));
  validate_boundary_mapping(mesh_, config_, communicator_);
  if (config_.numerics_required.spatial_order != 2) {
    throw std::invalid_argument("Phase 2 numerical core requires spatial_order=2");
  }
}

ResidualResult ResidualOperator::evaluate(std::vector<double>& U, double cfl,
                                          const ResidualEvaluationOptions& options) {
  ResidualResult result;
  evaluate_into(U, cfl, result, options);
  return result;
}

ResidualResult ResidualOperator::evaluate(std::vector<double>& U, double cfl,
                                          bool second_order) {
  ResidualEvaluationOptions options;
  options.reconstruction_blend = second_order ? 1.0 : 0.0;
  return evaluate(U, cfl, options);
}

void ResidualOperator::evaluate_into(std::vector<double>& U, double cfl,
                                     ResidualResult& result,
                                     const ResidualEvaluationOptions& options) {
  if (!std::isfinite(options.reconstruction_blend) ||
      options.reconstruction_blend < 0.0 ||
      options.reconstruction_blend > 1.0) {
    throw std::invalid_argument("reconstruction_blend must be finite and in [0,1]");
  }
  if (options.collective_preflight) {
    collective_state_preflight(U, mesh_.cells.size() * 4U, mesh_.owned_cell_count,
                               gas_, cfl, communicator_);
  }
  exchange_halo(mesh_, U, 4U, communicator_);
  const ReconstructionData& reconstruction = reconstruction_.compute(U);

  result.value.assign(mesh_.owned_cell_count * 4U, 0.0);
  residual_accumulator_.assign(mesh_.owned_cell_count * 4U, {});
  result.spectral_radius.assign(mesh_.owned_cell_count, 0.0);
  result.local_time_step.assign(mesh_.owned_cell_count, 0.0);
  result.norms = {};
  result.diagnostics = {};
  result.surface.clear();
  if (options.collect_surface) result.surface.reserve(boundary_face_count_);

  auto face_state = [&](LocalIndex cell, LocalIndex face_index) {
    const Primitive& center =
        reconstruction.primitive[static_cast<std::size_t>(cell)];
    if (options.reconstruction_blend == 0.0) return center;
    const Primitive reconstructed =
        reconstruction_.face_value(cell, face_index);
    if (options.reconstruction_blend == 1.0) return reconstructed;
    return blend_reconstructed_primitive(
        center, reconstructed, options.reconstruction_blend, gas_);
  };

  for (std::size_t local_face = 0; local_face < mesh_.faces.size(); ++local_face) {
    const LocalFace& face = mesh_.faces[local_face];
    const LocalIndex face_index = static_cast<LocalIndex>(local_face);
    const std::size_t left_index = static_cast<std::size_t>(face.left_cell);
    const bool left_owned = left_index < mesh_.owned_cell_count;
    const Primitive left_face = face_state(face.left_cell, face_index);
    const Conservative left_conservative = gas_.conservative(left_face);
    Conservative total_flux{};
    double convective_radius = 0.0;
    double viscous_radius = 0.0;

    if (face.right_cell >= 0) {
      const std::size_t right_index = static_cast<std::size_t>(face.right_cell);
      const bool right_owned = right_index < mesh_.owned_cell_count;
      const Primitive right_face = face_state(face.right_cell, face_index);
      const Conservative right_conservative = gas_.conservative(right_face);
      const NumericalFlux inviscid = rusanov_flux(
          left_conservative, right_conservative, face.normal, gas_, dissipation_scale_);
      total_flux = inviscid.value;
      convective_radius = inviscid.spectral_radius;

      if (viscosity_ > 0.0) {
        const Primitive& left_center = reconstruction.primitive[left_index];
        const Primitive& right_center = reconstruction.primitive[right_index];
        const auto left_gradient = velocity_temperature_gradients(
            left_center, reconstruction.gradient[left_index], gas_);
        const auto right_gradient = velocity_temperature_gradients(
            right_center, reconstruction.gradient[right_index], gas_);
        const Vec2 displacement =
            mesh_.cells[right_index].center - mesh_.cells[left_index].center;
        VelocityTemperatureGradients corrected;
        corrected.u = corrected_face_gradient(left_gradient.u, right_gradient.u,
                                              left_center.u, right_center.u, displacement);
        corrected.v = corrected_face_gradient(left_gradient.v, right_gradient.v,
                                              left_center.v, right_center.v, displacement);
        corrected.temperature = corrected_face_gradient(
            left_gradient.temperature, right_gradient.temperature, left_center.T,
            right_center.T, displacement);
        const Primitive face_primitive = average_primitive(left_face, right_face, gas_);
        const ViscousFaceFlux viscous =
            viscous_flux(face_primitive, corrected, face.normal, viscosity_, gas_);
        for (std::size_t k = 0; k < 4U; ++k) total_flux[k] -= viscous.value[k];
        const double inverse_density = std::max(1.0 / left_center.rho,
                                                1.0 / right_center.rho);
        const double diffusion = viscosity_ * inverse_density *
            std::max(4.0 / 3.0, gas_.gamma() / gas_.prandtl());
        const double distance = std::max(norm(displacement), 1.0e-14);
        viscous_radius = 2.0 * diffusion / distance;
      }
      if (left_owned) {
        add_flux(residual_accumulator_, left_index, total_flux, face.length);
        result.spectral_radius[left_index] +=
            (convective_radius + viscous_radius) * face.length;
      }
      if (right_owned) {
        add_flux(residual_accumulator_, right_index, total_flux, -face.length);
        result.spectral_radius[right_index] +=
            (convective_radius + viscous_radius) * face.length;
      }
      continue;
    }

    if (!left_owned) {
      throw std::logic_error("boundary face does not belong to an owned cell");
    }
    const BoundaryCondition condition = config_.boundary_conditions.at(face.boundary);
    SurfaceBoundaryState surface;
    surface.face_id = face.global_id;
    surface.condition = condition;
    surface.outward_fluid_normal = face.normal;
    surface.center = face.center;
    surface.length = face.length;

    if (condition == BoundaryCondition::farfield) {
      const Primitive exterior = boundary_exterior_state(
          condition, left_face, freestream_primitive_, face.normal, gas_);
      const NumericalFlux inviscid = rusanov_flux(
          left_conservative, gas_.conservative(exterior), face.normal, gas_,
          dissipation_scale_);
      total_flux = inviscid.value;
      convective_radius = inviscid.spectral_radius;
      surface.state = exterior;
    } else {
      total_flux = pressure_wall_flux(left_face.p, face.normal);
      convective_radius = std::abs(left_face.u * face.normal.x +
                                   left_face.v * face.normal.y) + left_face.a;
      surface.state = left_face;
      if (condition == BoundaryCondition::slip_wall) {
        const double normal_velocity =
            surface.state.u * face.normal.x + surface.state.v * face.normal.y;
        surface.state.u -= normal_velocity * face.normal.x;
        surface.state.v -= normal_velocity * face.normal.y;
      } else {
        surface.state.u = 0.0;
        surface.state.v = 0.0;
      }
      surface.pressure_body_force = left_face.p * face.length * face.normal;

      if (condition == BoundaryCondition::no_slip_adiabatic_wall && viscosity_ > 0.0) {
        const Primitive& center = reconstruction.primitive[left_index];
        const auto gradient = velocity_temperature_gradients(
            center, reconstruction.gradient[left_index], gas_);
        const Vec2 center_to_face = face.center - mesh_.cells[left_index].center;
        double distance = std::abs(dot(center_to_face, face.normal));
        if (!(distance > 1.0e-14)) distance = std::max(norm(center_to_face), 1.0e-14);
        const ViscousFaceFlux viscous = no_slip_adiabatic_wall_flux(
            center, gradient, face.normal, distance, viscosity_, gas_);
        for (std::size_t k = 0; k < 4U; ++k) total_flux[k] -= viscous.value[k];
        // Total force uses the complete viscous traction.  The separate skin
        // friction field intentionally retains only its tangential projection.
        surface.tangential_shear_force =
            -1.0 * face.length * viscous.tangential_traction;
        surface.viscous_body_force = -1.0 * face.length * viscous.traction;
        const double diffusion = viscosity_ / center.rho *
            std::max(4.0 / 3.0, gas_.gamma() / gas_.prandtl());
        viscous_radius = 2.0 * diffusion / distance;
      }
    }
    add_flux(residual_accumulator_, left_index, total_flux, face.length);
    result.spectral_radius[left_index] +=
        (convective_radius + viscous_radius) * face.length;
    if (options.collect_surface) result.surface.push_back(surface);
  }

  for (std::size_t index = 0; index < result.value.size(); ++index) {
    result.value[index] = residual_accumulator_[index].value();
  }
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const double radius = std::max(result.spectral_radius[cell], 1.0e-300);
    result.local_time_step[cell] = cfl * mesh_.cells[cell].area / radius;
  }
  if (options.compute_global_norms) {
    result.norms = global_residual_norms(
        result.value, mesh_.owned_cell_count, mesh_.global_cell_count,
        communicator_);
  }

  if (options.compute_global_diagnostics) {
    const ReconstructionDiagnostics& local = reconstruction_.data().diagnostics;
    std::array<unsigned long long, 5> local_diagnostics{
        static_cast<unsigned long long>(local.venkatakrishnan_limited_face_components),
        static_cast<unsigned long long>(local.shock_fallback_cells),
        static_cast<unsigned long long>(local.positivity_barth_fallbacks),
        static_cast<unsigned long long>(local.positivity_scaled),
        static_cast<unsigned long long>(local.first_order_fallbacks)};
    std::array<unsigned long long, 5> global_diagnostics{};
    MPI_Allreduce(local_diagnostics.data(), global_diagnostics.data(), 5,
                  MPI_UNSIGNED_LONG_LONG, MPI_SUM, communicator_);
    result.diagnostics.venkatakrishnan_limited_face_components = global_diagnostics[0];
    result.diagnostics.shock_fallback_cells = global_diagnostics[1];
    result.diagnostics.positivity_barth_fallbacks = global_diagnostics[2];
    result.diagnostics.positivity_scaled = global_diagnostics[3];
    result.diagnostics.first_order_fallbacks = global_diagnostics[4];
  }
}

void ResidualOperator::finalize_cached_result(ResidualResult& result) {
  if (result.value.size() != mesh_.owned_cell_count * 4U) {
    throw std::invalid_argument("cached residual dimensions do not match local mesh");
  }
  result.norms = global_residual_norms(
      result.value, mesh_.owned_cell_count, mesh_.global_cell_count,
      communicator_);

  const ReconstructionDiagnostics& local = reconstruction_.data().diagnostics;
  const std::array<unsigned long long, 5> local_diagnostics{
      static_cast<unsigned long long>(local.venkatakrishnan_limited_face_components),
      static_cast<unsigned long long>(local.shock_fallback_cells),
      static_cast<unsigned long long>(local.positivity_barth_fallbacks),
      static_cast<unsigned long long>(local.positivity_scaled),
      static_cast<unsigned long long>(local.first_order_fallbacks)};
  std::array<unsigned long long, 5> global_diagnostics{};
  MPI_Allreduce(local_diagnostics.data(), global_diagnostics.data(), 5,
                MPI_UNSIGNED_LONG_LONG, MPI_SUM, communicator_);
  result.diagnostics.venkatakrishnan_limited_face_components = global_diagnostics[0];
  result.diagnostics.shock_fallback_cells = global_diagnostics[1];
  result.diagnostics.positivity_barth_fallbacks = global_diagnostics[2];
  result.diagnostics.positivity_scaled = global_diagnostics[3];
  result.diagnostics.first_order_fallbacks = global_diagnostics[4];
}

ForceCoefficients ResidualOperator::forces(const ResidualResult& residual) const {
  std::array<double, 6> local{};  // pressure Fx,Fy,M; viscous Fx,Fy,M
  for (const SurfaceBoundaryState& face : residual.surface) {
    if (face.condition == BoundaryCondition::farfield) continue;
    local[0] += face.pressure_body_force.x;
    local[1] += face.pressure_body_force.y;
    local[2] += cross(face.center - config_.reference.moment_center,
                      face.pressure_body_force);
    local[3] += face.viscous_body_force.x;
    local[4] += face.viscous_body_force.y;
    local[5] += cross(face.center - config_.reference.moment_center,
                      face.viscous_body_force);
  }
  std::array<double, 6> global{};
  MPI_Allreduce(local.data(), global.data(), 6, MPI_DOUBLE, MPI_SUM, communicator_);
  constexpr double pi = 3.141592653589793238462643383279502884;
  const double angle = config_.freestream.aoa_degrees * pi / 180.0;
  const Vec2 drag_direction{std::cos(angle), std::sin(angle)};
  const Vec2 lift_direction{-std::sin(angle), std::cos(angle)};
  const double scale = 0.5 * config_.freestream.rho *
                       config_.freestream.velocity_magnitude *
                       config_.freestream.velocity_magnitude * config_.reference.area;
  const double moment_scale = scale * config_.reference.length;
  const Vec2 pressure{global[0], global[1]};
  const Vec2 viscous{global[3], global[4]};
  ForceCoefficients result;
  result.cd_pressure = dot(pressure, drag_direction) / scale;
  result.cl_pressure = dot(pressure, lift_direction) / scale;
  result.cmz_pressure = global[2] / moment_scale;
  result.cd_viscous = dot(viscous, drag_direction) / scale;
  result.cl_viscous = dot(viscous, lift_direction) / scale;
  result.cmz_viscous = global[5] / moment_scale;
  result.cd = result.cd_pressure + result.cd_viscous;
  result.cl = result.cl_pressure + result.cl_viscous;
  result.cmz = result.cmz_pressure + result.cmz_viscous;
  return result;
}

}  // namespace cfd
