#include "case_io.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>

namespace cfd {

const char* bc_type_name(BCType t) {
  switch (t) {
    case BCType::Interior: return "interior";
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

BCType bc_type_from_string(const std::string& s) {
  if (s == "farfield") return BCType::Farfield;
  if (s == "slip_wall") return BCType::SlipWall;
  if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
  throw std::runtime_error("unsupported boundary condition type: " + s);
}

static double get_double(const nlohmann::json& j, const char* path, double dflt) {
  if (j.contains(path)) return j.at(path).get<double>();
  return dflt;
}

Case load_case(const std::string& json_path) {
  std::ifstream in(json_path);
  if (!in) throw std::runtime_error("cannot open case file: " + json_path);
  nlohmann::json j;
  in >> j;

  Case c;
  c.schema_version = j.value("schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported case schema_version " +
                             std::to_string(c.schema_version));
  c.case_id = j.value("case_id", "");
  c.description = j.value("description", "");

  // mesh (path relative to the case file directory)
  if (!j.contains("mesh") || !j.at("mesh").contains("file"))
    throw std::runtime_error("case file missing mesh.file");
  std::string mf = j.at("mesh").at("file").get<std::string>();
  std::string base = json_path.substr(0, json_path.find_last_of('/') + 1);
  if (!mf.empty() && mf[0] != '/') mf = base + mf;
  c.mesh_file = mf;

  // physics
  const auto& phys = j.at("physics");
  std::string mode = phys.value("equations", "compressible_navier_stokes");
  std::string pmode = phys.value("mode", "inviscid");
  c.laminar = (pmode == "laminar");
  c.reynolds = get_double(phys, "reynolds", 0.0);
  c.viscosity_model = phys.value("viscosity_model", "constant");

  const auto& gas = j.at("gas");
  c.gamma = gas.value("gamma", 1.4);
  c.R_gas = gas.value("R", 1.0);
  c.prandtl = gas.value("prandtl", 0.72);

  // freestream
  const auto& fs = j.at("freestream");
  c.mach = fs.value("mach", 0.0);
  c.aoa_degrees = fs.value("aoa_degrees", 0.0);
  c.rho_inf = fs.value("rho", 1.0);
  c.vmag_inf = fs.value("velocity_magnitude", 1.0);
  c.p_inf = fs.value("pressure", 1.0);

  // reference
  const auto& ref = j.at("reference");
  c.ref_length = ref.value("length", 1.0);
  c.ref_area = ref.value("area", 1.0);
  c.ref_reynolds_length = ref.value("reynolds_length", 1.0);
  if (ref.contains("moment_center") && ref.at("moment_center").is_array()) {
    const auto& mc = ref.at("moment_center");
    if (mc.size() >= 2) {
      c.ref_moment_x = mc[0].get<double>();
      c.ref_moment_y = mc[1].get<double>();
    }
  }

  // boundary conditions
  if (!j.contains("boundary_conditions") || !j.at("boundary_conditions").is_object())
    throw std::runtime_error("case file missing boundary_conditions");
  for (auto it = j.at("boundary_conditions").begin();
       it != j.at("boundary_conditions").end(); ++it) {
    c.boundary_conditions[it.key()] = bc_type_from_string(it.value().get<std::string>());
  }

  // run control
  const auto& rc = j.at("run_control");
  std::string type = rc.value("type", "steady");
  c.transient = (type == "transient");
  c.max_steps = rc.value("max_steps", 0);
  c.residual_reduction_target = rc.value("residual_reduction_target", 0.0);
  c.cfl_initial = rc.value("cfl_initial", 1.0);
  c.cfl_max = rc.value("cfl_max", 1.0);
  c.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 0);
  c.min_inner_iterations = rc.value("min_inner_iterations", 0);
  c.max_inner_iterations = rc.value("max_inner_iterations", 0);
  c.inner_residual_reduction_target =
      rc.value("inner_residual_reduction_target", 0.01);
  c.time_step = rc.value("time_step", 0.0);
  c.final_time = rc.value("final_time", 0.0);
  c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

  // outputs
  if (j.contains("outputs")) {
    const auto& out = j.at("outputs");
    c.write_forces_every = out.value("write_forces_every", 1);
    c.write_residuals_every = out.value("write_residuals_every", 1);
    c.write_field_interval = out.value("write_field_interval", 0);
  }

  return c;
}

FreeStream freestream_from_case(const Case& c) {
  FreeStream fs;
  const double aoa = c.aoa_degrees * M_PI / 180.0;
  fs.rho = c.rho_inf;
  fs.u = c.vmag_inf * std::cos(aoa);
  fs.v = c.vmag_inf * std::sin(aoa);
  fs.p = c.p_inf;
  fs.mach = c.mach;
  fs.a = c.mach > 0 ? c.vmag_inf / c.mach : std::sqrt(c.gamma * c.p_inf / c.rho_inf);
  fs.qinf = 0.5 * c.rho_inf * c.vmag_inf * c.vmag_inf;
  fs.mu = (c.laminar && c.reynolds > 0)
              ? (c.rho_inf * c.vmag_inf * c.ref_reynolds_length / c.reynolds)
              : 0.0;
  fs.t_inf = c.p_inf / (c.rho_inf * c.R_gas);
  return fs;
}

}  // namespace cfd
