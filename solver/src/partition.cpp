#include "cfd/partition.hpp"

#include <metis.h>

#include <algorithm>
#include <numeric>
#include <set>
#include <unordered_map>

namespace cfd {

PartitionResult partition_cells_metis(const GlobalMesh& mesh, int nranks) {
  PartitionResult result;
  const int n = static_cast<int>(mesh.cells.size());
  result.owner.assign(static_cast<std::size_t>(n), 0);
  if (nranks <= 1 || n == 0) return result;

  std::vector<std::vector<idx_t>> adj(static_cast<std::size_t>(n));
  for (const Face& f : mesh.faces) {
    if (f.left_cell >= 0 && f.right_cell >= 0) {
      adj[static_cast<std::size_t>(f.left_cell)].push_back(f.right_cell);
      adj[static_cast<std::size_t>(f.right_cell)].push_back(f.left_cell);
    }
  }

  std::vector<idx_t> xadj(static_cast<std::size_t>(n + 1), 0);
  std::vector<idx_t> adjncy;
  for (int i = 0; i < n; ++i) {
    auto& row = adj[static_cast<std::size_t>(i)];
    std::sort(row.begin(), row.end());
    row.erase(std::unique(row.begin(), row.end()), row.end());
    xadj[static_cast<std::size_t>(i + 1)] =
        xadj[static_cast<std::size_t>(i)] + static_cast<idx_t>(row.size());
    adjncy.insert(adjncy.end(), row.begin(), row.end());
  }

  idx_t nvtxs = n;
  idx_t ncon = 1;
  idx_t nparts = nranks;
  idx_t objval = 0;
  std::vector<idx_t> part(static_cast<std::size_t>(n), 0);
  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 0;
  options[METIS_OPTION_CONTIG] = 1;

  const int status =
      METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr, nullptr,
                          &nparts, nullptr, nullptr, options, &objval, part.data());
  if (status != METIS_OK) {
    throw CfdError("METIS_PartGraphKway failed with status " + std::to_string(status));
  }
  result.edge_cut = static_cast<int>(objval);
  for (int i = 0; i < n; ++i) {
    result.owner[static_cast<std::size_t>(i)] =
        std::clamp(static_cast<int>(part[static_cast<std::size_t>(i)]), 0, nranks - 1);
  }
  return result;
}

LocalMesh build_local_mesh(const GlobalMesh& mesh, const std::vector<int>& owner, int rank) {
  LocalMesh local;
  local.global_num_cells = static_cast<int>(mesh.cells.size());
  local.global_num_faces = static_cast<int>(mesh.faces.size());
  std::unordered_map<int, int> vertex_map;

  auto map_vertex = [&](int global_vertex) -> int {
    auto it = vertex_map.find(global_vertex);
    if (it != vertex_map.end()) return it->second;
    const int lid = static_cast<int>(local.vertices.size());
    vertex_map[global_vertex] = lid;
    local.vertices.push_back(mesh.vertices[static_cast<std::size_t>(global_vertex)]);
    return lid;
  };

  auto add_cell = [&](int global_cell, bool owned) -> int {
    auto it = local.local_index_by_global_cell.find(global_cell);
    if (it != local.local_index_by_global_cell.end()) return it->second;
    const Cell& gc = mesh.cells[static_cast<std::size_t>(global_cell)];
    LocalCell lc;
    lc.global_id = global_cell;
    lc.owner_rank = owner[static_cast<std::size_t>(global_cell)];
    lc.owned = owned;
    lc.center = gc.center;
    lc.area = gc.area;
    lc.vertices.reserve(gc.vertices.size());
    for (int v : gc.vertices) lc.vertices.push_back(map_vertex(v));
    const int lid = static_cast<int>(local.cells.size());
    local.local_index_by_global_cell[global_cell] = lid;
    local.cells.push_back(std::move(lc));
    if (owned) local.owned_local_indices.push_back(lid);
    return lid;
  };

  for (const Cell& c : mesh.cells) {
    if (owner[static_cast<std::size_t>(c.global_id)] == rank) add_cell(c.global_id, true);
  }
  for (const Face& f : mesh.faces) {
    const bool left_owned =
        f.left_cell >= 0 && owner[static_cast<std::size_t>(f.left_cell)] == rank;
    const bool right_owned =
        f.right_cell >= 0 && owner[static_cast<std::size_t>(f.right_cell)] == rank;
    if (left_owned && f.right_cell >= 0 && !right_owned) add_cell(f.right_cell, false);
    if (right_owned && f.left_cell >= 0 && !left_owned) add_cell(f.left_cell, false);
  }

  for (const Face& f : mesh.faces) {
    const bool left_local = local.local_index_by_global_cell.count(f.left_cell) > 0;
    const bool right_local =
        f.right_cell >= 0 && local.local_index_by_global_cell.count(f.right_cell) > 0;
    const bool left_owned =
        f.left_cell >= 0 && owner[static_cast<std::size_t>(f.left_cell)] == rank;
    const bool right_owned =
        f.right_cell >= 0 && owner[static_cast<std::size_t>(f.right_cell)] == rank;
    if (!(left_owned || right_owned)) continue;
    if (!left_local) continue;
    if (f.right_cell >= 0 && !right_local) continue;

    LocalFace lf;
    lf.global_id = f.global_id;
    lf.left = local.local_index_by_global_cell.at(f.left_cell);
    lf.right = f.right_cell >= 0 ? local.local_index_by_global_cell.at(f.right_cell) : -1;
    lf.global_left = f.left_cell;
    lf.global_right = f.right_cell;
    lf.center = f.center;
    lf.normal = f.normal;
    lf.length = f.length;
    lf.tag = f.tag;
    local.faces.push_back(std::move(lf));
  }

  local.neighbor_cells.assign(local.cells.size(), {});
  local.faces_by_cell.assign(local.cells.size(), {});
  for (int iface = 0; iface < static_cast<int>(local.faces.size()); ++iface) {
    const LocalFace& f = local.faces[static_cast<std::size_t>(iface)];
    local.faces_by_cell[static_cast<std::size_t>(f.left)].push_back(iface);
    if (f.right >= 0) {
      local.faces_by_cell[static_cast<std::size_t>(f.right)].push_back(iface);
      local.neighbor_cells[static_cast<std::size_t>(f.left)].push_back(f.right);
      local.neighbor_cells[static_cast<std::size_t>(f.right)].push_back(f.left);
    }
  }
  for (auto& row : local.neighbor_cells) {
    std::sort(row.begin(), row.end());
    row.erase(std::unique(row.begin(), row.end()), row.end());
  }

  return local;
}

