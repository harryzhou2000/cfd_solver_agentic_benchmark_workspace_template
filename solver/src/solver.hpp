#pragma once

#include "common.hpp"
#include "mesh_local.hpp"
#include "case_io.hpp"
#include "physics.hpp"
#include <fstream>
#include <mpi.h>
#include <string>
#include <vector>

namespace cfd {

struct SolverState {
    int step = 0;
    double physical_time = 0.0;
    double cfl = 1.0;
    double dt = 0.0;
    Vec4 residual_l2{0,0,0,0};
    Vec4 residual_linf{0,0,0,0};
    double cl = 0.0, cd = 0.0, cmz = 0.0;
    double pd = 0.0, vd = 0.0, pl = 0.0, vl = 0.0;
};

struct InnerIterStats {
    int64_t total_inner = 0;
    int64_t min_inner = 1000000000;
    int64_t max_inner = 0;
    int64_t target_misses = 0;
    int64_t converged_steps = 0;
    int64_t total_steps = 0;
    double last_ratio = 1.0;
};

struct SolverResults {
    std::vector<SolverState> history;
    InnerIterStats inner_stats;
    double wall_time_seconds = 0.0;
    std::string convergence_status = "failed";
    double residual_reduction_orders = 0.0;
    int final_step = 0;
    double final_physical_time = 0.0;
    std::vector<Vec4> final_U;       // owned + ghost final states
    std::vector<Vec4> final_U_ghost; // ghost-only copy for output helpers
};

SolverResults run_solver(LocalMesh& mesh, const CaseConfig& cfg, int rank, int n_ranks,
                         MPI_Comm comm, double start_wall);

void write_metadata(const std::string& dir, const LocalMesh& mesh, const CaseConfig& cfg,
                    const SolverResults& res, int rank, int n_ranks, MPI_Comm comm);
void write_partition_diagnostics(const std::string& dir, const LocalMesh& mesh, int rank,
                                 int n_ranks, MPI_Comm comm);
void write_run_status(const std::string& dir, const CaseConfig& cfg, const SolverResults& res,
                      int rank, int n_ranks, const std::string& cmd_line);
void write_field_vtu(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                     int rank, int n_ranks, MPI_Comm comm, const std::string& tag);
void write_restart(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                   int step, double time, int rank, int n_ranks, MPI_Comm comm);
bool try_read_restart(const std::string& path, const LocalMesh& mesh, std::vector<Vec4>& U,
                      int& step, double& time, int rank, MPI_Comm comm);
void write_surface_csv(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                       const Vec4* U_ghost, const GasModel& gas, int rank, int n_ranks,
                       MPI_Comm comm, const CaseConfig& cfg, double q_inf);
void write_residuals_csv(const std::string& dir, const SolverResults& res);
void write_forces_csv(const std::string& dir, const SolverResults& res);

}  // namespace cfd
