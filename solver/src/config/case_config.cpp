#include "config/case_config.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace cfd {
namespace {

[[noreturn]] void missing(const std::string& where, const std::string& key) {
  throw std::runtime_error("case file '" + where + "': missing required field '" +
                           key + "'");
}

// Returns a pointer (never null) to the required sub-object, so that binding
// the result to a reference does not trip -Wdangling-reference.
const json* require_obj(const json& j, const std::string& key,
                        const std::string& where) {
  if (!j.contains(key) || !j.at(key).is_object()) missing(where, key);
  return &j.at(key);
}

const json& require_value(const json& j, const std::string& key,
                          const std::string& where) {
  if (!j.contains(key)) missing(where, key);
  return j.at(key);
}

std::string require_str(const json& j, const std::string& key,
                        const std::string& where) {
  const json& v = require_value(j, key, where);
  if (!v.is_string())
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' must be a string");
  return v.get<std::string>();
}

double require_num(const json& j, const std::string& key,
                   const std::string& where) {
  const json& v = require_value(j, key, where);
  if (!v.is_number())
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' must be a number");
  return v.get<double>();
}

int require_int(const json& j, const std::string& key,
                const std::string& where) {
  const json& v = require_value(j, key, where);
  if (!v.is_number_integer())
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' must be an integer");
  return v.get<int>();
}

bool require_bool(const json& j, const std::string& key,
                  const std::string& where) {
  const json& v = require_value(j, key, where);
  if (!v.is_boolean())
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' must be a boolean");
  return v.get<bool>();
}

std::vector<double> require_num_array(const json& j, const std::string& key,
                                      const std::string& where) {
  const json& v = require_value(j, key, where);
  if (!v.is_array())
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' must be an array of numbers");
  std::vector<double> out;
  for (const auto& e : v) {
    if (!e.is_number())
      throw std::runtime_error("case file '" + where + "': field '" + key +
                               "' must be an array of numbers");
    out.push_back(e.get<double>());
  }
  return out;
}

template <typename T>
std::optional<T> opt_num(const json& j, const std::string& key,
                         const std::string& where) {
  if (!j.contains(key)) return std::nullopt;
  try {
    return std::optional<T>(j.at(key).get<T>());
  } catch (const json::exception& e) {
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' has an invalid value: " + e.what());
  }
}

std::optional<std::string> opt_str(const json& j, const std::string& key,
                                   const std::string& where) {
  if (!j.contains(key)) return std::nullopt;
  try {
    return std::optional<std::string>(j.at(key).get<std::string>());
  } catch (const json::exception& e) {
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' has an invalid value: " + e.what());
  }
}

std::optional<std::vector<double>> opt_num_array(const json& j,
                                                 const std::string& key,
                                                 const std::string& where) {
  if (!j.contains(key)) return std::nullopt;
  try {
    const json& v = j.at(key);
    if (!v.is_array())
      throw std::runtime_error("case file '" + where + "': field '" + key +
                               "' must be an array of numbers");
    std::vector<double> out;
    for (const auto& e : v) {
      if (!e.is_number())
        throw std::runtime_error("case file '" + where + "': field '" + key +
                                 "' must be an array of numbers");
      out.push_back(e.get<double>());
    }
    return out;
  } catch (const json::exception& e) {
    throw std::runtime_error("case file '" + where + "': field '" + key +
                             "' has an invalid value: " + e.what());
  }
}

}  // namespace

