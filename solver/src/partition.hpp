#pragma once

#include "common.hpp"

namespace cfd {

// Partition the cell-adjacency graph with METIS (serial preprocessing).
Partition partition_mesh(const GlobalMesh& mesh, Index n_parts);

// Build the rank-local mesh (owned + ghost cells, local faces, neighbor
// communication lists).  The solver stage stores only this local mesh.
LocalMesh build_local_mesh(const GlobalMesh& global, const Partition& part,
                           Index rank);

} // namespace cfd
