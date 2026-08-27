#pragma once
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <Eigen/Dense>

using Vec2 = Eigen::Vector2d;
using Vec4 = Eigen::Vector4d;
using Mat4 = Eigen::Matrix4d;

struct GasModel {
    double gamma = 1.4;
    double R = 1.0;
    double Pr = 0.72;
    double cv() const { return R / (gamma - 1.0); }
    double cp() const { return gamma * R / (gamma - 1.0); }
    double pressure(double rho, double e) const { return (gamma - 1.0) * rho * e; }
    double sound_speed(double rho, double p) const { return std::sqrt(gamma * p / rho); }
    double temperature(double rho, double p) const { return p / (rho * R); }
    double internal_energy(double p, double rho) const { return p / ((gamma - 1.0) * rho); }
};

struct Freestream {
    double mach = 0.0;
    double aoa_deg = 0.0;
    double rho = 1.0;
    double vel_mag = 1.0;
    double pressure = 1.0;
    Vec4 state(const GasModel& gas) const {
        double aoa = aoa_deg * M_PI / 180.0;
        double u = vel_mag * std::cos(aoa);
        double v = vel_mag * std::sin(aoa);
        double e = gas.internal_energy(pressure, rho);
        double E = e + 0.5 * (u * u + v * v);
        return Vec4(rho, rho * u, rho * v, rho * E);
    }
};

struct RefValues {
    double length = 1.0;
    double area = 1.0;
    Vec2 moment_center = {0.25, 0.0};
    double reynolds_length = 1.0;
};

enum class BCType { FARFIELD, SLIP_WALL, NO_SLIP_ADIABATIC_WALL };

struct Face {
    int left_cell = -1;
    int right_cell = -1;
    Vec2 midpoint = {0, 0};
    Vec2 normal = {0, 0};
    double area = 0.0;
    int bc_id = -1;
    bool is_boundary = false;
    std::array<int, 2> nodes = {-1, -1};
};

struct Cell {
    std::vector<int> nodes;
    std::vector<int> faces;
    Vec2 centroid = {0, 0};
    double volume = 0.0;
    int global_id = -1;
    bool is_ghost = false;
    int owner_rank = -1;
};

struct BoundaryGroup {
    std::string family_name;
    BCType type;
    std::vector<int> face_ids;
};

struct Mesh {
    std::vector<Vec2> nodes;
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<BoundaryGroup> boundary_groups;
    int num_cells_global = 0;
    int num_faces_global = 0;
    int num_owned = 0;
    int num_ghost = 0;
};

enum class PhysicsMode { INVISCID, LAMINAR };
enum class RunType { STEADY, TRANSIENT };

struct RunControl {
    RunType type = RunType::STEADY;
    int max_steps = 20000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0;
    double cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 2000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    double inner_residual_reduction_target = 0.01;
    double time_step = 0.01;
    double final_time = 300.0;
    double rusanov_dissipation_scale = 1.0;
};

struct CaseConfig {
    std::string case_id;
    std::string mesh_file;
    PhysicsMode physics_mode = PhysicsMode::INVISCID;
    double reynolds = 0.0;
    GasModel gas;
    Freestream freestream;
    RefValues reference;
    RunControl run_control;
    std::map<std::string, BCType> boundary_conditions;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = -1.0;
};
