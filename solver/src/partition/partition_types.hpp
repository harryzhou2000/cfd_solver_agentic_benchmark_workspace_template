#pragma once

#include "types.hpp"
#include <vector>
#include <mpi.h>

namespace cfd {

// --- Per-rank MPI partition information ---
struct RankPartition {
    // Owned cells: cells for which this rank computes the residual
    std::vector<Int> owned_cell_ids;      // global cell indices
    std::vector<Int> owned_to_local;      // global -> local index map (size: n_global_cells, -1 if not owned)

    // Ghost cells: cells owned by other ranks but needed for stencil
    std::vector<Int> ghost_cell_ids;      // global cell indices
    std::vector<Int> ghost_owner_ranks;   // which rank owns each ghost cell

    // All cells (owned + ghost), contiguous
    // owned cells are [0, n_owned), ghost cells are [n_owned, n_owned+n_ghost)
    Int n_owned{0};
    Int n_ghost{0};

    // Faces: subset of global faces that touch owned cells
    // Face ownership: a face is computed by the rank that owns its left cell
    // Boundary faces: computed by the rank that owns the adjacent cell
    struct LocalFace {
        Int global_face_id;     // index into global Mesh::faces
        Int left_local{INVALID_INDEX};     // local cell index (or INVALID_INDEX sentinel)
        Int right_local{INVALID_INDEX};    // local cell index (or INVALID_INDEX for boundary)
    };
    std::vector<LocalFace> faces;

    // Halo exchange: which data to send/receive
    struct HaloNeighbor {
        int neighbor_rank;
        std::vector<Int> send_cells;     // local indices (owned cells) to send
        std::vector<Int> recv_cells;     // local indices (ghost cells) to receive
    };
    std::vector<HaloNeighbor> neighbors;

    // MPI communicator
    MPI_Comm comm{MPI_COMM_WORLD};
    int rank{0};
    int n_ranks{1};
};

// --- Partition result (from METIS partitioning) ---
struct PartitionResult {
    std::vector<Int> cell_partition;  // global cell index -> rank
    Int edge_cut{0};
};

/// Partition cell adjacency graph using METIS (serial)
/// cell_neighbors: adjacency list per cell (global indexing)
/// n_parts: number of partitions (= number of MPI ranks)
PartitionResult partition_metis(const std::vector<std::vector<Int>>& cell_neighbors,
                                 int n_parts);

/// Build rank-local mesh from global mesh and partition
/// This constructs the per-rank cell lists, face lists, ghost cells, and neighbor info
RankPartition build_rank_partition(
    const std::vector<Int>& cell_partition,
    const std::vector<Int>& cell_global_ids,    // global cell indices
    const std::vector<std::vector<Int>>& cell_neighbors,
    const std::vector<Int>& face_left_cells,
    const std::vector<Int>& face_right_cells,
    int rank, int n_ranks, MPI_Comm comm);

} // namespace cfd
