#pragma once

#include "common.hpp"
#include "numerics.hpp"
#include "driver.hpp"
#include <string>

namespace cfd {

// Write the final field (VTU cell-centered, rank 0 gathers all data).
void write_field_vtu(Solver& solver, const std::string& path,
                     const GlobalMesh& global, int rank);

// Write a rank-local binary restart file.
void write_restart(Solver& solver, const std::string& path,
                   int rank, int n_ranks);

// Write metadata.json, run_status.json, partition_diagnostics.csv,
// field_final.vtu, and restart_final.bin.
void write_outputs(Solver& solver, const std::string& output_dir,
                   const CaseInput& ci, const LocalMesh& lm,
                   const Partition& part, const GlobalMesh& gm,
                   const SteadyResult* steady,
                   const TransientResult* transient,
                   const std::string& command_line,
                   int rank, int n_ranks, double wall_time,
                   const std::string& start_utc,
                   const std::string& end_utc);

} // namespace cfd
