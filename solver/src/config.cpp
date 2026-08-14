#include "cfd/config.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cfd {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail(const std::string& source, const std::string& path,
                       const std::string& message) {
  throw std::runtime_error("case file '" + source + "', " + path + ": " + message);
}

void require_object(const Json& value, const std::string& source, const std::string& path) {
  if (!value.is_object()) {
    fail(source, path, "expected an object");
  }
}

void exact_keys(const Json& object, std::initializer_list<const char*> required,
                const std::string& source, const std::string& path) {
  require_object(object, source, path);
  std::set<std::string> expected;
  for (const char* key : required) {
    expected.emplace(key);
    if (!object.contains(key)) {
      fail(source, path, "missing required field '" + std::string(key) + "'");
    }
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (expected.count(it.key()) == 0U) {
      fail(source, path + "." + it.key(), "unknown field for schema version 1");
    }
  }
}

void allowed_keys(const Json& object, std::initializer_list<const char*> required,
                  std::initializer_list<const char*> optional, const std::string& source,
                  const std::string& path) {
  require_object(object, source, path);
  std::set<std::string> allowed;
  for (const char* key : required) {
    allowed.emplace(key);
    if (!object.contains(key)) {
      fail(source, path, "missing required field '" + std::string(key) + "'");
    }
  }
  for (const char* key : optional) {
    allowed.emplace(key);
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (allowed.count(it.key()) == 0U) {
      fail(source, path + "." + it.key(), "unknown field for schema version 1");
    }
  }
}

const Json& field(const Json& object, const char* key, const std::string& source,
                  const std::string& path) {
  if (!object.contains(key)) {
    fail(source, path, "missing required field '" + std::string(key) + "'");
  }
  return object.at(key);
}

std::string string_value(const Json& object, const char* key, const std::string& source,
                         const std::string& path) {
  const Json& value = field(object, key, source, path);
  if (!value.is_string()) {
    fail(source, path + "." + key, "expected a string");
  }
  const std::string result = value.get<std::string>();
  if (result.empty()) {
    fail(source, path + "." + key, "must not be empty");
  }
  return result;
}

double number_value(const Json& object, const char* key, const std::string& source,
                    const std::string& path) {
  const Json& value = field(object, key, source, path);
  if (!value.is_number()) {
    fail(source, path + "." + key, "expected a number");
  }
  const double result = value.get<double>();
  if (!std::isfinite(result)) {
    fail(source, path + "." + key, "must be finite");
  }
  return result;
}

double positive_number(const Json& object, const char* key, const std::string& source,
                       const std::string& path) {
  const double result = number_value(object, key, source, path);
  if (!(result > 0.0)) {
    fail(source, path + "." + key, "must be positive");
  }
  return result;
}

int integer_value(const Json& object, const char* key, const std::string& source,
                  const std::string& path, int minimum) {
  const Json& value = field(object, key, source, path);
  if (!value.is_number_integer()) {
    fail(source, path + "." + key, "expected an integer");
  }
  const auto wide = value.get<std::int64_t>();
  if (wide < minimum || wide > std::numeric_limits<int>::max()) {
    fail(source, path + "." + key, "integer is outside the accepted range");
  }
  return static_cast<int>(wide);
}

bool bool_value(const Json& object, const char* key, const std::string& source,
                const std::string& path) {
  const Json& value = field(object, key, source, path);
  if (!value.is_boolean()) {
    fail(source, path + "." + key, "expected a boolean");
  }
  return value.get<bool>();
}

Vec2 pair_value(const Json& object, const char* key, const std::string& source,
                const std::string& path) {
  const Json& value = field(object, key, source, path);
  if (!value.is_array() || value.size() != 2U || !value[0].is_number() ||
      !value[1].is_number()) {
    fail(source, path + "." + key, "expected an array of exactly two numbers");
  }
  Vec2 result{value[0].get<double>(), value[1].get<double>()};
  if (!finite(result)) {
    fail(source, path + "." + key, "entries must be finite");
  }
  return result;
}

void require_literal(const std::string& actual, const std::string& expected,
                     const std::string& source, const std::string& path) {
  if (actual != expected) {
    fail(source, path, "expected '" + expected + "', got '" + actual + "'");
  }
}

