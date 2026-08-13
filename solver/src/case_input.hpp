#pragma once

#include <map>
#include <string>

#include "common.hpp"

namespace cfd {

enum class RunType { Steady, Transient };

struct RunControl {
    RunType type = RunType::Steady;
    int max_steps = 0;
    double residual_reduction_target = 0.0;
    double cfl_initial = 1.0;
    double cfl_max = 1.0;
    int pseudo_cfl_ramp_steps = 0;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;

    // Transient fields.
    std::string time_integrator = "bdf2";
    double time_step = 0.0;
    double final_time = 0.0;
    std::string inner_residual_norm = "total_spatial_plus_physical_time";
    std::string bdf2_history_update = "after_inner_convergence";
    double rusanov_dissipation_scale = 1.0;

    // Diagnostic overrides (documented CLI extensions for testing).
    int max_steps_override = -1;
    double final_time_override = -1.0;
    double time_step_override = -1.0;
    double cfl_cap = -1.0;  // documented conservative CFL ceiling (>=0)
};

struct OutputControl {
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 0.0;  // 0 = final only
};

struct CaseInput {
    int schema_version = 0;
    std::string case_id;
    std::string description;
    std::string mesh_file;  // resolved absolute path
    std::string mesh_dir;

    std::string equations = "compressible_navier_stokes";
    std::string mode = "inviscid";  // inviscid | laminar
    double reynolds = 0.0;
    std::string viscosity_model = "constant";

    GasModel gas;
    Primitive freestream;
    double mach_inf = 0.0;
    double aoa_deg = 0.0;
    double q_inf = 0.0;  // freestream dynamic pressure

    double ref_length = 1.0;
    double ref_area = 1.0;
    Vec2 moment_center{0.0, 0.0};
    double ref_reynolds_length = 1.0;

    std::map<std::string, BcType> bc_map;

    // Required numerics (validated, recorded in metadata).
    int spatial_order = 2;
    std::string inviscid_flux = "approximate_riemann";
    std::string main_time_method = "implicit";
    std::string implicit_solver = "required";

    RunControl run_control;
    OutputControl outputs;

    // Computed after reading.
    double viscosity = 0.0;
    double thermal_conductivity = 0.0;
    FluxScheme flux_scheme = FluxScheme::Roe;
};

// Parse a case file. `case_path` may be relative; all internal paths are
// resolved against the directory containing the case file.
CaseInput parse_case(const std::string& case_path);

}  // namespace cfd
