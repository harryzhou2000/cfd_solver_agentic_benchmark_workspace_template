#pragma once

#include "cfd/types.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace cfd {

enum class PhysicsMode { inviscid, laminar };
enum class BoundaryCondition { farfield, slip_wall, no_slip_adiabatic_wall };
enum class RunType { steady, transient };

struct MeshConfig {
  std::filesystem::path file;
  std::string format;
  int dimension{};
};

struct PhysicsConfig {
  std::string equations;
  PhysicsMode mode{};
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
  Vec2 moment_center{};
  double reynolds_length{};
};

struct NumericsRequired {
  int spatial_order{};
  std::string inviscid_flux;
  std::string viscous_flux;
  std::string main_time_method;
  std::optional<int> transient_order;
  std::string implicit_solver;
};

struct RunControl {
  RunType type{};
  std::optional<int> max_steps;
  std::optional<double> residual_reduction_target;
  std::optional<std::string> time_integrator;
  std::optional<double> time_step;
  std::optional<double> final_time;
  int min_inner_iterations{};
  int max_inner_iterations{};
  double inner_residual_reduction_target{};
  std::optional<std::string> inner_residual_norm;
  std::optional<std::string> bdf2_history_update;
  double cfl_initial{};
  double cfl_max{};
  int pseudo_cfl_ramp_steps{};
  std::optional<double> rusanov_dissipation_scale;
};

struct OutputConfig {
  bool write_final_field{};
  bool write_surface{};
  int write_forces_every{};
  int write_residuals_every{};
  std::optional<double> write_field_every_time;
  std::optional<std::string> wake_visualization;
  std::optional<Vec2> recommended_vorticity_clip_range;
};

struct CaseConfig {
  int schema_version{};
  std::string case_id;
  std::string description;
  MeshConfig mesh;
  PhysicsConfig physics;
  GasConfig gas;
  FreestreamConfig freestream;
  ReferenceConfig reference;
  std::map<std::string, BoundaryCondition> boundary_conditions;
  NumericsRequired numerics_required;
  RunControl run_control;
  OutputConfig outputs;
  std::filesystem::path source_file;
};

CaseConfig parse_case_json(std::string_view text,
                           const std::filesystem::path& base_directory,
                           std::string source_name = "<memory>");
CaseConfig parse_case_file(const std::filesystem::path& path);
std::string to_string(PhysicsMode value);
std::string to_string(BoundaryCondition value);
std::string to_string(RunType value);

}  // namespace cfd
