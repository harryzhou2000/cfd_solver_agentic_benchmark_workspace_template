#include "case_reader.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {

namespace {

using nlohmann::json;

constexpr double kPi = 3.14159265358979323846;

std::string required_string(const json& j, const std::string& key,
                            const std::string& section) {
  if (!j.contains(key)) {
    throw std::runtime_error("case JSON: missing required field '" + key +
                             "' in section '" + section + "'");
  }
  return j.at(key).get<std::string>();
}

}  // namespace

RunConfig load_case(const std::string& case_path) {
  std::ifstream in(case_path);
  if (!in) {
    throw std::runtime_error("cannot open case file: " + case_path);
  }

  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw std::runtime_error("failed to parse case JSON '" + case_path +
                             "': " + e.what());
  }

  RunConfig cfg;
  cfg.schema_version = j.value("schema_version", 1);
  cfg.case_id = j.value("case_id", std::string(""));
  cfg.description = j.value("description", std::string(""));

  // ---- mesh ----
  const json& mesh = j.at("mesh");
  cfg.mesh_file = required_string(mesh, "file", "mesh");
  cfg.mesh_format = mesh.value("format", "CGNS");
  cfg.dimension = mesh.value("dimension", 2);
  if (cfg.dimension != 2) {
    throw std::runtime_error(
        "case JSON: unsupported mesh dimension " +
        std::to_string(cfg.dimension) +
        " in section 'mesh' (only 2D meshes are supported)");
  }

  // Resolve the mesh path relative to the case file's directory.
  std::filesystem::path mesh_path(cfg.mesh_file);
  if (mesh_path.is_relative()) {
    mesh_path = std::filesystem::path(case_path).parent_path() / mesh_path;
  }
  cfg.mesh_file = mesh_path.lexically_normal().string();

  // ---- physics ----
  const json& phys = j.at("physics");
  cfg.equations = phys.value("equations", std::string(""));
  cfg.mode = phys.value("mode", std::string(""));
  cfg.reynolds = phys.value("reynolds", 0.0);
  cfg.viscosity_model = phys.value("viscosity_model", std::string(""));

  // ---- gas ----
  const json& gas = j.at("gas");
  cfg.gas.model = gas.value("model", std::string("calorically_perfect"));
  cfg.gas.gamma = gas.value("gamma", 1.4);
  cfg.gas.R = gas.value("R", 1.0);
  cfg.gas.prandtl = gas.value("prandtl", 0.72);

  // ---- freestream ----
  const json& fs = j.at("freestream");
  cfg.freestream.mach = fs.value("mach", 0.0);
  const double aoa_deg = fs.value("aoa_degrees", 0.0);
  cfg.freestream.aoa_rad = aoa_deg * kPi / 180.0;
  cfg.freestream.rho = fs.value("rho", 1.0);
  cfg.freestream.u_mag = fs.value("velocity_magnitude", 1.0);
  cfg.freestream.pressure = fs.value("pressure", 1.0);

  // ---- reference ----
  if (j.contains("reference")) {
    const json& ref = j.at("reference");
    cfg.ref_length = ref.value("length", 1.0);
    cfg.ref_area = ref.value("area", 1.0);
    if (ref.contains("moment_center") && ref.at("moment_center").is_array()) {
      const json& mc = ref.at("moment_center");
      if (mc.size() >= 2) {
        cfg.moment_center = Vec2(mc[0].get<double>(), mc[1].get<double>());
      }
    }
    cfg.reynolds_length = ref.value("reynolds_length", 1.0);
  }

  // ---- boundary conditions: family name -> BC type string ----
  // Every BC string is validated here (at parse time) so that read_mesh can
  // consume a pre-validated map of BCType values.
  if (j.contains("boundary_conditions") &&
      j.at("boundary_conditions").is_object()) {
    for (auto it = j.at("boundary_conditions").begin();
         it != j.at("boundary_conditions").end(); ++it) {
      const std::string bc = it.value().get<std::string>();
      if (bc_type_from_string(bc) == BCType::Invalid) {
        throw std::runtime_error(
            "case JSON: unknown boundary condition type '" + bc +
            "' for family '" + it.key() + "' (expected 'farfield', "
            "'slip_wall', or 'no_slip_adiabatic_wall')");
      }
      cfg.boundary_conditions[it.key()] = bc;
    }
  }

  // ---- numerics_required ----
  if (j.contains("numerics_required")) {
    const json& nr = j.at("numerics_required");
    cfg.spatial_order = nr.value("spatial_order", 2);
    if (cfg.spatial_order != 1 && cfg.spatial_order != 2) {
      throw std::runtime_error(
          "case JSON: unsupported spatial_order " +
          std::to_string(cfg.spatial_order) + " in section "
          "'numerics_required' (supported: 1, 2)");
    }
    cfg.inviscid_flux = nr.value("inviscid_flux", std::string(""));
    cfg.viscous_flux = nr.value("viscous_flux", std::string(""));
    cfg.main_time_method = nr.value("main_time_method", std::string(""));
    cfg.implicit_solver = nr.value("implicit_solver", std::string(""));
    cfg.transient_order = nr.value("transient_order", 0);
  }

  // ---- run_control ----
  if (j.contains("run_control")) {
    const json& rc = j.at("run_control");
    cfg.run_type = rc.value("type", std::string("steady"));
    cfg.max_steps = rc.value("max_steps", 0);
    cfg.residual_reduction_target = rc.value("residual_reduction_target", 0.0);
    cfg.cfl_initial = rc.value("cfl_initial", 1.0);
    cfg.cfl_max = rc.value("cfl_max", 100.0);
    cfg.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 0);
    cfg.min_inner_iterations = rc.value("min_inner_iterations", 0);
    cfg.max_inner_iterations = rc.value("max_inner_iterations", 0);
    cfg.inner_residual_reduction_target =
        rc.value("inner_residual_reduction_target", 0.0);
    // Transient fields.
    cfg.time_integrator = rc.value("time_integrator", std::string(""));
    cfg.time_step = rc.value("time_step", 0.0);
    cfg.final_time = rc.value("final_time", 0.0);
    cfg.inner_residual_norm =
        rc.value("inner_residual_norm", std::string(""));
    cfg.bdf2_history_update =
        rc.value("bdf2_history_update", std::string(""));
    cfg.rusanov_dissipation_scale =
        rc.value("rusanov_dissipation_scale", 1.0);
  }

  // ---- outputs ----
  if (j.contains("outputs")) {
    const json& o = j.at("outputs");
    cfg.write_final_field = o.value("write_final_field", false);
    cfg.write_surface = o.value("write_surface", false);
    cfg.write_forces_every = o.value("write_forces_every", 0);
    cfg.write_residuals_every = o.value("write_residuals_every", 0);
    cfg.write_field_every_time = o.value("write_field_every_time", 0.0);
    cfg.wake_visualization =
        o.value("wake_visualization", std::string(""));
    if (o.contains("recommended_vorticity_clip_range") &&
        o.at("recommended_vorticity_clip_range").is_array()) {
      const json& v = o.at("recommended_vorticity_clip_range");
      if (v.size() >= 2) {
        cfg.recommended_vorticity_clip_range = {
            v[0].get<double>(), v[1].get<double>()};
      }
    }
  }

  return cfg;
}

