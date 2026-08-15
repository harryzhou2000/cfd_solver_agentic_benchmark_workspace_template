#pragma once

// Serial preprocessing setup: all ranks read the serial mesh, partition it
// deterministically (METIS), and build their rank-local DistributedMesh and
// the conservative-state array U (initialized to a reference field for the
// halo-exchange self-check).

#include <mpi.h>

#include <vector>

#include "partition/partition.hpp"

namespace cfd {

// Number of conservative variables carried in U (2D compressible flow:
// rho, rho*u, rho*v, rho*E).
constexpr int kNumConservedVars = 4;

struct SolverSetup {
  DistributedMesh dmesh;
  // Flat conservative state: nvars * total local cells doubles.
  std::vector<double> U;
  int nvars = kNumConservedVars;
};

// Partitions the serial mesh across `nranks` ranks and builds the local
// setup for `rank`. Pure serial preprocessing per rank (all ranks run the
// same deterministic partition).
SolverSetup setup_distributed(const Mesh2D& mesh, int rank, int nranks,
                              MPI_Comm comm);

// Runs one halo exchange and verifies that every ghost cell received the
// state its owner sent. U is initialized with a field derived from the cell
// geometry (center x/y, volume, global id), so after the exchange each ghost
// slot must bitwise match its own stored geometry/global id. Returns the
// maximum absolute error over all ghost cells (0.0 on success).
double run_halo_selfcheck(SolverSetup& setup, MPI_Comm comm);

}  // namespace cfd
