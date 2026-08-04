#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <string>

#include "cfd/mesh.hpp"

namespace cfd {

enum class PhysicsMode { Inviscid, Laminar };
enum class RunType { Steady, Transient };

struct GasModel {
  double gamma{1.4};
  double gas_constant{1.0};
  double prandtl{0.72};
};

struct Freestream {
  double mach{0.0};
  double aoa_degrees{0.0};
  double rho{1.0};
  double velocity_magnitude{1.0};
  double pressure{1.0};
};

struct ReferenceValues {
  double length{1.0};
  double area{1.0};
  std::array<double, 2> moment_center{0.0, 0.0};
  double reynolds_length{1.0};
};

struct RunControl {
  RunType type{RunType::Steady};
  int max_steps{1};
  double residual_reduction_target{0.0};
  double cfl_initial{1.0};
  double cfl_max{1.0};
  int pseudo_cfl_ramp_steps{0};
  int min_inner_iterations{1};
  int max_inner_iterations{1};
  double inner_residual_reduction_target{1.0e-2};
  double time_step{0.0};
  double final_time{0.0};
  std::string time_integrator;
  double rusanov_dissipation_scale{1.0};
};

struct CaseConfig {
  int schema_version{0};
  std::string case_id;
  std::string description;
  std::filesystem::path source_path;
  std::filesystem::path mesh_file;
  PhysicsMode physics_mode{PhysicsMode::Inviscid};
  double reynolds{0.0};
  GasModel gas;
  Freestream freestream;
  ReferenceValues reference;
  std::map<std::string, BoundaryType> boundary_conditions;
  RunControl run;
};

CaseConfig load_case_config(const std::filesystem::path& case_path);
std::string to_string(PhysicsMode mode);
std::string to_string(RunType type);
std::string to_string(BoundaryType type);

}  // namespace cfd
