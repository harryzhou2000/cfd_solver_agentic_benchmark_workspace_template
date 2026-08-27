#pragma once
// METIS-based cell-graph partitioning and distribution of rank-local meshes.
// Rank 0 reads the global mesh (serial preprocessing), partitions the cell
// adjacency graph with METIS_PartGraphKway, and distributes rank-local
// subdomains (owned cells + ghosts + halo plan) with point-to-point MPI.
#include "common.hpp"
#include "global_mesh.hpp"
#include "local_mesh.hpp"
#include <mpi.h>

namespace fv {

// Build the rank-local mesh for this rank of comm.
// On rank 0, gm must contain the global mesh; on other ranks it is ignored.
// bcMap: family name -> BC type (resolved from the case file).
LocalMesh partitionMesh(const GlobalMesh& gm,
                        const vector<std::pair<string, BCType>>& bcMap,
                        MPI_Comm comm);

}  // namespace fv
