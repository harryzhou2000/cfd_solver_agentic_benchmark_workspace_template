#pragma once
// JSON case-file loader. Reads the benchmark case schema (version 1) into a
// CaseConfig struct. Paths are resolved relative to the case-file directory.
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <string>
#include "types.hpp"

namespace cfd {

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;          // absolute
  std::string mesh_format = "CGNS";
  int dimension = 2;

  std::string physics_mode = "inviscid";  // "inviscid" | "laminar"
  double reynolds = 0.0;
  std::string viscosity_model = "constant";

  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;

  double mach = 0.0;
  double aoa_deg = 0.0;
  double rho_inf = 1.0;
  double vel_inf = 1.0;           // velocity magnitude
  double p_inf = 1.0;

  double ref_length = 1.0;
  double ref_area = 1.0;
  double moment_cx = 0.0, moment_cy = 0.0;
  double reynolds_length = 1.0;

  // boundary family name -> BC type
  std::vector<std::pair<std::string, BCType>> bcs;

  // run control
  std::string run_type = "steady";   // "steady" | "transient"
  std::string time_integrator;       // "bdf2_or_trapezoidal"
  int max_steps = 10000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 1000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  double time_step = 0.0;
  double final_time = 0.0;
  double rusanov_dissipation_scale = 1.0;

  // numerics required
  int spatial_order = 2;
  std::string inviscid_flux_req = "approximate_riemann";
  std::string viscous_flux_req = "disabled";
  int transient_order = 2;

  // outputs
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;
};

inline BCType parseBC(const std::string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  throw std::runtime_error("unsupported boundary condition: " + s);
}

inline CaseConfig loadCase(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open case file: " + path);
  nlohmann::json j;
  in >> j;
  CaseConfig c;
  c.schema_version = j.value("schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported case schema_version: " + std::to_string(c.schema_version));
  c.case_id = j.at("case_id").get<std::string>();
  c.description = j.value("description", c.case_id);

  const auto& mesh = j.at("mesh");
  c.mesh_format = mesh.value("format", "CGNS");
  c.dimension = mesh.value("dimension", 2);
  std::string mfile = mesh.at("file").get<std::string>();
  // resolve relative to case-file directory
  std::string caseDir;
  {
    auto pos = path.find_last_of('/');
    if (pos != std::string::npos) caseDir = path.substr(0, pos + 1);
  }
  if (!mfile.empty() && mfile[0] == '/') c.mesh_file = mfile;
  else c.mesh_file = caseDir + mfile;

  const auto& phys = j.at("physics");
  c.physics_mode = phys.value("mode", "inviscid");
  c.reynolds = phys.value("reynolds", 0.0);
  c.viscosity_model = phys.value("viscosity_model", "constant");

  const auto& gas = j.at("gas");
  c.gamma = gas.value("gamma", 1.4);
  c.R = gas.value("R", 1.0);
  c.prandtl = gas.value("prandtl", 0.72);

  const auto& fs = j.at("freestream");
  c.mach = fs.at("mach").get<double>();
  c.aoa_deg = fs.value("aoa_degrees", 0.0);
  c.rho_inf = fs.value("rho", 1.0);
  c.vel_inf = fs.value("velocity_magnitude", 1.0);
  c.p_inf = fs.value("pressure", 1.0);

  const auto& ref = j.at("reference");
  c.ref_length = ref.value("length", 1.0);
  c.ref_area = ref.value("area", 1.0);
  if (ref.contains("moment_center") && ref["moment_center"].is_array()
      && ref["moment_center"].size() >= 2) {
    c.moment_cx = ref["moment_center"][0].get<double>();
    c.moment_cy = ref["moment_center"][1].get<double>();
  }
  c.reynolds_length = ref.value("reynolds_length", 1.0);

  const auto& bcs = j.at("boundary_conditions");
  for (auto it = bcs.begin(); it != bcs.end(); ++it)
    c.bcs.push_back({it.key(), parseBC(it.value().get<std::string>())});

  if (j.contains("numerics_required")) {
    const auto& nr = j["numerics_required"];
    c.spatial_order = nr.value("spatial_order", 2);
    c.inviscid_flux_req = nr.value("inviscid_flux", "approximate_riemann");
    c.viscous_flux_req = nr.value("viscous_flux", "disabled");
    c.transient_order = nr.value("transient_order", 2);
  }

  const auto& rc = j.at("run_control");
  c.run_type = rc.value("type", "steady");
  c.time_integrator = rc.value("time_integrator", "");
  c.max_steps = rc.value("max_steps", 10000);
  c.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
  c.cfl_initial = rc.value("cfl_initial", 1.0);
  c.cfl_max = rc.value("cfl_max", 100.0);
  c.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 1000);
  c.min_inner_iterations = rc.value("min_inner_iterations", 3);
  c.max_inner_iterations = rc.value("max_inner_iterations", 50);
  c.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
  c.time_step = rc.value("time_step", 0.0);
  c.final_time = rc.value("final_time", 0.0);
  c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

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

}  // namespace cfd
