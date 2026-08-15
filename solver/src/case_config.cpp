#include "cfd/case_config.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace cfd {

namespace {

Real get_real(const nlohmann::json& j, const char* key, Real fallback = 0.0) {
  return j.contains(key) ? j.at(key).get<Real>() : fallback;
}

int get_int(const nlohmann::json& j, const char* key, int fallback = 0) {
  return j.contains(key) ? j.at(key).get<int>() : fallback;
}

fs::path resolve_relative(const fs::path& base_file, const std::string& raw) {
  fs::path p(raw);
  if (p.is_absolute()) return p;
  return fs::weakly_canonical(base_file.parent_path() / p);
}

}  // namespace

std::string to_string(PhysicsMode mode) {
  return mode == PhysicsMode::Laminar ? "laminar" : "inviscid";
}

std::string to_string(RunType type) {
  return type == RunType::Transient ? "transient" : "steady";
}

CaseConfig read_case_config(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw CfdError("failed to open case file: " + path.string());
  nlohmann::json j;
  in >> j;

  CaseConfig cfg;
  cfg.source_path = fs::weakly_canonical(path);
  cfg.schema_version = j.at("schema_version").get<int>();
  if (cfg.schema_version != 1) {
    throw CfdError("unsupported case schema_version " + std::to_string(cfg.schema_version));
  }
  cfg.case_id = j.at("case_id").get<std::string>();
  cfg.description = j.value("description", cfg.case_id);
  cfg.mesh_file = resolve_relative(cfg.source_path, j.at("mesh").at("file").get<std::string>());

  const auto& physics = j.at("physics");
  const std::string mode = physics.at("mode").get<std::string>();
  if (mode == "inviscid") {
    cfg.mode = PhysicsMode::Inviscid;
  } else if (mode == "laminar") {
    cfg.mode = PhysicsMode::Laminar;
    cfg.reynolds = physics.value("reynolds", 0.0);
    if (cfg.reynolds <= 0.0) throw CfdError("laminar case missing positive Reynolds number");
  } else {
    throw CfdError("unsupported physics.mode: " + mode);
  }

  const auto& gas = j.at("gas");
  cfg.gas.gamma = get_real(gas, "gamma", 1.4);
  cfg.gas.R = get_real(gas, "R", 1.0);
  cfg.gas.prandtl = get_real(gas, "prandtl", 0.72);

  const auto& fsj = j.at("freestream");
  cfg.freestream.mach = get_real(fsj, "mach", 0.0);
  cfg.freestream.aoa_degrees = get_real(fsj, "aoa_degrees", 0.0);
  cfg.freestream.rho = get_real(fsj, "rho", 1.0);
  cfg.freestream.velocity_magnitude = get_real(fsj, "velocity_magnitude", 1.0);
  cfg.freestream.pressure = get_real(fsj, "pressure", 1.0);

  const auto& ref = j.at("reference");
  cfg.reference.length = get_real(ref, "length", 1.0);
  cfg.reference.area = get_real(ref, "area", 1.0);
  cfg.reference.reynolds_length = get_real(ref, "reynolds_length", cfg.reference.length);
  if (ref.contains("moment_center") && ref.at("moment_center").is_array() &&
      ref.at("moment_center").size() >= 2) {
    cfg.reference.moment_center.x = ref.at("moment_center")[0].get<Real>();
    cfg.reference.moment_center.y = ref.at("moment_center")[1].get<Real>();
  }

  for (auto it = j.at("boundary_conditions").begin(); it != j.at("boundary_conditions").end(); ++it) {
    cfg.boundary_conditions[it.key()] = it.value().get<std::string>();
  }

  const auto& rc = j.at("run_control");
  const std::string rtype = rc.at("type").get<std::string>();
  cfg.run_control.type = (rtype == "transient") ? RunType::Transient : RunType::Steady;
  cfg.run_control.max_steps = get_int(rc, "max_steps", cfg.run_control.max_steps);
  cfg.run_control.residual_reduction_target =
      get_real(rc, "residual_reduction_target", cfg.run_control.residual_reduction_target);
  cfg.run_control.cfl_initial = get_real(rc, "cfl_initial", cfg.run_control.cfl_initial);
  cfg.run_control.cfl_max = get_real(rc, "cfl_max", cfg.run_control.cfl_max);
  cfg.run_control.pseudo_cfl_ramp_steps =
      get_int(rc, "pseudo_cfl_ramp_steps", cfg.run_control.pseudo_cfl_ramp_steps);
  cfg.run_control.min_inner_iterations =
      get_int(rc, "min_inner_iterations", cfg.run_control.min_inner_iterations);
  cfg.run_control.max_inner_iterations =
      get_int(rc, "max_inner_iterations", cfg.run_control.max_inner_iterations);
  cfg.run_control.inner_residual_reduction_target =
      get_real(rc, "inner_residual_reduction_target",
               cfg.run_control.inner_residual_reduction_target);
  cfg.run_control.time_step = get_real(rc, "time_step", cfg.run_control.time_step);
  cfg.run_control.final_time = get_real(rc, "final_time", cfg.run_control.final_time);
  cfg.run_control.rusanov_dissipation_scale =
      get_real(rc, "rusanov_dissipation_scale", cfg.run_control.rusanov_dissipation_scale);
  if (cfg.run_control.type == RunType::Transient) {
    cfg.run_control.max_steps =
        static_cast<int>(std::llround(cfg.run_control.final_time / cfg.run_control.time_step));
  }

  if (!fs::exists(cfg.mesh_file)) {
    throw CfdError("mesh file does not exist: " + cfg.mesh_file.string());
  }
  return cfg;
}

}  // namespace cfd
