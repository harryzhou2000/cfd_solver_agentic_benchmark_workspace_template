#pragma once

// Transient solver: BDF2 physical-time integration with inner dual-time
// (pseudo-time) iterations solved by LU-SGS.

#include <mpi.h>

#include <string>
#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"

namespace cfd {

struct TransientResult {
  long long steps_run = 0;
  double final_time = 0.0;
  long long inner_iterations_total = 0;
  std::string convergence_status;
  // Inner-iteration statistics (Phase 4 remediation).
  int observed_min_inner_iterations = 0;
  int observed_max_inner_iterations = 0;
  long long inner_target_misses = 0;           // steps hitting max_inner
  double inner_target_converged_fraction = 0.0;  // converged steps / total
  double last_inner_residual_ratio = 0.0;      // final inner RHS ratio
};

// Runs the BDF2 transient solver. U^{n-1} = U^n = U on entry (freestream).
// Per physical step, the inner pseudo-time loop drives the total residual
// (spatial + BDF2 physical-time term) below inner_residual_reduction_target;
// the histories U^{n-1}, U^n are frozen during the inner loop and shifted
// only after inner convergence. The number of physical steps is
// ceil(final_time / time_step), capped by run_control.max_steps when set
// (the CLI --max-steps override lands there).
TransientResult run_transient_solver(const CaseConfig& cfg,
                                     DistributedMesh& dmesh,
                                     std::vector<double>& U,
                                     const std::string& output_dir,
                                     MPI_Comm comm);

}  // namespace cfd
