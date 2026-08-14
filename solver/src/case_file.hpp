#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cfd {

enum class BCType { Farfield, SlipWall, NoSlipAdiabaticWall };

struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
};

struct Freestream {
  double mach = 1.0;
  double aoa_degrees = 0.0;
  double rho = 1.0;
  double velocity_magnitude = 1.0;
  double pressure = 1.0;
};

struct Reference {
  double length = 1.0;
  double area = 1.0;
  std::array<double, 2> moment_center{0.0, 0.0};
  double reynolds_length = 1.0;
};

struct RunControl {
  std::string type = "steady";
  int max_steps = 1000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 50.0;
  int pseudo_cfl_ramp_steps = 1000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  std::string time_integrator = "pseudo_implicit_euler";
  double time_step = 0.01;
  double final_time = 1.0;
  double rusanov_dissipation_scale = 1.0;
};

struct CaseFile {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string case_path;
  std::string case_dir;
  std::string mesh_file;
  std::string physics_mode = "inviscid";
  double reynolds = 0.0;
  std::string viscosity_model = "constant";
  GasModel gas;
  Freestream freestream;
  Reference reference;
  std::map<std::string, BCType> bc_map;
  RunControl run;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;
  bool debug_first_order = false;
  bool debug_explicit = false;
  int flux_choice = 0;  // 0 = rusanov cell-jump, 1 = roe
  double low_mach_mref = 0.0;  // 0 = off

  bool viscous() const { return physics_mode == "laminar"; }
  bool transient() const { return run.type == "transient"; }
  double viscosity() const {
    return freestream.rho * freestream.velocity_magnitude *
           reference.reynolds_length / reynolds;
  }
};

CaseFile load_case_file(const std::string& path);
BCType parse_bc_type(const std::string& s);
std::string bc_type_name(BCType t);

}  // namespace cfd
