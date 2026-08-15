#pragma once

#include <metis.h>

#include <unordered_map>
#include <vector>

#include "types.hpp"

namespace cfd {

// Result of a serial METIS partitioning run.
struct PartitionResult {
    // Part id in [0, nparts) for every global cell (size == mesh.n_cells).
    std::vector<idx_t> cell_partition;
    // Number of internal faces whose two cells landed in different parts.
    idx_t edge_cut = 0;
};

// Builds the cell-adjacency graph in CSR form from the mesh's internal
// faces. Every internal face (left, right) contributes both directions, so
// the graph is undirected; each cell's neighbor list is sorted in ascending
// order and de-duplicated (both are METIS requirements). Cells without
// internal neighbors get an empty list.
void build_cell_graph(const Mesh& mesh, std::vector<idx_t>& xadj,
                      std::vector<idx_t>& adjncy);

// Partitions the mesh cell graph into `nparts` parts with
// METIS_PartGraphKway using uniform vertex/edge weights and a fixed random
// seed, so every rank that calls this on the same mesh obtains the identical
// partition. nparts <= 1 assigns every cell to part 0 without calling METIS.
// Throws std::runtime_error if METIS reports failure.
PartitionResult partition_cells(const Mesh& mesh, idx_t nparts);

// Per-rank view of the partitioned mesh.
//
// Phase 2b: every rank holds the full mesh and the identical partition, so
// the local mesh is derived locally without inter-rank communication.
// Distributed-mesh construction with communication lands in Phase 3.
struct LocalMesh {
    // Global cell ids of this rank's owned cells, ascending (local indices
    // 0..n_owned-1 follow this order).
    std::vector<cgsize_t> owned_cells;
    cgsize_t n_owned = 0;

    // Global cell ids of ghost cells (owned cells' face neighbors that
    // belong to other ranks), ascending (local indices n_owned..n_owned +
    // n_ghost - 1 follow this order).
    std::vector<cgsize_t> ghost_cells;
    cgsize_t n_ghost = 0;

    // Global cell index -> local index for owned and ghost cells.
    std::unordered_map<cgsize_t, cgsize_t> global_to_local;

    // Local geometry arrays, owned cells first then ghosts, in the same
    // order as `owned_cells` / `ghost_cells`.
    std::vector<double> cell_vol_local;
    std::vector<double> cell_center_x_local, cell_center_y_local;
};

// Builds the local mesh for `my_rank` from the full mesh and a partition of
// it into `n_parts` parts. Owned cells are those with
// partition[i] == my_rank; a ghost cell is any face neighbor of an owned
// cell whose owning rank differs from my_rank.
LocalMesh build_local_mesh_simple(const Mesh& full_mesh,
                                  const std::vector<idx_t>& partition,
                                  int my_rank, int n_parts);

}  // namespace cfd
