#pragma once

// Steady-state solver: second-order reconstruction + Barth-Jespersen
// limiter, Rusanov fluxes, block LU-SGS inner iterations with defect
// correction (damped second-order defect, adaptive relaxation), and a CFL
// controller that follows the case-file ramp schedule and only retreats on
// a clear divergence indicator.

#include <mpi.h>

#include <string>
#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"

namespace cfd {

struct SteadyResult {
  int steps_run = 0;
  int inner_iterations_total = 0;
  double final_residual_l2 = 0.0;
  double final_residual_linf = 0.0;
  double residual_reduction_orders = 0.0;  // log10(R0/R)
  double final_cfl = 0.0;                  // CFL used in the last step
  std::string convergence_status;  // "converged" | "max_steps_reached" | ...
  // Inner-iteration statistics (Phase 4 remediation).
  int observed_min_inner_iterations = 0;
  int observed_max_inner_iterations = 0;
  long long inner_target_misses = 0;           // outer steps hitting max_inner
  double inner_target_converged_fraction = 0.0;  // converged outer steps / total
  double last_inner_residual_ratio = 0.0;      // final inner RHS ratio (last step)
};

// Runs the steady pseudo-time marching loop until convergence
// (residual_reduction_target orders of magnitude) or max_steps.
// U: in/out — the conservative state (initialized to freestream on entry).
// output_dir: where residuals.csv/forces.csv rows are appended (per-step
// cadence from cfg.outputs); an empty string disables the CSV writers.
SteadyResult run_steady_solver(const CaseConfig& cfg, DistributedMesh& dmesh,
                               std::vector<double>& U,
                               const std::string& output_dir, MPI_Comm comm);

}  // namespace cfd
