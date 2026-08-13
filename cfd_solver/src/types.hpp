#pragma once

#include <cstddef>
#include <vector>
#include <string>
#include <array>
#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace cfd {

// Global index types
using idx_t = int64_t;
using real_t = double;

// Vector types
using Vec2 = Eigen::Matrix<real_t, 2, 1>;
using Vec4 = Eigen::Matrix<real_t, 4, 1>;

// Conservative state: [rho, rhou, rhov, rhoE]
using StateVec = Vec4;

// Grid geometry
struct Face {
    idx_t id;
    idx_t left_cell;   // -1 if boundary
    idx_t right_cell;  // -1 if boundary
    Vec2 centroid;
    Vec2 normal;       // outward from left, magnitude = area
    idx_t bc_tag;      // boundary condition tag (0 = interior)
};

struct Cell {
    idx_t id;
    Vec2 centroid;
    real_t volume;
    std::vector<idx_t> face_ids;
    std::vector<idx_t> neighbor_ids;
};

struct Mesh {
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<idx_t> boundary_face_ids;
    std::map<std::string, std::vector<idx_t>> boundary_families;
    std::map<std::string, idx_t> bc_tag_map;
    idx_t n_cells;
    idx_t n_faces;
    idx_t n_boundary_faces;
};

// Partition info
struct PartitionInfo {
    std::vector<idx_t> cell_part;     // global cell -> partition
    std::vector<idx_t> part_ncells;   // number of cells per partition
    idx_t edge_cut;
};

// Case input
struct CaseInput {
    std::string case_id;
    std::string mesh_file;
    std::string physics_mode; // "inviscid" or "laminar"
    real_t reynolds;
    real_t mach;
    real_t aoa;
    real_t rho_inf;
    real_t u_inf;
    real_t v_inf;
    real_t p_inf;
    real_t gamma;
    real_t R;
    real_t prandtl;
    real_t ref_length;
    real_t ref_area;
    Vec2 moment_center;
    std::map<std::string, std::string> boundary_conditions;
    
    // Run control
    std::string run_type; // "steady" or "transient"
    idx_t max_steps;
    real_t residual_reduction_target;
    real_t cfl_initial;
    real_t cfl_max;
    idx_t pseudo_cfl_ramp_steps;
    idx_t min_inner_iterations;
    idx_t max_inner_iterations;
    real_t inner_residual_reduction_target;
    
    // Transient
    std::string time_integrator;
    real_t time_step;
    real_t final_time;
    real_t rusanov_dissipation_scale;

    // Derived
    real_t T_inf;
    real_t e_inf;
    real_t a_inf;
    real_t rhoE_inf;
    StateVec freestream_state;
};

// BC types
enum class BCType : int {
    Interior = 0,
    Farfield = 1,
    SlipWall = 2,
    NoSlipAdiabaticWall = 3
};

inline BCType bc_from_string(const std::string& s) {
    if (s == "farfield") return BCType::Farfield;
    if (s == "slip_wall") return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
    return BCType::Interior;
}

inline std::string bc_to_string(BCType bc) {
    switch (bc) {
        case BCType::Farfield: return "farfield";
        case BCType::SlipWall: return "slip_wall";
        case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
        default: return "interior";
    }
}

} // namespace cfd