CaseConfig load_case_config(const std::string& path) {
  std::ifstream ifs(path);
  if (!ifs.is_open())
    throw std::runtime_error("cannot open case file: " + path);

  json j;
  try {
    ifs >> j;
  } catch (const json::parse_error& e) {
    throw std::runtime_error("invalid JSON in case file '" + path + "': " +
                             e.what());
  }
  if (!j.is_object())
    throw std::runtime_error("case file '" + path +
                             "': top-level JSON must be an object");

  const std::string& where = path;

  CaseConfig c;
  c.schema_version = j.value("schema_version", 1);
  if (c.schema_version != 1) {
    throw std::runtime_error(
        "case file '" + path + "': unsupported schema_version " +
        std::to_string(c.schema_version) +
        " (only version 1 is supported; refusing to guess)");
  }

  c.case_id = require_str(j, "case_id", where);
  c.description = j.value("description", std::string());

  // --- mesh ---
  {
    const json& m = *require_obj(j, "mesh", where);
    c.mesh.file = require_str(m, "file", where);
    c.mesh.format = m.value("format", std::string("CGNS"));
    c.mesh.dimension = m.value("dimension", 2);
    if (c.mesh.dimension != 2) {
      throw std::runtime_error(
          "case file '" + path + "': mesh.dimension must be 2 (only 2D "
          "meshes are supported), got " + std::to_string(c.mesh.dimension));
    }
  }

  // --- physics ---
  {
    const json& p = *require_obj(j, "physics", where);
    c.physics.equations = require_str(p, "equations", where);
    c.physics.mode = require_str(p, "mode", where);
    if (c.physics.mode != "inviscid" && c.physics.mode != "laminar") {
      throw std::runtime_error(
          "case file '" + path + "': physics.mode must be 'inviscid' or "
          "'laminar', got '" + c.physics.mode + "'");
    }
    c.physics.reynolds = opt_num<double>(p, "reynolds", where);
    c.physics.viscosity_model = opt_str(p, "viscosity_model", where);
  }

  // --- gas ---
  {
    const json& g = *require_obj(j, "gas", where);
    c.gas.model = require_str(g, "model", where);
    c.gas.gamma = require_num(g, "gamma", where);
    c.gas.R = require_num(g, "R", where);
    c.gas.prandtl = require_num(g, "prandtl", where);
  }

  // --- freestream ---
  {
    const json& f = *require_obj(j, "freestream", where);
    c.freestream.mach = require_num(f, "mach", where);
    c.freestream.aoa_degrees = require_num(f, "aoa_degrees", where);
    c.freestream.rho = require_num(f, "rho", where);
    c.freestream.velocity_magnitude = require_num(f, "velocity_magnitude", where);
    c.freestream.pressure = require_num(f, "pressure", where);
    if (c.freestream.rho <= 0.0)
      throw std::runtime_error("case file '" + path +
                               "': freestream.rho must be positive");
    if (c.freestream.pressure <= 0.0)
      throw std::runtime_error("case file '" + path +
                               "': freestream.pressure must be positive");
  }

  // --- reference ---
  {
    const json& r = *require_obj(j, "reference", where);
    c.reference.length = require_num(r, "length", where);
    c.reference.area = require_num(r, "area", where);
    c.reference.moment_center = require_num_array(r, "moment_center", where);
    c.reference.reynolds_length = require_num(r, "reynolds_length", where);
    if (c.reference.moment_center.size() != 2) {
      throw std::runtime_error(
          "case file '" + path + "': reference.moment_center must contain "
          "exactly 2 entries [x, y], got " +
          std::to_string(c.reference.moment_center.size()));
    }
  }

  // --- boundary conditions ---
  {
    const json& bc = *require_obj(j, "boundary_conditions", where);
    for (auto it = bc.begin(); it != bc.end(); ++it) {
      if (!it.value().is_string())
        throw std::runtime_error("case file '" + where +
                                 "': boundary_conditions['" + it.key() +
                                 "'] must be a string");
      c.boundary_conditions[it.key()] = it.value().get<std::string>();
    }
  }

  // --- numerics_required ---
  {
    const json& n = *require_obj(j, "numerics_required", where);
    c.numerics_required.spatial_order = require_int(n, "spatial_order", where);
    c.numerics_required.inviscid_flux = require_str(n, "inviscid_flux", where);
    c.numerics_required.viscous_flux = require_str(n, "viscous_flux", where);
    c.numerics_required.main_time_method = require_str(n, "main_time_method", where);
    c.numerics_required.implicit_solver = require_str(n, "implicit_solver", where);
    c.numerics_required.transient_order =
        opt_num<int>(n, "transient_order", where);
  }

  // --- run_control ---
  {
    const json& rc = *require_obj(j, "run_control", where);
    c.run_control.type = require_str(rc, "type", where);
    if (c.run_control.type != "steady" && c.run_control.type != "transient") {
      throw std::runtime_error("case file '" + where +
                               "': run_control.type must be 'steady' or "
                               "'transient', got '" + c.run_control.type + "'");
    }
    c.run_control.max_steps = opt_num<int>(rc, "max_steps", where);
    if (c.run_control.type == "steady" && !c.run_control.max_steps.has_value()) {
      throw std::runtime_error("case file '" + path +
                               "': steady run_control must specify max_steps");
    }
    c.run_control.residual_reduction_target =
        opt_num<double>(rc, "residual_reduction_target", where);
    c.run_control.cfl_initial = require_num(rc, "cfl_initial", where);
    c.run_control.cfl_max = require_num(rc, "cfl_max", where);
    c.run_control.pseudo_cfl_ramp_steps = require_int(rc, "pseudo_cfl_ramp_steps", where);
    c.run_control.min_inner_iterations =
        opt_num<int>(rc, "min_inner_iterations", where);
    c.run_control.max_inner_iterations =
        opt_num<int>(rc, "max_inner_iterations", where);
    c.run_control.inner_residual_reduction_target =
        opt_num<double>(rc, "inner_residual_reduction_target", where);
    c.run_control.time_step = opt_num<double>(rc, "time_step", where);
    c.run_control.final_time = opt_num<double>(rc, "final_time", where);
    c.run_control.time_integrator = opt_str(rc, "time_integrator", where);
    c.run_control.inner_residual_norm = opt_str(rc, "inner_residual_norm", where);
    c.run_control.bdf2_history_update = opt_str(rc, "bdf2_history_update", where);
    c.run_control.rusanov_dissipation_scale =
        opt_num<double>(rc, "rusanov_dissipation_scale", where);
  }

  // --- outputs ---
  {
    const json& o = *require_obj(j, "outputs", where);
    c.outputs.write_final_field = require_bool(o, "write_final_field", where);
    c.outputs.write_surface = require_bool(o, "write_surface", where);
    c.outputs.write_forces_every = require_int(o, "write_forces_every", where);
    c.outputs.write_residuals_every = require_int(o, "write_residuals_every", where);
    c.outputs.write_field_every_time =
        opt_num<double>(o, "write_field_every_time", where);
    c.outputs.wake_visualization = opt_str(o, "wake_visualization", where);
    c.outputs.recommended_vorticity_clip_range =
        opt_num_array(o, "recommended_vorticity_clip_range", where);
  }

  // --- resolve mesh path relative to the case file directory ---
  {
    fs::path mesh_path(c.mesh.file);
    if (mesh_path.is_relative())
      mesh_path = fs::path(path).parent_path() / mesh_path;
    std::error_code ec;
    fs::path abs_path = fs::absolute(mesh_path, ec).lexically_normal();
    if (ec) abs_path = mesh_path.lexically_normal();
    c.mesh.file = abs_path.string();
  }

  return c;
}

}  // namespace cfd
