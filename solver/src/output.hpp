#pragma once
#include "types.hpp"
#include "solver.hpp"
#include <string>

namespace cfd {

// Write all final output artifacts for a completed run.
void write_outputs(Solver& solver, const std::string& output_dir,
                   const CaseInput& ci, const LocalMesh& local_mesh,
                   const PartitionInfo& part, const Mesh& global_mesh,
                   const SteadyStats* steady, const TransientStats* transient,
                   const std::string& command_line, int rank, int n_ranks,
                   double wall_time, const std::string& start_utc,
                   const std::string& end_utc);

// Field file (ASCII VTU with the real unstructured cells and CellData
// arrays; rank 0 writes using the full global mesh geometry).
void write_field_vtu(Solver& solver, const std::string& path, const Mesh& global_mesh, int rank);

// Binary restart file (rank-local owned conservative state).
void write_restart(Solver& solver, const std::string& path, int rank, int n_ranks);

} // namespace cfd
