#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>

namespace cfd {

// Boundary condition types understood by the solver.
enum class BCType {
  Interior = 0,
  Farfield,
  SlipWall,
  NoSlipAdiabaticWall,
};

const char* bc_type_name(BCType t);
BCType bc_type_from_string(const std::string& s);

// Case parameter container parsed from the JSON case files.
struct Case {
  int schema_version = 1;
  std::string case_id;
  std::string description;

  // mesh
  std::string mesh_file;

  // physics
  bool laminar = false;
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  double gamma = 1.4;
  double R_gas = 1.0;
  double prandtl = 0.72;

  // freestream
  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho_inf = 1.0;
  double vmag_inf = 1.0;
  double p_inf = 1.0;

  // reference
  double ref_length = 1.0;
  double ref_area = 1.0;
  double ref_moment_x = 0.0;
  double ref_moment_y = 0.0;
  double ref_reynolds_length = 1.0;

  // boundary conditions: CGNS family/boco name -> BC type
  std::map<std::string, BCType> boundary_conditions;

  // run control
  bool transient = false;
  int max_steps = 0;
  double residual_reduction_target = 0.0;  // orders of magnitude
  double cfl_initial = 1.0;
  double cfl_max = 1.0;
  int pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 0;
  int max_inner_iterations = 0;
  double inner_residual_reduction_target = 0.01;
  double time_step = 0.0;
  double final_time = 0.0;
  double rusanov_dissipation_scale = 1.0;

  // outputs
  int write_forces_every = 1;
  int write_residuals_every = 1;
  int write_field_interval = 0;  // 0 = final only
};

// Throws std::runtime_error on malformed input.
Case load_case(const std::string& json_path);

// Derived freestream quantities (nondimensional, R=1 units used by the cases).
struct FreeStream {
  double rho, u, v, p, mach, a;
  double qinf;          // 0.5 rho V^2
  double mu;            // constant viscosity for laminar cases
  double t_inf;
};

FreeStream freestream_from_case(const Case& c);

}  // namespace cfd
