#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace cfd {
namespace {

struct PartitionFileHeader {
  uint32_t nranks = 0;
  uint32_t rank = 0;
  uint64_t n_owned = 0;
  uint64_t n_ghost = 0;
  uint64_t n_faces = 0;
  uint64_t num_cells_global = 0;
  uint64_t num_faces_global = 0;
};

}  // namespace

Partition partition_cells(const GlobalMesh& m, int nranks) {
  const int ncells = m.num_cells_global();
  Partition p;
  p.nranks = nranks;
  p.cell_to_rank.assign(ncells, 0);
  p.owned.resize(nranks);
  p.ghosts.resize(nranks);
  p.ghost_owner.resize(nranks);

  // Build cell adjacency graph (CSR) from faces.
  std::vector<std::vector<int>> adj(ncells);
  for (const auto& f : m.faces) {
    if (f.c0 >= 0 && f.c1 >= 0) {
      adj[f.c0].push_back(f.c1);
      adj[f.c1].push_back(f.c0);
    }
  }

  if (nranks > 1) {
    idx_t nvtxs = ncells;
    std::vector<idx_t> xadj(ncells + 1), adjncy;
    xadj[0] = 0;
    for (int i = 0; i < ncells; ++i) {
      xadj[i + 1] = xadj[i] + static_cast<idx_t>(adj[i].size());
      for (int j : adj[i]) adjncy.push_back(j);
    }
    std::vector<idx_t> part(ncells);
    idx_t ncon = 1;
    idx_t nparts = nranks;
    idx_t objval = 0;
    std::vector<idx_t> options(METIS_NOPTIONS);
    METIS_SetDefaultOptions(options.data());
    options[METIS_OPTION_NUMBERING] = 0;
    options[METIS_OPTION_SEED] = 12345;
    options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
    int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                 nullptr, nullptr, nullptr, &nparts, nullptr,
                                 nullptr, options.data(), &objval, part.data());
    if (rc != METIS_OK)
      throw std::runtime_error("METIS_PartGraphKway failed");
    p.edge_cut = static_cast<int>(objval);
    for (int i = 0; i < ncells; ++i) p.cell_to_rank[i] = part[i];
  } else {
    p.edge_cut = 0;
  }

  for (int i = 0; i < ncells; ++i) p.owned[p.cell_to_rank[i]].push_back(i);
  for (int r = 0; r < nranks; ++r) {
    std::set<int> ghost_set;
    std::set<int> owner_set;
    for (int ci : p.owned[r]) {
      for (int nj : adj[ci]) {
        if (p.cell_to_rank[nj] != r) {
          ghost_set.insert(nj);
          owner_set.insert(p.cell_to_rank[nj]);
        }
      }
    }
    p.ghosts[r].assign(ghost_set.begin(), ghost_set.end());
    p.ghost_owner[r].reserve(p.ghosts[r].size());
    for (int g : p.ghosts[r]) p.ghost_owner[r].push_back(p.cell_to_rank[g]);
    (void)owner_set;
  }
  return p;
}

