#include "cfd/case_config.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace cfd {
namespace {

using json = nlohmann::json;

template <class T>
T required(const json& object, const char* key, const std::string& context) {
  if (!object.contains(key)) throw std::runtime_error(context + " missing required field '" + key + "'");
  try {
    return object.at(key).get<T>();
  } catch (const std::exception& error) {
    throw std::runtime_error(context + "." + key + ": " + error.what());
  }
}

void require_positive(double value, const std::string& name) {
  if (!std::isfinite(value) || value <= 0.0) throw std::runtime_error(name + " must be finite and positive");
}

}  // namespace

Primitive CaseConfig::freestream_primitive() const {
  const double angle = freestream.aoa_degrees * std::acos(-1.0) / 180.0;
  return {freestream.rho, freestream.velocity_magnitude * std::cos(angle),
          freestream.velocity_magnitude * std::sin(angle), freestream.pressure};
}

double CaseConfig::viscosity() const {
  if (!viscous) return 0.0;
  return freestream.rho * freestream.velocity_magnitude * reference.reynolds_length / reynolds;
}

double CaseConfig::dynamic_pressure() const {
  return 0.5 * freestream.rho * freestream.velocity_magnitude * freestream.velocity_magnitude;
}

CaseConfig load_case_config(const std::filesystem::path& input_path) {
  const auto path = std::filesystem::absolute(input_path).lexically_normal();
  std::ifstream stream(path);
  if (!stream) throw std::runtime_error("cannot open case JSON: " + path.string());

  json root;
  try {
    stream >> root;
  } catch (const std::exception& error) {
    throw std::runtime_error("malformed case JSON " + path.string() + ": " + error.what());
  }

  CaseConfig c;
  c.case_path = path;
  c.schema_version = required<int>(root, "schema_version", "case");
  if (c.schema_version != 1) {
    throw std::runtime_error("unsupported case schema_version " + std::to_string(c.schema_version) + "; expected 1");
  }
  c.case_id = required<std::string>(root, "case_id", "case");
  c.description = root.value("description", c.case_id);

  const auto& mesh = root.at("mesh");
  if (required<std::string>(mesh, "format", "mesh") != "CGNS") throw std::runtime_error("only CGNS mesh format is supported");
  if (required<int>(mesh, "dimension", "mesh") != 2) throw std::runtime_error("only two-dimensional cases are supported");
  c.mesh_path = required<std::string>(mesh, "file", "mesh");
  if (c.mesh_path.is_relative()) c.mesh_path = path.parent_path() / c.mesh_path;
  c.mesh_path = std::filesystem::absolute(c.mesh_path).lexically_normal();
  if (!std::filesystem::is_regular_file(c.mesh_path)) throw std::runtime_error("missing mesh file: " + c.mesh_path.string());

  const auto& physics = root.at("physics");
  if (required<std::string>(physics, "equations", "physics") != "compressible_navier_stokes") {
    throw std::runtime_error("physics.equations must be compressible_navier_stokes");
  }
  c.physics_mode = required<std::string>(physics, "mode", "physics");
  if (c.physics_mode == "laminar") {
    c.viscous = true;
    c.reynolds = required<double>(physics, "reynolds", "physics");
    require_positive(c.reynolds, "physics.reynolds");
    if (physics.value("viscosity_model", std::string("constant")) != "constant") {
      throw std::runtime_error("only constant laminar viscosity is currently supported");
    }
  } else if (c.physics_mode != "inviscid") {
    throw std::runtime_error("physics.mode must be inviscid or laminar");
  }

  const auto& gas = root.at("gas");
  if (required<std::string>(gas, "model", "gas") != "calorically_perfect") {
    throw std::runtime_error("gas.model must be calorically_perfect");
  }
  c.gas.gamma = required<double>(gas, "gamma", "gas");
  c.gas.gas_constant = required<double>(gas, "R", "gas");
  c.gas.prandtl = required<double>(gas, "prandtl", "gas");
  if (c.gas.gamma <= 1.0) throw std::runtime_error("gas.gamma must exceed one");
  require_positive(c.gas.gas_constant, "gas.R");
  require_positive(c.gas.prandtl, "gas.prandtl");

  const auto& fs = root.at("freestream");
  c.freestream.mach = required<double>(fs, "mach", "freestream");
  c.freestream.aoa_degrees = required<double>(fs, "aoa_degrees", "freestream");
  c.freestream.rho = required<double>(fs, "rho", "freestream");
  c.freestream.velocity_magnitude = required<double>(fs, "velocity_magnitude", "freestream");
  c.freestream.pressure = required<double>(fs, "pressure", "freestream");
  require_positive(c.freestream.rho, "freestream.rho");
  require_positive(c.freestream.velocity_magnitude, "freestream.velocity_magnitude");
  require_positive(c.freestream.pressure, "freestream.pressure");
  const double implied_mach = c.freestream.velocity_magnitude /
      std::sqrt(c.gas.gamma * c.freestream.pressure / c.freestream.rho);
  if (std::abs(implied_mach - c.freestream.mach) > 1.0e-8 * std::max(1.0, c.freestream.mach)) {
    throw std::runtime_error("freestream Mach is inconsistent with rho, velocity, pressure, and gamma");
  }

  const auto& ref = root.at("reference");
  c.reference.length = required<double>(ref, "length", "reference");
  c.reference.area = required<double>(ref, "area", "reference");
  c.reference.reynolds_length = required<double>(ref, "reynolds_length", "reference");
  const auto center = required<std::vector<double>>(ref, "moment_center", "reference");
  if (center.size() != 2) throw std::runtime_error("reference.moment_center must have two entries");
  c.reference.moment_center = {center[0], center[1]};
  require_positive(c.reference.length, "reference.length");
  require_positive(c.reference.area, "reference.area");
  require_positive(c.reference.reynolds_length, "reference.reynolds_length");

  const auto& bcs = root.at("boundary_conditions");
  if (!bcs.is_object() || bcs.empty()) throw std::runtime_error("boundary_conditions must be a non-empty object");
  for (auto it = bcs.begin(); it != bcs.end(); ++it) {
    c.boundary_conditions.emplace(it.key(), boundary_type_from_string(it.value().get<std::string>()));
  }

  const auto& numerics = root.at("numerics_required");
  if (required<int>(numerics, "spatial_order", "numerics_required") < 2) throw std::runtime_error("case requires second-order spatial accuracy");
  if (required<std::string>(numerics, "main_time_method", "numerics_required") != "implicit") {
    throw std::runtime_error("only required implicit main-time cases are supported");
  }

  const auto& run = root.at("run_control");
  const std::string run_type = required<std::string>(run, "type", "run_control");
  c.run.transient = run_type == "transient";
  if (!c.run.transient && run_type != "steady") throw std::runtime_error("run_control.type must be steady or transient");
  c.run.cfl_initial = required<double>(run, "cfl_initial", "run_control");
  c.run.cfl_max = required<double>(run, "cfl_max", "run_control");
  c.run.pseudo_cfl_ramp_steps = required<int>(run, "pseudo_cfl_ramp_steps", "run_control");
  c.run.min_inner_iterations = required<int>(run, "min_inner_iterations", "run_control");
  c.run.max_inner_iterations = required<int>(run, "max_inner_iterations", "run_control");
  c.run.inner_residual_reduction_target = required<double>(run, "inner_residual_reduction_target", "run_control");
  require_positive(c.run.cfl_initial, "run_control.cfl_initial");
  require_positive(c.run.cfl_max, "run_control.cfl_max");
  if (c.run.max_inner_iterations < c.run.min_inner_iterations || c.run.min_inner_iterations <= 0) {
    throw std::runtime_error("invalid run_control inner-iteration bounds");
  }
  if (c.run.inner_residual_reduction_target <= 0.0 || c.run.inner_residual_reduction_target >= 1.0) {
    throw std::runtime_error("run_control.inner_residual_reduction_target must be in (0,1)");
  }

  if (c.run.transient) {
    c.run.time_integrator = required<std::string>(run, "time_integrator", "run_control");
    if (c.run.time_integrator != "bdf2_or_trapezoidal") {
      throw std::runtime_error("transient time_integrator must request bdf2_or_trapezoidal");
    }
    c.run.time_step = required<double>(run, "time_step", "run_control");
    c.run.final_time = required<double>(run, "final_time", "run_control");
    c.run.rusanov_dissipation_scale = run.value("rusanov_dissipation_scale", 1.0);
    require_positive(c.run.time_step, "run_control.time_step");
    require_positive(c.run.final_time, "run_control.final_time");
    require_positive(c.run.rusanov_dissipation_scale, "run_control.rusanov_dissipation_scale");
    if (run.value("inner_residual_norm", std::string{}) != "total_spatial_plus_physical_time") {
      throw std::runtime_error("transient inner residual must include spatial and physical-time terms");
    }
    if (run.value("bdf2_history_update", std::string{}) != "after_inner_convergence") {
      throw std::runtime_error("BDF2 history must update only after inner convergence");
    }
  } else {
    c.run.max_steps = required<int>(run, "max_steps", "run_control");
    c.run.residual_reduction_target = required<double>(run, "residual_reduction_target", "run_control");
    if (c.run.max_steps <= 0) throw std::runtime_error("run_control.max_steps must be positive");
    require_positive(c.run.residual_reduction_target, "run_control.residual_reduction_target");
  }

  const auto& outputs = root.at("outputs");
  if (!outputs.value("write_final_field", false) || !outputs.value("write_surface", false)) {
    throw std::runtime_error("case must request final field and surface output");
  }
  c.outputs.write_forces_every = outputs.value("write_forces_every", 1);
  c.outputs.write_residuals_every = outputs.value("write_residuals_every", 1);
  c.outputs.write_field_every_time = outputs.value("write_field_every_time", 0.0);
  if (c.outputs.write_forces_every <= 0 || c.outputs.write_residuals_every <= 0) {
    throw std::runtime_error("output cadences must be positive");
  }
  return c;
}

}  // namespace cfd
