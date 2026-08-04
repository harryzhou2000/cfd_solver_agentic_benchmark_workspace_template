#pragma once

#include "cfd/types.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace cfd {

struct GasConfig {
  double gamma = 1.4;
  double gas_constant = 1.0;
  double prandtl = 0.72;
};

struct FreestreamConfig {
  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho = 1.0;
  double velocity_magnitude = 1.0;
  double pressure = 1.0;
};

struct ReferenceConfig {
  double length = 1.0;
  double area = 1.0;
  Vec2 moment_center{};
  double reynolds_length = 1.0;
};

struct RunControl {
  bool transient = false;
  std::string time_integrator = "steady_pseudo_time";
  int max_steps = 0;
  double residual_reduction_target = 0.0;
  double cfl_initial = 1.0;
  double cfl_max = 1.0;
  int pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 1.0e-2;
  double time_step = 0.0;
  double final_time = 0.0;
  double rusanov_dissipation_scale = 1.0;
};

struct OutputControl {
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 0.0;
};

struct CaseConfig {
  int schema_version = 0;
  std::string case_id;
  std::string description;
  std::filesystem::path case_path;
  std::filesystem::path mesh_path;
  std::string physics_mode;
  bool viscous = false;
  double reynolds = 0.0;
  GasConfig gas;
  FreestreamConfig freestream;
  ReferenceConfig reference;
  std::map<std::string, BoundaryType> boundary_conditions;
  RunControl run;
  OutputControl outputs;

  [[nodiscard]] Primitive freestream_primitive() const;
  [[nodiscard]] double viscosity() const;
  [[nodiscard]] double dynamic_pressure() const;
};

CaseConfig load_case_config(const std::filesystem::path& path);

}  // namespace cfd
