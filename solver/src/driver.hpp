#pragma once

#include "common.hpp"
#include "numerics.hpp"
#include <string>

namespace cfd {

struct SteadyResult {
    Index steps_run = 0;
    Real final_res_l2 = 0.0, final_res_linf = 0.0;
    Real residual_reduction = 0.0;
    std::string convergence_status = "failed";
    std::string notes;
    InnerStats inner;
};

struct TransientResult {
    Index physical_steps_run = 0;
    Real final_res_l2 = 0.0, final_res_linf = 0.0;
    std::string convergence_status = "failed";
    InnerStats inner;
};

SteadyResult run_steady(Solver& solver, const std::string& output_dir,
                        const std::string& case_path, int rank, int n_ranks);

TransientResult run_transient(Solver& solver, const std::string& output_dir,
                              const std::string& case_path, int rank, int n_ranks);

} // namespace cfd
