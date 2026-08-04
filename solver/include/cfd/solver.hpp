#pragma once

#include "cfd/case_config.hpp"
#include "cfd/partition.hpp"
#include "cfd/physics.hpp"

#include <mpi.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cfd {

struct ResidualRow {
  int step = 0;
  double physical_time = 0.0;
  int inner_iteration = 0;
  double cfl = 0.0;
  double dt = 0.0;
  std::array<double, nvars> component_l2{};
  double residual_l2 = 0.0;
  double residual_linf = 0.0;
};

struct ForceRow {
  int step = 0;
  double physical_time = 0.0;
  double cl = 0.0;
  double cd = 0.0;
  double cmz = 0.0;
  double pressure_drag = 0.0;
  double viscous_drag = 0.0;
  double pressure_lift = 0.0;
  double viscous_lift = 0.0;
};

struct SurfaceRow {
  double x = 0.0;
  double y = 0.0;
  double nx = 0.0;
  double ny = 0.0;
  double pressure = 0.0;
  double cp = 0.0;
  double cf = 0.0;
  double rho = 0.0;
  double u = 0.0;
  double v = 0.0;
  double mach = 0.0;
  std::string tag;
};

struct InnerIterationStatistics {
  int requested_min = 0;
  int requested_max = 0;
  int observed_min = 0;
  int observed_max = 0;
  double observed_mean = 0.0;
  double target = 0.0;
  int target_misses = 0;
  double target_converged_fraction = 1.0;
  double last_ratio = 0.0;
};

struct InitialState {
  std::vector<Conserved> state;
  std::vector<Conserved> previous_state;
  int step = 0;
  double physical_time = 0.0;
};

struct RunOptions {
  int debug_max_steps = 0;
  double debug_final_time = 0.0;
  int progress_interval = 100;
};

class RunObserver {
 public:
  virtual ~RunObserver() = default;
  virtual void residual(const ResidualRow& row) = 0;
  virtual void force(const ForceRow& row) = 0;
  virtual void field(int step, double physical_time, const std::vector<Conserved>& state,
                     const std::vector<double>& vorticity, bool final) = 0;
  virtual void progress(const std::string& line) = 0;
};

struct RunResult {
  std::vector<Conserved> state;
  std::vector<Conserved> previous_state;
  std::vector<double> vorticity;
  std::vector<SurfaceRow> local_surface;
  std::vector<ForceRow> force_history;
  int final_step = 0;
  double final_physical_time = 0.0;
  double residual_reduction_orders = 0.0;
  std::string convergence_status = "failed";
  std::string notes;
  InnerIterationStatistics inner_statistics;
  std::int64_t hllc_fallback_faces = 0;
  std::int64_t reconstruction_positivity_fallbacks = 0;
  std::int64_t damped_updates = 0;
  bool debug_limited = false;
};

RunResult run_solver(const CaseConfig& config, const LocalMesh& mesh, MPI_Comm communicator,
                     RunObserver& observer, const RunOptions& options = {},
                     const std::optional<InitialState>& restart = std::nullopt);

}  // namespace cfd
