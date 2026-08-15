#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstddef>

namespace cfd {

// --- Boundary condition types ---
enum class BcType {
    Farfield,
    SlipWall,
    NoSlipAdiabaticWall,
    Unsupported
};

BcType bc_type_from_string(const std::string& s);

// --- Mesh entity counts ---
struct MeshStats {
    Int n_vertices{0};
    Int n_cells{0};
    Int n_faces{0};
    Int n_boundary_faces{0};
};

// --- Cell data ---
struct Cell {
    std::vector<Int> face_ids;    // indices into global face list (filled by finalize())
    std::vector<Int> vertex_ids;  // indices into global vertex list (kept for
                                  // VTK output, gradients, boundary geometry)
    Vec2 centroid{0, 0};
    Real volume{0};
};

// --- Face data ---
struct Face {
    Int left_cell{0};   // index of cell on left side of normal
    Int right_cell{0};  // index of cell on right side (or INVALID_INDEX sentinel for boundary)
    Vec2 centroid{0, 0};
    FaceNormal normal{0,0,0};  // normal pointing left->right
    bool is_boundary{false};
    Int bc_tag{INVALID_INDEX};  // index into boundary_conditions vector (INVALID_INDEX sentinel)
};

// --- Boundary condition per family ---
struct BoundaryPatch {
    std::string family_name;
    BcType bc_type;
    std::vector<Int> face_ids;  // global face indices for this patch
};

// --- Full unstructured mesh (serial, pre-partition) ---
struct Mesh {
    MeshStats stats;
    std::vector<Vec2> vertices;
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<BoundaryPatch> boundary_patches;
    std::vector<std::vector<Int>> cell_neighbors;  // cell-cell adjacency (face neighbors)

    // Derived: boundary face indices (flat list for convenience)
    std::vector<Int> boundary_face_ids;

    // Guards finalize() against double execution (it overwrites cell.face_ids
    // with face indices, so it must run exactly once after loading).
    bool finalized{false};

    // Build adjacency and boundary face list (call after loading)
    void finalize();
};

// --- Solver configuration (from JSON case file) ---
struct GasConfig {
    Real gamma{1.4};
    Real R{1.0};
    Real prandtl{0.72};
};

struct FreestreamConfig {
    Real mach{0.1};
    Real aoa_deg{0.0};
    Real rho{1.0};
    Real u{1.0};
    Real v{0.0};
    Real pressure{1.0};
    Real temperature{1.0};
};

struct ReferenceConfig {
    Real length{1.0};
    Real area{1.0};
    Vec2 moment_center{0,0};
    Real reynolds_length{1.0};
};

struct BcMapping {
    std::string family_name;
    BcType bc_type;
};
using BcMappings = std::vector<BcMapping>;

struct RunControl {
    std::string type;           // "steady" or "transient"
    Int max_steps{0};
    Real residual_reduction_target{0};
    Real cfl_initial{1.0};
    Real cfl_max{100.0};
    Int pseudo_cfl_ramp_steps{0};
    Int min_inner_iterations{3};
    Int max_inner_iterations{50};
    Real inner_residual_reduction_target{0.01};
    // Transient-only
    std::string time_integrator;
    Real time_step{0.01};
    Real final_time{300.0};
    std::string inner_residual_norm;
    std::string bdf2_history_update;
    Real rusanov_dissipation_scale{1.0};
};

struct OutputConfig {
    bool write_final_field{true};
    bool write_surface{true};
    Int write_forces_every{1};
    Int write_residuals_every{1};
    Real write_field_every_time{1.0};
    std::string wake_visualization;
    Vec2 recommended_vorticity_clip_range{-5,5};
};

struct CaseConfig {
    Int schema_version{1};
    std::string case_id;
    std::string description;
    std::string mesh_file;
    std::string physics_mode;   // "inviscid" or "laminar"
    Real reynolds_number{0};
    std::string viscosity_model;
    GasConfig gas;
    FreestreamConfig freestream;
    ReferenceConfig reference;
    BcMappings bc_mappings;
    RunControl run_control;
    OutputConfig output;
};

// Parse JSON case file into CaseConfig
CaseConfig parse_case_config(const std::string& json_path);

} // namespace cfd
