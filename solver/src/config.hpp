#pragma once

#include <string>
#include <vector>
#include <map>

namespace cfd {

enum class BcType { Farfield, SlipWall, NoSlipAdiabaticWall };

// Parsed case configuration (schema_version 1).  Unknown extra fields are
// tolerated; required fields are validated with clear errors.
struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string mesh_file;         // path relative to the case file directory
  bool inviscid = false;         // physics.mode == "inviscid"
  bool transient = false;

  // gas
  double gamma = 1.4;
  double gas_R = 1.0;
  double prandtl = 0.72;

  // freestream
  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho_inf = 1.0;
  double vel_mag = 1.0;
  double p_inf = 0.0;

  // reference
  double ref_length = 1.0;
  double ref_area = 1.0;
  double ref_moment_x = 0.0;
  double ref_moment_y = 0.0;
  double ref_reynolds_length = 1.0;

  // physics
  double reynolds = 0.0;         // 0 for inviscid
  std::string viscosity_model = "constant";

  // boundary conditions: family name -> BC type
  std::map<std::string, BcType> boundary_conditions;

  // run control
  bool steady = true;
  int max_steps = 0;
  double residual_reduction_target = 4.0;   // orders of magnitude
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 2000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;

  // transient control
  std::string time_integrator = "bdf2";
  double time_step = 0.01;
  double final_time = 300.0;
  double rusanov_dissipation_scale = 1.0;

  // outputs
  double write_field_every_time = 0.0;   // transient cadence; 0 = off
  int write_forces_every = 1;
  int write_residuals_every = 1;

  // resolved absolute path of the case file (for relative path resolution)
  std::string case_dir;
};

// Parse a case JSON file. Throws std::runtime_error with a descriptive
// message on malformed or unsupported input.
CaseConfig load_case(const std::string& case_file);

// Map BC type to a stable string tag used in surface output.
std::string bc_type_string(BcType t);

}  // namespace cfd
