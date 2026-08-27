#include "mesh/Partitioner.hpp"

#include <algorithm>
#include <deque>
#include <numeric>
#include <unordered_map>

#include <metis.h>

#include "core/Exception.hpp"
#include "core/Log.hpp"

namespace cfd {
namespace {

// Reverse Cuthill-McKee ordering of the sub-graph induced by `cells`.
std::vector<Index> rcmOrder(const GlobalMesh& mesh, const std::vector<Index>& cells) {
  const std::size_t n = cells.size();
  if (n <= 2) return cells;
  std::unordered_map<Index, Index> local_of;
  local_of.reserve(n * 2);
  for (std::size_t i = 0; i < n; ++i) local_of[cells[i]] = static_cast<Index>(i);

  std::vector<std::vector<Index>> adj(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Index c = cells[i];
    for (Index k = mesh.adj_offset[c]; k < mesh.adj_offset[c + 1]; ++k) {
      auto it = local_of.find(mesh.adj_cells[k]);
      if (it != local_of.end()) adj[i].push_back(it->second);
    }
  }
  for (auto& a : adj) std::sort(a.begin(), a.end());

  std::vector<char> visited(n, 0);
  std::vector<Index> order;
  order.reserve(n);
  for (std::size_t seed = 0; seed < n; ++seed) {
    if (visited[seed]) continue;
    // pseudo-peripheral start: lowest degree unvisited node of this component
    Index start = static_cast<Index>(seed);
    {
      std::deque<Index> probe{start};
      std::vector<char> seen(n, 0);
      seen[start] = 1;
      std::size_t best_deg = adj[start].size();
      while (!probe.empty()) {
        const Index v = probe.front();
        probe.pop_front();
        if (adj[v].size() < best_deg) { best_deg = adj[v].size(); start = v; }
        for (Index w : adj[v]) if (!seen[w]) { seen[w] = 1; probe.push_back(w); }
      }
    }
    std::deque<Index> q{start};
    visited[start] = 1;
    while (!q.empty()) {
      const Index v = q.front();
      q.pop_front();
      order.push_back(v);
      std::vector<Index> nb;
      for (Index w : adj[v]) if (!visited[w]) nb.push_back(w);
      std::sort(nb.begin(), nb.end(),
                [&](Index a, Index b) { return adj[a].size() < adj[b].size(); });
      for (Index w : nb) { visited[w] = 1; q.push_back(w); }
    }
  }
  std::reverse(order.begin(), order.end());
  std::vector<Index> out(n);
  for (std::size_t i = 0; i < n; ++i) out[i] = cells[order[i]];
  return out;
}

}  // namespace

PartitionResult partitionMesh(const GlobalMesh& mesh, int nparts) {
  CFD_CHECK(nparts >= 1, "number of partitions must be >= 1");
  const Index nc = mesh.numCells();
  CFD_CHECK(nc >= nparts,
            "mesh has " << nc << " cells but " << nparts << " MPI ranks were requested");

  PartitionResult res;
  res.part.assign(static_cast<std::size_t>(nc), 0);

  if (nparts == 1) {
    res.method = "metis_kway_single_rank";
    res.edge_cut = 0;
  } else {
    static_assert(sizeof(idx_t) == 4 || sizeof(idx_t) == 8, "unexpected METIS idx_t");
    std::vector<idx_t> xadj(static_cast<std::size_t>(nc) + 1);
    std::vector<idx_t> adjncy(mesh.adj_cells.size());
    for (Index c = 0; c <= nc; ++c) xadj[c] = static_cast<idx_t>(mesh.adj_offset[c]);
    for (std::size_t k = 0; k < mesh.adj_cells.size(); ++k)
      adjncy[k] = static_cast<idx_t>(mesh.adj_cells[k]);

    idx_t nvtxs = static_cast<idx_t>(nc);
    idx_t ncon = 1;
    idx_t np = static_cast<idx_t>(nparts);
    idx_t objval = 0;
    std::vector<idx_t> part(static_cast<std::size_t>(nc), 0);
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    options[METIS_OPTION_CONTIG] = 1;   // prefer connected partitions
    options[METIS_OPTION_SEED] = 12345; // deterministic partitions across runs

    int st = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr,
                                 nullptr, &np, nullptr, nullptr, options, &objval, part.data());
    res.method = "metis_kway";
    if (st != METIS_OK) {
      // Retry without the contiguity constraint before giving up.
      options[METIS_OPTION_CONTIG] = 0;
      st = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr,
                               nullptr, &np, nullptr, nullptr, options, &objval, part.data());
    }
    CFD_CHECK(st == METIS_OK, "METIS_PartGraphKway failed with status " << st);
    for (Index c = 0; c < nc; ++c) res.part[c] = static_cast<int>(part[c]);
    res.edge_cut = static_cast<GlobalIndex>(objval);
  }

  // Group and renumber each part.
  res.ordered_cells.assign(static_cast<std::size_t>(nparts), {});
  for (Index c = 0; c < nc; ++c) res.ordered_cells[res.part[c]].push_back(c);
  for (int r = 0; r < nparts; ++r) {
    CFD_CHECK(!res.ordered_cells[r].empty(),
              "METIS produced an empty partition for rank " << r << "; use fewer ranks");
    res.ordered_cells[r] = rcmOrder(mesh, res.ordered_cells[r]);
  }
  return res;
}

}  // namespace cfd
