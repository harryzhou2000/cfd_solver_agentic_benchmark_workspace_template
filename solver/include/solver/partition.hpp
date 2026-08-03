#pragma once
#include "mesh_types.hpp"
#include "partition_types.hpp"
#include <vector>

namespace solver {

// Returns partition assignment: part[global_cell_id] = rank index
std::vector<int> partition_mesh(const CellGraph& graph, int num_cells, int nparts);

// Build distributed mesh on all ranks.
// On rank 0: global_mesh is valid, used to build all local meshes and send them
// On rank > 0: global_mesh is empty, receives from rank 0
DistributedMesh build_distributed_mesh(const Mesh& global_mesh,
                                        const std::vector<int>& partition,
                                        MPI_Comm comm);

} // namespace solver
