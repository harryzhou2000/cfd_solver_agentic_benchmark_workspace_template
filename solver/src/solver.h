#pragma once
#include "types.h"
#include "cgns_reader.h"
#include <mpi.h>
#include <metis.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <cstring>

using json = nlohmann::json;

struct PartitionInfo {
    int rank;
    int num_owned;
    int num_ghost;
    int num_boundary_faces;
    int num_neighbor_ranks;
    std::vector<int> neighbor_ranks;
    std::vector<int> send_cells;
    std::vector<int> recv_cells;
};

struct HaloExchange {
    struct NeighborComm {
        int rank;
        std::vector<int> send_local_ids;
        std::vector<int> recv_local_ids;
    };
    std::vector<NeighborComm> neighbors;
};

struct ForceRecord {
    int step;
    double physical_time;
    double cl, cd, cmz;
    double pressure_drag, viscous_drag;
    double pressure_lift, viscous_lift;
};

struct ResidualRecord {
    int step;
    double physical_time;
    int inner_iter;
    double cfl, dt;
    double rho_res, rhou_res, rhov_res, rhoE_res;
    double l2_res, linf_res;
};

struct SurfacePoint {
    double x, y, nx, ny;
    double pressure, cp, cf;
    double rho, u, v, mach;
    std::string tag;
};

class CFDSolver {
public:
    CaseConfig config;
    int mpi_rank = 0, mpi_size = 1;

    // Global mesh (rank 0 only for preprocessing)
    Mesh global_mesh;

    // Local (partitioned) mesh
    Mesh local_mesh;
    int num_owned = 0;
    int num_ghost = 0;

    // Partitioning
    std::vector<int> cell_partition; // global cell -> partition
    std::vector<int> global_to_local; // global cell id -> local id
    std::vector<int> local_to_global; // local cell id -> global id
    HaloExchange halo;
    PartitionInfo part_info;
    int partition_edge_cut = 0;

    // Solution
    std::vector<Vec4> U;      // conservative state per local cell
    std::vector<Vec4> U_old;  // for BDF2
    std::vector<Vec4> U_old2; // for BDF2
    std::vector<Vec4> residual;
    std::vector<Eigen::Matrix<double,4,2>> gradients; // per-cell gradients [4 vars x 2 dims]

    // Force/residual history
    std::vector<ForceRecord> force_history;
    std::vector<ResidualRecord> residual_history;
    std::vector<SurfacePoint> surface_data;

    // Timing
    std::chrono::steady_clock::time_point start_time;
    double wall_time_seconds = 0.0;
    int final_step = 0;
    double final_physical_time = 0.0;
    std::string convergence_status = "failed";
    std::string output_dir;

    // CLI overrides (-1 means use config value)
    int cli_max_inner = -1;
    double cli_dt = -1.0;
    double cli_cfl = -1.0;

    // Inner iteration stats
    int total_inner_iters = 0;
    int total_steps_counted = 0;
    int observed_min_inner = 1000000;
    int observed_max_inner = 0;
    int inner_target_misses = 0;
    int inner_target_converged = 0;
    double last_inner_residual_ratio = 1.0;

    void load_case(const std::string& case_file);
    void build_mesh_from_cgns(const std::string& mesh_path);
    void compute_geometry();
    void partition_mesh();
    void build_local_mesh();
    void initialize_solution();
    void solve_steady();
    void solve_transient();
    void compute_residual();
    void compute_gradients();
    void apply_limiter(std::vector<Eigen::Matrix<double,4,2>>& grad);
    Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal);
    Vec4 viscous_flux(int face_id);
    Vec4 boundary_state(int face_id, const Vec4& Uinterior);
    void halo_exchange_state();
    void lusgs_sweep(double dt_pseudo, const std::vector<Vec4>& rhs, bool forward);
    void compute_forces(int step, double time);
    void compute_surface_data();
    void write_outputs(const std::string& output_dir);
    void write_field_vtu(const std::string& filename);
    void write_restart(const std::string& filename);

    Vec4 freestream_state() const;
    double compute_viscosity() const;
    double compute_cfl(int step) const;

    // LU-SGS storage
    std::vector<Vec4> dU;
    std::vector<double> cell_spectral_radius;
    std::vector<double> face_smax; // per-face spectral radius (smax * S_f)
    // Limiter phi per cell
    std::vector<Vec4> limiter_phi;
};
