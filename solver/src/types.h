#pragma once
#include <vector>
#include <array>
#include <string>
#include <map>
#include <set>
#include <cmath>
#include <cassert>
#include <Eigen/Dense>

using Vec4 = Eigen::Vector4d;
using Mat4 = Eigen::Matrix4d;
using Vec2 = Eigen::Vector2d;

struct GasProperties {
    double gamma = 1.4;
    double R = 1.0;
    double Pr = 0.72;
};

struct FreestreamState {
    double mach = 0.0;
    double aoa_deg = 0.0;
    double rho = 1.0;
    double velocity_mag = 1.0;
    double pressure = 1.0;
};

struct ReferenceValues {
    double length = 1.0;
    double area = 1.0;
    Vec2 moment_center = {0.25, 0.0};
    double reynolds_length = 1.0;
};

struct RunControl {
    std::string type = "steady";
    int max_steps = 20000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0;
    double cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 2000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;
    // transient
    std::string time_integrator = "";
    double time_step = 0.01;
    double final_time = 300.0;
    double rusanov_dissipation_scale = 1.0;
};

struct CaseConfig {
    int schema_version = 1;
    std::string case_id;
    std::string description;
    std::string mesh_file;
    std::string physics_mode; // "inviscid" or "laminar"
    double reynolds = 0.0;
    GasProperties gas;
    FreestreamState freestream;
    ReferenceValues reference;
    std::map<std::string, std::string> boundary_conditions;
    RunControl run_control;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = -1.0;
    double vorticity_clip_min = -5.0;
    double vorticity_clip_max = 5.0;
};

struct Face {
    int left_cell = -1;
    int right_cell = -1;  // -1 for boundary
    int node0 = -1, node1 = -1;
    Vec2 normal = {0,0};  // outward normal (area-weighted, magnitude = face length)
    Vec2 center = {0,0};
    double length = 0.0;
    std::string bc_family;
    std::string bc_type;  // "farfield", "slip_wall", "no_slip_adiabatic_wall", "interior"
    bool is_boundary() const { return right_cell < 0; }
};

struct Cell {
    std::vector<int> nodes;
    std::vector<int> faces;
    Vec2 center = {0,0};
    double volume = 0.0;
    int partition = 0;
    bool is_ghost = false;
    int global_id = -1;
};

struct Mesh {
    std::vector<Vec2> nodes;
    std::vector<Cell> cells;
    std::vector<Face> faces;
    int num_nodes_global = 0;
    int num_cells_global = 0;
    int num_faces_global = 0;
};

enum class BCType { INTERIOR, FARFIELD, SLIP_WALL, NO_SLIP_ADIABATIC_WALL };

inline BCType parse_bc_type(const std::string& s) {
    if (s == "farfield") return BCType::FARFIELD;
    if (s == "slip_wall") return BCType::SLIP_WALL;
    if (s == "no_slip_adiabatic_wall") return BCType::NO_SLIP_ADIABATIC_WALL;
    return BCType::INTERIOR;
}

inline double pressure_from_conservative(const Vec4& U, double gamma) {
    double rho = U[0];
    double u = U[1] / rho;
    double v = U[2] / rho;
    double E = U[3] / rho;
    double ke = 0.5 * (u*u + v*v);
    return (gamma - 1.0) * rho * (E - ke);
}

inline double speed_of_sound(double p, double rho, double gamma) {
    return std::sqrt(gamma * std::max(p, 1e-14) / std::max(rho, 1e-14));
}

inline Vec4 primitive_to_conservative(double rho, double u, double v, double p, double gamma) {
    double E = p / ((gamma - 1.0) * rho) + 0.5 * (u*u + v*v);
    return Vec4(rho, rho*u, rho*v, rho*E);
}

inline void conservative_to_primitive(const Vec4& U, double gamma,
                                       double& rho, double& u, double& v, double& p, double& T) {
    rho = U[0];
    u = U[1] / rho;
    v = U[2] / rho;
    double E = U[3] / rho;
    double ke = 0.5 * (u*u + v*v);
    p = (gamma - 1.0) * rho * (E - ke);
    T = p / (rho * 1.0); // R=1
}
