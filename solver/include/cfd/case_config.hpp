#pragma once

#include <filesystem>
#include <map>
#include <string>

#include "cfd/common.hpp"

namespace cfd {

enum class PhysicsMode { Inviscid, Laminar };
enum class RunType { Steady, Transient };

struct Freestream {
  Real mach{0.0};
  Real aoa_degrees{0.0};
  Real rho{1.0};
  Real velocity_magnitude{1.0};
  Real pressure{1.0};
};

struct GasModel {
  Real gamma{1.4};
  Real R{1.0};
  Real prandtl{0.72};
};

struct Reference {
  Real length{1.0};
  Real area{1.0};
  Vec2 moment_center{0.0, 0.0};
  Real reynolds_length{1.0};
};

struct RunControl {
  RunType type{RunType::Steady};
  int max_steps{1000};
  Real residual_reduction_target{3.0};
  Real cfl_initial{1.0};
  Real cfl_max{10.0};
  int pseudo_cfl_ramp_steps{1000};
  int min_inner_iterations{3};
  int max_inner_iterations{50};
  Real inner_residual_reduction_target{1.0e-2};
  Real time_step{0.0};
  Real final_time{0.0};
  Real rusanov_dissipation_scale{1.0};
};

struct CaseConfig {
  std::filesystem::path source_path;
  int schema_version{1};
  std::string case_id;
  std::string description;
  std::filesystem::path mesh_file;
  PhysicsMode mode{PhysicsMode::Inviscid};
  Real reynolds{0.0};
  GasModel gas;
  Freestream freestream;
  Reference reference;
  std::map<std::string, std::string> boundary_conditions;
  RunControl run_control;
};

CaseConfig read_case_config(const std::filesystem::path& path);
std::string to_string(PhysicsMode mode);
std::string to_string(RunType type);

}  // namespace cfd
