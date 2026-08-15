#pragma once

// Output writers implementing OUTPUT_CONTRACT.md: CSV histories (residuals,
// forces, surface, partition diagnostics), VTK XML UnstructuredGrid field
// files (.vtu per rank + .pvtu master), metadata.json, run_status.json and
// the restart file.

#include <mpi.h>

#include <string>
#include <vector>

#include "config/case_config.hpp"
#include "partition/partition.hpp"
#include "solver/steady_solver.hpp"
#include "solver/transient_solver.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// residuals.csv / forces.csv
// ---------------------------------------------------------------------------

// Truncates and writes the residuals.csv header:
//   step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf
void write_residual_header(const std::string& output_dir);

// Appends one residual row (per-variable L2 norms of the residual).
void write_residual_row(const std::string& output_dir, int step,
                        double physical_time, int inner_iter, double cfl,
                        double dt, double res_rho, double res_rhou,
                        double res_rhov, double res_rhoE, double res_l2,
                        double res_linf);

// Truncates and writes the forces.csv header:
//   step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift
void write_forces_header(const std::string& output_dir);

// Appends one forces row (coefficients use case reference area/length and
// freestream dynamic pressure; see compute_forces).
void write_forces_row(const std::string& output_dir, int step,
                      double physical_time, double cl, double cd, double cmz,
                      double pressure_drag, double viscous_drag,
                      double pressure_lift, double viscous_lift);

// ---------------------------------------------------------------------------
// surface.csv
// ---------------------------------------------------------------------------

// Writes surface.csv with one row per wall boundary face (SlipWall +
// NoSlipAdiabaticWall), gathered across ranks and written by rank 0:
//   x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag
// Wall-state semantics ("boundary_value"): slip walls report the mirrored
// interior state (normal velocity zeroed, tangential preserved); no-slip
// walls report zero wall velocity. cp uses the freestream dynamic pressure;
// cf uses the tangential wall shear for no-slip walls (0 for slip walls).
// The output directory is created if missing. No-op on ranks other than 0
// except for the Gatherv.
void write_surface(const std::string& output_dir, const std::vector<double>& U,
                   const DistributedMesh& dmesh, const CaseConfig& cfg,
                   MPI_Comm comm);

// ---------------------------------------------------------------------------
// Field files (VTK XML UnstructuredGrid, per-rank pieces + pvtu master)
// ---------------------------------------------------------------------------

// Writes this rank's piece of the flow field to
// "<dir>/<stem>_<rank>.vtu" (owned cells only) and, on rank 0, the
// "<dir>/<stem>.pvtu" master referencing all pieces. The .pvtu is written
// after an MPI_Barrier so all pieces exist. CellData: density, velocity_x,
// velocity_y, velocity_magnitude, pressure, mach, temperature, energy, rank.
// Requires dmesh.vertices to be populated; gamma/R come from cfg.
void write_field_vtu(const std::string& output_dir, const std::string& stem,
                     const DistributedMesh& dmesh,
                     const std::vector<double>& U, const CaseConfig& cfg,
                     MPI_Comm comm);

// Convenience: writes the final field (stem "field_final").
void write_field_final(const std::string& output_dir,
                       const DistributedMesh& dmesh,
                       const std::vector<double>& U, const CaseConfig& cfg,
                       MPI_Comm comm);

// ---------------------------------------------------------------------------
// metadata.json / run_status.json / partition diagnostics / restart
// ---------------------------------------------------------------------------

// Writes metadata.json per OUTPUT_CONTRACT.md (all required fields).
// Called on rank 0 only. The SteadyResult/TransientResult overloads fill the
// run statistics (inner iterations, convergence); `started_utc` is the
// ISO-8601 start time captured at run start; `completed` is derived from the
// convergence status.
void write_metadata(const std::string& output_dir, const DistributedMesh& dmesh,
                    const CaseConfig& cfg, const SteadyResult& result,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const std::string& git_revision,
                    const std::string& started_utc);

void write_metadata(const std::string& output_dir, const DistributedMesh& dmesh,
                    const CaseConfig& cfg, const TransientResult& result,
                    const std::string& solver_name,
                    const std::string& solver_version,
                    const std::string& git_revision,
                    const std::string& started_utc);

// Writes run_status.json (rank 0).
void write_run_status(const std::string& output_dir, const CaseConfig& cfg,
                      const std::string& convergence_status, int steps_run,
                      double final_physical_time,
                      double residual_reduction_orders, int mpi_ranks,
                      double wall_time_seconds, const std::string& command);

// Writes partition_diagnostics.csv (rank 0, gathered from all ranks):
//   rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells
// plus partition_diagnostics.json with the same rows and global summaries
// (edge cut, min/max/mean owned, load-balance ratio).
void write_partition_diagnostics(const std::string& output_dir,
                                 const DistributedMesh& dmesh, MPI_Comm comm);

// Writes restart_final.json on rank 0: the full conservative state ordered
// by global cell id (gathered from all ranks), the partition count and the
// mesh path — enough to restart (and repartition) the run.
void write_restart(const std::string& output_dir, const DistributedMesh& dmesh,
                   const std::vector<double>& U, const CaseConfig& cfg,
                   MPI_Comm comm);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ISO-8601 UTC timestamp for the current time (e.g. "2026-08-05T12:34:56Z").
std::string utc_now_iso8601();

}  // namespace cfd
