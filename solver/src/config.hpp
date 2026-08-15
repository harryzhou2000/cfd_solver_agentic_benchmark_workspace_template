#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"

namespace cfd {

// Boundary-condition kinds understood by the solver.
enum class BcKind { Farfield, SlipWall, NoSlipAdiabaticWall };

struct BcSpec {
  std::string family;   // mesh family/section name, e.g. "bc-4", "WALL"
  BcKind kind = BcKind::Farfield;
};

struct CaseConfig {
  int schema_version = 1;
  std::string case_id;
  std::string description;

  std::string mesh_file;  // resolved to an absolute path
  std::string mesh_format = "CGNS";

  bool laminar = false;
  double reynolds = 0.0;
  std::string viscosity_model = "constant";

  double gamma = 1.4;
  double gas_R = 1.0;
  double prandtl = 0.72;

  double mach = 0.0;
  double aoa_degrees = 0.0;
  double rho_inf = 1.0;
  double u_inf = 0.0;
  double v_inf = 0.0;
  double p_inf = 0.0;

  double ref_length = 1.0;
  double ref_area = 1.0;
  double moment_cx = 0.0;
  double moment_cy = 0.0;
  double ref_reynolds_length = 1.0;

  std::vector<BcSpec> bcs;

  bool transient = false;
  std::string time_integrator = "bdf2";
  double time_step = 0.0;
  double final_time = 0.0;
  int max_steps = 20000;
  double residual_reduction_target = 4.0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int cfl_ramp_steps = 2000;
  int min_inner_iterations = 3;
  int max_inner_iterations = 50;
  double inner_residual_reduction_target = 0.01;
  double rusanov_dissipation_scale = 1.0;

  bool write_final_field = true;
  bool write_surface = true;
  int write_forces_every = 1;
  int write_residuals_every = 1;
  double write_field_every_time = 1.0;

  // Optional CLI overrides (debug/experiments only; production runs use the
  // case-file values).
  struct Overrides {
    std::optional<int> max_steps;
    std::optional<double> final_time;
    std::optional<int> min_inner;
    std::optional<int> max_inner;
    std::optional<double> cfl_initial;
    std::optional<double> cfl_max;
    std::optional<std::string> flux;  // rusanov | roe
    std::optional<int> ranks_check;   // reserved
  };
  Overrides overrides;

  std::string inviscid_flux = "rusanov";  // resolved flux family for metadata

  double mu() const { return rho_inf * u_inf * ref_reynolds_length / reynolds; }
  double q_inf() const { return 0.5 * rho_inf * (u_inf * u_inf); }
};

CaseConfig load_case_config(const std::string& case_path);

}  // namespace cfd
