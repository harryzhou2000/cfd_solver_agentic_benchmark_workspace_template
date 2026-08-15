#include "case_file.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace fv {

namespace {
std::string dirOf(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? std::string(".") : path.substr(0, pos);
}

std::string resolvePath(const std::string& baseDir, const std::string& p) {
  if (!p.empty() && p[0] == '/') return p;
  return baseDir + "/" + p;
}

BCType parseBCType(const std::string& s) {
  if (s == "farfield") return BCType::FARFIELD;
  if (s == "slip_wall") return BCType::SLIP_WALL;
  if (s == "no_slip_adiabatic_wall") return BCType::NO_SLIP_ADIABATIC;
  throw std::runtime_error("unsupported boundary condition type: " + s);
}
}  // namespace

CaseConfig loadCaseFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open case file: " + path);
  nlohmann::json j;
  in >> j;

  CaseConfig c;
  c.case_dir = dirOf(path);
  c.schema_version = j.at("schema_version").get<int>();
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported schema_version " + std::to_string(c.schema_version));
  c.case_id = j.at("case_id").get<std::string>();
  c.description = j.value("description", "");

  const auto& mesh = j.at("mesh");
  c.mesh_file = resolvePath(c.case_dir, mesh.at("file").get<std::string>());

  const auto& physics = j.at("physics");
  c.mode = physics.at("mode").get<std::string>();
  if (c.mode != "inviscid" && c.mode != "laminar")
    throw std::runtime_error("unsupported physics.mode: " + c.mode);
  c.reynolds = physics.value("reynolds", 0.0);

  const auto& gas = j.at("gas");
  c.gas.gamma = gas.at("gamma").get<double>();
  c.gas.R = gas.at("R").get<double>();
  c.gas.prandtl = gas.at("prandtl").get<double>();

  const auto& fs = j.at("freestream");
  c.mach = fs.at("mach").get<double>();
  c.aoa_deg = fs.value("aoa_degrees", 0.0);
  c.rho_inf = fs.at("rho").get<double>();
  c.vel_inf = fs.at("velocity_magnitude").get<double>();
  c.p_inf = fs.at("pressure").get<double>();
  const double aoa = c.aoa_deg * M_PI / 180.0;
  c.u_inf = c.vel_inf * std::cos(aoa);
  c.v_inf = c.vel_inf * std::sin(aoa);
  c.t_inf = c.p_inf / (c.rho_inf * c.gas.R);

  const auto& ref = j.at("reference");
  c.ref_length = ref.at("length").get<double>();
  c.ref_area = ref.at("area").get<double>();
  c.reynolds_length = ref.value("reynolds_length", c.ref_length);
  if (ref.contains("moment_center")) {
    c.moment_center[0] = ref["moment_center"][0].get<double>();
    c.moment_center[1] = ref["moment_center"][1].get<double>();
  }

  if (c.viscous()) {
    if (c.reynolds <= 0.0)
      throw std::runtime_error("laminar case missing positive physics.reynolds");
    c.mu_inf = c.rho_inf * c.vel_inf * c.reynolds_length / c.reynolds;
  }

  for (auto it = j.at("boundary_conditions").begin(); it != j.at("boundary_conditions").end();
       ++it) {
    c.boundaries.push_back({it.key(), parseBCType(it.value().get<std::string>())});
  }
  if (c.boundaries.empty()) throw std::runtime_error("case file has no boundary_conditions");

  if (j.contains("numerics_required")) {
    c.spatial_order = j["numerics_required"].value("spatial_order", 2);
  }

  const auto& rc = j.at("run_control");
  c.run_type = rc.at("type").get<std::string>();
  if (c.run_type != "steady" && c.run_type != "transient")
    throw std::runtime_error("unsupported run_control.type: " + c.run_type);
  c.max_steps = rc.value("max_steps", 0L);
  c.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
  c.cfl_initial = rc.value("cfl_initial", 1.0);
  c.cfl_max = rc.value("cfl_max", 1.0);
  c.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 0L);
  c.min_inner_iterations = rc.value("min_inner_iterations", 3);
  c.max_inner_iterations = rc.value("max_inner_iterations", 50);
  c.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
  c.time_integrator = rc.value("time_integrator", "");
  c.time_step = rc.value("time_step", 0.0);
  c.final_time = rc.value("final_time", 0.0);
  c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

  if (c.transient()) {
    if (c.time_step <= 0.0 || c.final_time <= 0.0)
      throw std::runtime_error("transient case requires positive time_step and final_time");
    c.max_steps = static_cast<long>(std::llround(c.final_time / c.time_step));
  } else if (c.max_steps <= 0) {
    throw std::runtime_error("steady case requires positive max_steps");
  }

  if (j.contains("outputs")) {
    const auto& o = j["outputs"];
    c.write_final_field = o.value("write_final_field", true);
    c.write_surface = o.value("write_surface", true);
    c.write_forces_every = o.value("write_forces_every", 1);
    c.write_residuals_every = o.value("write_residuals_every", 1);
    c.write_field_every_time = o.value("write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace fv
