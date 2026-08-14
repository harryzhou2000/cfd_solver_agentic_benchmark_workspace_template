#include "metis_partitioner.hpp"
#include <metis.h>
#include <iostream>
#include <algorithm>
#include <numeric>

std::vector<std::pair<Index, Index>> build_cell_adjacency(const Mesh& mesh) {
    std::vector<std::pair<Index, Index>> adj;
    adj.reserve(mesh.num_internal_faces);
    for (Index fid = 0; fid < mesh.num_internal_faces; fid++) {
        const Face& f = mesh.faces[fid];
        if (f.left >= 0 && f.right >= 0) {
            adj.emplace_back(f.left, f.right);
        }
    }
    return adj;
}

std::vector<int> partition_mesh_metis(const Mesh& mesh, int nparts) {
    idx_t n = (idx_t)mesh.num_cells;

    if (nparts <= 1 || n < nparts) {
        std::vector<int> result(n, 0);
        return result;
    }

    auto adj_pairs = build_cell_adjacency(mesh);

    // Build CSR format for METIS
    std::vector<idx_t> xadj(n + 1, 0);
    std::vector<idx_t> adjncy;

    // Count degree for each cell
    for (const auto& [l, r] : adj_pairs) {
        xadj[l + 1]++;
        xadj[r + 1]++;
    }
    for (idx_t i = 1; i <= n; i++) {
        xadj[i] += xadj[i - 1];
    }

    adjncy.resize(2 * adj_pairs.size());
    std::vector<idx_t> ptr = xadj;
    for (const auto& [l, r] : adj_pairs) {
        adjncy[ptr[l]++] = (idx_t)r;
        adjncy[ptr[r]++] = (idx_t)l;
    }

    idx_t ncon      = 1;
    idx_t edge_cut  = 0;
    idx_t objval    = 0;
    std::vector<idx_t> part(n, 0);

    int ier = METIS_PartGraphKway(
        &n, &ncon,
        xadj.data(), adjncy.data(),
        nullptr,    // vwgt
        nullptr,    // vsize
        nullptr,    // adjwgt
        &nparts,
        nullptr,    // tpwgts
        nullptr,    // ubvec
        nullptr,    // options
        &edge_cut,
        part.data()
    );

    if (ier != METIS_OK) {
        std::cerr << "METIS partitioning failed with error " << ier << std::endl;
        // Fallback: block partition
        for (idx_t i = 0; i < n; i++) part[i] = i % nparts;
    }

    std::vector<int> result(n);
    for (idx_t i = 0; i < n; i++) result[i] = (int)part[i];

    std::cout << "METIS partition: nparts=" << nparts
              << " edge_cut=" << edge_cut << std::endl;

    return result;
}

PartitionInfo compute_partition_info(const Mesh& mesh,
                                     const std::vector<int>& cell_rank,
                                     int nparts) {
    PartitionInfo info;
    info.cells_per_rank.resize(nparts, 0);
    info.edge_cut = 0;

    for (Index ic = 0; ic < mesh.num_cells; ic++) {
        info.cells_per_rank[cell_rank[ic]]++;
    }

    // Count edges crossing partitions
    for (const auto& [l, r] : build_cell_adjacency(mesh)) {
        if (cell_rank[l] != cell_rank[r]) info.edge_cut++;
    }

    return info;
}

void PartitionInfo::print() const {
    Index min_cells = *std::min_element(cells_per_rank.begin(), cells_per_rank.end());
    Index max_cells = *std::max_element(cells_per_rank.begin(), cells_per_rank.end());
    Real avg_cells = (Real)std::accumulate(cells_per_rank.begin(), cells_per_rank.end(), Index(0))
                     / cells_per_rank.size();

    std::cout << "Partition info: edge_cut=" << edge_cut
              << " cells/rank min=" << min_cells << " max=" << max_cells
              << " avg=" << avg_cells
              << " load_balance=" << (avg_cells > 0 ? max_cells / avg_cells : 0)
              << std::endl;
}
