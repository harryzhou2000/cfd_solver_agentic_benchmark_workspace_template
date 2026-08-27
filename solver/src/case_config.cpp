#include "case_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

using nlohmann::json;

namespace fv {

BCType bcTypeFromString(const string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  die("unsupported boundary condition type: " + s);
}

string bcTypeToString(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

static string dirnameOf(const string& path) {
  auto pos = path.find_last_of('/');
  if (pos == string::npos) return ".";
  return path.substr(0, pos);
}

static string resolvePath(const string& base_dir, const string& p) {
  if (!p.empty() && p[0] == '/') return p;
  return base_dir + "/" + p;
}

template <typename T>
static T getOr(const json& j, const char* key, T def) {
  if (j.contains(key)) return j.at(key).get<T>();
  return def;
}

CaseConfig loadCaseConfig(const string& path) {
  std::ifstream in(path);
  check(in.good(), "cannot open case file: " + path);
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    die("failed to parse case JSON " + path + ": " + e.what());
  }

  CaseConfig c;
  c.schema_version = getOr<int>(j, "schema_version", 1);
  check(c.schema_version == 1, "unsupported case schema_version " + std::to_string(c.schema_version));
  c.case_id = j.at("case_id").get<string>();
  c.description = getOr<string>(j, "description", "");

  const string base = dirnameOf(path);
  const json& mesh = j.at("mesh");
  c.mesh_file = resolvePath(base, mesh.at("file").get<string>());
  c.mesh_format = getOr<string>(mesh, "format", "CGNS");
  c.mesh_dimension = getOr<int>(mesh, "dimension", 2);
  check(c.mesh_dimension == 2, "only 2-D meshes are supported");
  check(c.mesh_format == "CGNS", "only CGNS mesh format is supported");

  const json& phys = j.at("physics");
  c.physics_mode = phys.at("mode").get<string>();
  check(c.physics_mode == "inviscid" || c.physics_mode == "laminar",
        "unsupported physics.mode: " + c.physics_mode);
  c.reynolds = getOr<double>(phys, "reynolds", 0.0);
  c.viscosity_model = getOr<string>(phys, "viscosity_model", "constant");
  if (c.viscous()) check(c.reynolds > 0.0, "laminar case requires physics.reynolds > 0");

  const json& gas = j.at("gas");
  check(getOr<string>(gas, "model", "calorically_perfect") == "calorically_perfect",
        "only calorically_perfect gas model is supported");
  c.gas.gamma = gas.at("gamma").get<double>();
  c.gas.R = gas.at("R").get<double>();
  c.gas.Pr = gas.at("prandtl").get<double>();

  const json& fs = j.at("freestream");
  c.fs_mach = fs.at("mach").get<double>();
  c.fs_aoa_deg = fs.at("aoa_degrees").get<double>();
  c.fs_rho = fs.at("rho").get<double>();
  c.fs_vel = fs.at("velocity_magnitude").get<double>();
  c.fs_pressure = fs.at("pressure").get<double>();

  const json& ref = j.at("reference");
  c.ref_length = ref.at("length").get<double>();
  c.ref_area = ref.at("area").get<double>();
  const json& mc = ref.at("moment_center");
  c.moment_center[0] = mc.at(0).get<double>();
  c.moment_center[1] = mc.at(1).get<double>();
  c.ref_reynolds_length = getOr<double>(ref, "reynolds_length", c.ref_length);

  for (auto it = j.at("boundary_conditions").begin(); it != j.at("boundary_conditions").end(); ++it) {
    c.bc_map.emplace_back(it.key(), bcTypeFromString(it.value().get<string>()));
  }

  if (j.contains("numerics_required")) {
    const json& nr = j["numerics_required"];
    c.spatial_order = getOr<int>(nr, "spatial_order", 2);
    c.inviscid_flux_req = getOr<string>(nr, "inviscid_flux", "approximate_riemann");
  }

  const json& rc = j.at("run_control");
  c.rc.type = rc.at("type").get<string>();
  c.rc.max_steps = getOr<long>(rc, "max_steps", c.rc.max_steps);
  c.rc.residual_reduction_target = getOr<double>(rc, "residual_reduction_target", c.rc.residual_reduction_target);
  c.rc.cfl_initial = getOr<double>(rc, "cfl_initial", c.rc.cfl_initial);
  c.rc.cfl_max = getOr<double>(rc, "cfl_max", c.rc.cfl_max);
  c.rc.pseudo_cfl_ramp_steps = getOr<long>(rc, "pseudo_cfl_ramp_steps", c.rc.pseudo_cfl_ramp_steps);
  c.rc.min_inner_iterations = getOr<long>(rc, "min_inner_iterations", c.rc.min_inner_iterations);
  c.rc.max_inner_iterations = getOr<long>(rc, "max_inner_iterations", c.rc.max_inner_iterations);
  c.rc.inner_residual_reduction_target = getOr<double>(rc, "inner_residual_reduction_target", c.rc.inner_residual_reduction_target);
  c.rc.time_integrator = getOr<string>(rc, "time_integrator", c.rc.time_integrator);
  c.rc.time_step = getOr<double>(rc, "time_step", c.rc.time_step);
  c.rc.final_time = getOr<double>(rc, "final_time", c.rc.final_time);
  c.rc.inner_residual_norm = getOr<string>(rc, "inner_residual_norm", c.rc.inner_residual_norm);
  c.rc.bdf2_history_update = getOr<string>(rc, "bdf2_history_update", c.rc.bdf2_history_update);
  c.rc.rusanov_dissipation_scale = getOr<double>(rc, "rusanov_dissipation_scale", c.rc.rusanov_dissipation_scale);
  c.rc.steady_init_steps = getOr<long>(rc, "steady_init_steps", c.rc.steady_init_steps);
  c.rc.steady_init_cfl = getOr<double>(rc, "steady_init_cfl", c.rc.steady_init_cfl);
  check(c.rc.type == "steady" || c.rc.type == "transient", "unsupported run_control.type: " + c.rc.type);

  if (j.contains("outputs")) {
    const json& o = j["outputs"];
    c.outputs.write_final_field = getOr<bool>(o, "write_final_field", true);
    c.outputs.write_surface = getOr<bool>(o, "write_surface", true);
    c.outputs.write_forces_every = getOr<long>(o, "write_forces_every", 1);
    c.outputs.write_residuals_every = getOr<long>(o, "write_residuals_every", 1);
    c.outputs.write_field_every_time = getOr<double>(o, "write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace fv