void print_case_summary(const RunConfig& cfg) {
  std::printf("=== Case Summary ===\n");
  std::printf("Case ID         : %s\n", cfg.case_id.c_str());
  std::printf("Schema version  : %d\n", cfg.schema_version);
  std::printf("Description     : %s\n", cfg.description.c_str());
  std::printf("Mesh file       : %s (%s, %dD)\n", cfg.mesh_file.c_str(),
              cfg.mesh_format.c_str(), cfg.dimension);
  std::printf("Equations       : %s\n", cfg.equations.c_str());
  std::printf("Mode            : %s\n", cfg.mode.c_str());
  if (cfg.reynolds > 0.0) {
    std::printf("Reynolds        : %.8g (viscosity model: %s)\n",
                cfg.reynolds, cfg.viscosity_model.c_str());
  }
  std::printf("Gas             : model=%s  gamma=%.8g  R=%.8g  Pr=%.8g\n",
              cfg.gas.model.c_str(), cfg.gas.gamma, cfg.gas.R, cfg.gas.prandtl);
  const Vec2 u = cfg.freestream.velocity();
  std::printf("Freestream      : M=%.8g  AoA=%.8g deg  rho=%.8g  |u|=%.8g  "
              "p=%.8g\n",
              cfg.freestream.mach, cfg.freestream.aoa_rad * 180.0 / kPi,
              cfg.freestream.rho, cfg.freestream.u_mag,
              cfg.freestream.pressure);
  std::printf("  Freestream u  : (%.8g, %.8g)\n", u.x, u.y);
  std::printf("Reference       : length=%.8g  area=%.8g  "
              "moment_center=(%.8g, %.8g)  reynolds_length=%.8g\n",
              cfg.ref_length, cfg.ref_area, cfg.moment_center.x,
              cfg.moment_center.y, cfg.reynolds_length);
  std::printf("Boundary conds  :\n");
  for (const auto& [family, bc] : cfg.boundary_conditions) {
    std::printf("    %-22s -> %s\n", family.c_str(), bc.c_str());
  }
  std::printf("Numerics        : spatial_order=%d  inviscid_flux=%s  "
              "viscous_flux=%s  time=%s  transient_order=%d  "
              "implicit_solver=%s\n",
              cfg.spatial_order, cfg.inviscid_flux.c_str(),
              cfg.viscous_flux.c_str(), cfg.main_time_method.c_str(),
              cfg.transient_order, cfg.implicit_solver.c_str());
  std::printf("Run control     : type=%s  max_steps=%d  "
              "residual_reduction=%.8g\n",
              cfg.run_type.c_str(), cfg.max_steps,
              cfg.residual_reduction_target);
  std::printf("                  cfl_initial=%.8g  cfl_max=%.8g  "
              "cfl_ramp_steps=%d  inner_iters=[%d, %d]  "
              "inner_reduction=%.8g\n",
              cfg.cfl_initial, cfg.cfl_max, cfg.pseudo_cfl_ramp_steps,
              cfg.min_inner_iterations, cfg.max_inner_iterations,
              cfg.inner_residual_reduction_target);
  if (cfg.run_type == "transient" || cfg.final_time > 0.0) {
    std::printf("                  time_integrator=%s  dt=%.8g  "
                "final_time=%.8g  inner_norm=%s  bdf2_update=%s  "
                "rusanov_scale=%.8g\n",
                cfg.time_integrator.c_str(), cfg.time_step, cfg.final_time,
                cfg.inner_residual_norm.c_str(),
                cfg.bdf2_history_update.c_str(), cfg.rusanov_dissipation_scale);
  }
  std::printf("Outputs         : final_field=%s  surface=%s  "
              "forces_every=%d  residuals_every=%d  field_every_time=%.8g\n",
              cfg.write_final_field ? "yes" : "no",
              cfg.write_surface ? "yes" : "no", cfg.write_forces_every,
              cfg.write_residuals_every, cfg.write_field_every_time);
  if (!cfg.wake_visualization.empty()) {
    std::printf("                  wake_visualization=%s  "
                "vorticity_clip=[%.8g, %.8g]\n",
                cfg.wake_visualization.c_str(),
                cfg.recommended_vorticity_clip_range[0],
                cfg.recommended_vorticity_clip_range[1]);
  }
  std::printf("=== End Case Summary ===\n");
}

}  // namespace cfd
