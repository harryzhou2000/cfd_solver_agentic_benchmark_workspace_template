#pragma once

#include "core.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace aerofv {

class CaseConfigError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class PhysicsMode { inviscid, laminar };
enum class RunType { steady, transient };

struct MeshConfig {
  std::filesystem::path file;
  std::string format;
  int dimension{0};
};

struct PhysicsConfig {
  std::string equations;
  PhysicsMode mode{PhysicsMode::inviscid};
  std::optional<double> reynolds;
  std::optional<std::string> viscosity_model;
};

struct FreestreamConfig {
  double mach{0.0};
  double aoa_degrees{0.0};
  double rho{0.0};
  double velocity_magnitude{0.0};
  double pressure{0.0};
};

struct ReferenceConfig {
  double length{0.0};
  double area{0.0};
  Vec2 moment_center;
  double reynolds_length{0.0};
};

struct NumericsRequirements {
  int spatial_order{0};
  std::string inviscid_flux;
  std::string viscous_flux;
  std::string main_time_method;
  std::optional<int> transient_order;
  std::string implicit_solver;
};

struct RunControl {
  RunType type{RunType::steady};

  // Steady-run controls.
  std::optional<int> max_steps;
  std::optional<double> residual_reduction_target;

  // Transient-run controls.
  std::optional<std::string> time_integrator;
  std::optional<double> time_step;
  std::optional<double> final_time;
  std::optional<std::string> inner_residual_norm;
  std::optional<std::string> bdf2_history_update;
  std::optional<double> rusanov_dissipation_scale;

  // Shared implicit/pseudo-time controls.
  double cfl_initial{0.0};
  double cfl_max{0.0};
  int pseudo_cfl_ramp_steps{0};
  int min_inner_iterations{0};
  int max_inner_iterations{0};
  double inner_residual_reduction_target{0.0};
};

struct OutputControl {
  bool write_final_field{false};
  bool write_surface{false};
  int write_forces_every{0};
  int write_residuals_every{0};
  std::optional<double> write_field_every_time;
  std::optional<std::string> wake_visualization;
  std::optional<std::pair<double, double>> recommended_vorticity_clip_range;
};

struct CaseConfig {
  int schema_version{0};
  std::filesystem::path case_file;
  std::string case_id;
  std::string description;
  MeshConfig mesh;
  PhysicsConfig physics;
  GasModel gas;
  FreestreamConfig freestream;
  ReferenceConfig reference;
  std::map<std::string, BoundaryType> boundary_conditions;
  NumericsRequirements numerics;
  RunControl run_control;
  OutputControl outputs;
};

/// Parses a schema-v1 JSON case file, resolves mesh.file relative to that file,
/// and validates all supported case controls before the solver sees the config.
CaseConfig load_case_config(const std::filesystem::path &case_file);

const char *to_string(PhysicsMode mode) noexcept;
const char *to_string(RunType type) noexcept;

} // namespace aerofv
