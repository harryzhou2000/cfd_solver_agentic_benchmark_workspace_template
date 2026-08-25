#include "case_file.hpp"

#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

using nlohmann::json;

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
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}

static std::string join_path(const std::string& dir, const std::string& p) {
  if (!p.empty() && p[0] == '/') return p;
  return dir + "/" + p;
}

template <typename T>
static T get_or(const json& j, const char* key, T def) {
  if (j.contains(key)) return j.at(key).get<T>();
  return def;
}

CaseFile load_case_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("cannot open case file: " + path);
  json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("failed to parse case JSON " + path + ": " + e.what());
  }

  CaseFile c;
  c.case_dir = dir_of(path);
  c.schema_version = get_or<int>(j, "schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported case schema_version " +
                             std::to_string(c.schema_version) + " (only 1 supported)");
  c.case_id = j.at("case_id").get<std::string>();
  c.description = get_or<std::string>(j, "description", "");

  const json& mesh = j.at("mesh");
  c.mesh_file = join_path(c.case_dir, mesh.at("file").get<std::string>());
  c.mesh_format = get_or<std::string>(mesh, "format", "CGNS");
  c.mesh_dimension = get_or<int>(mesh, "dimension", 2);
  if (c.mesh_dimension != 2)
    throw std::runtime_error("only 2-D meshes are supported");

  const json& phys = j.at("physics");
  c.physics_mode = phys.at("mode").get<std::string>();
  if (c.physics_mode != "inviscid" && c.physics_mode != "laminar")
    throw std::runtime_error("unsupported physics.mode: " + c.physics_mode);
  c.reynolds = get_or<double>(phys, "reynolds", 0.0);
  c.viscosity_model = get_or<std::string>(phys, "viscosity_model", "constant");
  if (c.physics_mode == "laminar" && c.reynolds <= 0.0)
    throw std::runtime_error("laminar case requires positive physics.reynolds");

  const json& gas = j.at("gas");
  c.gas.gamma = gas.at("gamma").get<double>();
  c.gas.R = gas.at("R").get<double>();
  c.gas.prandtl = gas.at("prandtl").get<double>();
  std::string gmodel = get_or<std::string>(gas, "model", "calorically_perfect");
  if (gmodel != "calorically_perfect")
    throw std::runtime_error("unsupported gas model: " + gmodel);

  const json& fs = j.at("freestream");
  c.mach = fs.at("mach").get<double>();
  c.aoa_deg = get_or<double>(fs, "aoa_degrees", 0.0);
  c.fs_rho = fs.at("rho").get<double>();
  c.fs_vmag = fs.at("velocity_magnitude").get<double>();
  c.fs_p = fs.at("pressure").get<double>();

  const json& ref = j.at("reference");
  c.ref.length = ref.at("length").get<double>();
  c.ref.area = ref.at("area").get<double>();
  auto mc = ref.at("moment_center").get<std::vector<double>>();
  c.ref.moment_center[0] = mc.at(0);
  c.ref.moment_center[1] = mc.at(1);
  c.ref.reynolds_length = get_or<double>(ref, "reynolds_length", c.ref.length);

  for (auto it = j.at("boundary_conditions").begin(); it != j.at("boundary_conditions").end(); ++it)
    c.bc_map[it.key()] = parse_bc_type(it.value().get<std::string>());
  if (c.bc_map.empty()) throw std::runtime_error("case has no boundary_conditions");

  const json& rc = j.at("run_control");
  c.run.type = rc.at("type").get<std::string>();
  c.run.max_steps = get_or<int>(rc, "max_steps", 10000);
  c.run.residual_reduction_target = get_or<double>(rc, "residual_reduction_target", 4.0);
  c.run.cfl_initial = get_or<double>(rc, "cfl_initial", 1.0);
  c.run.cfl_max = get_or<double>(rc, "cfl_max", 100.0);
  c.run.pseudo_cfl_ramp_steps = get_or<int>(rc, "pseudo_cfl_ramp_steps", 1000);
  c.run.min_inner_iterations = get_or<int>(rc, "min_inner_iterations", 3);
  c.run.max_inner_iterations = get_or<int>(rc, "max_inner_iterations", 50);
  c.run.inner_residual_reduction_target = get_or<double>(rc, "inner_residual_reduction_target", 0.01);
  c.run.time_integrator = get_or<std::string>(rc, "time_integrator", "");
  c.run.time_step = get_or<double>(rc, "time_step", 0.0);
  c.run.final_time = get_or<double>(rc, "final_time", 0.0);
  c.run.rusanov_dissipation_scale = get_or<double>(rc, "rusanov_dissipation_scale", 1.0);
  c.run.inner_residual_norm = get_or<std::string>(rc, "inner_residual_norm", "");
  c.run.bdf2_history_update = get_or<std::string>(rc, "bdf2_history_update", "");
  if (c.run.type != "steady" && c.run.type != "transient")
    throw std::runtime_error("unsupported run_control.type: " + c.run.type);
  if (c.run.type == "transient" && (c.run.time_step <= 0.0 || c.run.final_time <= 0.0))
    throw std::runtime_error("transient case requires positive time_step and final_time");

  const json& out = j.value("outputs", json::object());
  c.outputs.write_final_field = get_or<bool>(out, "write_final_field", true);
  c.outputs.write_surface = get_or<bool>(out, "write_surface", true);
  c.outputs.write_forces_every = get_or<int>(out, "write_forces_every", 1);
  c.outputs.write_residuals_every = get_or<int>(out, "write_residuals_every", 1);
  c.outputs.write_field_every_time = get_or<double>(out, "write_field_every_time", 0.0);
  if (out.contains("recommended_vorticity_clip_range")) {
    auto vr = out.at("recommended_vorticity_clip_range").get<std::vector<double>>();
    c.outputs.vorticity_clip_lo = vr.at(0);
    c.outputs.vorticity_clip_hi = vr.at(1);
    c.outputs.has_vorticity_clip = true;
  }

  c.finalize();
  return c;
}

void CaseFile::finalize() {
  fs.mach = mach;
  fs.aoa_deg = aoa_deg;
  fs.rho = fs_rho;
  fs.vmag = fs_vmag;
  fs.p = fs_p;
  const double aoa = aoa_deg * M_PI / 180.0;
  fs.u = fs_vmag * std::cos(aoa);
  fs.v = fs_vmag * std::sin(aoa);
  fs.a = fs_vmag / mach;
  fs.T = fs.p / (fs.rho * gas.R);
  fs.viscous = (physics_mode == "laminar");
  fs.reynolds = reynolds;
  if (fs.viscous) {
    // Constant viscosity matched to the case Reynolds number:
    //   mu = rho_inf * U_inf * L_ref / Re
    fs.mu = fs.rho * fs.vmag * ref.reynolds_length / reynolds;
    fs.k_cond = fs.mu * gas.cp() / gas.prandtl;
  }
  fs.q_dyn = 0.5 * fs.rho * fs_vmag * fs_vmag;
}

}  // namespace cfd
