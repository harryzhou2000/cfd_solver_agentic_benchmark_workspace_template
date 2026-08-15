#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace cfd {

class CaseConfigError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct MeshConfig {
  std::filesystem::path file;
  std::string format;
  int dimension{};
};

struct PhysicsConfig {
  std::string equations;
  std::string mode;
  std::optional<double> reynolds;
  std::optional<std::string> viscosity_model;
};

struct GasConfig {
  std::string model;
  double gamma{};
  double gas_constant{};
  double prandtl{};
};

struct FreestreamConfig {
  double mach{};
  double aoa_degrees{};
  double rho{};
  double velocity_magnitude{};
  double pressure{};
};

struct ReferenceConfig {
  double length{};
  double area{};
  std::array<double, 2> moment_center{};
  double reynolds_length{};
};

struct NumericsRequiredConfig {
  int spatial_order{};
  std::string inviscid_flux;
  std::string viscous_flux;
  std::string main_time_method;
  std::optional<int> transient_order;
  std::optional<std::string> implicit_solver;
};

struct RunControlConfig {
  std::string type;
  std::optional<int> max_steps;
  std::optional<double> residual_reduction_target;
  double cfl_initial{};
  double cfl_max{};
  int pseudo_cfl_ramp_steps{};
  int min_inner_iterations{};
  int max_inner_iterations{};
  double inner_residual_reduction_target{};

  std::optional<std::string> time_integrator;
  std::optional<double> time_step;
  std::optional<double> final_time;
  std::optional<std::string> inner_residual_norm;
  std::optional<std::string> bdf2_history_update;
  std::optional<double> rusanov_dissipation_scale;
};

struct OutputConfig {
  bool write_final_field{};
  bool write_surface{};
  int write_forces_every{};
  int write_residuals_every{};
  std::optional<double> write_field_every_time;
  std::optional<std::string> wake_visualization;
  std::optional<std::array<double, 2>> recommended_vorticity_clip_range;
};

/// Parsed schema-v1 input. Mesh paths are resolved relative to the case file.
struct CaseConfig {
  int schema_version{};
  std::string case_id;
  std::string description;
  std::filesystem::path source_file;
  MeshConfig mesh;
  PhysicsConfig physics;
  GasConfig gas;
  FreestreamConfig freestream;
  ReferenceConfig reference;
  std::map<std::string, std::string> boundary_conditions;
  NumericsRequiredConfig numerics_required;
  RunControlConfig run_control;
  OutputConfig outputs;

  static CaseConfig load(const std::filesystem::path& case_file);

  /// Throws CaseConfigError when a parsed configuration violates schema-v1
  /// invariants or the benchmark's supported value domain.
  void validate() const;
};

}  // namespace cfd
