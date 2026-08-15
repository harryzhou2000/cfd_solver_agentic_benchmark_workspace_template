#include "config.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <filesystem>
#include <fstream>

namespace cfd {

using json = nlohmann::json;

namespace {

BcType parse_bc_type(const std::string& s) {
  if (s == "farfield") return BcType::Farfield;
  if (s == "slip_wall") return BcType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
  throw std::runtime_error("unsupported boundary condition type: '" + s + "'");
}

double require_number(const json& j, const std::string& key) {
  if (!j.contains(key)) throw std::runtime_error("missing required field: " + key);
  if (!j[key].is_number()) throw std::runtime_error("field '" + key + "' must be a number");
  return j[key].get<double>();
}

std::string require_string(const json& j, const std::string& key) {
  if (!j.contains(key)) throw std::runtime_error("missing required field: " + key);
  if (!j[key].is_string()) throw std::runtime_error("field '" + key + "' must be a string");
  return j[key].get<std::string>();
}

}  // namespace

CaseConfig load_case(const std::string& case_file) {
  std::ifstream in(case_file);
  if (!in) throw std::runtime_error("cannot open case file: " + case_file);
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("malformed JSON in case file '" + case_file + "': " + e.what());
  }

  CaseConfig c;
  c.schema_version = j.value("schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported case schema_version " +
                             std::to_string(c.schema_version) + " (only version 1 is supported)");

  c.case_id = require_string(j, "case_id");

  if (!j.contains("mesh")) throw std::runtime_error("missing 'mesh' object");
  c.mesh_file = require_string(j["mesh"], "file");
  std::filesystem::path cpath = std::filesystem::absolute(case_file);
  c.case_dir = cpath.parent_path().string();
  std::filesystem::path mesh_path(c.mesh_file);
  if (mesh_path.is_relative()) mesh_path = cpath.parent_path() / mesh_path;
  c.mesh_file = mesh_path.lexically_normal().string();

  if (!j.contains("physics")) throw std::runtime_error("missing 'physics' object");
  const std::string mode = require_string(j["physics"], "mode");
  if (mode == "inviscid") {
    c.inviscid = true;
  } else if (mode == "laminar") {
    c.inviscid = false;
    c.reynolds = require_number(j["physics"], "reynolds");
    c.viscosity_model = j["physics"].value("viscosity_model", "constant");
  } else {
    throw std::runtime_error("unsupported physics.mode '" + mode + "'");
  }

  if (!j.contains("gas")) throw std::runtime_error("missing 'gas' object");
  c.gamma = require_number(j["gas"], "gamma");
  c.gas_R = require_number(j["gas"], "R");
  c.prandtl = require_number(j["gas"], "prandtl");

  if (!j.contains("freestream")) throw std::runtime_error("missing 'freestream' object");
  c.mach = require_number(j["freestream"], "mach");
  c.aoa_degrees = require_number(j["freestream"], "aoa_degrees");
  c.rho_inf = require_number(j["freestream"], "rho");
  c.vel_mag = require_number(j["freestream"], "velocity_magnitude");
  c.p_inf = require_number(j["freestream"], "pressure");

  if (!j.contains("reference")) throw std::runtime_error("missing 'reference' object");
  c.ref_length = require_number(j["reference"], "length");
  c.ref_area = require_number(j["reference"], "area");
  if (j["reference"].contains("moment_center")) {
    const auto& mc = j["reference"]["moment_center"];
    if (mc.is_array() && mc.size() >= 2) {
      c.ref_moment_x = mc[0].get<double>();
      c.ref_moment_y = mc[1].get<double>();
    }
  }
  c.ref_reynolds_length = j["reference"].value("reynolds_length", c.ref_length);

  if (!j.contains("boundary_conditions"))
    throw std::runtime_error("missing 'boundary_conditions' object");
  for (auto it = j["boundary_conditions"].begin(); it != j["boundary_conditions"].end(); ++it) {
    c.boundary_conditions[it.key()] = parse_bc_type(it.value().get<std::string>());
  }

  if (!j.contains("run_control")) throw std::runtime_error("missing 'run_control' object");
  const auto& rc = j["run_control"];
  c.steady = rc.value("type", std::string("steady")) == "steady";
  c.transient = !c.steady;
  if (c.transient) {
    c.time_integrator = rc.value("time_integrator", std::string("bdf2"));
    c.time_step = require_number(rc, "time_step");
    c.final_time = require_number(rc, "final_time");
  } else {
    c.max_steps = static_cast<int>(require_number(rc, "max_steps"));
    c.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
  }
  c.cfl_initial = require_number(rc, "cfl_initial");
  c.cfl_max = require_number(rc, "cfl_max");
  c.pseudo_cfl_ramp_steps = static_cast<int>(rc.value("pseudo_cfl_ramp_steps", 0));
  c.min_inner_iterations = static_cast<int>(rc.value("min_inner_iterations", 3));
  c.max_inner_iterations = static_cast<int>(rc.value("max_inner_iterations", 50));
  c.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
  c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

  if (j.contains("outputs")) {
    const auto& o = j["outputs"];
    c.write_field_every_time = o.value("write_field_every_time", 0.0);
    c.write_forces_every = o.value("write_forces_every", 1);
    c.write_residuals_every = o.value("write_residuals_every", 1);
  }
  return c;
}

std::string bc_type_string(BcType t) {
  switch (t) {
    case BcType::Farfield: return "farfield";
    case BcType::SlipWall: return "slip_wall";
    case BcType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

}  // namespace cfd
