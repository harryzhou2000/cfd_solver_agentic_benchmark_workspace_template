// JSON case-file parser (nlohmann/json). Validates schema_version==1 and
// maps mesh boundary-family names to solver BCType.
#include "case.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace cfd {
using json = nlohmann::json;

static double getd(const json& o, const char* k, double def) {
  if (o.contains(k) && o[k].is_number()) return o[k].get<double>();
  return def;
}
static int geti(const json& o, const char* k, int def) {
  if (o.contains(k) && o[k].is_number_integer()) return o[k].get<int>();
  if (o.contains(k) && o[k].is_number()) return (int)o[k].get<double>();
  return def;
}
static std::string gets(const json& o, const char* k, const std::string& def) {
  if (o.contains(k) && o[k].is_string()) return o[k].get<std::string>();
  return def;
}

CaseDef parse_case(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open case file: " + path);
  json j; f >> j;
  CaseDef c;
  c.schema_version = geti(j, "schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported schema_version " + std::to_string(c.schema_version) +
                             " (expected 1) in " + path);
  c.case_id = gets(j, "case_id", "unknown");
  c.description = gets(j, "description", "");

  if (j.contains("mesh")) {
    c.mesh_file = gets(j["mesh"], "file", "");
  }
  if (c.mesh_file.empty())
    throw std::runtime_error("case " + c.case_id + ": mesh.file missing");

  if (j.contains("physics")) {
    c.mode = gets(j["physics"], "mode", "inviscid");
    c.laminar = (c.mode == "laminar");
    c.reynolds = getd(j["physics"], "reynolds", 0.0);
    c.viscosity_model = gets(j["physics"], "viscosity_model", "constant");
  }
  if (j.contains("gas")) {
    c.gas.gamma = getd(j["gas"], "gamma", 1.4);
    c.gas.R = getd(j["gas"], "R", 1.0);
    c.gas.Pr = getd(j["gas"], "prandtl", 0.72);
  }
  if (j.contains("freestream")) {
    const json& fs = j["freestream"];
    c.fs.mach = getd(fs, "mach", 0.1);
    double aoa_deg = getd(fs, "aoa_degrees", 0.0);
    c.fs.aoa_rad = aoa_deg * M_PI / 180.0;
    c.fs.rho = getd(fs, "rho", 1.0);
    c.fs.velocity = getd(fs, "velocity_magnitude", 1.0);
    c.fs.pressure = getd(fs, "pressure", 1.0 / c.gas.gm1());
  }
  if (j.contains("reference")) {
    const json& r = j["reference"];
    c.ref.length = getd(r, "length", 1.0);
    c.ref.area = getd(r, "area", 1.0);
    if (r.contains("moment_center") && r["moment_center"].is_array()
        && r["moment_center"].size() >= 2) {
      c.ref.moment_center.x = r["moment_center"][0].get<double>();
      c.ref.moment_center.y = r["moment_center"][1].get<double>();
    }
    c.ref.reynolds_length = getd(r, "reynolds_length", c.ref.length);
  }
  if (j.contains("boundary_conditions") && j["boundary_conditions"].is_object()) {
    for (auto it = j["boundary_conditions"].begin();
         it != j["boundary_conditions"].end(); ++it) {
      std::string v = it.value().get<std::string>();
      BCType bt;
      if (v == "farfield") bt = BCType::Farfield;
      else if (v == "slip_wall") bt = BCType::SlipWall;
      else if (v == "no_slip_adiabatic_wall") bt = BCType::NoSlipAdiabaticWall;
      else throw std::runtime_error("case " + c.case_id + ": unsupported BC '" + v + "'");
      c.bc_map[it.key()] = bt;
    }
  }
  if (c.bc_map.empty())
    throw std::runtime_error("case " + c.case_id + ": no boundary_conditions defined");

  if (j.contains("run_control")) {
    const json& r = j["run_control"];
    std::string t = gets(r, "type", "steady");
    c.rc.type = (t == "transient") ? RunType::Transient : RunType::Steady;
    c.rc.max_steps = geti(r, "max_steps", 20000);
    c.rc.residual_reduction_target = getd(r, "residual_reduction_target", 4.0);
    c.rc.cfl_initial = getd(r, "cfl_initial", 1.0);
    c.rc.cfl_max = getd(r, "cfl_max", 100.0);
    c.rc.pseudo_cfl_ramp_steps = geti(r, "pseudo_cfl_ramp_steps", 2000);
    c.rc.min_inner = geti(r, "min_inner_iterations", 3);
    c.rc.max_inner = geti(r, "max_inner_iterations", 50);
    c.rc.inner_residual_target = getd(r, "inner_residual_reduction_target", 0.01);
    c.rc.time_step = getd(r, "time_step", 0.01);
    c.rc.final_time = getd(r, "final_time", 300.0);
    c.rc.time_integrator = gets(r, "time_integrator", "bdf2_or_trapezoidal");
    c.rc.rusanov_dissipation_scale = getd(r, "rusanov_dissipation_scale", 1.0);
    c.rc.bdf2_history_update = gets(r, "bdf2_history_update", "after_inner_convergence");
    c.rc.inner_residual_norm = gets(r, "inner_residual_norm", "total_spatial_plus_physical_time");
  }
  if (j.contains("outputs")) {
    const json& o = j["outputs"];
    c.write_final_field = o.value("write_final_field", true);
    c.write_surface = o.value("write_surface", true);
    c.write_forces_every = geti(o, "write_forces_every", 1);
    c.write_residuals_every = geti(o, "write_residuals_every", 1);
    c.write_field_every_time = getd(o, "write_field_every_time", 0.0);
    c.wake_viz = gets(o, "wake_visualization", "vorticity_or_velocity");
  }
  return c;
}

}  // namespace cfd