HaloPlan build_halo_plan(const LocalMesh& local, const std::vector<int>& owner, MPI_Comm comm) {
  int rank = 0;
  int nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  HaloPlan halo;
  std::vector<int> send_counts(static_cast<std::size_t>(nranks), 0);
  std::vector<int> recv_counts(static_cast<std::size_t>(nranks), 0);
  std::vector<std::vector<int>> requested_ids(static_cast<std::size_t>(nranks));

  for (int i = 0; i < static_cast<int>(local.cells.size()); ++i) {
    const LocalCell& c = local.cells[static_cast<std::size_t>(i)];
    if (!c.owned) {
      const int owner_rank = owner[static_cast<std::size_t>(c.global_id)];
      halo.recv_ghost_local_indices[owner_rank].push_back(i);
      requested_ids[static_cast<std::size_t>(owner_rank)].push_back(c.global_id);
    }
  }
  for (int r = 0; r < nranks; ++r) send_counts[static_cast<std::size_t>(r)] =
      static_cast<int>(requested_ids[static_cast<std::size_t>(r)].size());

  MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT, comm);

  std::vector<int> sdispls(static_cast<std::size_t>(nranks + 1), 0);
  std::vector<int> rdispls(static_cast<std::size_t>(nranks + 1), 0);
  for (int r = 0; r < nranks; ++r) {
    sdispls[static_cast<std::size_t>(r + 1)] =
        sdispls[static_cast<std::size_t>(r)] + send_counts[static_cast<std::size_t>(r)];
    rdispls[static_cast<std::size_t>(r + 1)] =
        rdispls[static_cast<std::size_t>(r)] + recv_counts[static_cast<std::size_t>(r)];
  }
  std::vector<int> send_ids(static_cast<std::size_t>(sdispls.back()));
  for (int r = 0; r < nranks; ++r) {
    std::copy(requested_ids[static_cast<std::size_t>(r)].begin(),
              requested_ids[static_cast<std::size_t>(r)].end(),
              send_ids.begin() + sdispls[static_cast<std::size_t>(r)]);
  }
  std::vector<int> recv_ids(static_cast<std::size_t>(rdispls.back()));
  MPI_Alltoallv(send_ids.data(), send_counts.data(), sdispls.data(), MPI_INT, recv_ids.data(),
                recv_counts.data(), rdispls.data(), MPI_INT, comm);

  for (int r = 0; r < nranks; ++r) {
    for (int j = rdispls[static_cast<std::size_t>(r)]; j < rdispls[static_cast<std::size_t>(r + 1)];
         ++j) {
      const int global_id = recv_ids[static_cast<std::size_t>(j)];
      const auto it = local.local_index_by_global_cell.find(global_id);
      if (it == local.local_index_by_global_cell.end() ||
          !local.cells[static_cast<std::size_t>(it->second)].owned) {
        throw CfdError("halo setup requested non-owned cell from rank " + std::to_string(rank));
      }
      halo.send_owned_local_indices[r].push_back(it->second);
    }
  }

  return halo;
}

PartitionDiagnostics diagnostics_for_rank(const LocalMesh& local, const HaloPlan& halo, int rank) {
  PartitionDiagnostics d;
  d.rank = rank;
  d.num_cells_owned = static_cast<int>(local.owned_local_indices.size());
  d.num_cells_ghost = static_cast<int>(local.cells.size()) - d.num_cells_owned;
  for (const LocalFace& f : local.faces) {
    if (f.right < 0) ++d.num_boundary_faces;
  }
  std::set<int> neighbors;
  for (const auto& [r, ids] : halo.recv_ghost_local_indices) {
    if (!ids.empty()) {
      neighbors.insert(r);
      d.recv_cells[r] = static_cast<int>(ids.size());
    }
  }
  for (const auto& [r, ids] : halo.send_owned_local_indices) {
    if (!ids.empty()) {
      neighbors.insert(r);
      d.send_cells[r] = static_cast<int>(ids.size());
    }
  }
  d.neighbor_ranks.assign(neighbors.begin(), neighbors.end());
  return d;
}

}  // namespace cfd
