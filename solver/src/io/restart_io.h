// cns2d -- restart file I/O.
//
// The restart file is a compact binary file holding the global conserved state
// indexed by global cell id.  Rank 0 performs the file access and scatters /
// gathers the rank-local pieces, so restart files are independent of the rank
// count: a run started on 8 ranks can be restarted on 2.
//
// Gathering the full state on rank 0 happens only at output time, never during
// solver iterations, so it does not violate the no-full-state-replication
// requirement for the iteration phase.
#pragma once

#include <string>

#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"

namespace cns2d {

// Write the current state.  Collective over 'comm'.
void writeRestart(const std::string &path, const DistributedMesh &mesh, const StateField &U,
                  MPI_Comm comm, int step, Real physical_time);

// Read a restart file into the rank-local state.  Collective over 'comm'.
// Throws CnsError if the file does not match the current mesh size.
void readRestart(const std::string &path, const DistributedMesh &mesh, StateField &U, MPI_Comm comm);

}  // namespace cns2d
