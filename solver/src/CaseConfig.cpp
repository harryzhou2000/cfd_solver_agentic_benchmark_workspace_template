#include "cfd/CaseConfig.hpp"
#include "cfd/Json.hpp"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <utility>

namespace cfd {
namespace {

using Json = json::Value;

[[noreturn]] void fail(const std::string& field, const std::string& detail) {
  throw CaseConfigError("invalid case configuration at '" + field + "': " + detail);
}

const Json& object_at(const Json& parent, const char* key) {
  const auto& values = parent.object();
  const auto it = values.find(key);
  if (it == values.end()) {
    fail(key, "required field is missing");
  }
  if (!it->second.is_object()) {
    fail(key, "must be an object");
  }
  return it->second;
}

template <typename T>
T required(const Json& object, const char* key) {
  const auto& values = object.object();
  const auto it = values.find(key);
  if (it == values.end() || it->second.is_null()) {
    fail(key, "required field is missing");
  }
  try {
    return it->second.get<T>();
  } catch (const json::Error& error) {
    fail(key, error.what());
  }
}

template <typename T>
std::optional<T> optional(const Json& object, const char* key) {
  const auto& values = object.object();
  const auto it = values.find(key);
  if (it == values.end() || it->second.is_null()) {
    return std::nullopt;
  }
  try {
    return it->second.get<T>();
  } catch (const json::Error& error) {
    fail(key, error.what());
  }
}

std::array<double, 2> required_pair(const Json& object, const char* key) {
  const auto& values = object.object();
  const auto it = values.find(key);
  if (it == values.end() || !it->second.is_array() || it->second.size() != 2) {
    fail(key, "must be an array of exactly two numbers");
  }
  try {
    return {it->second[0].get<double>(), it->second[1].get<double>()};
  } catch (const json::Error& error) {
    fail(key, error.what());
  }
}

std::optional<std::array<double, 2>> optional_pair(const Json& object, const char* key) {
  const auto& values = object.object();
  if (values.find(key) == values.end() || values.at(key).is_null()) {
    return std::nullopt;
  }
  return required_pair(object, key);
}

bool finite(double value) { return std::isfinite(value); }

void require_finite(double value, const char* field) {
  if (!finite(value)) {
    fail(field, "must be finite");
  }
}

void require_positive(double value, const char* field) {
  require_finite(value, field);
  if (value <= 0.0) {
    fail(field, "must be positive");
  }
}

void require_positive(int value, const char* field) {
  if (value <= 0) {
    fail(field, "must be positive");
  }
}

void require_one_of(const std::string& value, const char* field,
                    std::initializer_list<const char*> allowed) {
  for (const char* candidate : allowed) {
    if (value == candidate) {
      return;
    }
  }
  std::ostringstream options;
  bool first = true;
  for (const char* candidate : allowed) {
    if (!first) {
      options << ", ";
    }
    options << candidate;
    first = false;
  }
  fail(field, "must be one of: " + options.str());
}

}  // namespace

CaseConfig CaseConfig::load(const std::filesystem::path& case_file) {
  std::ifstream input(case_file);
  if (!input) {
    throw CaseConfigError("cannot open case configuration: " + case_file.string());
  }

  Json root;
  try {
    std::ostringstream text;
    text << input.rdbuf();
    root = Json::parse(text.str());
  } catch (const json::Error& error) {
    throw CaseConfigError("cannot parse JSON in " + case_file.string() + ": " + error.what());
  }
  if (!root.is_object()) {
    throw CaseConfigError("case configuration root must be an object: " + case_file.string());
  }

  CaseConfig result;
  result.source_file = std::filesystem::absolute(case_file).lexically_normal();
  result.schema_version = required<int>(root, "schema_version");
  result.case_id = required<std::string>(root, "case_id");
  result.description = required<std::string>(root, "description");

  const auto& mesh = object_at(root, "mesh");
  const auto mesh_file = std::filesystem::path(required<std::string>(mesh, "file"));
  result.mesh.file = mesh_file.is_absolute()
      ? mesh_file.lexically_normal()
      : (result.source_file.parent_path() / mesh_file).lexically_normal();
  result.mesh.format = required<std::string>(mesh, "format");
  result.mesh.dimension = required<int>(mesh, "dimension");

  const auto& physics = object_at(root, "physics");
  result.physics.equations = required<std::string>(physics, "equations");
  result.physics.mode = required<std::string>(physics, "mode");
  result.physics.reynolds = optional<double>(physics, "reynolds");
  result.physics.viscosity_model = optional<std::string>(physics, "viscosity_model");

  const auto& gas = object_at(root, "gas");
  result.gas.model = required<std::string>(gas, "model");
  result.gas.gamma = required<double>(gas, "gamma");
  result.gas.gas_constant = required<double>(gas, "R");
  result.gas.prandtl = required<double>(gas, "prandtl");

  const auto& freestream = object_at(root, "freestream");
  result.freestream.mach = required<double>(freestream, "mach");
  result.freestream.aoa_degrees = required<double>(freestream, "aoa_degrees");
  result.freestream.rho = required<double>(freestream, "rho");
  result.freestream.velocity_magnitude = required<double>(freestream, "velocity_magnitude");
  result.freestream.pressure = required<double>(freestream, "pressure");

  const auto& reference = object_at(root, "reference");
  result.reference.length = required<double>(reference, "length");
  result.reference.area = required<double>(reference, "area");
  result.reference.moment_center = required_pair(reference, "moment_center");
  result.reference.reynolds_length = required<double>(reference, "reynolds_length");

  const auto& boundaries = object_at(root, "boundary_conditions");
  for (const auto& [key, value] : boundaries.object()) {
    try {
      result.boundary_conditions.emplace(key, value.get<std::string>());
    } catch (const json::Error&) {
      fail("boundary_conditions." + key, "boundary type must be a string");
    }
  }

  const auto& numerics = object_at(root, "numerics_required");
  result.numerics_required.spatial_order = required<int>(numerics, "spatial_order");
  result.numerics_required.inviscid_flux = required<std::string>(numerics, "inviscid_flux");
  result.numerics_required.viscous_flux = required<std::string>(numerics, "viscous_flux");
  result.numerics_required.main_time_method = required<std::string>(numerics, "main_time_method");
  result.numerics_required.transient_order = optional<int>(numerics, "transient_order");
  result.numerics_required.implicit_solver = optional<std::string>(numerics, "implicit_solver");

  const auto& run = object_at(root, "run_control");
  result.run_control.type = required<std::string>(run, "type");
  result.run_control.max_steps = optional<int>(run, "max_steps");
  result.run_control.residual_reduction_target = optional<double>(run, "residual_reduction_target");
  result.run_control.cfl_initial = required<double>(run, "cfl_initial");
  result.run_control.cfl_max = required<double>(run, "cfl_max");
  result.run_control.pseudo_cfl_ramp_steps = required<int>(run, "pseudo_cfl_ramp_steps");
  result.run_control.min_inner_iterations = required<int>(run, "min_inner_iterations");
  result.run_control.max_inner_iterations = required<int>(run, "max_inner_iterations");
  result.run_control.inner_residual_reduction_target = required<double>(run, "inner_residual_reduction_target");
  result.run_control.time_integrator = optional<std::string>(run, "time_integrator");
  result.run_control.time_step = optional<double>(run, "time_step");
  result.run_control.final_time = optional<double>(run, "final_time");
  result.run_control.inner_residual_norm = optional<std::string>(run, "inner_residual_norm");
  result.run_control.bdf2_history_update = optional<std::string>(run, "bdf2_history_update");
  result.run_control.rusanov_dissipation_scale = optional<double>(run, "rusanov_dissipation_scale");

  const auto& outputs = object_at(root, "outputs");
  result.outputs.write_final_field = required<bool>(outputs, "write_final_field");
  result.outputs.write_surface = required<bool>(outputs, "write_surface");
  result.outputs.write_forces_every = required<int>(outputs, "write_forces_every");
  result.outputs.write_residuals_every = required<int>(outputs, "write_residuals_every");
  result.outputs.write_field_every_time = optional<double>(outputs, "write_field_every_time");
  result.outputs.wake_visualization = optional<std::string>(outputs, "wake_visualization");
  result.outputs.recommended_vorticity_clip_range = optional_pair(outputs, "recommended_vorticity_clip_range");

  result.validate();
  return result;
}

void CaseConfig::validate() const {
  if (schema_version != 1) {
    fail("schema_version", "only schema version 1 is supported");
  }
  if (case_id.empty()) fail("case_id", "must not be empty");
  if (mesh.file.empty()) fail("mesh.file", "must not be empty");
  require_one_of(mesh.format, "mesh.format", {"CGNS"});
  if (mesh.dimension != 2) fail("mesh.dimension", "only two-dimensional cases are supported");

  require_one_of(physics.equations, "physics.equations", {"compressible_navier_stokes"});
  require_one_of(physics.mode, "physics.mode", {"inviscid", "laminar"});
  if (physics.mode == "laminar") {
    if (!physics.reynolds) fail("physics.reynolds", "is required for laminar mode");
    require_positive(*physics.reynolds, "physics.reynolds");
    if (!physics.viscosity_model) fail("physics.viscosity_model", "is required for laminar mode");
  } else if (physics.reynolds || physics.viscosity_model) {
    fail("physics", "inviscid mode must not define Reynolds number or viscosity model");
  }

  require_one_of(gas.model, "gas.model", {"calorically_perfect"});
  if (!(gas.gamma > 1.0) || !finite(gas.gamma)) fail("gas.gamma", "must be finite and greater than one");
  require_positive(gas.gas_constant, "gas.R");
  require_positive(gas.prandtl, "gas.prandtl");
  if (freestream.mach < 0.0 || !finite(freestream.mach)) fail("freestream.mach", "must be finite and nonnegative");
  require_finite(freestream.aoa_degrees, "freestream.aoa_degrees");
  require_positive(freestream.rho, "freestream.rho");
  require_positive(freestream.velocity_magnitude, "freestream.velocity_magnitude");
  require_positive(freestream.pressure, "freestream.pressure");
  require_positive(reference.length, "reference.length");
  require_positive(reference.area, "reference.area");
  require_positive(reference.reynolds_length, "reference.reynolds_length");
  require_finite(reference.moment_center[0], "reference.moment_center[0]");
  require_finite(reference.moment_center[1], "reference.moment_center[1]");

  if (boundary_conditions.empty()) fail("boundary_conditions", "must not be empty");
  for (const auto& [family, type] : boundary_conditions) {
    if (family.empty()) fail("boundary_conditions", "boundary family names must not be empty");
    require_one_of(type, ("boundary_conditions." + family).c_str(),
                   {"farfield", "slip_wall", "no_slip_adiabatic_wall"});
    if (physics.mode == "inviscid" && type == "no_slip_adiabatic_wall") {
      fail("boundary_conditions." + family, "no-slip wall is incompatible with inviscid mode");
    }
  }

  if (numerics_required.spatial_order < 1) fail("numerics_required.spatial_order", "must be positive");
  if (numerics_required.inviscid_flux.empty()) fail("numerics_required.inviscid_flux", "must not be empty");
  if (numerics_required.main_time_method.empty()) fail("numerics_required.main_time_method", "must not be empty");
  if (physics.mode == "inviscid" && numerics_required.viscous_flux != "disabled") {
    fail("numerics_required.viscous_flux", "must be disabled for inviscid mode");
  }
  if (physics.mode == "laminar" && numerics_required.viscous_flux != "required") {
    fail("numerics_required.viscous_flux", "must be required for laminar mode");
  }
  if (numerics_required.transient_order && *numerics_required.transient_order < 1) {
    fail("numerics_required.transient_order", "must be positive");
  }

  require_one_of(run_control.type, "run_control.type", {"steady", "transient"});
  require_positive(run_control.cfl_initial, "run_control.cfl_initial");
  require_positive(run_control.cfl_max, "run_control.cfl_max");
  if (run_control.cfl_max < run_control.cfl_initial) fail("run_control.cfl_max", "must be at least cfl_initial");
  if (run_control.pseudo_cfl_ramp_steps < 0) fail("run_control.pseudo_cfl_ramp_steps", "must be nonnegative");
  require_positive(run_control.min_inner_iterations, "run_control.min_inner_iterations");
  require_positive(run_control.max_inner_iterations, "run_control.max_inner_iterations");
  if (run_control.max_inner_iterations < run_control.min_inner_iterations) {
    fail("run_control.max_inner_iterations", "must be at least min_inner_iterations");
  }
  require_positive(run_control.inner_residual_reduction_target, "run_control.inner_residual_reduction_target");
  if (run_control.type == "steady") {
    if (!run_control.max_steps || !run_control.residual_reduction_target) {
      fail("run_control", "steady cases require max_steps and residual_reduction_target");
    }
    require_positive(*run_control.max_steps, "run_control.max_steps");
    require_positive(*run_control.residual_reduction_target, "run_control.residual_reduction_target");
    if (run_control.time_integrator || run_control.time_step || run_control.final_time ||
        run_control.inner_residual_norm || run_control.bdf2_history_update || run_control.rusanov_dissipation_scale) {
      fail("run_control", "steady cases must not define transient-only controls");
    }
  } else {
    if (!run_control.time_integrator || !run_control.time_step || !run_control.final_time ||
        !run_control.inner_residual_norm || !run_control.bdf2_history_update) {
      fail("run_control", "transient cases require time integrator, timestep, final time, and inner-loop controls");
    }
    require_positive(*run_control.time_step, "run_control.time_step");
    require_positive(*run_control.final_time, "run_control.final_time");
    if (run_control.rusanov_dissipation_scale) {
      require_positive(*run_control.rusanov_dissipation_scale, "run_control.rusanov_dissipation_scale");
    }
  }

  require_positive(outputs.write_forces_every, "outputs.write_forces_every");
  require_positive(outputs.write_residuals_every, "outputs.write_residuals_every");
  if (outputs.write_field_every_time) require_positive(*outputs.write_field_every_time, "outputs.write_field_every_time");
  if (outputs.recommended_vorticity_clip_range) {
    const auto& range = *outputs.recommended_vorticity_clip_range;
    require_finite(range[0], "outputs.recommended_vorticity_clip_range[0]");
    require_finite(range[1], "outputs.recommended_vorticity_clip_range[1]");
    if (range[0] >= range[1]) fail("outputs.recommended_vorticity_clip_range", "must be ordered low to high");
  }
}

}  // namespace cfd
