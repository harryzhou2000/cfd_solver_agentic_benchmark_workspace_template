#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include <string>
#include <vector>
#include <mpi.h>
#include <chrono>

struct OutputManager {
    std::string output_dir;
    const CaseConfig& cfg;
    const LocalMesh& lm;
    MPI_Comm comm;
    int rank, n_ranks;
    std::chrono::steady_clock::time_point start_time;

    // Statistics for metadata
    long long total_inner_iters = 0;
    int n_physical_steps = 0;
    int min_inner = 9999, max_inner = 0;
    int inner_target_misses = 0;
    double last_inner_residual_ratio = 0.0;

    OutputManager(const std::string& dir, const CaseConfig& c,
                  const LocalMesh& l, MPI_Comm comm_);

    void writeResidualRow(int step, double phys_time, int inner_iter,
                          double cfl, double dt, const StateVec& res_l2,
                          double res_linf);

    void writeForceRow(int step, double phys_time, const Forces& forces);

    void writeSurface(const std::vector<StateVec>& states,
                      const std::vector<StateGrad>& grads,
                      const std::vector<std::array<GradVec,3>>& prim_grads);

    void writeFieldVTU(const std::vector<StateVec>& states, int step_id = -1);

    void writeMetadata(bool completed, const std::string& conv_status,
                       double residual_reduction, int final_step, double final_time);

    void writeRunStatus(const std::string& command, double wall_time,
                        int final_step, double final_time,
                        const std::string& conv_status, double residual_reduction);

    void writePartitionDiagnostics();

    Forces computeForces(const std::vector<StateVec>& states,
                         const std::vector<StateGrad>& grads,
                         const std::vector<std::array<GradVec,3>>& prim_grads);

    void initFiles();  // Write CSV headers
};
