#pragma once

#include "mesh.hpp"
#include <string>

namespace cfd {

// Per-rank partition file written by rank 0 during preprocessing.
struct PartitionInfo {
    int n_owned = 0, n_ghost = 0;
    int num_cells_global = 0, num_faces_global = 0, num_nodes_global = 0;
    int edge_cut = 0;
};

// Rank 0: build METIS partition of the global mesh and write
// <outdir>/partition_rank_<r>.bin for every rank.
PartitionInfo write_partition_files(const GlobalMesh& gm, int n_ranks,
                                    const std::string& outdir);

}  // namespace cfd
