#pragma once

#include "cfd/CaseConfig.hpp"
#include "cfd/DistributedMesh.hpp"
#include "cfd/Physics.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cfd {

/// One globally reduced nonlinear-residual sample.
struct ResidualRow {
  int step = 0;
  double physical_time = 0.0;
  int inner_iter = 0;
  double cfl = 0.0;
  double dt = 0.0;
  Conserved component_l2{};
  double residual_l2 = 0.0;
  double residual_linf = 0.0;
};

/// Aerodynamic force coefficients and their pressure/viscous components.
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

/// A boundary-state sample.  Values are deliberately separate from the
/// adjacent cell state so no-slip and slip-wall semantics remain unambiguous.
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

/// Actual observed statistics, rather than requested run-control values.
struct InnerSolveStats {
  int observed_min_inner_iterations = 0;
  int observed_max_inner_iterations = 0;
  double observed_mean_inner_iterations = 0.0;
  int inner_target_misses = 0;
  double inner_target_converged_fraction = 0.0;
  double last_inner_residual_ratio = 0.0;
};

/// Information that becomes known only after a solve completes.
struct RunSummary {
  std::string command;
  double wall_time_seconds = 0.0;
  int final_step = 0;
  double final_physical_time = 0.0;
  std::string convergence_status = "failed";
  double residual_reduction_orders = 0.0;
  std::string notes;
  std::string solver_name = "cfd_solver";
  std::string solver_version = "0.1";
  std::string git_revision;
  std::string start_time_utc;
  std::string end_time_utc;
  bool completed = false;
  bool true_bdf2_inner_loop = false;
  std::string reconstruction = "least_squares_linear_with_startup_ramp";
  std::string limiter = "barth_jespersen";
  std::string positivity_preservation = "density_pressure_fallback";
  std::string wall_boundary_output_semantics = "boundary_value";
  InnerSolveStats inner_solve;
};

/// MPI-aware output sink.  Every rank calls collective methods; only rank zero
/// writes shared histories and JSON, while each rank writes its own field and
/// restart payload.  State arrays contain owned cells first and may optionally
/// include ghost states after mesh.owned_count.
class OutputWriter {
 public:
  OutputWriter(std::filesystem::path directory, const CaseConfig& config,
               const LocalMesh& mesh, MPI_Comm communicator);

  void initialize();
  void write_partition_diagnostics();
  void write_residual(const ResidualRow& row);
  void write_force(const ForceRow& row);
  void write_surface(const std::vector<SurfaceRow>& local_rows);
  void write_final_field(const std::vector<Conserved>& states) const;
  void write_restart(const std::vector<Conserved>& states) const;
  void write_metadata_and_status(const RunSummary& summary) const;

  const std::filesystem::path& directory() const { return directory_; }
  int rank() const { return rank_; }
  int ranks() const { return ranks_; }

 private:
  std::filesystem::path directory_;
  const CaseConfig& config_;
  const LocalMesh& mesh_;
  MPI_Comm communicator_;
  int rank_ = 0;
  int ranks_ = 1;
  bool initialized_ = false;
  mutable std::ofstream residual_stream_;
  mutable std::ofstream force_stream_;
};

}  // namespace cfd
