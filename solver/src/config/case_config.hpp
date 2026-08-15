#pragma once

// Case configuration structs mirroring the benchmark case JSON format.
// See cfd_solver_agentic_benchmark/INPUT_FORMAT.md for field semantics.
// Fields that only appear in steady or transient cases are std::optional.

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cfd {

struct MeshConfig {
  std::string file;    // CGNS mesh path (resolved to absolute by loader)
  std::string format;  // "CGNS"
  int dimension = 2;   // spatial dimension
};

struct PhysicsConfig {
  std::string equations;  // e.g. "compressible_navier_stokes"
  std::string mode;       // "inviscid" | "laminar"
  std::optional<double> reynolds;        // laminar only
  std::optional<std::string> viscosity_model;  // laminar only, e.g. "constant"
};

struct GasConfig {
  std::string model;  // "calorically_perfect"
  double gamma = 1.4;
  double R = 1.0;       // specific gas constant (nondimensional)
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
  std::vector<double> moment_center;  // [x, y]
  double reynolds_length = 1.0;
};

struct NumericsConfig {
  int spatial_order = 2;
  std::string inviscid_flux;   // e.g. "approximate_riemann"
  std::string viscous_flux;    // "disabled" | "required"
  std::string main_time_method;  // "implicit"
  std::string implicit_solver;   // "required"
  std::optional<int> transient_order;  // transient cases only
};

struct RunControlConfig {
  std::string type;  // "steady" | "transient"

  // steady-only
  std::optional<int> max_steps;
  std::optional<double> residual_reduction_target;  // orders of magnitude

  // both steady and transient
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 0;
  std::optional<int> min_inner_iterations;
  std::optional<int> max_inner_iterations;
  std::optional<double> inner_residual_reduction_target;

  // transient-only
  std::optional<double> time_step;
  std::optional<double> final_time;
  std::optional<std::string> time_integrator;  // e.g. "bdf2_or_trapezoidal"
  std::optional<std::string> inner_residual_norm;
  std::optional<std::string> bdf2_history_update;
  std::optional<double> rusanov_dissipation_scale;
};

struct OutputsConfig {
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;

  // transient-only
  std::optional<double> write_field_every_time;
  std::optional<std::string> wake_visualization;
  std::optional<std::vector<double>> recommended_vorticity_clip_range;
};

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;

  MeshConfig mesh;
  PhysicsConfig physics;
  GasConfig gas;
  FreestreamConfig freestream;
  ReferenceConfig reference;

  // mesh boundary family/tag name -> solver BC type name
  std::map<std::string, std::string> boundary_conditions;

  NumericsConfig numerics_required;
  RunControlConfig run_control;
  OutputsConfig outputs;
};

// Loads and validates a case JSON file. Relative mesh paths are resolved
// against the directory containing the case file. Throws std::runtime_error
// with a descriptive message on any parsing/validation failure.
CaseConfig load_case_config(const std::string& path);

}  // namespace cfd
