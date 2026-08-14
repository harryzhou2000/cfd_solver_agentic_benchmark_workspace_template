#pragma once

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>

namespace cfds {

// Small 2-vector holder used in the config below.
struct Vec2Ref {
  double x;
  double y;
};

// Fully-resolved case configuration parsed from a benchmark case JSON file.
struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string mesh_file;            // absolute path (resolved against case dir)
  std::string mode;                 // "inviscid" | "laminar"
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  double gamma = 1.4;
  double gas_R = 1.0;
  double prandtl = 0.72;
  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho_inf = 1.0;
  double u_inf = 1.0;
  double v_inf = 0.0;
  double p_inf = 0.0;
  double ref_length = 1.0;
  double ref_area = 1.0;
  Vec2Ref moment_center = Vec2Ref{0.0, 0.0};
  double reynolds_length = 1.0;
  std::map<std::string, std::string> boundary_conditions;  // family -> bc type
  bool steady = true;
  int max_steps = 0;
  double residual_reduction_target = 0.0;
  double cfl_initial = 1.0;
  double cfl_max = 1.0;
  int pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 3;
  int max_inner_iterations = 100;
  double inner_residual_reduction_target = 0.01;
  double time_step = 0.0;
  double final_time = 0.0;
  std::string time_integrator = "steady";
  double rusanov_dissipation_scale = 1.0;
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;
};

// Parse a benchmark case file. Throws std::runtime_error on malformed input.
CaseConfig parse_case_file(const std::string& path);

}  // namespace cfds
