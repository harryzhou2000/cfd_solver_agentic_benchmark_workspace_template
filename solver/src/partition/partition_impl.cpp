#include "partition/partition_types.hpp"
#include "metis.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace cfd {

PartitionResult partition_metis(const std::vector<std::vector<Int>>& cell_neighbors,
                                 int n_parts) {
    Int n_cells = cell_neighbors.size();

    // Build CSR format for METIS
    // xadj: start index for each row in adjncy
    // adjncy: adjacency list (flattened)
    std::vector<idx_t> xadj(n_cells + 1);
    std::vector<idx_t> adjncy;

    // Count edges first
    Int total_edges = 0;
    for (Int i = 0; i < n_cells; ++i) {
        total_edges += cell_neighbors[i].size();
    }
    adjncy.reserve(total_edges);

    idx_t edge_count = 0;
    for (Int i = 0; i < n_cells; ++i) {
        xadj[i] = edge_count;
        for (auto nb : cell_neighbors[i]) {
            adjncy.push_back(static_cast<idx_t>(nb));
            edge_count++;
        }
    }
    xadj[n_cells] = edge_count;

    // METIS options
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    options[METIS_OPTION_CTYPE] = METIS_CTYPE_SHEM;
    options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_EDGE;
    options[METIS_OPTION_NITER] = 10;
    options[METIS_OPTION_NCUTS] = 1;

    // Number of vertices = n_cells
    idx_t nvtxs = static_cast<idx_t>(n_cells);
    idx_t ncon = 1;  // single constraint (equal cells per partition)
    idx_t nparts = static_cast<idx_t>(n_parts);

    std::vector<idx_t> part(n_cells);
    idx_t objval;

    int result = METIS_PartGraphKway(
        &nvtxs, &ncon,
        xadj.data(), adjncy.data(),
        nullptr,        // vwgt (vertex weights)
        nullptr,        // vsize
        nullptr,        // adjwgt (edge weights)
        &nparts,
        nullptr,        // tpwgts (target partition weights)
        nullptr,        // ubvec
        options,
        &objval,
        part.data());

    if (result != METIS_OK) {
        throw std::runtime_error("METIS partitioning failed with error code " +
                                 std::to_string(result));
    }

    PartitionResult pr;
    pr.edge_cut = objval;
    pr.cell_partition.resize(n_cells);
    for (Int i = 0; i < n_cells; ++i) {
        pr.cell_partition[i] = static_cast<Int>(part[i]);
    }

    return pr;
}

RankPartition build_rank_partition(
    const std::vector<Int>& cell_partition,
    const std::vector<Int>& cell_global_ids,
    const std::vector<std::vector<Int>>& cell_neighbors,
    const std::vector<Int>& face_left_cells,
    const std::vector<Int>& face_right_cells,
    int rank, int n_ranks, MPI_Comm comm) {

    Int n_global_cells = cell_partition.size();
    Int n_global_faces = face_left_cells.size();

    RankPartition rp;
    rp.rank = rank;
    rp.n_ranks = n_ranks;
    rp.comm = comm;

    // Identify owned cells and build global->local map
    // Unmapped cells get INVALID_INDEX (-1) as the sentinel
    rp.owned_to_local.resize(n_global_cells, INVALID_INDEX);
    for (Int gc = 0; gc < n_global_cells; ++gc) {
        if (cell_partition[gc] == rank) {
            rp.owned_to_local[gc] = rp.owned_cell_ids.size();
            rp.owned_cell_ids.push_back(gc);
        }
    }
    rp.n_owned = rp.owned_cell_ids.size();

    // Find ghost cells: cells that neighbor owned cells but are owned by other ranks
    std::set<Int> ghost_set;
    std::unordered_map<Int, int> ghost_owner;  // global cell -> owner rank

    for (auto gcid : rp.owned_cell_ids) {
        for (auto nb : cell_neighbors[gcid]) {
            int owner = cell_partition[nb];
            if (owner != rank) {
                ghost_set.insert(nb);
                ghost_owner[nb] = owner;
            }
        }
    }

    rp.n_ghost = ghost_set.size();
    rp.ghost_cell_ids.assign(ghost_set.begin(), ghost_set.end());
    rp.ghost_owner_ranks.resize(rp.n_ghost);
    for (Int i = 0; i < rp.n_ghost; ++i) {
        rp.ghost_owner_ranks[i] = ghost_owner[rp.ghost_cell_ids[i]];
    }

    // Build local face list: faces adjacent to owned cells
    for (Int gf = 0; gf < n_global_faces; ++gf) {
        Int left = face_left_cells[gf];
        Int right = face_right_cells[gf];

        bool left_owned = (left != INVALID_INDEX && cell_partition[left] == rank);
        bool right_owned = (right != INVALID_INDEX && cell_partition[right] == rank);

        if (!left_owned && !right_owned) continue;  // face not touching this rank

        RankPartition::LocalFace lf;
        lf.global_face_id = gf;
        lf.left_local = (left != INVALID_INDEX) ? rp.owned_to_local[left] : INVALID_INDEX;
        lf.right_local = (right != INVALID_INDEX) ? rp.owned_to_local[right] : INVALID_INDEX;

        // If right cell is a ghost, map it to ghost local index
        if (right != INVALID_INDEX && cell_partition[right] != rank &&
            lf.right_local == INVALID_INDEX) {
            // Find ghost local index
            for (Int gi = 0; gi < rp.n_ghost; ++gi) {
                if (rp.ghost_cell_ids[gi] == right) {
                    lf.right_local = rp.n_owned + gi;
                    break;
                }
            }
        }
        if (left != INVALID_INDEX && cell_partition[left] != rank &&
            lf.left_local == INVALID_INDEX) {
            for (Int gi = 0; gi < rp.n_ghost; ++gi) {
                if (rp.ghost_cell_ids[gi] == left) {
                    lf.left_local = rp.n_owned + gi;
                    break;
                }
            }
        }

        rp.faces.push_back(lf);
    }

    // Build halo neighbor lists
    // Group ghost cells by owner rank
    std::map<int, std::vector<Int>> ghost_by_owner;
    for (Int gi = 0; gi < rp.n_ghost; ++gi) {
        ghost_by_owner[rp.ghost_owner_ranks[gi]].push_back(rp.n_owned + gi);
    }

    // For each neighbor rank, determine which owned cells to send
    for (auto& [neighbor_rank, ghost_local_ids] : ghost_by_owner) {
        RankPartition::HaloNeighbor hn;
        hn.neighbor_rank = neighbor_rank;
        hn.recv_cells = ghost_local_ids;

        // Find owned cells that the neighbor needs (ones that are ghosts on the neighbor)
        // For each owned cell, check if any neighbor is on the neighbor rank
        std::set<Int> send_cells_set;
        for (auto gcid : rp.owned_cell_ids) {
            for (auto nb : cell_neighbors[gcid]) {
                if (static_cast<int>(cell_partition[nb]) == neighbor_rank) {
                    send_cells_set.insert(rp.owned_to_local[gcid]);
                    break;
                }
            }
        }
        hn.send_cells.assign(send_cells_set.begin(), send_cells_set.end());
        rp.neighbors.push_back(std::move(hn));
    }

    return rp;
}

} // namespace cfd
