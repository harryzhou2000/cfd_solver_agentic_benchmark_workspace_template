#pragma once

#include "mesh/mesh.hpp"
#include "common/types.hpp"
#include <vector>

// METIS-based cell adjacency graph partitioning.
// Returns: per-cell rank assignment (size = num_cells in full mesh)
// nparts: number of MPI ranks
std::vector<int> partition_mesh_metis(const Mesh& mesh, int nparts);

// Build cell adjacency graph (edge-based: cells sharing a face are adjacent)
std::vector<std::pair<Index, Index>> build_cell_adjacency(const Mesh& mesh);

// Write per-rank partition info for diagnostics
struct PartitionInfo {
    Index edge_cut;
    std::vector<Index> cells_per_rank;
    std::vector<Index> boundary_faces_per_rank;
    void print() const;
};

PartitionInfo compute_partition_info(const Mesh& mesh,
                                     const std::vector<int>& cell_rank,
                                     int nparts);
