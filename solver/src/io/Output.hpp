#pragma once

#include "fv/Solver.hpp"
#include "mesh/GlobalMesh.hpp"
#include "mesh/Partition.hpp"
#include "physics/Gas.hpp"

#include <mpi.h>

#include <string>

namespace cfds {

// Current UTC timestamp in ISO-8601 format.
std::string utc_now();

struct OutputContext {
  const CaseConfig& cfg;
  const GasModel& gas;
  const GlobalMesh* global;   // rank 0 only
  DistributedMesh& mesh;
  const SolverStats& stats;
  const Solver& solver;
  int edge_cut = 0;
  PartitionSummary summary;
  std::string command;
  std::string git_revision;
  std::string start_utc;
  std::string end_utc;
  MPI_Comm comm;
};

// Write the output-directory artifacts for a completed run:
// metadata.json, run_status.json, partition_diagnostics.csv, surface.csv,
// field_final.vtu, restart_final.bin.
void write_outputs(const OutputContext& ctx);

// Write CSV headers (called once before the run starts).
void write_csv_headers(const CaseConfig& cfg);

// Load a restart file written by write_outputs. Rank 0 reads the file and
// scatters each rank's owned slice; all ranks return their owned state.
bool read_restart(const std::string& path, const DistributedMesh& mesh,
                  std::vector<ConsVec>& U_owned, int& step, double& time,
                  MPI_Comm comm);

}  // namespace cfds
