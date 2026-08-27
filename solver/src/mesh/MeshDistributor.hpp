// Serial preprocessing -> distributed solver mesh.
//
// Rank 0 reads the CGNS file, builds the cell adjacency graph, calls METIS and
// cuts one rank-local sub-mesh (owned cells + one ghost layer) per rank.  Each
// sub-mesh is sent to its rank, the global mesh is released, and every rank
// then builds its own face topology, geometry and halo descriptors.  After
// this call no rank holds the global mesh or a global conservative state.
#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "mesh/GlobalMesh.hpp"
#include "mesh/LocalMesh.hpp"
#include "mesh/Partitioner.hpp"

namespace cfd {

struct PartitionDiagnostics {
  int rank = 0;
  Index num_cells_owned = 0;
  Index num_cells_ghost = 0;
  Index num_boundary_faces = 0;
  int num_neighbor_ranks = 0;
  std::vector<int> neighbor_ranks;
  Index send_cells = 0;
  Index recv_cells = 0;
  double setup_seconds = 0.0;
};

// `global` is consumed (released) inside the call on rank 0.
void distributeMesh(GlobalMesh& global, const PartitionResult& part, MPI_Comm comm,
                    LocalMesh& local);

// Builds the halo neighbour lists (needs cell_gid / cell_owner already set).
void buildHaloDescriptors(LocalMesh& local, MPI_Comm comm);

// Gathers per-rank diagnostics onto rank 0 (index r == rank r).
std::vector<PartitionDiagnostics> gatherPartitionDiagnostics(const LocalMesh& local, MPI_Comm comm);

}  // namespace cfd
