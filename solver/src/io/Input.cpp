#include "io/Input.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cfds {

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open case file: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string dirname(const std::string& path) {
  auto pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}

template <typename T>
T get(const nlohmann::json& j, const std::string& key, const T& fallback) {
  if (j.contains(key)) return j.at(key).get<T>();
  return fallback;
}

}  // namespace

CaseConfig parse_case_file(const std::string& path) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(read_file(path));
  } catch (const std::exception& e) {
    throw std::runtime_error("failed to parse case JSON " + path + ": " + e.what());
  }

  CaseConfig c;
  c.schema_version = get(j, "schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported schema_version " +
                             std::to_string(c.schema_version));
  c.case_id = j.at("case_id").get<std::string>();

  // Mesh path is relative to the case-file directory unless absolute.
  std::string mesh_rel = j.at("mesh").at("file").get<std::string>();
  if (!mesh_rel.empty() && mesh_rel[0] == '/') {
    c.mesh_file = mesh_rel;
  } else {
    c.mesh_file = dirname(path) + "/" + mesh_rel;
  }

  const auto& phys = j.at("physics");
  c.mode = get(phys, "mode", std::string("inviscid"));
  c.reynolds = get(phys, "reynolds", 0.0);
  c.viscosity_model = get(phys, "viscosity_model", std::string("constant"));

  const auto& gas = j.at("gas");
  c.gamma = get(gas, "gamma", 1.4);
  c.gas_R = get(gas, "R", 1.0);
  c.prandtl = get(gas, "prandtl", 0.72);

  const auto& fs = j.at("freestream");
  c.mach = fs.at("mach").get<double>();
  c.aoa_degrees = get(fs, "aoa_degrees", 0.0);
  c.rho_inf = get(fs, "rho", 1.0);
  double u_mag = get(fs, "velocity_magnitude", 1.0);
  const double aoa = c.aoa_degrees * 3.14159265358979323846 / 180.0;
  c.u_inf = u_mag * std::cos(aoa);
  c.v_inf = u_mag * std::sin(aoa);
  c.p_inf = fs.at("pressure").get<double>();

  const auto& ref = j.at("reference");
  c.ref_length = get(ref, "length", 1.0);
  c.ref_area = get(ref, "area", 1.0);
  if (ref.contains("moment_center")) {
    const auto& mc = ref.at("moment_center");
    c.moment_center = Vec2Ref{mc.at(0).get<double>(), mc.at(1).get<double>()};
  }
  c.reynolds_length = get(ref, "reynolds_length", 1.0);

  for (const auto& [family, type] : j.at("boundary_conditions").items())
    c.boundary_conditions[family] = type.get<std::string>();

  const auto& rc = j.at("run_control");
  const std::string type = rc.at("type").get<std::string>();
  c.steady = (type == "steady");
  if (!c.steady && type != "transient")
    throw std::runtime_error("unknown run_control.type: " + type);

  c.max_steps = get(rc, "max_steps", 0);
  c.residual_reduction_target = get(rc, "residual_reduction_target", 0.0);
  c.cfl_initial = get(rc, "cfl_initial", 1.0);
  c.cfl_max = get(rc, "cfl_max", 1.0);
  c.pseudo_cfl_ramp_steps = get(rc, "pseudo_cfl_ramp_steps", 0);
  c.min_inner_iterations = get(rc, "min_inner_iterations", 3);
  c.max_inner_iterations = get(rc, "max_inner_iterations", 100);
  c.inner_residual_reduction_target = get(rc, "inner_residual_reduction_target", 0.01);
  c.time_step = get(rc, "time_step", 0.0);
  c.final_time = get(rc, "final_time", 0.0);
  c.time_integrator = get(rc, "time_integrator", std::string("steady"));
  c.rusanov_dissipation_scale = get(rc, "rusanov_dissipation_scale", 1.0);

  if (j.contains("outputs")) {
    const auto& out = j.at("outputs");
    c.write_final_field = get(out, "write_final_field", true);
    c.write_surface = get(out, "write_surface", true);
    c.write_forces_every = get(out, "write_forces_every", 1);
    c.write_residuals_every = get(out, "write_residuals_every", 1);
    c.write_field_every_time = get(out, "write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace cfds
