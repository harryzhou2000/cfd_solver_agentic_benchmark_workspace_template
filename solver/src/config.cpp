#include "cfd/config.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {
namespace {

using Json = nlohmann::json;

const Json& required(const Json& object, const char* key) {
  if (!object.contains(key)) {
    throw std::runtime_error(std::string("case JSON is missing required key: ") + key);
  }
  return object.at(key);
}

double number(const Json& object, const char* key) {
  if (!object.contains(key) || !object.at(key).is_number()) {
    throw std::runtime_error(std::string("case JSON key must be numeric: ") + key);
  }
  const double value = object.at(key).get<double>();
  if (!std::isfinite(value)) {
    throw std::runtime_error(std::string("case JSON key must be finite: ") + key);
  }
  return value;
}

int integer(const Json& object, const char* key) {
  const double value = number(object, key);
  if (std::floor(value) != value || value < 0.0) {
    throw std::runtime_error(std::string("case JSON key must be a nonnegative integer: ") + key);
  }
  return static_cast<int>(value);
}

BoundaryType parse_boundary_type(const std::string& value) {
  if (value == "farfield") {
    return BoundaryType::farfield;
  }
  if (value == "slip_wall") {
    return BoundaryType::slip_wall;
  }
  if (value == "no_slip_adiabatic_wall") {
    return BoundaryType::no_slip_adiabatic_wall;
  }
  throw std::runtime_error("unsupported boundary-condition type in case JSON: " + value);
}

}  // namespace

