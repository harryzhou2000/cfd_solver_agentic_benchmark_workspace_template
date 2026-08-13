#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <stdexcept>

namespace cfd {

PartitionResult partition_cells_metis(const GlobalMesh& mesh, int nparts,
                                      unsigned int seed) {
    PartitionResult result;
    const int n = mesh.num_cells();
    if (n <= 0) throw std::runtime_error("cannot partition an empty mesh");
    if (nparts <= 0)
        throw std::runtime_error("METIS partition requires at least one part");

    result.cell_part.assign(n, 0);

    // Cell adjacency from interior faces only.
    std::vector<std::vector<int>> adj(n);
    for (const auto& f : mesh.faces) {
        if (f.bc != BcType::Interior) continue;
        adj[f.left].push_back(f.right);
        adj[f.right].push_back(f.left);
    }
    for (auto& a : adj) {
        std::sort(a.begin(), a.end());
        a.erase(std::unique(a.begin(), a.end()), a.end());
    }

    if (nparts == 1) {
        result.partitioner = "metis_kway";
        result.edge_cut = 0;
        return result;
    }

    // Build CSR.
    idx_t nn = static_cast<idx_t>(n);
    std::vector<idx_t> xadj(n + 1, 0);
    size_t total_adj = 0;
    for (int i = 0; i < n; ++i) total_adj += adj[i].size();
    std::vector<idx_t> adjncy(total_adj);
    size_t pos = 0;
    for (int i = 0; i < n; ++i) {
        xadj[i] = static_cast<idx_t>(pos);
        for (int v : adj[i]) adjncy[pos++] = static_cast<idx_t>(v);
    }
    xadj[n] = static_cast<idx_t>(pos);

    idx_t ncon = 1;
    idx_t nparts_arg = static_cast<idx_t>(nparts);
    std::vector<idx_t> part(n, 0);
    idx_t objval = 0;

    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;  // C-style 0-based indexing
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    options[METIS_OPTION_CTYPE] = METIS_CTYPE_SHEM;
    options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_GROW;
    options[METIS_OPTION_CONTIG] = 1;
    options[METIS_OPTION_MINCONN] = 1;
    options[METIS_OPTION_SEED] = static_cast<idx_t>(seed);
    options[METIS_OPTION_DBGLVL] = METIS_DBG_INFO;

    std::vector<real_t> tpwgts(static_cast<size_t>(ncon) * nparts_arg, 0.0);
    for (size_t i = 0; i < tpwgts.size(); ++i) tpwgts[i] = 1.0 / nparts_arg;

    const int status = METIS_PartGraphKway(
        &nn, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr, nullptr,
        &nparts_arg, tpwgts.data(), nullptr, options, &objval, part.data());
    if (status != METIS_OK) {
        throw std::runtime_error("METIS_PartGraphKway failed with status " +
                                 std::to_string(status));
    }

    for (int i = 0; i < n; ++i) {
        result.cell_part[i] = static_cast<int>(part[i]);
    }
    result.edge_cut = static_cast<int64_t>(objval);

    // Verify the cut against the face list for diagnostics consistency.
    int64_t cut = 0;
    for (const auto& f : mesh.faces) {
        if (f.bc == BcType::Interior &&
            result.cell_part[f.left] != result.cell_part[f.right])
            ++cut;
    }
    result.edge_cut = cut;
    return result;
}

}  // namespace cfd
