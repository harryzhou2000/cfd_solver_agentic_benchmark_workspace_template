#pragma once

#include <string>
#include <vector>

#include "global_mesh.hpp"

namespace cfd {

struct PartitionResult {
    std::vector<int> cell_part;  // global cell id -> MPI rank
    int64_t edge_cut = 0;
    std::string partitioner = "metis_kway";
};

// Partition the cell adjacency graph with METIS into `nparts` parts.
// Runs on the rank that owns the serial global mesh (rank 0 in the default
// pipeline). The returned part vector is valid for all cells.
PartitionResult partition_cells_metis(const GlobalMesh& mesh, int nparts,
                                      unsigned int seed = 1);

}  // namespace cfd
