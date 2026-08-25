#pragma once
// Parsing of benchmark JSON case files (schema_version 1).

#include <string>
#include <map>
#include "common.hpp"

namespace cfd {

struct ReferenceData {
  double length = 1.0;
  double area = 1.0;
  double moment_center[2] = {0.0, 0.0};
  double reynolds_length = 1.0;
};

struct RunControl {
  std::string type = "steady";  // "steady" or "transient"
  // steady controls
  int max_steps = 10000;
  double residual_reduction_target = 4.0;  // orders of magnitude
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 1000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  // transient controls
  std::string time_integrator;  // "bdf2_or_trapezoidal" etc.
  double time_step = 0.01;
  double final_time = 300.0;
  double rusanov_dissipation_scale = 1.0;
  std::string inner_residual_norm;
  std::string bdf2_history_update;
};

struct OutputControl {
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;  // 0 = only final field
  double vorticity_clip_lo = -5.0, vorticity_clip_hi = 5.0;
  bool has_vorticity_clip = false;
};

struct CaseFile {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;      // absolute path after resolution
  std::string mesh_format;
  int mesh_dimension = 2;
  std::string physics_mode;   // "inviscid" or "laminar"
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  GasModel gas;
  double mach = 0.0, aoa_deg = 0.0;
  double fs_rho = 1.0, fs_vmag = 1.0, fs_p = 1.0;
  ReferenceData ref;
  std::map<std::string, BCType> bc_map;  // family name -> BC type
  RunControl run;
  OutputControl outputs;
  std::string case_dir;  // directory containing the case file

  // Derived free-stream state, computed by finalize().
  FreeStream fs;
  void finalize();
};

CaseFile load_case_file(const std::string& path);

}  // namespace cfd
