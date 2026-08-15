#pragma once

#include <Eigen/Dense>
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

namespace cfd {

// ============================================================================
// Basic types
// ============================================================================

/// Real floating-point type (configurable precision)
using Real = double;

/// 2-D vector using Eigen for performance
using Vec2 = Eigen::Matrix<Real, 2, 1>;

/// 4-component conservative state vector: [rho, rho*u, rho*v, rho*E]
using Vec4 = Eigen::Matrix<Real, 4, 1>;

/// 4x4 matrix
using Mat4 = Eigen::Matrix<Real, 4, 4>;

/// 2x2 matrix (for velocity gradients, etc.)
using Mat2 = Eigen::Matrix<Real, 2, 2>;

// ============================================================================
// Enumerations
// ============================================================================

enum class PhysicsMode {
    Inviscid,
    Laminar
};

enum class BoundaryType {
    Farfield,
    SlipWall,
    NoSlipAdiabaticWall,
    Unknown
};

enum class RunType {
    Steady,
    Transient
};

enum class TimeIntegrator {
    BDF2,
    Trapezoidal,
    None
};

enum class FluxScheme {
    Rusanov,
    Roe
};

// ============================================================================
// Mesh data structures
// ============================================================================

/// A single mesh node (vertex)
struct Node {
    Vec2 coord;       ///< (x, y) position
};

/// A face (edge in 2D) separating two cells or a cell and a boundary
struct Face {
    std::array<std::size_t, 2> nodes;  ///< node indices defining the face

    Vec2 centroid;     ///< face midpoint
    Vec2 normal;       ///< face unit normal (pointing from left to right cell)
    Real area;         ///< face length

    std::size_t left_cell;   ///< index of left cell (or INVALID if boundary)
    std::size_t right_cell;  ///< index of right cell (or INVALID if boundary)
    bool is_boundary;        ///< true if face is on a domain boundary

    int bc_tag;        ///< boundary condition tag if is_boundary (from mesh/case mapping)
    std::string bc_family;  ///< boundary family name from CGNS

    static constexpr std::size_t INVALID = static_cast<std::size_t>(-1);
};

/// A cell-centered finite-volume cell
struct Cell {
    Vec2 centroid{Vec2::Zero()};  ///< cell centroid (x, y)
    Real volume{0.0};             ///< cell area

    std::vector<std::size_t> nodes;  ///< node indices defining the cell (in order)
    std::vector<std::size_t> faces;  ///< face indices
    std::vector<std::size_t> neighbors;  ///< neighboring cell indices
    std::size_t global_id{0};  ///< global cell index (index into the original full Mesh::cells)

    Vec4 U{Vec4::Zero()};   ///< conservative state vector [rho, rho*u, rho*v, rho*E]
    Vec4 dU{Vec4::Zero()};  ///< residual (for updating)
    Real dt_local{0.0};     ///< local time step

    // Partition metadata
    bool is_ghost{false};   ///< true if this cell is a ghost (owned by another rank)
    int owner_rank{0};      ///< MPI rank that owns this cell (meaningful for ghost cells)
};

/// The unstructured mesh
struct Mesh {
    std::vector<Node> nodes;
    std::vector<Cell> cells;
    std::vector<Face> faces;

    // Boundary face groups for easy force/surface extraction
    std::unordered_map<int, std::vector<std::size_t>> boundary_faces;  ///< bc_tag -> face indices

    // CGNS zone info
    std::string zone_name;
    int cell_dimension;  ///< should be 2

    // Computed geometry
    Vec2 min_coord, max_coord;

    // Partition metadata (meaningful for local sub-meshes)
    std::size_t n_owned_cells{0};  ///< first n_owned_cells in cells[] are owned; rest are ghosts

    /// Total number of cells, faces, nodes
    std::size_t n_cells()   const { return cells.size(); }
    std::size_t n_faces()   const { return faces.size(); }
    std::size_t n_nodes()   const { return nodes.size(); }

    /// Number of owned cells (first chunk of the cells vector).
    std::size_t n_owned()   const { return n_owned_cells; }
};

// ============================================================================
// Case configuration (parsed from JSON)
// ============================================================================

struct ReferenceConfig {
    Real length;         ///< reference length
    Real area;           ///< reference area
    Vec2 moment_center;  ///< moment reference point
    Real reynolds_length; ///< length scale for Reynolds number
};

struct PhysicsConfig {
    PhysicsMode mode;        ///< inviscid or laminar
    Real reynolds;           ///< Reynolds number (N/A for inviscid)
    std::string viscosity_model;  ///< "constant" or "sutherland"
};

struct GasConfig {
    Real gamma;
    Real R;
    Real prandtl;
};

struct FreestreamConfig {
    Real mach;
    Real aoa_degrees;
    Real rho;
    Real velocity_magnitude;
    Real pressure;

    // Computed from above
    Real aoa_radians;
    Vec2 velocity;  ///< (u_inf, v_inf)
    Real temperature;
    Real speed_of_sound;  ///< a = sqrt(gamma * p / rho)
    Real viscosity;  ///< computed from Re or supplied
};

struct RunControl {
    RunType type;
    int max_steps;
    Real residual_reduction_target;

    // Steady
    Real cfl_initial;
    Real cfl_max;
    int pseudo_cfl_ramp_steps;
    int min_inner_iterations;
    int max_inner_iterations;
    Real inner_residual_reduction_target;

    // Transient
    Real time_step;
    Real final_time;
    TimeIntegrator time_integrator;
    std::string inner_residual_norm;
    Real rusanov_dissipation_scale;
};

struct OutputConfig {
    bool write_final_field;
    bool write_surface;
    int write_forces_every;
    int write_residuals_every;
    Real write_field_every_time;
    std::string wake_visualization;
    std::vector<Real> recommended_vorticity_clip_range;
};

struct NumericsRequired {
    int spatial_order;
    std::string inviscid_flux;
    std::string viscous_flux;
    std::string main_time_method;
    std::string implicit_solver;
    int transient_order;
};

struct CaseConfig {
    int schema_version;
    std::string case_id;
    std::string description;

    struct {
        std::string file;
        std::string format;
        int dimension;
    } mesh;

    PhysicsConfig physics;
    GasConfig gas;
    FreestreamConfig freestream;
    ReferenceConfig reference;
    std::unordered_map<std::string, std::string> boundary_conditions;
    NumericsRequired numerics_required;
    RunControl run_control;
    OutputConfig outputs;
};

// ============================================================================
// Constants
// ============================================================================

constexpr Real SMALL = 1e-12;
constexpr Real EPS = 1e-10;

/// Convert a boundary-condition type string (as used in case-file
/// `boundary_conditions` maps and CGNS family names) to a BoundaryType.
inline BoundaryType boundary_type_from_string(const std::string& name) {
    if (name == "farfield")              return BoundaryType::Farfield;
    if (name == "slip_wall")             return BoundaryType::SlipWall;
    if (name == "no_slip_adiabatic_wall") return BoundaryType::NoSlipAdiabaticWall;
    return BoundaryType::Unknown;
}

/// Integer tag used in `Face::bc_tag` / `Mesh::boundary_faces` keys.
inline int boundary_type_to_tag(BoundaryType type) {
    return static_cast<int>(type);
}

} // namespace cfd
