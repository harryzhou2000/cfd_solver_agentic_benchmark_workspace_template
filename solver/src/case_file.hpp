#pragma once

#include "common.hpp"

namespace fv {

enum class BCType { Farfield, SlipWall, NoSlipAdiabatic };

struct GasModel {
    double gamma = 1.4;
    double R = 1.0;
    double prandtl = 0.72;
    double cp() const { return gamma * R / (gamma - 1.0); }
};

struct FreeStream {
    double mach = 0.0;
    double aoa_deg = 0.0;
    double rho = 1.0;
    double vel_mag = 1.0;
    double pressure = 1.0;
    double u = 1.0, v = 0.0;   // from aoa
    double T = 1.0;            // p / (rho R)
    double a = 1.0;            // sqrt(gamma p / rho)
    double q = 0.5;            // dynamic pressure 0.5 rho |U|^2
};

struct RunControl {
    std::string type = "steady";       // steady | transient
    long max_steps = 10000;
    double residual_reduction_target = 4.0;  // orders of magnitude (steady)
    double cfl_initial = 1.0;
    double cfl_max = 100.0;
    long pseudo_cfl_ramp_steps = 2000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;
    // transient
    std::string time_integrator;       // bdf2_or_trapezoidal
    double time_step = 0.0;
    double final_time = 0.0;
    std::string inner_residual_norm;
    std::string bdf2_history_update;
    double rusanov_dissipation_scale = 1.0;
};

struct CaseConfig {
    std::string path;          // case json path
    std::string case_dir;      // directory of the case json
    int schema_version = 1;
    std::string case_id;
    std::string description;
    std::string mesh_file;     // resolved absolute/relative path
    std::string physics_mode = "inviscid";  // inviscid | laminar
    double reynolds = 0.0;     // 0 for inviscid
    std::string viscosity_model = "constant";
    GasModel gas;
    FreeStream fs;
    double ref_length = 1.0;
    double ref_area = 1.0;
    double ref_moment_center[2] = {0.0, 0.0};
    double reynolds_length = 1.0;
    std::vector<std::pair<std::string, BCType>> bc_map;  // family name -> type
    RunControl rc;
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 0.0;   // 0 = disabled
    double mu = 0.0;           // resolved constant viscosity (laminar)

    // optional debug knobs (never set by production case files)
    bool dbg_first_order = false;
    int dbg_sweeps = -1;       // override inner sweep count
    bool dbg_explicit = false; // explicit forward-Euler pseudo step (debug only)
    bool low_mach_fix = false; // scale Rusanov dissipation by local Mach (low-speed flows)

    bool is_viscous() const { return physics_mode == "laminar"; }
    bool is_transient() const { return rc.type == "transient"; }
};

CaseConfig load_case(const std::string& path);
std::string bc_type_name(BCType t);

} // namespace fv
