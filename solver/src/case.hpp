// Case-file parsing. Reads the benchmark JSON case files (INPUT_FORMAT.md)
// into a CaseDef. The solver consumes CaseDef for physics, BC mapping, and
// run control. Only the documented fields are required; extra fields are
// ignored so future schema additions stay forward-compatible.
#pragma once
#include "types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace cfd {

struct Freestream {
  double mach = 0.1;
  double aoa_rad = 0.0;
  double rho = 1.0;
  double velocity = 1.0;
  double pressure = 71.42857142857142;
  // derived: u = velocity*cos(aoa), v = velocity*sin(aoa), T = p/(R*rho)
  double u() const { return velocity * std::cos(aoa_rad); }
  double v() const { return velocity * std::sin(aoa_rad); }
};

struct Reference {
  double length = 1.0;
  double area = 1.0;
  Vec2 moment_center{0.25, 0.0};
  double reynolds_length = 1.0;
};

enum class RunType { Steady, Transient };

struct RunControl {
  RunType type = RunType::Steady;
  // steady
  int max_steps = 20000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 2000;
  int min_inner = 3;
  int max_inner = 50;
  double inner_residual_target = 0.01;
  // transient
  double time_step = 0.01;
  double final_time = 300.0;
  std::string time_integrator = "bdf2_or_trapezoidal";
  double rusanov_dissipation_scale = 1.0;
  std::string bdf2_history_update = "after_inner_convergence";
  std::string inner_residual_norm = "total_spatial_plus_physical_time";
};

struct CaseDef {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;
  std::string mode = "inviscid";   // "inviscid" or "laminar"
  bool laminar = false;
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  GasModel gas;
  Freestream fs;
  Reference ref;
  std::unordered_map<std::string, BCType> bc_map;
  RunControl rc;
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;
  std::string wake_viz = "vorticity_or_velocity";
};

CaseDef parse_case(const std::string& path);

}  // namespace cfd
