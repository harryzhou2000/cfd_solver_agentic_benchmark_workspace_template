#pragma once

/// @file output.hpp
/// Output file generation: residuals.csv, forces.csv, surface.csv,
/// metadata.json, run_status.json, VTU field files.

#include "common.hpp"

namespace cfd {

/// Helper to write the CSV header for residuals.
void write_residuals_header(std::ofstream& file);

/// Append one row to residuals.csv.
void write_residuals_row(std::ofstream& file, int step, Real physical_time,
                         int inner_iter, Real cfl, Real dt,
                         const Vec4& res_norm, Real res_l2, Real res_linf);

/// Helper to write the CSV header for forces.
void write_forces_header(std::ofstream& file);

/// Append one row to forces.csv.
void write_forces_row(std::ofstream& file, int step, Real physical_time,
                       Real cl, Real cd, Real cmz,
                       Real pressure_drag, Real viscous_drag,
                       Real pressure_lift, Real viscous_lift);

/// Write surface.csv for wall boundaries.
/// @param mesh            the mesh
/// @param U               cell-center conservative states
/// @param config          case configuration
/// @param path            output file path
void write_surface_csv(const Mesh& mesh, const std::vector<Vec4>& U,
                       const CaseConfig& config, const std::string& path);

/// Write VTU unstructured grid file with cell-centered field data.
/// @param mesh            the mesh
/// @param U               cell-center conservative states
/// @param config          case configuration
/// @param path            output file path
void write_field_vtu(const Mesh& mesh, const std::vector<Vec4>& U,
                      const CaseConfig& config, const std::string& path);

/// Write metadata.json with solver configuration and run details.
/// Keys follow OUTPUT_CONTRACT.md (flat schema).
/// @param config            case configuration
/// @param output_dir        output directory
/// @param wall_time_s       wall clock time in seconds
/// @param num_cells_global  total cells in the mesh
/// @param num_faces_global  total faces in the mesh
/// @param num_cells_owned_local  owned cells on this rank
/// @param num_cells_ghost_local  ghost cells on this rank
/// @param mpi_ranks         number of MPI ranks
/// @param converged         did the run converge?
/// @param is_transient      true if run_transient was used
/// @param inner_iter_min    min inner iterations observed (transient)
/// @param inner_iter_max    max inner iterations observed (transient)
/// @param inner_iter_mean   mean inner iterations (transient)
/// @param inner_target_misses  number of steps that missed inner target (transient)
/// @param inner_converged_frac fraction of steps that converged within target (transient)
void write_metadata_json(const CaseConfig& config, const std::string& output_dir,
                         Real wall_time_s,
                         std::size_t num_cells_global, std::size_t num_faces_global,
                         std::size_t num_cells_owned_local, std::size_t num_cells_ghost_local,
                         int mpi_ranks, bool converged,
                         bool is_transient = false,
                         int inner_iter_min = 0, int inner_iter_max = 0,
                         Real inner_iter_mean = 0.0,
                         int inner_target_misses = 0, Real inner_converged_frac = 1.0);

/// Write run_status.json (flat schema per OUTPUT_CONTRACT.md).
/// @param output_dir      output directory
/// @param case_id         case identifier
/// @param command         command line string
/// @param n_mpi_ranks     number of MPI ranks
/// @param wall_time_s     wall clock time
/// @param n_steps         steps completed
/// @param final_physical_time  final physical time (0 for steady pseudo-time)
/// @param converged       did the run converge?
/// @param residual_reduction final residual reduction (log10 orders)
void write_run_status_json(const std::string& output_dir,
                           const std::string& case_id,
                           const std::string& command,
                           int n_mpi_ranks, Real wall_time_s,
                           int n_steps, Real final_physical_time,
                           bool converged, Real residual_reduction);

} // namespace cfd
