#pragma once
#include "partition_types.hpp"
#include "case_config.hpp"
#include "residual.hpp"
#include "time_integrator.hpp"

#include <fstream>
#include <string>

namespace solver {

// Per-step CSV streams opened once at the start of a solve and closed at
// the end. Rank 0 writes; other ranks keep empty streams.
struct StepLogFiles {
    std::ofstream residuals;
    std::ofstream forces;
    bool active = false;  // true on rank 0 only
};

StepLogFiles open_step_logs(const std::string& output_dir, int rank);
void close_step_logs(StepLogFiles& logs);

// Append one row to residuals.csv / forces.csv (rank 0 only; no-op
// otherwise). comp_l2[4] are the per-component residual L2 norms.
void write_residual_row(StepLogFiles& logs, int step, double phys_time,
                        int inner_iter, double cfl, double dt,
                        const double comp_l2[4], double l2, double linf);
void write_force_row(StepLogFiles& logs, int step, double phys_time,
                     const ForceCoeffs& f);

// Write partition_diagnostics.csv (one row per rank; rank 0 gathers and
// writes the file).
void write_partition_diagnostics(const DistributedMesh& mesh,
                                 const std::string& output_dir, MPI_Comm comm,
                                 int rank);

// Write surface.csv over the wall boundary faces (rank 0 gathers and
// writes). The reported state is the boundary state (reconstructed interior
// state passed through the wall BC), so slip walls have near-zero normal
// velocity and no-slip walls have u = v = 0.
void write_surface_csv(DistributedMesh& mesh, std::vector<double>& state,
                       const CaseConfig& config, const std::string& output_dir,
                       MPI_Comm comm, int rank);

// Write field_final.vtu (owned cells of all ranks gathered to rank 0).
void write_field_vtu(const DistributedMesh& mesh,
                     const std::vector<double>& state,
                     const CaseConfig& config, const std::string& output_dir,
                     MPI_Comm comm, int rank);

// Write restart_final.bin: magic, num_cells_global, kStateSize, then the
// flat owned states of all ranks (gathered to rank 0).
void write_restart(const DistributedMesh& mesh,
                   const std::vector<double>& state,
                   const std::string& output_dir, MPI_Comm comm, int rank);

// Write metadata.json and run_status.json (rank 0 only; the other ranks
// still participate in nothing here).
void write_metadata_json(const DistributedMesh& mesh, const CaseConfig& config,
                         const SolverStats& stats, const std::string& flux_type,
                         const std::string& output_dir, int num_cells_global,
                         int num_faces_global, int edge_cut,
                         const std::string& start_utc,
                         const std::string& end_utc);
void write_run_status_json(const CaseConfig& config, const SolverStats& stats,
                           int final_step, const std::string& command,
                           const std::string& output_dir, int mpi_ranks);

// ISO-8601 UTC timestamp of "now".
std::string iso8601_utc_now();

} // namespace solver
