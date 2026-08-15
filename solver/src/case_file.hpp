#pragma once

#include <string>

#include "common.hpp"

namespace fv {

enum class BCType { FARFIELD, SLIP_WALL, NO_SLIP_ADIABATIC };

struct BoundaryMapping {
  std::string family;  // mesh family/section name
  BCType type;
};

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string case_dir;  // directory containing the case json

  std::string mesh_file;  // resolved path

  // physics: "inviscid" or "laminar"
  std::string mode = "inviscid";
  double reynolds = 0.0;  // 0 for inviscid
  bool viscous() const { return mode == "laminar"; }

  Gas gas;

  // freestream
  double mach = 0.0;
  double aoa_deg = 0.0;
  double rho_inf = 1.0;
  double vel_inf = 1.0;
  double p_inf = 1.0;
  double u_inf = 1.0;
  double v_inf = 0.0;
  double mu_inf = 0.0;  // rho U L / Re for laminar
  double t_inf = 0.0;

  // reference
  double ref_length = 1.0;
  double ref_area = 1.0;
  double moment_center[2] = {0.25, 0.0};
  double reynolds_length = 1.0;

  std::vector<BoundaryMapping> boundaries;

  // numerics
  int spatial_order = 2;

  // run control
  std::string run_type = "steady";  // or "transient"
  long max_steps = 0;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  long pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;

  // transient
  std::string time_integrator;  // "bdf2_or_trapezoidal"
  double time_step = 0.0;
  double final_time = 0.0;
  double rusanov_dissipation_scale = 1.0;

  // outputs
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;  // 0 = disabled

  bool transient() const { return run_type == "transient"; }
  double dyn_pressure() const { return 0.5 * rho_inf * vel_inf * vel_inf; }
};

CaseConfig loadCaseFile(const std::string& path);

}  // namespace fv
