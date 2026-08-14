#pragma once
#include "types.hpp"
#include "solver.hpp"
#include <string>

namespace cfd {

// Run a steady pseudo-time march. Returns aggregated statistics.
SteadyStats run_steady(Solver& solver, const std::string& output_dir,
                       const std::string& case_path, int rank, int n_ranks);

// Run a transient BDF2 dual-time march.
TransientStats run_transient(Solver& solver, const std::string& output_dir,
                             const std::string& case_path, int rank, int n_ranks);

// Write CSV histories and final artifacts.
void write_outputs(Solver& solver, const std::string& output_dir,
                   const CaseInput& ci, const LocalMesh& local_mesh,
                   const PartitionInfo& part, const Mesh& global_mesh,
                   const SteadyStats* steady, const TransientStats* transient,
                   const std::string& command_line, int rank, int n_ranks,
                   double wall_time, const std::string& start_utc,
                   const std::string& end_utc);

} // namespace cfd
