#pragma once
#include "MeshData.hpp"
#include "Config.hpp"
#include "OutputManager.hpp"
#include <mpi.h>
#include <vector>
#include <string>

struct TransientResult {
    bool completed = false;
    double final_time = 0.0;
    int n_steps = 0;
    double mean_inner_iters = 0.0;
    int min_inner = 9999, max_inner = 0;
    int target_misses = 0;
    double last_inner_residual_ratio = 0.0;
};

TransientResult runTransient(LocalMesh& lm,
                              std::vector<StateVec>& states,
                              const CaseConfig& cfg,
                              const std::string& output_dir,
                              MPI_Comm comm);
