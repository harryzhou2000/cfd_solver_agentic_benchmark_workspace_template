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

/// Provenance attached to a rank-local restart and its output package.  The
/// residual reference always denotes the fully assembled residual of the
/// original fresh state; it is never reset to a checkpoint-local value.
struct RestartProvenance {
  bool restarted{false};
  int chain_depth{0};
  std::string parent_manifest;
  std::string cumulative_residual_trace{"residuals.csv"};
  std::string compatibility_signature;
  int segment_start_step{0};
  double segment_start_physical_time{0.0};
  double residual_reference_l2{0.0};
  double residual_reference_linf{0.0};
  double segment_start_residual_l2{0.0};
  double segment_start_residual_linf{0.0};
  int checkpoint_step{0};
  double checkpoint_physical_time{0.0};
  double checkpoint_residual_l2{0.0};
  double checkpoint_residual_linf{0.0};
};

/// Return a stable, human-readable signature of all physics and numerical
/// controls that affect a restart state.  Segment length and residual target
/// are deliberately excluded so a compatible run can extend a continuation.
std::string restart_compatibility_signature(const CaseConfig& config, const LocalMesh& mesh);

struct RunStatus {
  std::string command;
  double wall_time_seconds = 0.0;
  int final_step = 0;
  double final_physical_time = 0.0;
  std::string convergence_status = "failed";
  double residual_reduction_orders = 0.0;
  RestartProvenance restart;
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
  double observed_cfl_min = 0.0, observed_cfl_max = 0.0;
  std::string termination_reason;
  RestartProvenance restart;
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
  /// Write the traceable, cumulative residual history.  For a fresh run this
  /// begins with ``initial_reference`` at step zero; for a restart it first
  /// verifies and copies its parent trace before appending this segment.
  void write_residual_trace(const std::filesystem::path& parent_trace,
                            const ResidualRecord& initial_reference,
                            const std::vector<ResidualRecord>& segment_rows,
                            const RestartProvenance& provenance) const;
  void append_force(const ForceRecord& row) const;
  void write_surface(const std::vector<SurfaceRecord>& local_rows) const;
  void write_run_status(const CaseConfig& config, const RunStatus& status) const;

  /// Writes rank-local VTU pieces, a parallel PVTU index, and a rank-zero
  /// multi-piece field_final.vtu.  The latter is a complete, standard VTK XML
  /// UnstructuredGrid artifact for consumers that require the literal final
  /// VTU filename in the output contract; gathering happens only at output.
  void write_field_final(const LocalMesh& mesh, const std::vector<double>& local_state,
                         const PerfectGas& gas) const;

  /// Writes restart_final.rank<N>.bin on every rank plus a rank-zero JSON
  /// manifest.  Files are deliberately partition-specific and cannot pretend
  /// to be a global replicated state.
  void write_restart_final(const CaseConfig& config, const LocalMesh& mesh,
                           const std::vector<double>& local_state,
                           const RestartProvenance& provenance) const;
  /// Read and validate the v2 manifest before rank-local state is restored.
  /// Legacy v1 manifests are intentionally rejected because they cannot make
  /// a cumulative residual claim traceable.
  [[nodiscard]] RestartProvenance read_restart_provenance(
      const std::filesystem::path& manifest_path, const CaseConfig& config,
      const LocalMesh& mesh) const;
  std::vector<double> read_restart_local(const LocalMesh& mesh, int& step,
                                         double& physical_time) const;

  const std::filesystem::path& directory() const noexcept { return output_dir_; }

 private:
  std::filesystem::path output_dir_;
  MPI_Comm comm_ = MPI_COMM_NULL;
  int rank_ = 0, size_ = 1;
};

}  // namespace cfd
