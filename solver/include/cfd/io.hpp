#pragma once

#include "cfd/config.hpp"
#include "cfd/partition.hpp"
#include "cfd/physics.hpp"
#include "cfd/solver.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <mpi.h>

namespace cfd {

struct RunStatus {
  std::string command;
  double wall_time_seconds = 0.0;
  int final_step = 0;
  double final_physical_time = 0.0;
  std::string convergence_status = "failed";
  double residual_reduction_orders = 0.0;
  std::string notes;
};

struct OutputMetadata {
  std::string solver_name = "cfd_solver_agentic_benchmark";
  std::string solver_version = "0.1";
  std::string git_revision;
  std::string partitioner = "metis_kway";
  std::string halo_exchange = "neighbor_isend_irecv";
  std::string inviscid_flux = "rusanov";
  std::string entropy_fix;
  std::string viscous_flux = "none";
  std::string implicit_solver = "pseudo_time";
  std::string reconstruction = "linear_least_squares";
  std::string limiter = "barth_jespersen";
  std::string positivity_preservation = "state_fallback";
  std::string wall_boundary_output_semantics = "boundary_value";
  int spatial_order_claimed = 2;
  bool true_bdf2_inner_loop = false;
  int typical_inner_iterations = 0, min_inner_iterations = 0, max_inner_iterations = 0;
  int observed_min_inner_iterations = 0, observed_max_inner_iterations = 0;
  double inner_residual_reduction_target = 0.0;
  int inner_target_misses = 0;
  double inner_target_converged_fraction = 1.0, last_inner_residual_ratio = 0.0;
  std::string start_time_utc, end_time_utc;
  bool completed = false;
  std::string convergence_status = "failed";
};

/// Rank-aware output writer. It never stores replicated solution state: the
/// supplied state is local (owned cells first, optional ghosts after them).
class OutputWriter {
 public:
  OutputWriter(std::filesystem::path output_dir, MPI_Comm comm);

  void write_metadata(const CaseConfig& config, const LocalMesh& mesh,
                      const OutputMetadata& metadata) const;
  void write_partition_diagnostics(const LocalMesh& mesh) const;
  void append_residual(const ResidualRecord& row) const;
  void append_force(const ForceRecord& row) const;
  void write_surface(const std::vector<SurfaceRecord>& local_rows) const;
  void write_run_status(const CaseConfig& config, const RunStatus& status) const;

  /// Writes field_rank<N>.vtu on every rank and field_final.pvtu on rank zero.
  /// In serial field_final.vtu is also produced for tools requiring a VTU file.
  void write_field_final(const LocalMesh& mesh, const std::vector<double>& local_state,
                         const PerfectGas& gas) const;

  /// Writes restart_final.rank<N>.bin on every rank plus a rank-zero JSON
  /// manifest.  Files are deliberately partition-specific and cannot pretend
  /// to be a global replicated state.
  void write_restart_final(const LocalMesh& mesh, const std::vector<double>& local_state,
                           int step, double physical_time) const;
  std::vector<double> read_restart_local(const LocalMesh& mesh, int& step,
                                         double& physical_time) const;

  const std::filesystem::path& directory() const noexcept { return output_dir_; }

 private:
  std::filesystem::path output_dir_;
  MPI_Comm comm_ = MPI_COMM_NULL;
  int rank_ = 0, size_ = 1;
};

}  // namespace cfd
