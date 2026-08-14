#pragma once
#include <Eigen/Dense>
#include <vector>
#include <string>
#include <map>
#include <cstddef>

namespace omo {

// ---- Core types ----
using Real = double;
using Vector2 = Eigen::Matrix<Real, 2, 1>;
using Vector4 = Eigen::Matrix<Real, 4, 1>;
using Matrix22 = Eigen::Matrix<Real, 2, 2>;

// ---- Conservative state: [rho, rho*u, rho*v, rho*E] ----
struct CellData {
    Vector4 U;         // conservative state
    Vector4 dU;        // update/residual
    Vector4 R;         // residual
    Real vol;          // cell volume
    Real dt_local;     // local time step
    Vector4 grad_rhoU[3]; // gradients for rho, rhou, rhov, rhoE for reconstruction
};

struct FaceData {
    int left_cell;   // -1 if boundary face
    int right_cell;  // -1 if boundary face
    Vector2 centroid;
    Vector2 normal;  // outward normal (from left to right, or out of domain for boundary)
    Real area;       // face length (2-D area)
    int bc_tag;      // boundary condition index, -1 if interior
};

struct NodeData {
    Vector2 x;
};

// ---- Mesh data ----
struct MeshData {
    // Global mesh (pre-partition, rank 0 only during preprocessing)
    std::vector<NodeData> nodes;
    std::vector<CellData> cells;     // all cells
    std::vector<FaceData> faces;     // all faces
    std::vector<std::vector<int>> cell_faces;  // faces per cell
    std::vector<std::vector<int>> cell_neighbors; // cell neighbors via faces

    // Boundary info
    std::map<std::string, std::vector<int>> boundary_faces; // tag name -> face indices
    std::vector<std::string> face_bc_names;  // per-face BC name
    int n_boundary_faces_total = 0;

    // Mesh dimensions
    int n_cells_global = 0;
    int n_faces_global = 0;
    int n_nodes_global = 0;
};

// ---- Rank-local mesh ----
struct LocalMeshData {
    int rank;
    int n_cells_owned;    // owned cells
    int n_cells_ghost;    // ghost cells (total cells = owned + ghost)
    int n_cells_total;
    int n_faces_local;    // all faces touching owned cells

    std::vector<CellData> cells;  // [0..owned-1] = owned, [owned..total-1] = ghost
    std::vector<FaceData> faces;  // all faces touching owned cells
    std::vector<std::vector<int>> cell_faces;   // index into local faces

    // Partition info
    std::vector<int> cell_partition;  // original global cell index for each local cell
    std::vector<int> ghost_owner;     // rank that owns each ghost cell
    std::vector<std::vector<int>> send_cells;  // per-neighbor send cell indices
    std::vector<std::vector<int>> recv_cells;  // per-neighbor recv cell indices
    std::vector<int> neighbor_ranks;
    int edge_cut = 0;

    // Boundary faces (local indices)
    struct BCFaceGroup {
        std::string name;
        std::vector<int> face_indices;  // local face indices
        std::string bc_type;            // farfield | slip_wall | no_slip_adiabatic_wall
    };
    std::vector<BCFaceGroup> bc_groups;
};

// ---- Gas model ----
struct GasParams {
    Real gamma = 1.4;
    Real R = 1.0;
    Real Pr = 0.72;

    Real p_from_e(Real rho, Real e) const { return (gamma - 1.0) * rho * e; }
    Real e_from_p(Real rho, Real p) const { return p / ((gamma - 1.0) * rho); }
    Real T_from_p_rho(Real p, Real rho) const { return p / (rho * R); }
    Real a_from_p_rho(Real p, Real rho) const { return std::sqrt(gamma * p / rho); }

    Vector4 prim_to_cons(Real rho, Real u, Real v, Real p) const {
        Real e_int = e_from_p(rho, p);
        Real E = e_int + 0.5 * (u*u + v*v);
        return {rho, rho*u, rho*v, rho*E};
    }

    void cons_to_prim(const Vector4& U, Real& rho, Real& u, Real& v, Real& p) const {
        rho = U(0);
        Real inv_rho = 1.0 / rho;
        u = U(1) * inv_rho;
        v = U(2) * inv_rho;
        Real E = U(3) * inv_rho;
        Real ke = 0.5 * (u*u + v*v);
        Real e_int = E - ke;
        p = (gamma - 1.0) * rho * e_int;
    }
};

// ---- Freestream ----
struct FreestreamParams {
    Real mach;
    Real aoa_deg;
    Real rho;
    Real u_inf, v_inf;
    Real p_inf;
    Real a_inf;
    Real T_inf;
};

// ---- Run control ----
struct RunControl {
    enum Type { STEADY, TRANSIENT };
    Type type = STEADY;
    int max_steps = 20000;
    Real residual_reduction_target = 4.0;
    Real cfl_initial = 1.0;
    Real cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 2000;
    int min_inner_iterations = 3;
    int max_inner_iterations = 50;
    Real inner_residual_reduction_target = 0.01;

    // Transient
    std::string time_integrator;
    Real time_step = 0.01;
    Real final_time = 300.0;
    std::string inner_residual_norm;
    std::string bdf2_history_update;
    Real rusanov_dissipation_scale = 1.0;
};

// ---- Output settings ----
struct OutputSettings {
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    Real write_field_every_time = -1; // -1 = final only
    std::string wake_visualization;
    std::pair<Real, Real> recommended_vorticity_clip = {-5.0, 5.0};
};

// ---- Case configuration ----
struct CaseConfig {
    std::string case_id;
    std::string description;
    std::string mesh_file;
    std::string mesh_format;
    int mesh_dimension = 2;

    std::string physics_mode; // inviscid | laminar
    Real reynolds = -1;
    std::string viscosity_model;

    GasParams gas;
    FreestreamParams freestream;

    struct Reference {
        Real length = 1.0;
        Real area = 1.0;
        Vector2 moment_center{0.25, 0.0};
        Real reynolds_length = 1.0;
    } reference;

    std::map<std::string, std::string> boundary_conditions;

    RunControl run_control;
    OutputSettings outputs;
};

} // namespace omo
