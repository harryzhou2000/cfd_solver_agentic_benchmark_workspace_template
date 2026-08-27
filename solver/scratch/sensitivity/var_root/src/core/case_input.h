// cns2d -- parsed representation of a benchmark case JSON file.
//
// The case file is the single source of truth for physics, boundary
// conditions and run control.  Nothing in the solver branches on case_id:
// every behavioural difference between cases is expressed through the fields
// below.  Unknown boundary-family names or unsupported schema versions are
// reported as errors instead of being guessed.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/types.h"

namespace cns2d {

enum class PhysicsMode { kInviscid, kLaminar };

enum class BCType {
  kFarfield,
  kSlipWall,
  kNoSlipAdiabaticWall,
};

// Wall-type predicate used by force/surface post-processing.
inline bool isWall(BCType t) {
  return t == BCType::kSlipWall || t == BCType::kNoSlipAdiabaticWall;
}

std::string bcTypeName(BCType t);
BCType parseBCType(const std::string &name);

enum class ViscosityModel { kConstant, kSutherland };

enum class RunType { kSteady, kTransient };

struct GasProperties {
  std::string model{"calorically_perfect"};
  Real gamma{1.4};
  Real R{1.0};
  Real prandtl{0.72};
};

struct FreestreamState {
  Real mach{0.0};
  Real aoa_degrees{0.0};
  Real rho{1.0};
  Real velocity_magnitude{1.0};
  Real pressure{1.0};
};

struct ReferenceQuantities {
  Real length{1.0};
  Real area{1.0};
  Vec2 moment_center{0.25, 0.0};
  Real reynolds_length{1.0};
};

struct PhysicsSettings {
  std::string equations{"compressible_navier_stokes"};
  PhysicsMode mode{PhysicsMode::kInviscid};
  Real reynolds{0.0};
  ViscosityModel viscosity_model{ViscosityModel::kConstant};
};

struct RunControl {
  RunType type{RunType::kSteady};

  // Steady controls.
  int max_steps{20000};
  Real residual_reduction_target{4.0};

  // Pseudo-time CFL schedule (used by both steady and transient runs).
  Real cfl_initial{1.0};
  Real cfl_max{100.0};
  int pseudo_cfl_ramp_steps{2000};

  // Inner relaxation / linear-solver controls.
  int min_inner_iterations{3};
  int max_inner_iterations{50};
  Real inner_residual_reduction_target{1.0e-2};

  // Transient controls.
  std::string time_integrator{"bdf2_or_trapezoidal"};
  Real time_step{0.0};
  Real final_time{0.0};
  std::string inner_residual_norm{"total_spatial_plus_physical_time"};
  std::string bdf2_history_update{"after_inner_convergence"};
  Real rusanov_dissipation_scale{1.0};
};

struct OutputControl {
  bool write_final_field{true};
  bool write_surface{true};
  int write_forces_every{1};
  int write_residuals_every{1};
  Real write_field_every_time{0.0};  // 0 disables intermediate field output
  std::string wake_visualization{};
  std::vector<Real> recommended_vorticity_clip_range{};
};

struct NumericsRequired {
  int spatial_order{2};
  std::string inviscid_flux{"approximate_riemann"};
  std::string viscous_flux{"disabled"};
  std::string main_time_method{"implicit"};
  std::string implicit_solver{"required"};
  int transient_order{0};
};

struct CaseInput {
  int schema_version{1};
  std::string case_id;
  std::string description;

  std::string mesh_file;      // resolved absolute path
  std::string mesh_format{"CGNS"};
  int mesh_dimension{2};

  PhysicsSettings physics;
  GasProperties gas;
  FreestreamState freestream;
  ReferenceQuantities reference;
  NumericsRequired numerics_required;
  RunControl run_control;
  OutputControl outputs;

  // Mesh boundary-family name -> boundary-condition type.
  std::map<std::string, BCType> boundary_conditions;

  // Absolute path of the case file itself (used for path resolution/reporting).
  std::string case_file_path;
};

// Parse and validate a case JSON file.  Throws CnsError on any malformed or
// unsupported input, including unknown BC names and missing mesh files.
CaseInput loadCaseInput(const std::string &path);

}  // namespace cns2d
