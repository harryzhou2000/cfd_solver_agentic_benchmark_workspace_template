#pragma once
#include <string>
#include <map>

namespace solver {

struct PhysicsConfig {
    std::string equations;
    std::string mode;       // "inviscid" or "laminar"
    double reynolds = 1.0;
    std::string viscosity_model;
};

struct GasConfig {
    std::string model;
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;
};

struct FreestreamConfig {
    double mach = 0.0;
    double aoa_degrees = 0.0;
    double rho = 1.0;
    double velocity_magnitude = 1.0;
    double pressure = 0.0;
    // Derived
    double u_inf = 0.0;
    double v_inf = 0.0;
    double temperature = 0.0;
    double speed_of_sound = 0.0;
    double viscosity = 0.0;  // computed from Re
};

struct ReferenceConfig {
    double length = 1.0;
    double area = 1.0;
    double moment_center[2] = {0.0, 0.0};
    double reynolds_length = 1.0;
};

struct RunControl {
    std::string type;  // "steady" or "transient"
    int max_steps = 1000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0;
    double cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 1000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;
    // Transient
    std::string time_integrator;
    double time_step = 0.0;
    double final_time = 0.0;
    std::string inner_residual_norm;
    std::string bdf2_history_update;
    double rusanov_dissipation_scale = 1.0;
};

struct OutputConfig {
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 0.0; // transient
    std::string wake_visualization;
    double recommended_vorticity_clip_range[2] = {0.0, 0.0};
};

struct MeshInput {
    std::string file;      // CGNS file path
    std::string format;    // "CGNS"
    int dimension = 2;
};

struct CaseConfig {
    int schema_version = 1;
    std::string case_id;
    std::string description;
    MeshInput mesh;
    PhysicsConfig physics;
    GasConfig gas;
    FreestreamConfig freestream;
    ReferenceConfig reference;
    std::map<std::string, std::string> boundary_conditions;
    RunControl run_control;
    OutputConfig outputs;
};

// Parse case JSON file
CaseConfig parse_case_config(const std::string& filepath);

} // namespace solver
