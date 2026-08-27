// cns2d -- everything one run needs, assembled once.
//
// The two drivers (steady pseudo-time march and transient dual-time BDF2) share
// this context so they use identical discretization, identical output paths and
// identical diagnostics.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/case_input.h"
#include "core/options.h"
#include "numerics/residual.h"
#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "parallel/halo_exchange.h"
#include "physics/perfect_gas.h"
#include "solve/lusgs.h"

namespace cns2d {

// Global (MPI-reduced) residual norms.
struct ResidualNorms {
  std::array<Real, kNumVars> component_l2{};  // per-variable L2, volume weighted
  Real l2{0.0};                               // combined L2
  Real linf{0.0};                             // combined Linf
};

// Statistics of the inner (implicit) solve, aggregated over a run.
struct InnerSolveStatistics {
  long long total_steps{0};
  long long total_inner{0};
  int min_inner{0};
  int max_inner{0};
  long long target_misses{0};
  long long converged_steps{0};
  Real last_ratio{0.0};

  void record(int inner_iterations, bool hit_target, Real ratio) {
    if (total_steps == 0) {
      min_inner = inner_iterations;
      max_inner = inner_iterations;
    } else {
      min_inner = std::min(min_inner, inner_iterations);
      max_inner = std::max(max_inner, inner_iterations);
    }
    ++total_steps;
    total_inner += inner_iterations;
    if (hit_target) {
      ++converged_steps;
    } else {
      ++target_misses;
    }
    last_ratio = ratio;
  }

  Real meanInner() const {
    return total_steps > 0 ? static_cast<Real>(total_inner) / static_cast<Real>(total_steps) : 0.0;
  }
  Real convergedFraction() const {
    return total_steps > 0 ? static_cast<Real>(converged_steps) / static_cast<Real>(total_steps) : 0.0;
  }
};

struct RunOutcome {
  std::string convergence_status{"failed"};
  int final_step{0};
  Real final_physical_time{0.0};
  Real residual_reduction_orders{0.0};
  Real initial_residual{0.0};
  Real final_residual{0.0};
  std::string notes;
  bool completed{false};
  InnerSolveStatistics inner_stats;
  long long positivity_fallbacks{0};
};

class SolverContext {
 public:
  SolverContext(const CaseInput &input, const CommandLineOptions &options, MPI_Comm comm);

  const CaseInput &input() const { return input_; }
  const CommandLineOptions &options() const { return options_; }
  const DistributedMesh &mesh() const { return *mesh_; }
  const FlowContext &flow() const { return flow_; }
  const SchemeOptions &scheme() const { return scheme_; }
  ResidualAssembler &assembler() { return *assembler_; }
  ImplicitSolver &implicit() { return *implicit_; }
  HaloExchange &halo() { return *halo_; }

  StateField &state() { return U_; }
  const StateField &state() const { return U_; }

  MPI_Comm comm() const { return comm_; }
  int rank() const { return rank_; }
  int size() const { return size_; }

  // Initialise the field to freestream, or from a restart file.
  void initializeState();

  // Synchronise ghost states of U.
  void syncState();

  // Globally reduced residual norms of 'residual' (volume-scaled to be a
  // density-of-change norm, which makes the value mesh-independent).
  ResidualNorms computeNorms(const StateField &residual) const;

  // Enforce positivity on the updated state; returns the number of cells that
  // needed limiting.
  long long enforcePositivity(StateField &U) const;

  const std::string &implicitSolverLabel() const { return implicit_label_; }

 private:
  CaseInput input_;
  CommandLineOptions options_;
  MPI_Comm comm_{MPI_COMM_NULL};
  int rank_{0};
  int size_{1};

  std::unique_ptr<DistributedMesh> mesh_;
  FlowContext flow_;
  SchemeOptions scheme_;
  std::unique_ptr<HaloExchange> halo_;
  std::unique_ptr<ResidualAssembler> assembler_;
  std::unique_ptr<ImplicitSolver> implicit_;
  std::string implicit_label_;

  StateField U_;
};

}  // namespace cns2d
