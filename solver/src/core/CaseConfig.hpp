// Parsing and validation of the benchmark case JSON (INPUT_FORMAT.md).
//
// The case file is the single source of truth for physics, boundary-condition
// mapping and run control.  Nothing in the solver branches on `case_id`; the
// only use of the identifier is as a label in output metadata.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace cfd {

enum class PhysicsMode { kInviscid, kLaminar };

enum class BcType { kFarfield, kSlipWall, kNoSlipAdiabaticWall };

enum class RunType { kSteady, kTransient };

enum class TimeIntegratorType { kBdf2, kTrapezoidal };

struct GasProperties {
  std::string model = "calorically_perfect";
  Real gamma = 1.4;
  Real R = 1.0;
  Real prandtl = 0.72;

  Real cp() const { return gamma * R / (gamma - 1.0); }
  Real cv() const { return R / (gamma - 1.0); }
};

struct FreestreamState {
  Real mach = 0.0;
  Real aoa_degrees = 0.0;
  Real rho = 1.0;
  Real velocity_magnitude = 1.0;
  Real pressure = 1.0;
};

struct ReferenceQuantities {
  Real length = 1.0;
  Real area = 1.0;
  Vec2 moment_center{{0.0, 0.0}};
  Real reynolds_length = 1.0;
};

struct RunControl {
  RunType type = RunType::kSteady;
  // steady
  int max_steps = 1000;
  Real residual_reduction_target = 4.0;
  // transient
  TimeIntegratorType time_integrator = TimeIntegratorType::kBdf2;
  Real time_step = 0.0;
  Real final_time = 0.0;
  std::string inner_residual_norm = "total_spatial_plus_physical_time";
  std::string bdf2_history_update = "after_inner_convergence";
  // shared pseudo-time controls
  Real cfl_initial = 1.0;
  Real cfl_max = 10.0;
  int pseudo_cfl_ramp_steps = 0;
  int min_inner_iterations = 1;
  int max_inner_iterations = 20;
  Real inner_residual_reduction_target = 1.0e-2;
  Real rusanov_dissipation_scale = 1.0;
};

struct OutputControl {
  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  Real write_field_every_time = 0.0;   // 0 => disabled
  std::string wake_visualization;
  std::vector<Real> recommended_vorticity_clip_range;
};

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;
  std::string mesh_file;      // resolved absolute/relative-to-cwd path
  std::string mesh_format = "CGNS";
  int mesh_dimension = 2;

  std::string equations = "compressible_navier_stokes";
  PhysicsMode mode = PhysicsMode::kInviscid;
  Real reynolds = 0.0;
  std::string viscosity_model = "constant";

  GasProperties gas;
  FreestreamState freestream;
  ReferenceQuantities reference;

  std::map<std::string, BcType> boundary_conditions;  // mesh family name -> BC

  int required_spatial_order = 2;
  std::string required_inviscid_flux;
  std::string required_viscous_flux;
  std::string required_main_time_method;
  int required_transient_order = 0;

  RunControl run;
  OutputControl outputs;

  std::string source_path;   // for provenance in metadata

  // Derived freestream quantities (nondimensional, as supplied by the case).
  Vec2 freestreamVelocity() const;
  ConsVec freestreamConservative() const;
  Real freestreamTemperature() const;
  Real freestreamSoundSpeed() const;
  Real dynamicPressure() const;   // 0.5 * rho_inf * |U_inf|^2
  Real molecularViscosity() const;  // mu from Re (0 for inviscid)

  static CaseConfig loadFromFile(const std::string& path);
  void validate() const;
};

const char* toString(BcType t);
const char* toString(PhysicsMode m);

}  // namespace cfd
