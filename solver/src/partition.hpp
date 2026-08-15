#pragma once

#include <mpi.h>

#include "mesh.hpp"

namespace fv {

// Partition the global mesh with METIS on rank 0, scatter rank-local meshes
// (owned + one ghost layer) to all ranks. Rank 0 is the only rank that ever
// holds the global mesh; after this call every rank stores only its local
// owned cells plus ghost cells.
//
// edgeCut: global edge cut reported back for diagnostics (all ranks).
LocalMesh partitionAndScatter(const GlobalMesh* g, MPI_Comm comm, long& edgeCut,
                              std::string& partitionerName);

}  // namespace fv
