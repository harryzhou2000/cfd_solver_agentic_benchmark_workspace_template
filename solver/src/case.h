#pragma once

#include "common.h"
#include <nlohmann/json.hpp>

namespace cfd {

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;  // resolved absolute path
  std::string physics_mode = "inviscid";  // inviscid | laminar
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  double gamma = 1.4;
  double gas_R = 1.0;
  double prandtl = 0.72;
  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho_inf = 1.0;
  double vel_inf = 1.0;
  double p_inf = 1.0;
  double ref_length = 1.0;
  double ref_area = 1.0;
  double moment_center[2] = {0.0, 0.0};
  double reynolds_length = 1.0;
  std::map<std::string, std::string> bc_map;  // family name -> bc type

  std::string run_type = "steady";  // steady | transient
  int max_steps = 0;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 3000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  double time_step = 0.0;
  double final_time = 0.0;
  std::string time_integrator = "steady";
  std::string inner_residual_norm = "";
  std::string bdf2_history_update = "";
  double rusanov_dissipation_scale = 1.0;

  int write_forces_every = 1;
  int write_residuals_every = 1;
  bool write_final_field = true;
  bool write_surface = true;
  double write_field_every_time = 0.0;

  // Derived quantities.
  double q_inf = 0.0;
  double t_inf = 0.0;
  double viscosity = 0.0;
  double u_inf = 0.0, v_inf = 0.0;

  bool is_viscous() const { return physics_mode == "laminar"; }
  bool is_transient() const { return run_type == "transient"; }
};

// Loads a case JSON. On error returns false and fills err.
bool load_case(const std::string& json_path, CaseConfig& cfg, std::string& err);

}  // namespace cfd
