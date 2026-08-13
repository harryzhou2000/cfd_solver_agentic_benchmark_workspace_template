#include "case_config.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace aerofv {
namespace {

using boost::property_tree::ptree;

[[noreturn]] void fail(const std::string &message) {
  throw CaseConfigError("invalid case configuration: " + message);
}

bool is_scalar(const ptree &node) { return node.empty(); }

void require_keys(const ptree &object, std::initializer_list<const char *> allowed,
                  const std::string &where) {
  if (!object.data().empty()) {
    fail(where + " must be a JSON object");
  }
  const std::set<std::string> names(allowed.begin(), allowed.end());
  for (const auto &child : object) {
    if (child.first.empty()) {
      fail(where + " must be a JSON object, not an array");
    }
    if (names.find(child.first) == names.end()) {
      fail("unsupported field '" + where + "." + child.first + "'");
    }
    if (object.count(child.first) != 1U) {
      fail("duplicate field '" + where + "." + child.first + "'");
    }
  }
}

const ptree *require_child(const ptree &object, const std::string &name,
                           const std::string &where) {
  const auto count = object.count(name);
  if (count == 0U) {
    fail("missing required field '" + where + "." + name + "'");
  }
  if (count != 1U) {
    fail("duplicate field '" + where + "." + name + "'");
  }
  return &object.get_child(name);
}

const ptree *optional_child(const ptree &object, const std::string &name,
                            const std::string &where) {
  const auto count = object.count(name);
  if (count > 1U) {
    fail("duplicate field '" + where + "." + name + "'");
  }
  if (count == 0U) {
    return nullptr;
  }
  return &object.get_child(name);
}

std::string scalar_text(const ptree &node, const std::string &where) {
  if (!is_scalar(node)) {
    fail("field '" + where + "' must be a scalar");
  }
  return node.data();
}

std::string require_string(const ptree &object, const std::string &name,
                           const std::string &where) {
  const std::string value = scalar_text(*require_child(object, name, where), where + "." + name);
  if (value.empty()) {
    fail("field '" + where + "." + name + "' must not be empty");
  }
  return value;
}

std::optional<std::string> optional_string(const ptree &object, const std::string &name,
                                           const std::string &where) {
  const ptree *node = optional_child(object, name, where);
  if (node == nullptr) {
    return std::nullopt;
  }
  const std::string value = scalar_text(*node, where + "." + name);
  if (value.empty()) {
    fail("field '" + where + "." + name + "' must not be empty");
  }
  return value;
}

double parse_number(const ptree &node, const std::string &where) {
  const std::string value = scalar_text(node, where);
  std::size_t used = 0;
  double number = 0.0;
  try {
    number = std::stod(value, &used);
  } catch (const std::exception &) {
    fail("field '" + where + "' must be a finite number");
  }
  if (used != value.size() || !std::isfinite(number)) {
    fail("field '" + where + "' must be a finite number");
  }
  return number;
}

double require_number(const ptree &object, const std::string &name,
                      const std::string &where) {
  return parse_number(*require_child(object, name, where), where + "." + name);
}

std::optional<double> optional_number(const ptree &object, const std::string &name,
                                      const std::string &where) {
  const ptree *node = optional_child(object, name, where);
  return node == nullptr ? std::nullopt : std::optional<double>(parse_number(*node, where + "." + name));
}

int checked_int(double number, const std::string &where) {
  if (std::floor(number) != number || number < static_cast<double>(std::numeric_limits<int>::min()) ||
      number > static_cast<double>(std::numeric_limits<int>::max())) {
    fail("field '" + where + "' must be an integer");
  }
  return static_cast<int>(number);
}

int require_int(const ptree &object, const std::string &name, const std::string &where) {
  return checked_int(require_number(object, name, where), where + "." + name);
}

std::optional<int> optional_int(const ptree &object, const std::string &name,
                                const std::string &where) {
  const auto number = optional_number(object, name, where);
  return number ? std::optional<int>(checked_int(*number, where + "." + name)) : std::nullopt;
}

bool require_bool(const ptree &object, const std::string &name, const std::string &where) {
  const std::string value = scalar_text(*require_child(object, name, where), where + "." + name);
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  fail("field '" + where + "." + name + "' must be a boolean");
}

void require_positive(double value, const std::string &where) {
  if (!(value > 0.0)) {
    fail("field '" + where + "' must be positive");
  }
}

void require_nonnegative(double value, const std::string &where) {
  if (value < 0.0) {
    fail("field '" + where + "' must be nonnegative");
  }
}

const ptree &require_object(const ptree &parent, const char *name, const char *where) {
  const ptree &node = *require_child(parent, name, where);
  if (!node.data().empty()) {
    fail("field '" + std::string(where) + "." + name + "' must be an object");
  }
  for (const auto &child : node) {
    if (child.first.empty()) {
      fail("field '" + std::string(where) + "." + name + "' must be an object, not an array");
    }
  }
  return node;
}

std::pair<double, double> require_number_pair(const ptree &object, const std::string &name,
                                               const std::string &where) {
  const ptree &array = *require_child(object, name, where);
  if (!array.data().empty() || array.size() != 2U) {
    fail("field '" + where + "." + name + "' must be a two-element numeric array");
  }
  std::vector<double> values;
  values.reserve(2);
  for (const auto &child : array) {
    if (!child.first.empty()) {
      fail("field '" + where + "." + name + "' must be an array");
    }
    values.push_back(parse_number(child.second, where + "." + name));
  }
  return {values[0], values[1]};
}

std::optional<std::pair<double, double>> optional_number_pair(const ptree &object,
                                                               const std::string &name,
                                                               const std::string &where) {
  return optional_child(object, name, where) == nullptr
             ? std::nullopt
             : std::optional<std::pair<double, double>>(require_number_pair(object, name, where));
}

PhysicsMode parse_mode(const std::string &value) {
  if (value == "inviscid") {
    return PhysicsMode::inviscid;
  }
  if (value == "laminar") {
    return PhysicsMode::laminar;
  }
  fail("unsupported physics.mode '" + value + "'; expected inviscid or laminar");
}

RunType parse_run_type(const std::string &value) {
  if (value == "steady") {
    return RunType::steady;
  }
  if (value == "transient") {
    return RunType::transient;
  }
  fail("unsupported run_control.type '" + value + "'; expected steady or transient");
}

void parse_mesh(const ptree &root, CaseConfig &config) {
  const ptree &mesh = require_object(root, "mesh", "root");
  require_keys(mesh, {"file", "format", "dimension"}, "mesh");
  const std::filesystem::path supplied = require_string(mesh, "file", "mesh");
  config.mesh.file = supplied.is_absolute() ? supplied : config.case_file.parent_path() / supplied;
  config.mesh.file = std::filesystem::absolute(config.mesh.file).lexically_normal();
  config.mesh.format = require_string(mesh, "format", "mesh");
  config.mesh.dimension = require_int(mesh, "dimension", "mesh");
  if (config.mesh.format != "CGNS") {
    fail("unsupported mesh.format '" + config.mesh.format + "'; expected CGNS");
  }
  if (config.mesh.dimension != 2) {
    fail("mesh.dimension must be 2");
  }
  if (!std::filesystem::exists(config.mesh.file)) {
    fail("mesh.file does not exist after relative-path resolution: '" + config.mesh.file.string() + "'");
  }
}

void parse_physics(const ptree &root, CaseConfig &config) {
  const ptree &physics = require_object(root, "physics", "root");
  require_keys(physics, {"equations", "mode", "reynolds", "viscosity_model"}, "physics");
  config.physics.equations = require_string(physics, "equations", "physics");
  if (config.physics.equations != "compressible_navier_stokes") {
    fail("unsupported physics.equations '" + config.physics.equations + "'");
  }
  config.physics.mode = parse_mode(require_string(physics, "mode", "physics"));
  config.physics.reynolds = optional_number(physics, "reynolds", "physics");
  config.physics.viscosity_model = optional_string(physics, "viscosity_model", "physics");
  if (config.physics.mode == PhysicsMode::laminar) {
    if (!config.physics.reynolds || !config.physics.viscosity_model) {
      fail("laminar physics requires reynolds and viscosity_model");
    }
    require_positive(*config.physics.reynolds, "physics.reynolds");
    if (*config.physics.viscosity_model != "constant") {
      fail("unsupported physics.viscosity_model '" + *config.physics.viscosity_model + "'");
    }
  } else if (config.physics.reynolds || config.physics.viscosity_model) {
    fail("inviscid physics must not define reynolds or viscosity_model");
  }
}

void parse_gas(const ptree &root, CaseConfig &config) {
  const ptree &gas = require_object(root, "gas", "root");
  require_keys(gas, {"model", "gamma", "R", "prandtl"}, "gas");
  const std::string model = require_string(gas, "model", "gas");
  if (model != "calorically_perfect") {
    fail("unsupported gas.model '" + model + "'");
  }
  config.gas.gamma = require_number(gas, "gamma", "gas");
  config.gas.gas_constant = require_number(gas, "R", "gas");
  config.gas.prandtl = require_number(gas, "prandtl", "gas");
  if (!(config.gas.gamma > 1.0)) {
    fail("gas.gamma must be greater than 1");
  }
  require_positive(config.gas.gas_constant, "gas.R");
  require_positive(config.gas.prandtl, "gas.prandtl");
}

void parse_freestream(const ptree &root, CaseConfig &config) {
  const ptree &stream = require_object(root, "freestream", "root");
  require_keys(stream, {"mach", "aoa_degrees", "rho", "velocity_magnitude", "pressure"}, "freestream");
  config.freestream.mach = require_number(stream, "mach", "freestream");
  config.freestream.aoa_degrees = require_number(stream, "aoa_degrees", "freestream");
  config.freestream.rho = require_number(stream, "rho", "freestream");
  config.freestream.velocity_magnitude = require_number(stream, "velocity_magnitude", "freestream");
  config.freestream.pressure = require_number(stream, "pressure", "freestream");
  require_nonnegative(config.freestream.mach, "freestream.mach");
  require_positive(config.freestream.rho, "freestream.rho");
  require_positive(config.freestream.velocity_magnitude, "freestream.velocity_magnitude");
  require_positive(config.freestream.pressure, "freestream.pressure");
}

void parse_reference(const ptree &root, CaseConfig &config) {
  const ptree &reference = require_object(root, "reference", "root");
  require_keys(reference, {"length", "area", "moment_center", "reynolds_length"}, "reference");
  config.reference.length = require_number(reference, "length", "reference");
  config.reference.area = require_number(reference, "area", "reference");
  const auto center = require_number_pair(reference, "moment_center", "reference");
  config.reference.moment_center = {center.first, center.second};
  config.reference.reynolds_length = require_number(reference, "reynolds_length", "reference");
  require_positive(config.reference.length, "reference.length");
  require_positive(config.reference.area, "reference.area");
  require_positive(config.reference.reynolds_length, "reference.reynolds_length");
}

void parse_boundaries(const ptree &root, CaseConfig &config) {
  const ptree &boundaries = require_object(root, "boundary_conditions", "root");
  if (boundaries.empty()) {
    fail("boundary_conditions must not be empty");
  }
  bool has_farfield = false;
  bool has_wall = false;
  for (const auto &entry : boundaries) {
    if (entry.first.empty()) {
      fail("boundary_conditions must be a mapping, not an array");
    }
    if (entry.first.empty() || !entry.second.empty()) {
      fail("each boundary_conditions entry must map a nonempty family name to a scalar type");
    }
    if (config.boundary_conditions.count(entry.first) != 0U) {
      fail("duplicate boundary family '" + entry.first + "'");
    }
    const BoundaryType type = boundary_type_from_string(entry.second.data());
    if (type != BoundaryType::farfield && type != BoundaryType::slip_wall &&
        type != BoundaryType::no_slip_adiabatic_wall) {
      fail("unsupported boundary condition '" + entry.second.data() + "' for family '" + entry.first + "'");
    }
    has_farfield = has_farfield || type == BoundaryType::farfield;
    has_wall = has_wall || type == BoundaryType::slip_wall || type == BoundaryType::no_slip_adiabatic_wall;
    if (config.physics.mode == PhysicsMode::inviscid && type == BoundaryType::no_slip_adiabatic_wall) {
      fail("inviscid physics cannot use no_slip_adiabatic_wall");
    }
    config.boundary_conditions.emplace(entry.first, type);
  }
  if (!has_farfield || !has_wall) {
    fail("boundary_conditions must include at least one farfield and one wall family");
  }
}

void parse_numerics(const ptree &root, CaseConfig &config) {
  const ptree &numerics = require_object(root, "numerics_required", "root");
  require_keys(numerics, {"spatial_order", "inviscid_flux", "viscous_flux", "main_time_method",
                          "transient_order", "implicit_solver"}, "numerics_required");
  config.numerics.spatial_order = require_int(numerics, "spatial_order", "numerics_required");
  config.numerics.inviscid_flux = require_string(numerics, "inviscid_flux", "numerics_required");
  config.numerics.viscous_flux = require_string(numerics, "viscous_flux", "numerics_required");
  config.numerics.main_time_method = require_string(numerics, "main_time_method", "numerics_required");
  config.numerics.transient_order = optional_int(numerics, "transient_order", "numerics_required");
  config.numerics.implicit_solver = require_string(numerics, "implicit_solver", "numerics_required");
  if (config.numerics.spatial_order < 2) {
    fail("numerics_required.spatial_order must be at least 2");
  }
  if (config.numerics.inviscid_flux != "approximate_riemann" ||
      config.numerics.main_time_method != "implicit" || config.numerics.implicit_solver != "required") {
    fail("numerics_required requests an unsupported required solver capability");
  }
  const std::string expected_viscous = config.physics.mode == PhysicsMode::laminar ? "required" : "disabled";
  if (config.numerics.viscous_flux != expected_viscous) {
    fail("numerics_required.viscous_flux is incompatible with physics.mode");
  }
  if (config.numerics.transient_order && *config.numerics.transient_order < 2) {
    fail("numerics_required.transient_order must be at least 2 when provided");
  }
}

void parse_run_control(const ptree &root, CaseConfig &config) {
  const ptree &run = require_object(root, "run_control", "root");
  require_keys(run, {"type", "max_steps", "residual_reduction_target", "cfl_initial", "cfl_max",
                     "pseudo_cfl_ramp_steps", "min_inner_iterations", "max_inner_iterations",
                     "inner_residual_reduction_target", "time_integrator", "time_step", "final_time",
                     "inner_residual_norm", "bdf2_history_update", "rusanov_dissipation_scale"}, "run_control");
  config.run_control.type = parse_run_type(require_string(run, "type", "run_control"));
  config.run_control.max_steps = optional_int(run, "max_steps", "run_control");
  config.run_control.residual_reduction_target = optional_number(run, "residual_reduction_target", "run_control");
  config.run_control.time_integrator = optional_string(run, "time_integrator", "run_control");
  config.run_control.time_step = optional_number(run, "time_step", "run_control");
  config.run_control.final_time = optional_number(run, "final_time", "run_control");
  config.run_control.inner_residual_norm = optional_string(run, "inner_residual_norm", "run_control");
  config.run_control.bdf2_history_update = optional_string(run, "bdf2_history_update", "run_control");
  config.run_control.rusanov_dissipation_scale = optional_number(run, "rusanov_dissipation_scale", "run_control");
  config.run_control.cfl_initial = require_number(run, "cfl_initial", "run_control");
  config.run_control.cfl_max = require_number(run, "cfl_max", "run_control");
  config.run_control.pseudo_cfl_ramp_steps = require_int(run, "pseudo_cfl_ramp_steps", "run_control");
  config.run_control.min_inner_iterations = require_int(run, "min_inner_iterations", "run_control");
  config.run_control.max_inner_iterations = require_int(run, "max_inner_iterations", "run_control");
  config.run_control.inner_residual_reduction_target = require_number(run, "inner_residual_reduction_target", "run_control");

  require_positive(config.run_control.cfl_initial, "run_control.cfl_initial");
  if (config.run_control.cfl_max < config.run_control.cfl_initial) {
    fail("run_control.cfl_max must be at least cfl_initial");
  }
  if (config.run_control.pseudo_cfl_ramp_steps < 0 || config.run_control.min_inner_iterations <= 0 ||
      config.run_control.max_inner_iterations < config.run_control.min_inner_iterations) {
    fail("run_control has invalid iteration bounds or CFL ramp steps");
  }
  if (!(config.run_control.inner_residual_reduction_target > 0.0 &&
        config.run_control.inner_residual_reduction_target < 1.0)) {
    fail("run_control.inner_residual_reduction_target must be in (0, 1)");
  }

  if (config.run_control.type == RunType::steady) {
    if (!config.run_control.max_steps || !config.run_control.residual_reduction_target) {
      fail("steady run_control requires max_steps and residual_reduction_target");
    }
    if (*config.run_control.max_steps <= 0) {
      fail("run_control.max_steps must be positive");
    }
    require_positive(*config.run_control.residual_reduction_target, "run_control.residual_reduction_target");
    if (config.run_control.time_integrator || config.run_control.time_step || config.run_control.final_time ||
        config.run_control.inner_residual_norm || config.run_control.bdf2_history_update ||
        config.run_control.rusanov_dissipation_scale) {
      fail("steady run_control must not contain transient-only controls");
    }
  } else {
    if (!config.run_control.time_integrator || !config.run_control.time_step || !config.run_control.final_time ||
        !config.run_control.inner_residual_norm || !config.run_control.bdf2_history_update ||
        !config.run_control.rusanov_dissipation_scale) {
      fail("transient run_control is missing required physical-time controls");
    }
    if (*config.run_control.time_integrator != "bdf2_or_trapezoidal" ||
        *config.run_control.inner_residual_norm != "total_spatial_plus_physical_time" ||
        *config.run_control.bdf2_history_update != "after_inner_convergence") {
      fail("transient run_control requests unsupported integration semantics");
    }
    require_positive(*config.run_control.time_step, "run_control.time_step");
    require_positive(*config.run_control.final_time, "run_control.final_time");
    require_positive(*config.run_control.rusanov_dissipation_scale, "run_control.rusanov_dissipation_scale");
    if (!config.numerics.transient_order || *config.numerics.transient_order < 2) {
      fail("transient run_control requires numerics_required.transient_order >= 2");
    }
  }
}

void parse_outputs(const ptree &root, CaseConfig &config) {
  const ptree &outputs = require_object(root, "outputs", "root");
  require_keys(outputs, {"write_final_field", "write_surface", "write_forces_every", "write_residuals_every",
                         "write_field_every_time", "wake_visualization", "recommended_vorticity_clip_range"}, "outputs");
  config.outputs.write_final_field = require_bool(outputs, "write_final_field", "outputs");
  config.outputs.write_surface = require_bool(outputs, "write_surface", "outputs");
  config.outputs.write_forces_every = require_int(outputs, "write_forces_every", "outputs");
  config.outputs.write_residuals_every = require_int(outputs, "write_residuals_every", "outputs");
  config.outputs.write_field_every_time = optional_number(outputs, "write_field_every_time", "outputs");
  config.outputs.wake_visualization = optional_string(outputs, "wake_visualization", "outputs");
  config.outputs.recommended_vorticity_clip_range = optional_number_pair(outputs, "recommended_vorticity_clip_range", "outputs");
  if (!config.outputs.write_final_field || !config.outputs.write_surface || config.outputs.write_forces_every <= 0 ||
      config.outputs.write_residuals_every <= 0) {
    fail("outputs must request final field, surface, and positive force/residual cadence");
  }
  if (config.outputs.write_field_every_time) {
    require_positive(*config.outputs.write_field_every_time, "outputs.write_field_every_time");
  }
  if (config.outputs.recommended_vorticity_clip_range &&
      !(config.outputs.recommended_vorticity_clip_range->first < config.outputs.recommended_vorticity_clip_range->second)) {
    fail("outputs.recommended_vorticity_clip_range must be ordered [min, max]");
  }
  if (config.run_control.type == RunType::transient &&
      (!config.outputs.write_field_every_time || !config.outputs.wake_visualization)) {
    fail("transient outputs require write_field_every_time and wake_visualization");
  }
}

} // namespace