CaseConfig load_case_config(const std::filesystem::path& case_path) {
  std::ifstream stream(case_path);
  if (!stream) {
    throw std::runtime_error("could not open case JSON: " + case_path.string());
  }

  Json root;
  try {
    stream >> root;
  } catch (const std::exception& error) {
    throw std::runtime_error("invalid case JSON " + case_path.string() + ": " + error.what());
  }

  CaseConfig result;
  result.source_path = std::filesystem::absolute(case_path);
  result.schema_version = integer(root, "schema_version");
  if (result.schema_version != 1) {
    throw std::runtime_error("unsupported case schema_version " + std::to_string(result.schema_version) +
                             "; only schema version 1 is supported");
  }
  result.case_id = required(root, "case_id").get<std::string>();
  result.description = root.value("description", std::string{});

  const auto& mesh = required(root, "mesh");
  if (!mesh.is_object()) {
    throw std::runtime_error("case JSON mesh must be an object");
  }
  const std::string mesh_name = required(mesh, "file").get<std::string>();
  const auto mesh_path = std::filesystem::path(mesh_name);
  result.mesh_file = mesh_path.is_absolute()
                         ? mesh_path
                         : std::filesystem::absolute(case_path.parent_path() / mesh_path);
  if (mesh.value("format", std::string{}) != "CGNS") {
    throw std::runtime_error("only CGNS mesh input is supported");
  }
  if (integer(mesh, "dimension") != 2) {
    throw std::runtime_error("this solver supports only two-dimensional cases");
  }

  const auto& physics = required(root, "physics");
  if (!physics.is_object()) {
    throw std::runtime_error("case JSON physics must be an object");
  }
  if (physics.value("equations", std::string{}) != "compressible_navier_stokes") {
    throw std::runtime_error("only compressible_navier_stokes cases are supported");
  }
  const std::string mode = required(physics, "mode").get<std::string>();
  if (mode == "inviscid") {
    result.physics_mode = PhysicsMode::Inviscid;
  } else if (mode == "laminar") {
    result.physics_mode = PhysicsMode::Laminar;
    result.reynolds = number(physics, "reynolds");
    if (result.reynolds <= 0.0) {
      throw std::runtime_error("laminar case Reynolds number must be positive");
    }
  } else {
    throw std::runtime_error("unsupported physics mode: " + mode);
  }

  const auto& gas = required(root, "gas");
  if (!gas.is_object()) {
    throw std::runtime_error("case JSON gas must be an object");
  }
  if (gas.value("model", std::string{}) != "calorically_perfect") {
    throw std::runtime_error("only calorically perfect gas cases are supported");
  }
  result.gas.gamma = number(gas, "gamma");
  result.gas.gas_constant = number(gas, "R");
  result.gas.prandtl = number(gas, "prandtl");
  if (result.gas.gamma <= 1.0 || result.gas.gas_constant <= 0.0 || result.gas.prandtl <= 0.0) {
    throw std::runtime_error("gas constants must be positive and gamma must exceed one");
  }

  const auto& freestream = required(root, "freestream");
  if (!freestream.is_object()) {
    throw std::runtime_error("case JSON freestream must be an object");
  }
  result.freestream.mach = number(freestream, "mach");
  result.freestream.aoa_degrees = number(freestream, "aoa_degrees");
  result.freestream.rho = number(freestream, "rho");
  result.freestream.velocity_magnitude = number(freestream, "velocity_magnitude");
  result.freestream.pressure = number(freestream, "pressure");
  if (result.freestream.rho <= 0.0 || result.freestream.velocity_magnitude < 0.0 ||
      result.freestream.pressure <= 0.0 || result.freestream.mach < 0.0) {
    throw std::runtime_error("freestream density and pressure must be positive");
  }

  const auto& reference = required(root, "reference");
  if (!reference.is_object()) {
    throw std::runtime_error("case JSON reference must be an object");
  }
  result.reference.length = number(reference, "length");
  result.reference.area = number(reference, "area");
  result.reference.reynolds_length = number(reference, "reynolds_length");
  if (!reference.contains("moment_center") || !reference.at("moment_center").is_array() ||
      reference.at("moment_center").size() != 2) {
    throw std::runtime_error("reference.moment_center must be an array of length two");
  }
  result.reference.moment_center = {reference.at("moment_center")[0].get<double>(),
                                    reference.at("moment_center")[1].get<double>()};
  if (result.reference.length <= 0.0 || result.reference.area <= 0.0 ||
      result.reference.reynolds_length <= 0.0) {
    throw std::runtime_error("reference scales must be positive");
  }

  const auto& boundary_conditions = required(root, "boundary_conditions");
  if (!boundary_conditions.is_object()) {
    throw std::runtime_error("case JSON boundary_conditions must be an object");
  }
  for (const auto& [name, value] : boundary_conditions.items()) {
    if (!value.is_string()) {
      throw std::runtime_error("boundary-condition value for " + name + " must be a string");
    }
    result.boundary_conditions.emplace(name, parse_boundary_type(value.get<std::string>()));
  }
  if (result.boundary_conditions.empty()) {
    throw std::runtime_error("case JSON has no boundary-condition mappings");
  }

  const auto& run = required(root, "run_control");
  if (!run.is_object()) {
    throw std::runtime_error("case JSON run_control must be an object");
  }
  const std::string type = required(run, "type").get<std::string>();
  if (type == "steady") {
    result.run.type = RunType::Steady;
    result.run.max_steps = integer(run, "max_steps");
    result.run.residual_reduction_target = number(run, "residual_reduction_target");
    result.run.time_integrator = "steady_pseudo_time_block_jacobi";
  } else if (type == "transient") {
    result.run.type = RunType::Transient;
    result.run.time_step = number(run, "time_step");
    result.run.final_time = number(run, "final_time");
    result.run.time_integrator = required(run, "time_integrator").get<std::string>();
    if (result.run.time_step <= 0.0 || result.run.final_time <= 0.0) {
      throw std::runtime_error("transient time_step and final_time must be positive");
    }
  } else {
    throw std::runtime_error("unsupported run-control type: " + type);
  }
  result.run.cfl_initial = number(run, "cfl_initial");
  result.run.cfl_max = number(run, "cfl_max");
  result.run.pseudo_cfl_ramp_steps = integer(run, "pseudo_cfl_ramp_steps");
  result.run.min_inner_iterations = integer(run, "min_inner_iterations");
  result.run.max_inner_iterations = integer(run, "max_inner_iterations");
  result.run.inner_residual_reduction_target = number(run, "inner_residual_reduction_target");
  result.run.rusanov_dissipation_scale = run.value("rusanov_dissipation_scale", 1.0);
  result.run.reconstruction_gradient_scale = run.value("reconstruction_gradient_scale", 1.0);
  result.run.steady_newton_only = run.value("steady_newton_only", false);
  if (result.run.cfl_initial <= 0.0 || result.run.cfl_max <= 0.0 ||
      result.run.min_inner_iterations <= 0 || result.run.max_inner_iterations < result.run.min_inner_iterations ||
      result.run.inner_residual_reduction_target <= 0.0 || result.run.rusanov_dissipation_scale <= 0.0 ||
      result.run.reconstruction_gradient_scale < 0.0 || result.run.reconstruction_gradient_scale > 1.0) {
    throw std::runtime_error("invalid run-control values");
  }
  return result;
}

std::string to_string(const PhysicsMode mode) {
  return mode == PhysicsMode::Inviscid ? "inviscid" : "laminar";
}

std::string to_string(const RunType type) {
  return type == RunType::Steady ? "steady" : "transient";
}

std::string to_string(const BoundaryType type) {
  switch (type) {
    case BoundaryType::farfield:
      return "farfield";
    case BoundaryType::slip_wall:
      return "slip_wall";
    case BoundaryType::no_slip_adiabatic_wall:
      return "no_slip_adiabatic_wall";
    case BoundaryType::interior:
      return "interior";
    case BoundaryType::unspecified:
      return "unspecified";
  }
  return "unknown";
}

}  // namespace cfd