BoundaryCondition parse_boundary(const std::string& value, const std::string& source,
                                 const std::string& path) {
  if (value == "farfield") return BoundaryCondition::farfield;
  if (value == "slip_wall") return BoundaryCondition::slip_wall;
  if (value == "no_slip_adiabatic_wall") return BoundaryCondition::no_slip_adiabatic_wall;
  fail(source, path, "unsupported boundary condition '" + value + "'");
}

}  // namespace

CaseConfig parse_case_json(std::string_view text, const std::filesystem::path& base_directory,
                           std::string source_name) {
  Json root;
  try {
    root = Json::parse(text.begin(), text.end());
  } catch (const Json::parse_error& error) {
    fail(source_name, "$", std::string("invalid JSON: ") + error.what());
  }

  CaseConfig result;
  require_object(root, source_name, "$");
  result.schema_version = integer_value(root, "schema_version", source_name, "$", 1);
  if (result.schema_version != 1) {
    fail(source_name, "$.schema_version",
         "unsupported schema version " + std::to_string(result.schema_version) +
             "; only version 1 is supported");
  }
  exact_keys(root,
             {"schema_version", "case_id", "description", "mesh", "physics", "gas",
              "freestream", "reference", "boundary_conditions", "numerics_required",
              "run_control", "outputs"},
             source_name, "$");
  result.case_id = string_value(root, "case_id", source_name, "$");
  result.description = string_value(root, "description", source_name, "$");

  const Json& mesh = root.at("mesh");
  exact_keys(mesh, {"file", "format", "dimension"}, source_name, "$.mesh");
  const auto mesh_text = string_value(mesh, "file", source_name, "$.mesh");
  std::filesystem::path mesh_path(mesh_text);
  if (!mesh_path.is_absolute()) {
    mesh_path = std::filesystem::absolute(base_directory) / mesh_path;
  }
  result.mesh.file = mesh_path.lexically_normal();
  result.mesh.format = string_value(mesh, "format", source_name, "$.mesh");
  require_literal(result.mesh.format, "CGNS", source_name, "$.mesh.format");
  result.mesh.dimension = integer_value(mesh, "dimension", source_name, "$.mesh", 1);
  if (result.mesh.dimension != 2) fail(source_name, "$.mesh.dimension", "must be 2");
  if (!std::filesystem::is_regular_file(result.mesh.file)) {
    fail(source_name, "$.mesh.file", "resolved mesh does not exist: " + result.mesh.file.string());
  }

  const Json& physics = root.at("physics");
  allowed_keys(physics, {"equations", "mode"}, {"reynolds", "viscosity_model"}, source_name,
               "$.physics");
  result.physics.equations = string_value(physics, "equations", source_name, "$.physics");
  require_literal(result.physics.equations, "compressible_navier_stokes", source_name,
                  "$.physics.equations");
  const auto mode = string_value(physics, "mode", source_name, "$.physics");
  if (mode == "inviscid") {
    result.physics.mode = PhysicsMode::inviscid;
    if (physics.contains("reynolds") || physics.contains("viscosity_model")) {
      fail(source_name, "$.physics", "inviscid mode must not define viscous properties");
    }
  } else if (mode == "laminar") {
    result.physics.mode = PhysicsMode::laminar;
    if (!physics.contains("reynolds") || !physics.contains("viscosity_model")) {
      fail(source_name, "$.physics", "laminar mode requires reynolds and viscosity_model");
    }
    result.physics.reynolds = positive_number(physics, "reynolds", source_name, "$.physics");
    result.physics.viscosity_model =
        string_value(physics, "viscosity_model", source_name, "$.physics");
    require_literal(*result.physics.viscosity_model, "constant", source_name,
                    "$.physics.viscosity_model");
  } else {
    fail(source_name, "$.physics.mode", "expected 'inviscid' or 'laminar'");
  }

  const Json& gas = root.at("gas");
  exact_keys(gas, {"model", "gamma", "R", "prandtl"}, source_name, "$.gas");
  result.gas.model = string_value(gas, "model", source_name, "$.gas");
  require_literal(result.gas.model, "calorically_perfect", source_name, "$.gas.model");
  result.gas.gamma = positive_number(gas, "gamma", source_name, "$.gas");
  if (!(result.gas.gamma > 1.0)) fail(source_name, "$.gas.gamma", "must exceed 1");
  result.gas.gas_constant = positive_number(gas, "R", source_name, "$.gas");
  result.gas.prandtl = positive_number(gas, "prandtl", source_name, "$.gas");

  const Json& freestream = root.at("freestream");
  exact_keys(freestream, {"mach", "aoa_degrees", "rho", "velocity_magnitude", "pressure"},
             source_name, "$.freestream");
  result.freestream.mach = number_value(freestream, "mach", source_name, "$.freestream");
  if (result.freestream.mach < 0.0) fail(source_name, "$.freestream.mach", "must be nonnegative");
  result.freestream.aoa_degrees =
      number_value(freestream, "aoa_degrees", source_name, "$.freestream");
  result.freestream.rho = positive_number(freestream, "rho", source_name, "$.freestream");
  result.freestream.velocity_magnitude =
      positive_number(freestream, "velocity_magnitude", source_name, "$.freestream");
  result.freestream.pressure =
      positive_number(freestream, "pressure", source_name, "$.freestream");

  const Json& reference = root.at("reference");
  exact_keys(reference, {"length", "area", "moment_center", "reynolds_length"}, source_name,
             "$.reference");
  result.reference.length = positive_number(reference, "length", source_name, "$.reference");
  result.reference.area = positive_number(reference, "area", source_name, "$.reference");
  result.reference.moment_center = pair_value(reference, "moment_center", source_name,
                                              "$.reference");
  result.reference.reynolds_length =
      positive_number(reference, "reynolds_length", source_name, "$.reference");

  const Json& boundaries = root.at("boundary_conditions");
  require_object(boundaries, source_name, "$.boundary_conditions");
  if (boundaries.empty()) fail(source_name, "$.boundary_conditions", "must not be empty");
  for (auto it = boundaries.begin(); it != boundaries.end(); ++it) {
    if (!it.value().is_string()) {
      fail(source_name, "$.boundary_conditions." + it.key(), "expected a string");
    }
    result.boundary_conditions.emplace(
        it.key(), parse_boundary(it.value().get<std::string>(), source_name,
                                 "$.boundary_conditions." + it.key()));
  }

  const Json& numerics = root.at("numerics_required");
  allowed_keys(numerics,
               {"spatial_order", "inviscid_flux", "viscous_flux", "main_time_method",
                "implicit_solver"},
               {"transient_order"}, source_name, "$.numerics_required");
  result.numerics_required.spatial_order =
      integer_value(numerics, "spatial_order", source_name, "$.numerics_required", 1);
  result.numerics_required.inviscid_flux =
      string_value(numerics, "inviscid_flux", source_name, "$.numerics_required");
  result.numerics_required.viscous_flux =
      string_value(numerics, "viscous_flux", source_name, "$.numerics_required");
  result.numerics_required.main_time_method =
      string_value(numerics, "main_time_method", source_name, "$.numerics_required");
  result.numerics_required.implicit_solver =
      string_value(numerics, "implicit_solver", source_name, "$.numerics_required");
  if (numerics.contains("transient_order")) {
    result.numerics_required.transient_order =
        integer_value(numerics, "transient_order", source_name, "$.numerics_required", 1);
  }

  const Json& run = root.at("run_control");
  const auto run_type = string_value(run, "type", source_name, "$.run_control");
  if (run_type == "steady") {
    allowed_keys(run,
                 {"type", "max_steps", "residual_reduction_target", "cfl_initial", "cfl_max",
                  "pseudo_cfl_ramp_steps", "min_inner_iterations", "max_inner_iterations",
                  "inner_residual_reduction_target"},
                 {"rusanov_dissipation_scale"}, source_name, "$.run_control");
    result.run_control.type = RunType::steady;
    result.run_control.max_steps = integer_value(run, "max_steps", source_name, "$.run_control", 1);
    result.run_control.residual_reduction_target =
        positive_number(run, "residual_reduction_target", source_name, "$.run_control");
  } else if (run_type == "transient") {
    exact_keys(run,
               {"type", "time_integrator", "time_step", "final_time", "min_inner_iterations",
                "max_inner_iterations", "inner_residual_reduction_target", "inner_residual_norm",
                "bdf2_history_update", "cfl_initial", "cfl_max", "pseudo_cfl_ramp_steps",
                "rusanov_dissipation_scale"},
               source_name, "$.run_control");
    result.run_control.type = RunType::transient;
    result.run_control.time_integrator =
        string_value(run, "time_integrator", source_name, "$.run_control");
    result.run_control.time_step = positive_number(run, "time_step", source_name, "$.run_control");
    result.run_control.final_time = positive_number(run, "final_time", source_name, "$.run_control");
    result.run_control.inner_residual_norm =
        string_value(run, "inner_residual_norm", source_name, "$.run_control");
    result.run_control.bdf2_history_update =
        string_value(run, "bdf2_history_update", source_name, "$.run_control");
    result.run_control.rusanov_dissipation_scale =
        positive_number(run, "rusanov_dissipation_scale", source_name, "$.run_control");
  } else {
    fail(source_name, "$.run_control.type", "expected 'steady' or 'transient'");
  }
  result.run_control.min_inner_iterations =
      integer_value(run, "min_inner_iterations", source_name, "$.run_control", 1);
  result.run_control.max_inner_iterations =
      integer_value(run, "max_inner_iterations", source_name, "$.run_control", 1);
  if (result.run_control.max_inner_iterations < result.run_control.min_inner_iterations) {
    fail(source_name, "$.run_control.max_inner_iterations",
         "must be at least min_inner_iterations");
  }
  result.run_control.inner_residual_reduction_target =
      positive_number(run, "inner_residual_reduction_target", source_name, "$.run_control");
  result.run_control.cfl_initial = positive_number(run, "cfl_initial", source_name, "$.run_control");
  result.run_control.cfl_max = positive_number(run, "cfl_max", source_name, "$.run_control");
  if (result.run_control.cfl_max < result.run_control.cfl_initial) {
    fail(source_name, "$.run_control.cfl_max", "must be at least cfl_initial");
  }
  result.run_control.pseudo_cfl_ramp_steps =
      integer_value(run, "pseudo_cfl_ramp_steps", source_name, "$.run_control", 0);
  if (run.contains("rusanov_dissipation_scale")) {
    result.run_control.rusanov_dissipation_scale =
        positive_number(run, "rusanov_dissipation_scale", source_name, "$.run_control");
  }

  const Json& outputs = root.at("outputs");
  if (result.run_control.type == RunType::steady) {
    exact_keys(outputs,
               {"write_final_field", "write_surface", "write_forces_every",
                "write_residuals_every"},
               source_name, "$.outputs");
  } else {
    exact_keys(outputs,
               {"write_final_field", "write_surface", "write_forces_every",
                "write_residuals_every", "write_field_every_time", "wake_visualization",
                "recommended_vorticity_clip_range"},
               source_name, "$.outputs");
    result.outputs.write_field_every_time =
        positive_number(outputs, "write_field_every_time", source_name, "$.outputs");
    result.outputs.wake_visualization =
        string_value(outputs, "wake_visualization", source_name, "$.outputs");
    result.outputs.recommended_vorticity_clip_range =
        pair_value(outputs, "recommended_vorticity_clip_range", source_name, "$.outputs");
  }
  result.outputs.write_final_field =
      bool_value(outputs, "write_final_field", source_name, "$.outputs");
  result.outputs.write_surface = bool_value(outputs, "write_surface", source_name, "$.outputs");
  result.outputs.write_forces_every =
      integer_value(outputs, "write_forces_every", source_name, "$.outputs", 1);
  result.outputs.write_residuals_every =
      integer_value(outputs, "write_residuals_every", source_name, "$.outputs", 1);
  result.source_file = source_name == "<memory>" ? std::filesystem::path{}
                                                  : std::filesystem::path(source_name);
  return result;
}

CaseConfig parse_case_file(const std::filesystem::path& path) {
  const auto absolute_path = std::filesystem::absolute(path).lexically_normal();
  std::ifstream input(absolute_path);
  if (!input) {
    throw std::runtime_error("cannot open case file '" + absolute_path.string() + "'");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_case_json(buffer.str(), absolute_path.parent_path(), absolute_path.string());
}

std::string to_string(PhysicsMode value) {
  return value == PhysicsMode::inviscid ? "inviscid" : "laminar";
}

std::string to_string(BoundaryCondition value) {
  switch (value) {
    case BoundaryCondition::farfield: return "farfield";
    case BoundaryCondition::slip_wall: return "slip_wall";
    case BoundaryCondition::no_slip_adiabatic_wall: return "no_slip_adiabatic_wall";
  }
  throw std::logic_error("invalid BoundaryCondition");
}

std::string to_string(RunType value) {
  return value == RunType::steady ? "steady" : "transient";
}

}  // namespace cfd
