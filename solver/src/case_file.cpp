#include "case_file.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {

BCType parse_bc_type(const std::string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  throw std::runtime_error("unsupported boundary condition type: " + s);
}

std::string bc_type_name(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

static std::string dir_of(const std::string& path) {
  auto pos = path.find_last_of('/');
  return pos == std::string::npos ? "." : path.substr(0, pos);
}

static std::string join_path(const std::string& dir, const std::string& p) {
  if (!p.empty() && p[0] == '/') return p;
  return dir + "/" + p;
}

CaseFile load_case_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open case file: " + path);
  nlohmann::json j;
  in >> j;

  CaseFile c;
  c.case_path = path;
  c.case_dir = dir_of(path);
  c.schema_version = j.at("schema_version").get<int>();
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported schema_version " +
                             std::to_string(c.schema_version));
  c.case_id = j.at("case_id").get<std::string>();
  c.description = j.value("description", "");
  c.mesh_file = join_path(c.case_dir, j.at("mesh").at("file").get<std::string>());
  c.physics_mode = j.at("physics").at("mode").get<std::string>();
  if (c.physics_mode != "inviscid" && c.physics_mode != "laminar")
    throw std::runtime_error("unsupported physics mode: " + c.physics_mode);
  c.reynolds = j.at("physics").value("reynolds", 0.0);
  c.viscosity_model = j.at("physics").value("viscosity_model", "constant");
  if (c.viscous() && c.reynolds <= 0.0)
    throw std::runtime_error("laminar case requires positive reynolds number");
  if (c.viscosity_model != "constant")
    throw std::runtime_error("unsupported viscosity model: " + c.viscosity_model);

  const auto& g = j.at("gas");
  c.gas.gamma = g.value("gamma", 1.4);
  c.gas.R = g.value("R", 1.0);
  c.gas.prandtl = g.value("prandtl", 0.72);

  const auto& f = j.at("freestream");
  c.freestream.mach = f.at("mach").get<double>();
  c.freestream.aoa_degrees = f.value("aoa_degrees", 0.0);
  c.freestream.rho = f.at("rho").get<double>();
  c.freestream.velocity_magnitude = f.at("velocity_magnitude").get<double>();
  c.freestream.pressure = f.at("pressure").get<double>();

  const auto& r = j.at("reference");
  c.reference.length = r.value("length", 1.0);
  c.reference.area = r.value("area", 1.0);
  auto mc = r.value("moment_center", std::vector<double>{0.0, 0.0});
  c.reference.moment_center = {mc.at(0), mc.at(1)};
  c.reference.reynolds_length = r.value("reynolds_length", c.reference.length);

  for (const auto& [name, type] : j.at("boundary_conditions").items())
    c.bc_map[name] = parse_bc_type(type.get<std::string>());

  const auto& rc = j.at("run_control");
  c.run.type = rc.value("type", "steady");
  if (c.run.type != "steady" && c.run.type != "transient")
    throw std::runtime_error("unsupported run_control.type: " + c.run.type);
  c.run.max_steps = rc.value("max_steps", 1000);
  c.run.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
  c.run.cfl_initial = rc.value("cfl_initial", 1.0);
  c.run.cfl_max = rc.value("cfl_max", 50.0);
  c.run.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 1000);
  c.run.min_inner_iterations = rc.value("min_inner_iterations", 3);
  c.run.max_inner_iterations = rc.value("max_inner_iterations", 50);
  c.run.inner_residual_reduction_target =
      rc.value("inner_residual_reduction_target", 0.01);
  c.run.time_integrator = rc.value("time_integrator", "pseudo_implicit_euler");
  c.run.time_step = rc.value("time_step", 0.01);
  c.run.final_time = rc.value("final_time", 1.0);
  c.run.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

  if (j.contains("outputs")) {
    const auto& o = j.at("outputs");
    c.write_forces_every = o.value("write_forces_every", 1);
    c.write_residuals_every = o.value("write_residuals_every", 1);
    c.write_field_every_time = o.value("write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace cfd
