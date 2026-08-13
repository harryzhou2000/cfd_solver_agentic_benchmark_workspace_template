#pragma once
// Phase 3/4/5 output: residuals.csv and forces.csv (Phase 3/4) plus the full
// OUTPUT_CONTRACT.md output set (Phase 5): metadata.json, surface.csv, the
// final flow field (field_final.pvtu + per-rank .vtu pieces), the binary
// restart file, and run_status.json.
//
// Headers follow OUTPUT_CONTRACT.md:
//   residuals.csv: step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,
//                  residual_l2,residual_linf,min_inner_its,max_inner_its,
//                  mean_inner_its,inner_target_misses,last_inner_residual_ratio
//   forces.csv:    step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,
//                  pressure_lift,viscous_lift
//
// The trailing inner-iteration columns (Phase 4) are running per-step
// aggregates; only rank 0 writes the files (the caller guarantees this); I/O
// failures are reported to stderr and skipped rather than thrown, so a
// filesystem problem cannot abort the run.

#include <string>
#include <vector>

#include "solver.h"

namespace cfd {

// Create (truncate) the residuals CSV and write the header.
void write_residual_header(const std::string& path);

// Append one residual row.
void write_residual_row(const std::string& path, const SolverStats& stats);

// Create (truncate) the forces CSV and write the header.
void write_force_header(const std::string& path);

// Append one force row.
void write_force_row(const std::string& path, const SolverStats& stats);

// ---------------------------------------------------------------------------
// Phase 5: full output contract
// ---------------------------------------------------------------------------

// Write metadata.json (OUTPUT_CONTRACT.md): the case/mesh/numerics fields
// from RunConfig + SolverConfig, the observed inner-iteration statistics of
// the run, and the run outcome. start_time/end_time are seconds since the
// Unix epoch (time(nullptr)); they are written as ISO-8601 UTC strings.
// git_revision is written as JSON null. Call on rank 0 only (no MPI).
void write_metadata(const std::string& path, const RunConfig& run_cfg,
                    const LocalMesh& lm, int mpi_ranks, int n_cells_global,
                    int n_faces_global, int edge_cut,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const SolverConfig& scfg, double start_time,
                    double end_time, bool completed,
                    const std::string& convergence_status,
                    const std::string& case_id, const std::string& mesh_file,
                    int obs_min_inner, int obs_max_inner,
                    double obs_mean_inner, int inner_target_misses,
                    double inner_converged_fraction,
                    double last_inner_residual_ratio, bool is_transient);

// Write surface.csv: one row per boundary face (face.right == -1) with the
// wall boundary semantics — no-slip walls report u = v = mach = 0 and a
// laminar skin-friction coefficient, slip walls report the tangential
// velocity projection (normal component removed); non-wall boundary faces
// (farfield) report the adjacent cell-center values. Collective: every rank
// contributes its local rows; rank 0 writes the single file (point-to-point
// gather, no shared-file appends).
void write_surface_csv(const std::string& path, const LocalMesh& lm,
                       const std::vector<ConsState>& U_local,
                       const GasConfig& gas, const Freestream& fs,
                       const SolverConfig& scfg);

// Write the parallel piece index field_final.pvtu referencing the per-rank
// .vtu pieces (rank 0 only; the pieces are written by every rank).
void write_field_pvtu(const std::string& path, int nranks);

// Write this rank's field piece field_final_p{rank}.vtu: the owned cells
// with their nodes, connectivity, and the CellData arrays density,
// velocity_x, velocity_y, pressure, mach, temperature, rank (ascii VTK XML).
void write_field_vtu_piece(const std::string& path, const LocalMesh& lm,
                           const std::vector<ConsState>& U_local,
                           const GasConfig& gas, int rank);

// Write the restart_final.bin file (collective MPI-IO). Layout:
//   header (32 bytes): int32 n_owned, int32 nranks, double physical_time,
//                       double padding[2]
//   data: n_owned * 4 doubles per rank (rho, rhou, rhov, rhoE), rank r's
//         block at byte offset 32 + (sum of n_owned of ranks < r) * 4 * 8.
// Every rank writes its own owned-cell block at a disjoint offset.
void write_restart_binary(const std::string& path,
                          const std::vector<ConsState>& U_local,
                          const LocalMesh& lm, int mpi_ranks,
                          double physical_time);

// Read a restart_final.bin written by write_restart_binary (collective).
// Fills the owned cells of U_local and physical_time. Returns false (and
// leaves U_local untouched) when the file is missing, the rank count does
// not match, or the data block is truncated.
bool read_restart_binary(const std::string& path,
                         std::vector<ConsState>& U_local, const LocalMesh& lm,
                         double& physical_time);

// Write run_status.json (OUTPUT_CONTRACT.md). Call on rank 0 only.
void write_run_status(const std::string& path, const RunConfig& run_cfg,
                      int mpi_ranks, double wall_time, int final_step,
                      double final_physical_time,
                      const std::string& convergence_status,
                      double residual_reduction_orders,
                      const std::string& command, const std::string& notes);

}  // namespace cfd
