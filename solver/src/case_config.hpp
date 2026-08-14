#pragma once

#include <map>
#include <string>

struct GasModel {
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;
};

struct FreeStream {
    double mach = 0.0;
    double aoa_deg = 0.0;
    double rho = 1.0;
    double vel_mag = 1.0;
    double pressure = 1.0;
    double u = 1.0, v = 0.0;
};

struct Reference {
    double length = 1.0;
    double area = 1.0;
    double moment_center[2] = {0.0, 0.0};
    double reynolds_length = 1.0;
};

struct RunControl {
    std::string type = "steady"; // "steady" | "transient"
    long max_steps = 1000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0;
    double cfl_max = 50.0;
    long pseudo_cfl_ramp_steps = 1000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 1e-2;
    // transient
    std::string time_integrator = "bdf2_or_trapezoidal";
    double time_step = 0.01;
    double final_time = 1.0;
    double rusanov_dissipation_scale = 1.0;
};

struct Outputs {
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 0.0;
};

struct CaseConfig {
    int schema_version = 1;
    std::string case_id;
    std::string description;
    std::string mesh_file;       // resolved absolute or cwd-relative path
    std::string physics_mode;    // "inviscid" | "laminar"
    double reynolds = 0.0;       // 0 for inviscid
    std::string viscosity_model = "constant";
    GasModel gas;
    FreeStream freestream;
    Reference reference;
    std::map<std::string, std::string> boundary_conditions; // family name -> bc type
    RunControl run_control;
    Outputs outputs;

    double viscosity() const; // dynamic viscosity consistent with Reynolds
};

// Parse a case JSON file. Throws std::runtime_error with a clear message on
// malformed input or unsupported schema version.
CaseConfig load_case(const std::string& path);