void write_partition_files(const GlobalMesh& m, const Partition& p,
                           int nranks, const std::string& outdir) {
  const std::string pdir = outdir + "/partition";
  std::error_code ec;
  std::filesystem::create_directories(pdir, ec);
  if (ec) throw std::runtime_error("cannot create partition directory " + pdir);

  // Per-cell face lists.
  std::vector<std::vector<int>> cell_faces(m.num_cells_global());
  for (size_t fi = 0; fi < m.faces.size(); ++fi) {
    const auto& f = m.faces[fi];
    if (f.c0 >= 0) cell_faces[f.c0].push_back(static_cast<int>(fi));
    if (f.c1 >= 0) cell_faces[f.c1].push_back(static_cast<int>(fi));
  }

  for (int r = 0; r < nranks; ++r) {
    const std::string path = pdir + "/rank_" + std::to_string(r) + ".bin";
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);

    // Determine local faces: global faces with at least one owned cell.
    std::vector<int> local_faces;
    std::set<int> owned_set(p.owned[r].begin(), p.owned[r].end());
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
      const auto& f = m.faces[fi];
      if (owned_set.count(f.c0) || (f.c1 >= 0 && owned_set.count(f.c1)))
        local_faces.push_back(static_cast<int>(fi));
    }
    std::map<int, int> face_local_index;  // global face id -> local index
    for (size_t k = 0; k < local_faces.size(); ++k)
      face_local_index[local_faces[k]] = static_cast<int>(k);

    PartitionFileHeader h;
    h.nranks = nranks;
    h.rank = r;
    h.n_owned = p.owned[r].size();
    h.n_ghost = p.ghosts[r].size();
    h.n_faces = local_faces.size();
    h.num_cells_global = m.num_cells_global();
    h.num_faces_global = m.num_faces_global();
    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    // Owned cells.
    for (int gid : p.owned[r]) {
      const auto& cell = m.cells[gid];
      int32_t id = gid;
      double geo[3] = {cell.vol, cell.cx, cell.cy};
      out.write(reinterpret_cast<const char*>(&id), 4);
      out.write(reinterpret_cast<const char*>(geo), 24);
    }
    // Ghost cells.
    for (size_t k = 0; k < p.ghosts[r].size(); ++k) {
      const int gid = p.ghosts[r][k];
      const auto& cell = m.cells[gid];
      int32_t data[2] = {gid, p.ghost_owner[r][k]};
      double geo[3] = {cell.vol, cell.cx, cell.cy};
      out.write(reinterpret_cast<const char*>(data), 8);
      out.write(reinterpret_cast<const char*>(geo), 24);
    }
    // Local faces.
    for (int gid : local_faces) {
      const auto& f = m.faces[gid];
      int32_t data[6] = {gid, f.n0, f.n1, f.c0, f.c1,
                         static_cast<int32_t>(f.bc)};
      out.write(reinterpret_cast<const char*>(data), 24);
      uint32_t nlen = f.bc_name.size();
      out.write(reinterpret_cast<const char*>(&nlen), 4);
      out.write(f.bc_name.data(), nlen);
      double geo[5] = {f.area, f.nx, f.ny, f.fx, f.fy};
      out.write(reinterpret_cast<const char*>(geo), 40);
    }
    // Per-owned-cell face lists (face local indices).
    for (int gid : p.owned[r]) {
      const auto& fl = cell_faces[gid];
      uint32_t nf = fl.size();
      out.write(reinterpret_cast<const char*>(&nf), 4);
      for (int fi : fl) {
        int32_t li = face_local_index[fi];
        out.write(reinterpret_cast<const char*>(&li), 4);
      }
    }
    // Halo structure.
    std::map<int, std::set<int>> send_sets, recv_sets;
    for (int gid : local_faces) {
      const auto& f = m.faces[gid];
      if (f.c1 < 0) continue;
      const int rc0 = p.cell_to_rank[f.c0];
      const int rc1 = p.cell_to_rank[f.c1];
      if (rc0 == rc1) continue;
      if (rc0 == r) {
        send_sets[rc1].insert(f.c0);
        recv_sets[rc1].insert(f.c1);
      } else if (rc1 == r) {
        send_sets[rc0].insert(f.c1);
        recv_sets[rc0].insert(f.c0);
      }
    }
    uint32_t nneigh = send_sets.size();
    out.write(reinterpret_cast<const char*>(&nneigh), 4);
    for (const auto& kv : send_sets) {
      int32_t rank = kv.first;
      out.write(reinterpret_cast<const char*>(&rank), 4);
      uint64_t ns = kv.second.size();
      out.write(reinterpret_cast<const char*>(&ns), 8);
      for (int id : kv.second) {
        int32_t v = id;
        out.write(reinterpret_cast<const char*>(&v), 4);
      }
      // recv side: global ids sorted; local ghost index resolved below.
      std::vector<int> recv_ids(recv_sets[kv.first].begin(),
                                recv_sets[kv.first].end());
      uint64_t nr = recv_ids.size();
      out.write(reinterpret_cast<const char*>(&nr), 8);
      for (int id : recv_ids) {
        int32_t v = id;
        out.write(reinterpret_cast<const char*>(&v), 4);
      }
      // local ghost ids: map global -> ghost local index (owned then ghosts)
      std::map<int, int> ghost_local;
      for (size_t k = 0; k < p.ghosts[r].size(); ++k)
        ghost_local[p.ghosts[r][k]] = static_cast<int>(p.owned[r].size()) +
                                      static_cast<int>(k);
      for (int id : recv_ids) {
        int32_t v = ghost_local[id];
        out.write(reinterpret_cast<const char*>(&v), 4);
      }
    }
  }

  write_global_mesh_bin(m, pdir + "/global_mesh.bin");
}

}  // namespace cfd
