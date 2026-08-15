#pragma once

#include <map>
#include <string>
#include <vector>

#include "common.hpp"

namespace cfd {

enum class BcType { Farfield, SlipWall, NoSlipAdiabaticWall };

struct BoundaryMap {
  // family name -> BC type
  std::map<std::string, BcType> families;
  // boco/section name -> BC type (fallback when family lookup fails)
  std::map<std::string, BcType> names;
  std::map<std::string, std::string> nameToFamily;
};

struct Freestream {
  double mach = 0.0;
  double aoa_deg = 0.0;
  double rho = 1.0;
  double velocity_magnitude = 1.0;
  double pressure = 1.0;
  double u = 0.0;
  double v = 0.0;
};

struct Reference {
  double length = 1.0;
  double area = 1.0;
  double moment_center[2] = {0.0, 0.0};
  double reynolds_length = 1.0;
};

struct RunControl {
  std::string type = "steady";  // steady | transient
  long max_steps = 10000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  long pseudo_cfl_ramp_steps = 2000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  // transient
  std::string time_integrator = "bdf2_or_trapezoidal";
  double time_step = 0.01;
  double final_time = 300.0;
  double rusanov_dissipation_scale = 1.0;
};

struct Outputs {
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;  // transient intermediate fields
};

struct Case {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;
  std::string physics_mode = "inviscid";  // inviscid | laminar
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  GasModel gas;
  Freestream freestream;
  Reference reference;
  BoundaryMap bc;
  RunControl run;
  Outputs outputs;

  bool isLaminar() const { return physics_mode == "laminar"; }

  // dynamic pressure q_inf = 0.5 rho U^2
  double qInf() const {
    double u = freestream.u;
    double v = freestream.v;
    return 0.5 * freestream.rho * (u * u + v * v);
  }
};

// Parse a case JSON file.  Throws std::runtime_error with a clear message on
// malformed input or unsupported schema.
Case loadCase(const std::string& json_path);

// Throws std::runtime_error on unsupported BC names.
BcType parseBcType(const std::string& name);

}  // namespace cfd
