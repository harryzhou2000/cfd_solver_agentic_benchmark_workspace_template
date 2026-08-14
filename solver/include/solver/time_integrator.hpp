#pragma once
#include "partition_types.hpp"
#include "case_config.hpp"
#include "mesh_types.hpp"
#include "residual.hpp"
#include <vector>
#include <string>

namespace solver {

struct SolverStats {
    int total_steps = 0;
    int inner_iterations = 0;
    double final_residual_l2 = 0.0;
    double final_residual_linf = 0.0;
    double residual_reduction_orders = 0.0;
    int inner_target_misses = 0;
    double converged_fraction = 0.0;
    double last_inner_residual_ratio = 0.0;
    double wall_time_seconds = 0.0;
    std::string convergence_status = "failed"; // "converged", "statistically_periodic", "failed"
    
    // Inner iteration statistics
    int min_inner_its = 0, max_inner_its = 0;
    double mean_inner_its = 0.0;
};

// Run steady implicit solve using LU-SGS
// state is modified in-place
SolverStats run_steady_solve(DistributedMesh& mesh,
                              std::vector<double>& state,
                              const CaseConfig& config,
                              const std::string& flux_type,
                              const std::string& output_dir);

// Run transient BDF2 solve for cylinder Re200
SolverStats run_transient_solve(DistributedMesh& mesh,
                                 std::vector<double>& state,
                                 const CaseConfig& config,
                                 const std::string& flux_type,
                                 const std::string& output_dir);

} // namespace solver
