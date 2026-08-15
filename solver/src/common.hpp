#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Eigen/Dense>

namespace cfd {

using Index = int64_t;
using Real = double;
using Vec2 = Eigen::Matrix<Real, 2, 1>;
using State = Eigen::Matrix<Real, 4, 1>;
using Mat4 = Eigen::Matrix<Real, 4, 4>;

// ---------------------------------------------------------------------------
// Boundary condition identifiers
// ---------------------------------------------------------------------------
enum class BcKind : int {
    Interior = 0,
    Farfield = 1,
    SlipWall = 2,
    NoSlipWall = 3,
};

inline BcKind bc_from_name(const std::string& name) {
    if (name == "farfield") return BcKind::Farfield;
    if (name == "slip_wall") return BcKind::SlipWall;
    if (name == "no_slip_adiabatic_wall") return BcKind::NoSlipWall;
    return BcKind::Interior;
}

// ---------------------------------------------------------------------------
// Case input (parsed from the benchmark JSON files)
// ---------------------------------------------------------------------------
struct CaseInput {
    std::string case_id;
    std::string mesh_file;
    std::string physics_mode;   // "inviscid" | "laminar"
    Real reynolds = 0.0;

    Real gamma = 1.4;
    Real R = 1.0;
    Real prandtl = 0.72;
    Real cp = 0.0;

    Real mach = 0.0;
    Real aoa = 0.0;             // radians
    Real rho_inf = 1.0;
    Real u_inf = 1.0;
    Real v_inf = 0.0;
    Real p_inf = 1.0;
    Real T_inf = 1.0;
    Real a_inf = 1.0;
    Real mu_ref = 0.0;          // constant viscosity matching the case Re
    State freestream;

    Real ref_length = 1.0;
    Real ref_area = 1.0;
    Vec2 moment_center = Vec2::Zero();

    std::map<std::string, std::string> boundary_conditions;

    std::string run_type = "steady";   // "steady" | "transient"
    std::string time_integrator = "bdf2_or_trapezoidal";
    Index max_steps = 0;
    Index pseudo_cfl_ramp_steps = 0;
    Index min_inner_iterations = 3;
    Index max_inner_iterations = 50;
    Real residual_reduction_target = 4.0;
    Real cfl_initial = 1.0;
    Real cfl_max = 100.0;
    Real inner_residual_reduction_target = 0.01;
    Real time_step = 0.0;
    Real final_time = 0.0;
    Real rusanov_dissipation_scale = 1.0;

    Index write_forces_every = 1;
    Index write_residuals_every = 1;
};

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
struct Face {
    Index id = -1;
    Index left = -1;    // local cell index (owned or ghost)
    Index right = -1;   // -1 for boundary faces
    Vec2 centroid;
    Vec2 normal;        // |normal| = edge length
    Index bc_tag = 0;   // 0 = interior
};

struct Cell {
    Index id = -1;
    Vec2 centroid;
    Real volume = 0.0;
    std::vector<Index> faces;
    std::vector<Index> neighbors;
    std::vector<Index> nodes;
};

struct GlobalMesh {
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<Vec2> nodes;
    std::map<std::string, std::vector<Index>> boundary_families;
    std::map<std::string, Index> tag_of_family;
    Index n_cells = 0;
    Index n_faces = 0;
    Index n_boundary_faces = 0;
};

// ---------------------------------------------------------------------------
// Partitioning
// ---------------------------------------------------------------------------
struct Partition {
    std::vector<Index> cell_part;      // global cell -> rank
    std::vector<Index> part_ncells;
    Index edge_cut = 0;
};

// Rank-local mesh: owned cells + ghost cells needed for the stencil.
struct LocalMesh {
    std::vector<Cell> owned;
    std::vector<Cell> ghost;
    std::vector<Face> faces;
    std::vector<Index> owned_to_global;
    std::vector<Index> ghost_to_global;
    std::vector<Index> global_to_local; // -1 for cells not present
    Index n_owned = 0;
    Index n_ghost = 0;
    Index n_total = 0;
    Index n_boundary_faces = 0;

    struct Neighbor {
        Index rank = -1;
        std::vector<Index> send_cells;  // local owned indices to send
        std::vector<Index> recv_cells;  // local (owned+ghost) indices to fill
        std::vector<Index> send_global;
        std::vector<Index> recv_global;
    };
    std::vector<Neighbor> neighbors;
};

// Primitive variables at a point.
struct Prims {
    Real rho = 0.0, u = 0.0, v = 0.0, p = 0.0, T = 0.0, a = 0.0, e = 0.0;
};

// Cell-averaged primitive gradients (rho, u, v, T).
struct PrimGrad {
    Vec2 grho = Vec2::Zero();
    Vec2 gu = Vec2::Zero();
    Vec2 gv = Vec2::Zero();
    Vec2 gT = Vec2::Zero();
};

struct ForceCoeffs {
    Real cl = 0.0, cd = 0.0, cmz = 0.0;
    Real pressure_drag = 0.0, viscous_drag = 0.0;
    Real pressure_lift = 0.0, viscous_lift = 0.0;
};

struct ResNorm {
    Real l2 = 0.0, linf = 0.0;
    Real rho = 0.0, rhou = 0.0, rhov = 0.0, rhoE = 0.0;
};

// Inner-iteration statistics used for the transient metadata contract.
struct InnerStats {
    Index total_inner = 0;
    Index observed_min = 0;
    Index observed_max = 0;
    Index target_misses = 0;
    Index steps_counted = 0;
    Real converged_fraction = 0.0;
    Real last_ratio = 0.0;
};

inline Prims prims_from_state(const State& U, Real gamma, Real R) {
    Prims q;
    q.rho = U[0];
    q.u = U[1] / U[0];
    q.v = U[2] / U[0];
    const Real ke = 0.5 * (q.u * q.u + q.v * q.v);
    q.p = (gamma - 1.0) * (U[3] - U[0] * ke);
    if (q.p < 0.0) q.p = 0.0;
    q.T = q.p / (q.rho * R);
    q.a = std::sqrt(gamma * q.p / q.rho);
    q.e = q.p / ((gamma - 1.0) * q.rho);
    return q;
}

inline State state_from_prims(const Prims& q, Real gamma) {
    State U;
    const Real ke = 0.5 * (q.u * q.u + q.v * q.v);
    U << q.rho, q.rho * q.u, q.rho * q.v, q.rho * (q.e + ke);
    return U;
}

} // namespace cfd
