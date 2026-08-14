#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <array>
#include <map>
#include <Eigen/Dense>

namespace cfd {

using idx_t = int64_t;
using real_t = double;
using Vec2 = Eigen::Matrix<real_t, 2, 1>;
using Vec4 = Eigen::Matrix<real_t, 4, 1>;
using StateVec = Vec4;
using Mat4 = Eigen::Matrix<real_t, 4, 4>;

struct Face {
    idx_t id;
    idx_t left_cell;
    idx_t right_cell;
    Vec2 centroid;
    Vec2 normal;
    idx_t bc_tag;
};

struct Cell {
    idx_t id;
    Vec2 centroid;
    real_t volume;
    std::vector<idx_t> face_ids;
    std::vector<idx_t> neighbor_ids;
    std::vector<idx_t> node_ids;
};

struct Mesh {
    std::vector<Cell> cells;
    std::vector<Face> faces;
    std::vector<idx_t> boundary_face_ids;
    std::map<std::string, std::vector<idx_t>> boundary_families;
    std::map<std::string, idx_t> bc_tag_map;
    idx_t n_cells, n_faces, n_boundary_faces;
    std::vector<Vec2> nodes;
};

struct PartitionInfo {
    std::vector<idx_t> cell_part;
    std::vector<idx_t> part_ncells;
    idx_t edge_cut;
};

struct CaseInput {
    std::string case_id, mesh_file, physics_mode;
    real_t reynolds, mach, aoa, rho_inf, u_inf, v_inf, p_inf;
    real_t gamma, R, prandtl, ref_length, ref_area;
    real_t mu_ref, cp;
    Vec2 moment_center;
    std::map<std::string, std::string> boundary_conditions;
    std::string run_type, time_integrator;
    idx_t max_steps, pseudo_cfl_ramp_steps;
    idx_t min_inner_iterations, max_inner_iterations;
    real_t residual_reduction_target, cfl_initial, cfl_max;
    real_t inner_residual_reduction_target;
    real_t time_step, final_time, rusanov_dissipation_scale;
    real_t T_inf, e_inf, a_inf, rhoE_inf;
    StateVec freestream_state;
    idx_t write_forces_every, write_residuals_every;
};

// Cell-centered gradient of the primitive set (rho, u, v, T).
struct PrimGrad {
    Vec2 grho, gu, gv, gT;
};

struct ForceCoeffs {
    real_t cl = 0, cd = 0, cmz = 0;
    real_t pressure_drag = 0, viscous_drag = 0;
    real_t pressure_lift = 0, viscous_lift = 0;
};

struct ResNorm {
    real_t l2 = 0, linf = 0;
    real_t rho = 0, rhou = 0, rhov = 0, rhoE = 0;
};

// Runtime workspace shared by the steady and transient drivers.
struct SolverStats {
    idx_t steps_run = 0;
    real_t final_res_l2 = 0, final_res_linf = 0;
    real_t residual_reduction_orders = 0;
    std::string convergence_status = "failed";
    // inner-iteration statistics
    idx_t total_inner_iterations = 0;
    idx_t observed_min_inner = 0, observed_max_inner = 0;
    idx_t inner_target_misses = 0, inner_steps_counted = 0;
    real_t inner_target_converged_fraction = 0;
    real_t last_inner_residual_ratio = 0;
};

enum class BCType : int { Interior = 0, Farfield = 1, SlipWall = 2, NoSlipAdiabaticWall = 3 };

inline BCType bc_from_string(const std::string& s) {
    if (s == "farfield") return BCType::Farfield;
    if (s == "slip_wall") return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabaticWall;
    return BCType::Interior;
}

struct LocalMesh {
    std::vector<Cell> owned_cells, ghost_cells;
    std::vector<Face> faces;
    std::vector<idx_t> owned_to_global, ghost_to_global, global_to_local;
    struct NeighborInfo {
        idx_t rank;
        std::vector<idx_t> send_cells, recv_cells, send_global, recv_global;
    };
    std::vector<NeighborInfo> neighbors;
    idx_t n_owned, n_ghost, n_total, n_boundary_faces;
};

struct SteadyStats {
    idx_t steps_run;
    real_t final_res_l2, final_res_linf, residual_reduction;
    std::string convergence_status, notes;
    idx_t total_inner_iterations = 0;
    idx_t observed_min_inner = 0, observed_max_inner = 0;
    idx_t inner_target_misses = 0, inner_steps_counted = 0;
    real_t inner_target_converged_fraction = 0;
    real_t last_inner_residual_ratio = 0;
};

struct TransientStats {
    idx_t physical_steps_run, total_inner_iterations;
    idx_t observed_min_inner, observed_max_inner, inner_target_misses;
    real_t inner_target_converged_fraction, last_inner_residual_ratio;
    real_t final_res_l2, final_res_linf;
    std::string convergence_status;
};

} // namespace cfd
