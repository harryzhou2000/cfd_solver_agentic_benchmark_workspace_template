#include "config.hpp"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace cfd {
namespace {

using nlohmann::json;

double get_num(const json& j, const std::string& path, double dflt) {
  const json* p = &j;
  size_t start = 0;
  while (true) {
    size_t dot = path.find('.', start);
    std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!p->contains(key)) return dflt;
    p = &(*p)[key];
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (p->is_number()) return p->get<double>();
  if (p->is_boolean()) return p->get<bool>() ? 1.0 : 0.0;
  if (p->is_string()) return std::stod(p->get<std::string>());
  return dflt;
}

std::string get_str(const json& j, const std::string& path, const std::string& dflt) {
  const json* p = &j;
  size_t start = 0;
  while (true) {
    size_t dot = path.find('.', start);
    std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!p->contains(key)) return dflt;
    p = &(*p)[key];
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (p->is_string()) return p->get<std::string>();
  return dflt;
}

BcKind parse_bc(const std::string& s) {
  if (s == "farfield") return BcKind::Farfield;
  if (s == "slip_wall") return BcKind::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BcKind::NoSlipAdiabaticWall;
  fatal("unsupported boundary condition type: '" + s + "'");
}

}  // namespace

CaseConfig load_case_config(const std::string& case_path) {
  std::ifstream f(case_path);
  if (!f) fatal("cannot open case file: " + case_path);
  json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    fatal("malformed JSON in case file " + case_path + ": " + e.what());
  }

  CaseConfig c;
  c.schema_version = (int)get_num(j, "schema_version", 1);
  if (c.schema_version != 1) {
    fatal("unsupported case schema_version " + std::to_string(c.schema_version));
  }
  c.case_id = get_str(j, "case_id", "");
  c.description = get_str(j, "description", "");

  std::string mesh = get_str(j, "mesh.file", "");
  if (mesh.empty()) fatal("case '" + c.case_id + "': missing mesh.file");
  std::error_code ec;
  std::filesystem::path mp = mesh;
  if (mp.is_relative()) {
    std::filesystem::path case_dir = std::filesystem::path(case_path).parent_path();
    mp = case_dir / mp;
  }
  c.mesh_file = std::filesystem::weakly_canonical(mp, ec).string();
  c.mesh_format = get_str(j, "mesh.format", "CGNS");

  std::string mode = get_str(j, "physics.mode", "inviscid");
  c.laminar = (mode == "laminar");
  c.reynolds = get_num(j, "physics.reynolds", 0.0);
  c.viscosity_model = get_str(j, "physics.viscosity_model", "constant");

  c.gamma = get_num(j, "gas.gamma", 1.4);
  c.gas_R = get_num(j, "gas.R", 1.0);
  c.prandtl = get_num(j, "gas.prandtl", 0.72);

  c.mach = get_num(j, "freestream.mach", 0.0);
  c.aoa_degrees = get_num(j, "freestream.aoa_degrees", 0.0);
  c.rho_inf = get_num(j, "freestream.rho", 1.0);
  double vmag = get_num(j, "freestream.velocity_magnitude", 1.0);
  c.p_inf = get_num(j, "freestream.pressure", 1.0);
  double aoa = c.aoa_degrees * 3.14159265358979323846 / 180.0;
  c.u_inf = vmag * std::cos(aoa);
  c.v_inf = vmag * std::sin(aoa);

  c.ref_length = get_num(j, "reference.length", 1.0);
  c.ref_area = get_num(j, "reference.area", 1.0);
  c.moment_cx = get_num(j, "reference.moment_center.0", 0.0);
  c.moment_cy = get_num(j, "reference.moment_center.1", 0.0);
  c.ref_reynolds_length = get_num(j, "reference.reynolds_length", 1.0);

  if (j.contains("boundary_conditions") && j["boundary_conditions"].is_object()) {
    for (auto& [family, kind] : j["boundary_conditions"].items()) {
      c.bcs.push_back(BcSpec{family, parse_bc(kind.get<std::string>())});
    }
  }
  if (c.bcs.empty()) fatal("case '" + c.case_id + "': no boundary_conditions mapping");

  std::string run_type = get_str(j, "run_control.type", "steady");
  c.transient = (run_type == "transient");
  c.time_integrator = get_str(j, "run_control.time_integrator", "bdf2_or_trapezoidal");
  c.time_step = get_num(j, "run_control.time_step", 0.0);
  c.final_time = get_num(j, "run_control.final_time", 0.0);
  c.max_steps = (int)get_num(j, "run_control.max_steps", 20000);
  c.residual_reduction_target = get_num(j, "run_control.residual_reduction_target", 4.0);
  c.cfl_initial = get_num(j, "run_control.cfl_initial", 1.0);
  c.cfl_max = get_num(j, "run_control.cfl_max", 100.0);
  c.cfl_ramp_steps = (int)get_num(j, "run_control.pseudo_cfl_ramp_steps", 0);
  c.min_inner_iterations = (int)get_num(j, "run_control.min_inner_iterations", 3);
  c.max_inner_iterations = (int)get_num(j, "run_control.max_inner_iterations", 50);
  c.inner_residual_reduction_target =
      get_num(j, "run_control.inner_residual_reduction_target", 0.01);
  c.rusanov_dissipation_scale = get_num(j, "run_control.rusanov_dissipation_scale", 1.0);

  c.write_final_field = get_num(j, "outputs.write_final_field", 1) > 0.5;
  c.write_surface = get_num(j, "outputs.write_surface", 1) > 0.5;
  c.write_forces_every = (int)get_num(j, "outputs.write_forces_every", 1);
  c.write_residuals_every = (int)get_num(j, "outputs.write_residuals_every", 1);
  c.write_field_every_time = get_num(j, "outputs.write_field_every_time", 1.0);

  // Sanity checks on the freestream/gas state.
  double a2 = c.gamma * c.p_inf / c.rho_inf;
  if (!(a2 > 0.0)) fatal("case '" + c.case_id + "': non-positive sound speed from freestream");
  double a = std::sqrt(a2);
  double mach_check = vmag / a;
  if (std::abs(mach_check - c.mach) > 1e-6 * std::max(1.0, std::abs(c.mach))) {
    log0("note: freestream velocity_magnitude gives M=" + fmt1("%.6f", mach_check) +
         " vs mach field " + fmt1("%.6f", c.mach));
  }
  if (c.transient && c.time_step <= 0.0) fatal("transient case needs positive time_step");
  if (c.transient && c.final_time <= 0.0) fatal("transient case needs positive final_time");
  if (c.max_steps <= 0) fatal("run_control.max_steps must be positive");

  return c;
}

}  // namespace cfd
