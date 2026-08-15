// METIS cell-graph partitioning and rank-local mesh construction.
// Phase 2b: serial METIS partitioning; local meshes are derived from the
// full mesh, which every rank holds (distributed-mesh construction with
// inter-rank communication lands in Phase 3).

#include "partition.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace cfd {

namespace {

// Collects the ghost cells of `my_rank`: face neighbors of owned cells whose
// owning rank differs from my_rank. Sorted ascending and de-duplicated so
// the ordering matches LocalMesh::ghost_cells.
std::vector<cgsize_t> collect_ghost_ids(const Mesh& mesh,
                                        const std::vector<idx_t>& partition,
                                        int my_rank) {
    std::vector<cgsize_t> ghosts;
    ghosts.reserve(static_cast<size_t>(mesh.n_faces));
    const auto is_owned = [&](cgsize_t c) {
        return c >= 0 && c < mesh.n_cells &&
               partition[static_cast<size_t>(c)] == my_rank;
    };
    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const cgsize_t l = mesh.face_left[static_cast<size_t>(f)];
        const cgsize_t r = mesh.face_right[static_cast<size_t>(f)];
        const bool l_owned = is_owned(l);
        const bool r_owned = is_owned(r);
        if (l_owned && !r_owned) ghosts.push_back(r);
        if (r_owned && !l_owned) ghosts.push_back(l);
    }
    std::sort(ghosts.begin(), ghosts.end());
    ghosts.erase(std::unique(ghosts.begin(), ghosts.end()), ghosts.end());
    return ghosts;
}

}  // namespace

void build_cell_graph(const Mesh& mesh, std::vector<idx_t>& xadj,
                      std::vector<idx_t>& adjncy) {
    const size_t n = static_cast<size_t>(mesh.n_cells);
    std::vector<std::vector<cgsize_t>> neighbors(n);

    for (cgsize_t f = 0; f < mesh.n_faces; ++f) {
        const cgsize_t l = mesh.face_left[static_cast<size_t>(f)];
        const cgsize_t r = mesh.face_right[static_cast<size_t>(f)];
        if (l >= 0 && l < mesh.n_cells && r >= 0 && r < mesh.n_cells &&
            l != r) {
            neighbors[static_cast<size_t>(l)].push_back(r);
            neighbors[static_cast<size_t>(r)].push_back(l);
        }
    }

    xadj.assign(n + 1, 0);
    adjncy.clear();
    adjncy.reserve(2 * static_cast<size_t>(mesh.n_faces));
    for (size_t i = 0; i < n; ++i) {
        auto& nb = neighbors[i];
        // METIS requires sorted, unique adjacency lists.
        std::sort(nb.begin(), nb.end());
        nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
        xadj[i + 1] = xadj[i] + static_cast<idx_t>(nb.size());
        for (cgsize_t j : nb) {
            adjncy.push_back(static_cast<idx_t>(j));
        }
    }
}

PartitionResult partition_cells(const Mesh& mesh, idx_t nparts) {
    PartitionResult result;
    const size_t n = static_cast<size_t>(mesh.n_cells);
    result.cell_partition.assign(n, 0);

    if (n == 0 || nparts <= 1) {
        // Single part (or empty mesh): no partitioning needed.
        result.edge_cut = 0;
        return result;
    }
    if (n > static_cast<size_t>(std::numeric_limits<idx_t>::max())) {
        throw std::runtime_error(
            "partition_cells: cell count exceeds METIS idx_t range");
    }

    std::vector<idx_t> xadj, adjncy;
    build_cell_graph(mesh, xadj, adjncy);

    idx_t nvtxs = static_cast<idx_t>(n);
    idx_t ncon = 1;
    idx_t nparts_in = nparts;
    idx_t edgecut = 0;

    // Fixed seed so every rank partitioning the same mesh gets the identical
    // result (required by the Phase-2b no-communication halo plan).
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_SEED] = 1;

    const int rcode = METIS_PartGraphKway(
        &nvtxs, &ncon, xadj.data(), adjncy.data(),
        /*vwgt=*/nullptr, /*vsize=*/nullptr, /*adjwgt=*/nullptr, &nparts_in,
        /*tpwgts=*/nullptr, /*ubvec=*/nullptr, options, &edgecut,
        result.cell_partition.data());
    if (rcode != METIS_OK) {
        throw std::runtime_error("partition_cells: METIS_PartGraphKway failed "
                                 "with code " +
                                 std::to_string(rcode));
    }

    result.edge_cut = edgecut;
    return result;
}

LocalMesh build_local_mesh_simple(const Mesh& full_mesh,
                                  const std::vector<idx_t>& partition,
                                  int my_rank, int n_parts) {
    (void)n_parts;
    LocalMesh lm;
    const size_t n = static_cast<size_t>(full_mesh.n_cells);
    if (partition.size() != n) {
        throw std::runtime_error(
            "build_local_mesh_simple: partition size does not match n_cells");
    }

    // Owned cells, in ascending global-id order (matches local indices).
    lm.owned_cells.reserve(n);
    for (cgsize_t c = 0; c < full_mesh.n_cells; ++c) {
        if (partition[static_cast<size_t>(c)] == my_rank) {
            lm.owned_cells.push_back(c);
        }
    }
    lm.n_owned = static_cast<cgsize_t>(lm.owned_cells.size());

    // Ghost cells: owned cells' neighbors on other ranks.
    lm.ghost_cells = collect_ghost_ids(full_mesh, partition, my_rank);
    lm.n_ghost = static_cast<cgsize_t>(lm.ghost_cells.size());

    // global -> local mapping (owned first, then ghosts).
    lm.global_to_local.reserve(lm.owned_cells.size() + lm.ghost_cells.size());
    for (size_t k = 0; k < lm.owned_cells.size(); ++k) {
        lm.global_to_local[lm.owned_cells[k]] = static_cast<cgsize_t>(k);
    }
    for (size_t k = 0; k < lm.ghost_cells.size(); ++k) {
        lm.global_to_local[lm.ghost_cells[k]] =
            lm.n_owned + static_cast<cgsize_t>(k);
    }

    // Local geometry: owned cells first, then ghosts.
    const size_t n_local = lm.owned_cells.size() + lm.ghost_cells.size();
    lm.cell_vol_local.resize(n_local);
    lm.cell_center_x_local.resize(n_local);
    lm.cell_center_y_local.resize(n_local);
    const auto copy_geometry = [&](const std::vector<cgsize_t>& cells,
                                   size_t base) {
        for (size_t k = 0; k < cells.size(); ++k) {
            const size_t g = static_cast<size_t>(cells[k]);
            lm.cell_vol_local[base + k] = full_mesh.cell_vol[g];
            lm.cell_center_x_local[base + k] = full_mesh.cell_center_x[g];
            lm.cell_center_y_local[base + k] = full_mesh.cell_center_y[g];
        }
    };
    copy_geometry(lm.owned_cells, 0);
    copy_geometry(lm.ghost_cells, lm.owned_cells.size());

    return lm;
}

}  // namespace cfd
