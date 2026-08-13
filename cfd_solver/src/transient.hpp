#pragma once
#include "solver.hpp"

namespace cfd {

struct TransientStats {
    idx_t physical_steps_run;
    idx_t total_inner_iterations;
    real_t final_res_l2;
    real_t final_res_linf;
    real_t residual_reduction;
    idx_t observed_min_inner;
    idx_t observed_max_inner;
    real_t inner_target_converged_fraction;
    idx_t inner_target_misses;
    real_t last_inner_residual_ratio;
    std::string convergence_status;
    std::string notes;
};

TransientStats run_transient(Solver& solver, const std::string& output_dir);

} // namespace cfd
