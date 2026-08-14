#pragma once

#include <mpi.h>

#include <map>
#include <string>
#include <vector>

#include "cgns_mesh.hpp"
#include "local_mesh.hpp"

// Partition the global mesh with METIS on rank 0, distribute rank-local
// submeshes (owned cells + one ghost layer + halo maps) to every rank with
// neighbor-scoped MPI communication, and gather partition diagnostics back on
// rank 0. bc_map maps CGNS boundary family names to solver BC types; a
// boundary family missing from bc_map is an error (caught on rank 0 and
// broadcast).
LocalMesh distribute_mesh(MPI_Comm comm, const SerialMesh& gm,
                          const std::map<std::string, BCType>& bc_map,
                          PartitionInfo& pinfo);
