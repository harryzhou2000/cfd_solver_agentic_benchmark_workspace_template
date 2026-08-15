#include "case.h"

#include <filesystem>

namespace cfd {
namespace {

double get_num(const nlohmann::json& j, const char* key, double dflt) {
  if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
  return dflt;
}

int get_int(const nlohmann::json& j, const char* key, int dflt) {
  if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
  return dflt;
}

std::string get_str(const nlohmann::json& j, const char* key, const std::string& dflt) {
  if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
  return dflt;
}

}  // namespace

bool load_case(const std::string& json_path, CaseConfig& cfg, std::string& err) {
  std::ifstream f(json_path);
  if (!f.is_open()) {
    err = "cannot open case file: " + json_path;
    return false;
  }
  nlohmann::json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    err = std::string("case JSON parse error: ") + e.what();
    return false;
  }

  cfg.schema_version = get_int(j, "schema_version", 1);
  if (cfg.schema_version != 1) {
    err = "unsupported schema_version " + std::to_string(cfg.schema_version) +
          " (only version 1 is supported)";
    return false;
  }
  cfg.case_id = get_str(j, "case_id", "");
  cfg.description = get_str(j, "description", "");

  std::filesystem::path case_dir = std::filesystem::path(json_path).parent_path();
  if (j.contains("mesh") && j["mesh"].is_object()) {
    std::string mf = get_str(j["mesh"], "file", "");
    std::filesystem::path mp(mf);
    if (mp.is_absolute()) {
      cfg.mesh_file = mp.string();
    } else {
      cfg.mesh_file = (case_dir / mp).lexically_normal().string();
    }
  }

  if (j.contains("physics") && j["physics"].is_object()) {
    const auto& ph = j["physics"];
    cfg.physics_mode = get_str(ph, "mode", "inviscid");
    cfg.reynolds = get_num(ph, "reynolds", 0.0);
    cfg.viscosity_model = get_str(ph, "viscosity_model", "constant");
  }

  if (j.contains("gas") && j["gas"].is_object()) {
    const auto& g = j["gas"];
    cfg.gamma = get_num(g, "gamma", 1.4);
    cfg.gas_R = get_num(g, "R", 1.0);
    cfg.prandtl = get_num(g, "prandtl", 0.72);
  }

  if (j.contains("freestream") && j["freestream"].is_object()) {
    const auto& fs = j["freestream"];
    cfg.mach = get_num(fs, "mach", 0.0);
    cfg.aoa_degrees = get_num(fs, "aoa_degrees", 0.0);
    cfg.rho_inf = get_num(fs, "rho", 1.0);
    cfg.vel_inf = get_num(fs, "velocity_magnitude", 1.0);
    cfg.p_inf = get_num(fs, "pressure", 1.0);
  }

  if (j.contains("reference") && j["reference"].is_object()) {
    const auto& r = j["reference"];
    cfg.ref_length = get_num(r, "length", 1.0);
    cfg.ref_area = get_num(r, "area", 1.0);
    cfg.reynolds_length = get_num(r, "reynolds_length", 1.0);
    if (r.contains("moment_center") && r["moment_center"].is_array() && r["moment_center"].size() >= 2) {
      cfg.moment_center[0] = r["moment_center"][0].get<double>();
      cfg.moment_center[1] = r["moment_center"][1].get<double>();
    }
  }

  if (j.contains("boundary_conditions") && j["boundary_conditions"].is_object()) {
    for (auto it = j["boundary_conditions"].begin(); it != j["boundary_conditions"].end(); ++it) {
      cfg.bc_map[it.key()] = it.value().get<std::string>();
    }
  }

  if (j.contains("run_control") && j["run_control"].is_object()) {
    const auto& rc = j["run_control"];
    cfg.run_type = get_str(rc, "type", "steady");
    cfg.max_steps = get_int(rc, "max_steps", 0);
    cfg.residual_reduction_target = get_num(rc, "residual_reduction_target", 4.0);
    cfg.cfl_initial = get_num(rc, "cfl_initial", 1.0);
    cfg.cfl_max = get_num(rc, "cfl_max", 100.0);
    cfg.pseudo_cfl_ramp_steps = get_int(rc, "pseudo_cfl_ramp_steps", 0);
    cfg.min_inner_iterations = get_int(rc, "min_inner_iterations", 3);
    cfg.max_inner_iterations = get_int(rc, "max_inner_iterations", 50);
    cfg.inner_residual_reduction_target = get_num(rc, "inner_residual_reduction_target", 0.01);
    cfg.time_step = get_num(rc, "time_step", 0.0);
    cfg.final_time = get_num(rc, "final_time", 0.0);
    cfg.time_integrator = get_str(rc, "time_integrator", "steady");
    cfg.inner_residual_norm = get_str(rc, "inner_residual_norm", "");
    cfg.bdf2_history_update = get_str(rc, "bdf2_history_update", "");
    cfg.rusanov_dissipation_scale = get_num(rc, "rusanov_dissipation_scale", 1.0);
  }

  if (j.contains("outputs") && j["outputs"].is_object()) {
    const auto& o = j["outputs"];
    cfg.write_forces_every = get_int(o, "write_forces_every", 1);
    cfg.write_residuals_every = get_int(o, "write_residuals_every", 1);
    auto read_bool = [&o](const char* key, bool dflt) {
      if (!o.contains(key)) return dflt;
      const auto& v = o[key];
      if (v.is_boolean()) return v.get<bool>();
      return v.get<std::string>() == "true";
    };
    cfg.write_final_field = read_bool("write_final_field", true);
    cfg.write_surface = read_bool("write_surface", true);
    cfg.write_field_every_time = get_num(o, "write_field_every_time", 0.0);
  }

  if (cfg.case_id.empty() || cfg.mesh_file.empty()) {
    err = "case file missing required fields (case_id, mesh.file)";
    return false;
  }
  if (cfg.run_type == "transient") {
    if (cfg.time_step <= 0.0 || cfg.final_time <= 0.0) {
      err = "transient case requires run_control.time_step and run_control.final_time";
      return false;
    }
    if (cfg.max_steps <= 0) {
      cfg.max_steps = (int)std::lround(cfg.final_time / cfg.time_step);
    }
  } else if (cfg.max_steps <= 0) {
    err = "steady case requires run_control.max_steps";
    return false;
  }
  if (cfg.physics_mode != "inviscid" && cfg.physics_mode != "laminar") {
    err = "unsupported physics mode: " + cfg.physics_mode;
    return false;
  }

  // Derived freestream quantities.
  double aoa = cfg.aoa_degrees * M_PI / 180.0;
  cfg.u_inf = cfg.vel_inf * std::cos(aoa);
  cfg.v_inf = cfg.vel_inf * std::sin(aoa);
  cfg.q_inf = 0.5 * cfg.rho_inf * cfg.vel_inf * cfg.vel_inf;
  cfg.t_inf = cfg.p_inf / (cfg.gas_R * cfg.rho_inf);
  if (cfg.is_viscous()) {
    if (cfg.reynolds <= 0.0) {
      err = "laminar case requires positive reynolds number";
      return false;
    }
    cfg.viscosity = cfg.rho_inf * cfg.vel_inf * cfg.reynolds_length / cfg.reynolds;
  }
  return true;
}

}  // namespace cfd