const char *to_string(PhysicsMode mode) noexcept {
  return mode == PhysicsMode::inviscid ? "inviscid" : "laminar";
}

const char *to_string(RunType type) noexcept {
  return type == RunType::steady ? "steady" : "transient";
}

CaseConfig load_case_config(const std::filesystem::path &case_file) {
  if (case_file.empty()) {
    throw CaseConfigError("case file path is empty");
  }
  CaseConfig config;
  config.case_file = std::filesystem::absolute(case_file).lexically_normal();
  if (!std::filesystem::exists(config.case_file)) {
    throw CaseConfigError("case file does not exist: '" + config.case_file.string() + "'");
  }

  ptree root;
  try {
    boost::property_tree::read_json(config.case_file.string(), root);
  } catch (const boost::property_tree::json_parser::json_parser_error &error) {
    throw CaseConfigError("could not parse case JSON '" + config.case_file.string() + "': " + error.message());
  }
  require_keys(root, {"schema_version", "case_id", "description", "mesh", "physics", "gas", "freestream",
                      "reference", "boundary_conditions", "numerics_required", "run_control", "outputs"}, "root");
  config.schema_version = require_int(root, "schema_version", "root");
  if (config.schema_version != 1) {
    throw CaseConfigError("unsupported case schema_version " + std::to_string(config.schema_version) + "; only version 1 is supported");
  }
  config.case_id = require_string(root, "case_id", "root");
  config.description = require_string(root, "description", "root");
  parse_mesh(root, config);
  parse_physics(root, config);
  parse_gas(root, config);
  parse_freestream(root, config);
  parse_reference(root, config);
  parse_boundaries(root, config);
  parse_numerics(root, config);
  parse_run_control(root, config);
  parse_outputs(root, config);
  return config;
}

} // namespace aerofv
