#include "case.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cfd {

using nlohmann::json;

BcType parseBcType(const std::string& name) {
  if (name == "farfield") return BcType::Farfield;
  if (name == "slip_wall") return BcType::SlipWall;
  if (name == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
  throw std::runtime_error("unsupported boundary condition type: " + name);
}

static std::string requireString(const json& j, const std::string& key,
                                 const std::string& where) {
  if (!j.contains(key) || !j[key].is_string())
    throw std::runtime_error("case file: missing or non-string '" + key + "' in " + where);
  return j[key].get<std::string>();
}

static double requireNumber(const json& j, const std::string& key,
                            const std::string& where) {
  if (!j.contains(key) || !j[key].is_number())
    throw std::runtime_error("case file: missing or non-number '" + key + "' in " + where);
  return j[key].get<double>();
}

Case loadCase(const std::string& json_path) {
  std::ifstream f(json_path);
  if (!f.is_open()) throw std::runtime_error("cannot open case file: " + json_path);
  json j;
  try {
    f >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("malformed JSON in case file " + json_path + ": " + e.what());
  }

  Case c;
  c.schema_version = j.value("schema_version", 1);
  if (c.schema_version != 1)
    throw std::runtime_error("unsupported case schema_version " +
                             std::to_string(c.schema_version) + " (only version 1 is supported)");

  c.case_id = requireString(j, "case_id", "root");
  c.description = j.value("description", "");

  const json& mesh = j.at("mesh");
  std::string mesh_rel = requireString(mesh, "file", "mesh");
  // Resolve mesh path relative to the directory containing the case file.
  if (!mesh_rel.empty() && mesh_rel[0] == '/') {
    c.mesh_file = mesh_rel;
  } else {
    size_t slash = json_path.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? "." : json_path.substr(0, slash);
    c.mesh_file = dir + "/" + mesh_rel;
  }

  const json& physics = j.at("physics");
  c.physics_mode = physics.value("mode", "inviscid");
  c.reynolds = physics.value("reynolds", 0.0);
  c.viscosity_model = physics.value("viscosity_model", "constant");
  if (c.physics_mode != "inviscid" && c.physics_mode != "laminar")
    throw std::runtime_error("unsupported physics mode: " + c.physics_mode);

  const json& gas = j.at("gas");
  c.gas.gamma = gas.value("gamma", 1.4);
  c.gas.R = gas.value("R", 1.0);
  c.gas.prandtl = gas.value("prandtl", 0.72);

  const json& fs = j.at("freestream");
  c.freestream.mach = fs.value("mach", 0.0);
  c.freestream.aoa_deg = fs.value("aoa_degrees", 0.0);
  c.freestream.rho = fs.value("rho", 1.0);
  c.freestream.velocity_magnitude = fs.value("velocity_magnitude", 1.0);
  c.freestream.pressure = fs.value("pressure", 1.0);
  double aoa = c.freestream.aoa_deg * 3.14159265358979323846 / 180.0;
  c.freestream.u = c.freestream.velocity_magnitude * std::cos(aoa);
  c.freestream.v = c.freestream.velocity_magnitude * std::sin(aoa);

  const json& ref = j.at("reference");
  c.reference.length = ref.value("length", 1.0);
  c.reference.area = ref.value("area", 1.0);
  c.reference.reynolds_length = ref.value("reynolds_length", 1.0);
  if (ref.contains("moment_center") && ref["moment_center"].is_array()) {
    const auto& mc = ref["moment_center"];
    if (mc.size() >= 2) {
      c.reference.moment_center[0] = mc[0].get<double>();
      c.reference.moment_center[1] = mc[1].get<double>();
    }
  }

  const json& bcj = j.at("boundary_conditions");
  for (auto it = bcj.begin(); it != bcj.end(); ++it) {
    std::string fam = it.key();
    std::string type = it.value().get<std::string>();
    c.bc.families[fam] = parseBcType(type);
  }

  const json& rc = j.at("run_control");
  c.run.type = rc.value("type", "steady");
  c.run.max_steps = rc.value("max_steps", 10000L);
  c.run.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
  c.run.cfl_initial = rc.value("cfl_initial", 1.0);
  c.run.cfl_max = rc.value("cfl_max", 100.0);
  c.run.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 2000L);
  c.run.min_inner_iterations = rc.value("min_inner_iterations", 3);
  c.run.max_inner_iterations = rc.value("max_inner_iterations", 50);
  c.run.inner_residual_reduction_target =
      rc.value("inner_residual_reduction_target", 0.01);
  c.run.time_integrator = rc.value("time_integrator", "bdf2_or_trapezoidal");
  c.run.time_step = rc.value("time_step", 0.01);
  c.run.final_time = rc.value("final_time", 300.0);
  c.run.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

  if (j.contains("outputs")) {
    const json& out = j.at("outputs");
    c.outputs.write_final_field = out.value("write_final_field", true);
    c.outputs.write_surface = out.value("write_surface", true);
    c.outputs.write_forces_every = out.value("write_forces_every", 1);
    c.outputs.write_residuals_every = out.value("write_residuals_every", 1);
    c.outputs.write_field_every_time = out.value("write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace cfd
