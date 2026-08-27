#pragma once
// Case JSON parsing (nlohmann/json is used for infrastructure-level parsing only).
#include "common.hpp"
#include "gas.hpp"

namespace fv {

enum class BCType { Farfield = 1, SlipWall = 2, NoSlipAdiabaticWall = 3 };

BCType bcTypeFromString(const string& s);
string bcTypeToString(BCType t);

struct RunControl {
  string type = "steady";  // "steady" or "transient"
  // steady
  long max_steps = 20000;
  double residual_reduction_target = 4.0;  // orders of magnitude
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  long pseudo_cfl_ramp_steps = 2000;
  long min_inner_iterations = 3;
  long max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  // transient
  string time_integrator = "bdf2";
  double time_step = 0.01;
  double final_time = 300.0;
  string inner_residual_norm = "total_spatial_plus_physical_time";
  string bdf2_history_update = "after_inner_convergence";
  double rusanov_dissipation_scale = 1.0;
  // optional steady pseudo-time phase used to initialize transient runs
  long steady_init_steps = 4000;
  double steady_init_cfl = 20.0;
};

struct Outputs {
  bool write_final_field = true;
  bool write_surface = true;
  long write_forces_every = 1;
  long write_residuals_every = 1;
  double write_field_every_time = 0.0;  // <=0 disables intermediate fields
};

struct CaseConfig {
  int schema_version = 1;
  string case_id;
  string description;
  string mesh_file;      // resolved path
  string mesh_format = "CGNS";
  int mesh_dimension = 2;
  string physics_mode = "inviscid";  // inviscid | laminar
  double reynolds = 0.0;             // 0 for inviscid
  string viscosity_model = "constant";
  Gas gas;
  // freestream
  double fs_mach = 0.0, fs_aoa_deg = 0.0, fs_rho = 1.0, fs_vel = 1.0, fs_pressure = 1.0;
  // reference
  double ref_length = 1.0, ref_area = 1.0, ref_reynolds_length = 1.0;
  double moment_center[2] = {0.0, 0.0};
  // boundary condition mapping: mesh family name -> bc type
  vector<std::pair<string, BCType>> bc_map;
  // numerics
  int spatial_order = 2;
  string inviscid_flux_req = "approximate_riemann";
  RunControl rc;
  Outputs outputs;

  bool viscous() const { return physics_mode == "laminar"; }
  bool transient() const { return rc.type == "transient"; }

  // derived flow constants
  double fs_u() const { return fs_vel * std::cos(fs_aoa_deg * M_PI / 180.0); }
  double fs_v() const { return fs_vel * std::sin(fs_aoa_deg * M_PI / 180.0); }
  double fs_a() const { return fs_vel / fs_mach; }
  double fs_T() const { return fs_pressure / (fs_rho * gas.R); }
  double dynamic_pressure() const { return 0.5 * fs_rho * fs_vel * fs_vel; }
  double viscosity() const {
    if (!viscous()) return 0.0;
    return fs_rho * fs_vel * ref_reynolds_length / reynolds;
  }
  double conductivity() const { return viscosity() * gas.cp() / gas.Pr; }
};

CaseConfig loadCaseConfig(const string& path);

}  // namespace fv
