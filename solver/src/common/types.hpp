#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <string>
#include <vector>
#include <cstdint>

// ---- Fundamental numeric types ----
using Real = double;
using Int   = int64_t;
using Index = int64_t;

// ---- 2-D vector alias ----
using Vec2   = Eigen::Matrix<Real, 2, 1>;
using Vec2i  = Eigen::Matrix<Int, 2, 1>;
using Mat22  = Eigen::Matrix<Real, 2, 2>;

// ---- Conservative state vector: [rho, rho*u, rho*v, rho*E] ----
static constexpr int NCONS = 4;
using StateVector = Eigen::Matrix<Real, NCONS, 1>;

// ---- Primitive variable vector: [rho, u, v, p] ----
static constexpr int NPRIM = 4;
using PrimVector  = Eigen::Matrix<Real, NPRIM, 1>;

// ---- Spatial gradient of a scalar (2 components) ----
using Grad2 = Vec2;   // d/dx, d/dy

// ---- Cell / Face / Node identifiers ----
using CellID = Index;
using FaceID = Index;
using NodeID = Index;
using RankID = int;

// ---- Boundary condition types ----
enum class BCType : int {
    Farfield              = 0,
    SlipWall              = 1,
    NoSlipAdiabaticWall   = 2,
    Undefined             = -1
};

inline std::string bc_type_to_string(BCType t) {
    switch (t) {
        case BCType::Farfield:              return "farfield";
        case BCType::SlipWall:              return "slip_wall";
        case BCType::NoSlipAdiabaticWall:   return "no_slip_adiabatic_wall";
        default:                            return "undefined";
    }
}

inline BCType bc_type_from_string(const std::string& s) {
    if (s == "farfield")              return BCType::Farfield;
    if (s == "slip_wall")             return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
    return BCType::Undefined;
}

// ---- Face connectivity ----
struct Face {
    CellID left   = -1;  // negative = boundary face
    CellID right  = -1;
    Vec2   centroid;
    Vec2   normal;       // unit normal pointing left->right
    Real   area  = 0.0;
    BCType bc_type = BCType::Undefined;  // boundary type for boundary faces
};

// ---- Boundary face info ----
struct BoundaryFace {
    Index  global_face_id;
    BCType bc_type;
    std::string family_name;  // original CGNS family name
};

// ---- Cell ----
struct Cell {
    Vec2          centroid;
    Real          volume = 0.0;
    std::vector<FaceID> faces;
};

// ---- Node ----
struct Node {
    Vec2 position;
};

// ---- Physics mode ----
enum class PhysicsMode : int {
    Inviscid = 0,
    Laminar  = 1
};

// ---- Run-control type ----
enum class RunType : int {
    Steady    = 0,
    Transient = 1
};

// ---- Case configuration (parsed from JSON) ----
struct CaseConfig {
    std::string case_id;
    std::string mesh_file;
    int         schema_version = 1;

    // Physics
    PhysicsMode physics_mode = PhysicsMode::Inviscid;
    Real        reynolds     = 0.0;
    std::string viscosity_model;

    // Gas
    Real gamma   = 1.4;
    Real R_gas   = 1.0;
    Real prandtl = 0.72;

    // Freestream
    Real mach              = 0.0;
    Real aoa_degrees       = 0.0;
    Real rho_inf           = 1.0;
    Real u_inf             = 0.0;
    Real v_inf             = 0.0;
    Real p_inf             = 0.0;
    Real velocity_magnitude = 1.0;

    // Reference quantities
    Real ref_length       = 1.0;
    Real ref_area         = 1.0;
    Vec2 moment_center    = Vec2::Zero();
    Real ref_reynolds_length = 1.0;

    // BC mapping: mesh family name -> BCType
    std::vector<std::pair<std::string, BCType>> bc_map;

    // Run control
    RunType run_type = RunType::Steady;
    int     max_steps             = 10000;
    Real    residual_reduction_target = 4.0;
    Real    cfl_initial           = 1.0;
    Real    cfl_max               = 100.0;
    int     pseudo_cfl_ramp_steps = 2000;
    int     min_inner_iterations  = 3;
    int     max_inner_iterations  = 50;
    Real    inner_residual_reduction_target = 0.01;
    std::string inner_residual_norm;

    // Transient
    Real    time_step               = 0.01;
    Real    final_time              = 0.0;
    std::string time_integrator;
    std::string bdf2_history_update;
    Real    rusanov_dissipation_scale = 1.0;

    // Numerics required
    int     spatial_order          = 2;
    std::string inviscid_flux_type;
    std::string main_time_method;
    int     transient_order        = 0;

    // Output
    bool    write_final_field     = true;
    bool    write_surface          = true;
    int     write_forces_every     = 1;
    int     write_residuals_every  = 1;
    Real    write_field_every_time = 0.0;
};
