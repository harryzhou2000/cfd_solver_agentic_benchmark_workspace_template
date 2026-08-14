#include "mesh/Partition.hpp"

#include <mpi.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

extern "C" {
#include <metis.h>
}

namespace cfds {

namespace {

// ---------------------------------------------------------------------------
// Global mesh broadcast (serial preprocessing support).
// ---------------------------------------------------------------------------
GlobalMesh broadcast_global_mesh_impl(const GlobalMesh& local_mesh, MPI_Comm comm) {
  int rank = 0;
  MPI_Comm_rank(comm, &rank);
  int size = 0;
  if (rank == 0) {
    auto payload = serialize_global_mesh(local_mesh);
    size = static_cast<int>(payload.size());
    MPI_Bcast(&size, 1, MPI_INT, 0, comm);
    MPI_Bcast(payload.data(), size, MPI_CHAR, 0, comm);
    return local_mesh;
  }
  MPI_Bcast(&size, 1, MPI_INT, 0, comm);
  std::vector<char> payload(size);
  MPI_Bcast(payload.data(), size, MPI_CHAR, 0, comm);
  return deserialize_global_mesh(payload.data(), payload.size());
}

}  // namespace

GlobalMesh broadcast_global_mesh(const GlobalMesh& local_mesh, MPI_Comm comm) {
  return broadcast_global_mesh_impl(local_mesh, comm);
}

namespace {

// ---------------------------------------------------------------------------
// Simple byte-buffer serializer for the per-rank mesh payload.
// ---------------------------------------------------------------------------
struct Buffer {
  std::vector<char> data;
  template <typename T>
  void push(const T& v) {
    const char* p = reinterpret_cast<const char*>(&v);
    data.insert(data.end(), p, p + sizeof(T));
  }
  void push_str(const std::string& s) {
    push(static_cast<int>(s.size()));
    data.insert(data.end(), s.begin(), s.end());
  }
};

struct Reader {
  const char* p;
  const char* end;
  template <typename T>
  T get() {
    if (p + sizeof(T) > end) throw std::runtime_error("corrupt partition payload");
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return v;
  }
  std::string get_str() {
    int n = get<int>();
    if (p + n > end) throw std::runtime_error("corrupt partition payload (str)");
    std::string s(p, n);
    p += n;
    return s;
  }
};

void pack_mesh(const DistributedMesh& m, Buffer& buf) {
  buf.push(m.n_owned);
  buf.push(m.n_ghost);
  buf.push(m.n_local);
  for (const auto& c : m.cell_centroid) { buf.push(c[0]); buf.push(c[1]); }
  for (double v : m.cell_volume) buf.push(v);
  for (const auto& n : m.cell_nodes) {
    buf.push(static_cast<int>(n.size()));
    for (int x : n) buf.push(x);
  }
  for (int x : m.owned_global_ids) buf.push(x);
  buf.push(static_cast<int>(m.faces.size()));
  for (const auto& f : m.faces) {
    buf.push(f.cellL); buf.push(f.cellR);
    buf.push(f.length);
    buf.push(f.centroid[0]); buf.push(f.centroid[1]);
    buf.push(f.normal[0]); buf.push(f.normal[1]);
    buf.push(f.bc);
    buf.push(f.bnd_nodes[0]); buf.push(f.bnd_nodes[1]);
    buf.push(f.family_id);
  }
  buf.push(static_cast<int>(m.cell_face_offsets.size()));
  for (int x : m.cell_face_offsets) buf.push(x);
  buf.push(static_cast<int>(m.cell_faces.size()));
  for (int x : m.cell_faces) buf.push(x);
  buf.push(static_cast<int>(m.cell_neighbor_offsets.size()));
  for (int x : m.cell_neighbor_offsets) buf.push(x);
  buf.push(static_cast<int>(m.cell_neighbors.size()));
  for (int x : m.cell_neighbors) buf.push(x);
  buf.push(static_cast<int>(m.family_names.size()));
  for (const auto& s : m.family_names) buf.push_str(s);
  buf.push(static_cast<int>(m.neighbor_ranks.size()));
  for (int r : m.neighbor_ranks) buf.push(r);
  for (const auto& v : m.send_cells) {
    buf.push(static_cast<int>(v.size()));
    for (int x : v) buf.push(x);
  }
  for (const auto& v : m.recv_cells) {
    buf.push(static_cast<int>(v.size()));
    for (int x : v) buf.push(x);
  }
  buf.push(m.num_boundary_faces);
  buf.push(static_cast<int>(m.neighbor_rank_list.size()));
  for (int x : m.neighbor_rank_list) buf.push(x);
  buf.push(m.send_count);
  buf.push(m.recv_count);
}

void unpack_mesh(Reader& r, DistributedMesh& m) {
  m.n_owned = r.get<int>();
  m.n_ghost = r.get<int>();
  m.n_local = r.get<int>();
  m.cell_centroid.resize(m.n_local);
  for (auto& c : m.cell_centroid) { c[0] = r.get<double>(); c[1] = r.get<double>(); }
  m.cell_volume.resize(m.n_local);
  for (auto& v : m.cell_volume) v = r.get<double>();
  m.cell_nodes.resize(m.n_owned);
  for (auto& n : m.cell_nodes) {
    int k = r.get<int>();
    n.resize(k);
    for (auto& x : n) x = r.get<int>();
  }
  m.owned_global_ids.resize(m.n_owned);
  for (auto& x : m.owned_global_ids) x = r.get<int>();
  int nf = r.get<int>();
  m.faces.resize(nf);
  for (auto& f : m.faces) {
    f.cellL = r.get<int>(); f.cellR = r.get<int>();
    f.length = r.get<double>();
    f.centroid[0] = r.get<double>(); f.centroid[1] = r.get<double>();
    f.normal[0] = r.get<double>(); f.normal[1] = r.get<double>();
    f.bc = r.get<int>();
    f.bnd_nodes[0] = r.get<int>(); f.bnd_nodes[1] = r.get<int>();
    f.family_id = r.get<int>();
  }
  int k = r.get<int>();
  m.cell_face_offsets.resize(k);
  for (auto& x : m.cell_face_offsets) x = r.get<int>();
  k = r.get<int>();
  m.cell_faces.resize(k);
  for (auto& x : m.cell_faces) x = r.get<int>();
  k = r.get<int>();
  m.cell_neighbor_offsets.resize(k);
  for (auto& x : m.cell_neighbor_offsets) x = r.get<int>();
  k = r.get<int>();
  m.cell_neighbors.resize(k);
  for (auto& x : m.cell_neighbors) x = r.get<int>();
  k = r.get<int>();
  m.family_names.resize(k);
  for (auto& s : m.family_names) s = r.get_str();
  k = r.get<int>();
  m.neighbor_ranks.resize(k);
  for (auto& x : m.neighbor_ranks) x = r.get<int>();
  m.send_cells.resize(k);
  for (auto& v : m.send_cells) {
    int n = r.get<int>();
    v.resize(n);
    for (auto& x : v) x = r.get<int>();
  }
  m.recv_cells.resize(k);
  for (auto& v : m.recv_cells) {
    int n = r.get<int>();
    v.resize(n);
    for (auto& x : v) x = r.get<int>();
  }
  m.num_boundary_faces = r.get<int>();
  k = r.get<int>();
  m.neighbor_rank_list.resize(k);
  for (auto& x : m.neighbor_rank_list) x = r.get<int>();
  m.send_count = r.get<int>();
  m.recv_count = r.get<int>();
}

// Build the rank-local mesh for one rank from the partition (serial, rank 0).
DistributedMesh build_local_mesh(const GlobalMesh& mesh,
                                 const std::vector<int>& part,
                                 int rank, int nranks,
                                 const CaseConfig& cfg,
                                 int& edge_cut_out) {
  DistributedMesh m;
  m.rank = rank;
  const int nc = static_cast<int>(mesh.cells.size());

  // Owned cells.
  std::vector<int> owned;
  for (int c = 0; c < nc; ++c)
    if (part[c] == rank) owned.push_back(c);
  std::set<int> owned_set(owned.begin(), owned.end());

  // Ghost cells: face-neighbors of owned cells that belong to other ranks.
  std::set<int> ghost_set;
  for (int c : owned) {
    for (int k = mesh.cell_neighbor_offsets[c]; k < mesh.cell_neighbor_offsets[c + 1]; ++k) {
      int nb = mesh.cell_neighbors[k];
      if (nb >= 0 && !owned_set.count(nb)) ghost_set.insert(nb);
    }
  }
  std::vector<int> ghosts(ghost_set.begin(), ghost_set.end());

  m.n_owned = static_cast<int>(owned.size());
  m.n_ghost = static_cast<int>(ghosts.size());
  m.n_local = m.n_owned + m.n_ghost;

  // Global cell id -> local id (owned then ghost).
  std::unordered_map<int, int> g2l;
  g2l.reserve(m.n_local * 2);
  for (int i = 0; i < m.n_owned; ++i) g2l[owned[i]] = i;
  for (int i = 0; i < m.n_ghost; ++i) g2l[ghosts[i]] = m.n_owned + i;

  // Cell geometry.
  m.cell_centroid.resize(m.n_local);
  m.cell_volume.resize(m.n_local);
  m.cell_nodes.resize(m.n_owned);
  for (int i = 0; i < m.n_owned; ++i) {
    const Cell& c = mesh.cells[owned[i]];
    m.cell_centroid[i] = c.centroid;
    m.cell_volume[i] = c.volume;
    m.cell_nodes[i] = c.nodes;
    m.owned_global_ids.push_back(owned[i]);
  }
  for (int i = 0; i < m.n_ghost; ++i) {
    const Cell& c = mesh.cells[ghosts[i]];
    m.cell_centroid[m.n_owned + i] = c.centroid;
    m.cell_volume[m.n_owned + i] = c.volume;
  }

  // Boundary family name -> id and BC type.
  std::map<std::string, int> fam_id;
  auto family_id_of = [&](const std::string& name) {
    auto it = fam_id.find(name);
    if (it != fam_id.end()) return it->second;
    int id = static_cast<int>(m.family_names.size());
    m.family_names.push_back(name);
    fam_id.emplace(name, id);
    return id;
  };

  // Faces: keep faces touching at least one owned cell.
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const Face& f = mesh.faces[fi];
    const bool owned_l = owned_set.count(f.cellL) > 0;
    const bool owned_r = f.cellR >= 0 && owned_set.count(f.cellR) > 0;
    if (!owned_l && !owned_r) continue;
    LocalFace lf;
    lf.cellL = g2l.at(f.cellL);
    lf.length = f.length;
    lf.centroid = f.centroid;
    lf.normal = f.normal;
    if (f.cellR >= 0) {
      lf.cellR = g2l.at(f.cellR);
      lf.bc = -1;
    } else {
      lf.cellR = -1;
      // Boundary: resolve BC from the case mapping by family name.
      lf.bnd_nodes[0] = f.n0;
      lf.bnd_nodes[1] = f.n1;
      BcType bt;
      if (bc_from_string(cfg.boundary_conditions.at(f.family), bt)) {
        lf.bc = static_cast<int>(bt);
      } else {
        throw std::runtime_error("unmapped boundary family: " + f.family);
      }
      lf.family_id = family_id_of(f.family);
      ++m.num_boundary_faces;
    }
    m.faces.push_back(lf);
  }

  // CSR adjacency.
  m.cell_face_offsets.assign(m.n_local + 1, 0);
  for (const auto& f : m.faces) {
    m.cell_face_offsets[f.cellL + 1]++;
    if (f.cellR >= 0) m.cell_face_offsets[f.cellR + 1]++;
  }
  for (int i = 0; i < m.n_local; ++i) m.cell_face_offsets[i + 1] += m.cell_face_offsets[i];
  m.cell_faces.assign(m.cell_face_offsets[m.n_local], -1);
  std::vector<int> fill = m.cell_face_offsets;
  for (size_t i = 0; i < m.faces.size(); ++i) {
    const auto& f = m.faces[i];
    m.cell_faces[fill[f.cellL]++] = static_cast<int>(i);
    if (f.cellR >= 0) m.cell_faces[fill[f.cellR]++] = static_cast<int>(i);
  }
  m.cell_neighbor_offsets.assign(m.n_local + 1, 0);
  for (const auto& f : m.faces) {
    m.cell_neighbor_offsets[f.cellL + 1]++;
    if (f.cellR >= 0) m.cell_neighbor_offsets[f.cellR + 1]++;
  }
  for (int i = 0; i < m.n_local; ++i) m.cell_neighbor_offsets[i + 1] += m.cell_neighbor_offsets[i];
  m.cell_neighbors.assign(m.cell_neighbor_offsets[m.n_local], -1);
  fill = m.cell_neighbor_offsets;
  for (const auto& f : m.faces) {
    m.cell_neighbors[fill[f.cellL]++] = f.cellR;
    if (f.cellR >= 0) m.cell_neighbors[fill[f.cellR]++] = f.cellL;
  }

  // Halo exchange bookkeeping: ghosts grouped by owning rank; send lists from
  // the owning rank's perspective.
  std::map<int, std::vector<int>> ghosts_by_owner;
  for (int i = 0; i < m.n_ghost; ++i)
    ghosts_by_owner[part[ghosts[i]]].push_back(m.n_owned + i);
  for (const auto& [r, cells] : ghosts_by_owner) {
    m.neighbor_ranks.push_back(r);
    m.recv_cells.push_back(cells);
    m.neighbor_rank_list.push_back(r);
  }
  m.recv_count = m.n_ghost;

  // Send lists: cells this rank owns that other ranks need as ghosts.
  // Computed on rank 0 for all ranks (needs ghost lists of other ranks).
  m.send_cells.resize(m.neighbor_ranks.size());
  // Filled after distribution below (needs all ranks' recv lists).

  // METIS edge cut for this partition (computed once, globally).
  (void)edge_cut_out;
  return m;
}

}  // namespace

int distribute_mesh(const GlobalMesh& global, const CaseConfig& cfg,
                    DistributedMesh& local, MPI_Comm comm) {
  int rank = 0, nranks = 0;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  int edge_cut = 0;
  std::vector<int> part;

  if (rank == 0) {
    const int nc = static_cast<int>(global.cells.size());
    // METIS graph: cell adjacency.
    std::vector<idx_t> xadj, adjncy;
    xadj.reserve(nc + 1);
    xadj.push_back(0);
    for (int c = 0; c < nc; ++c) {
      for (int k = global.cell_neighbor_offsets[c]; k < global.cell_neighbor_offsets[c + 1]; ++k) {
        int nb = global.cell_neighbors[k];
        if (nb >= 0) adjncy.push_back(nb);
      }
      xadj.push_back(static_cast<idx_t>(adjncy.size()));
    }
    part.resize(nc);
    if (nranks == 1) {
      std::fill(part.begin(), part.end(), 0);
      edge_cut = 0;
    } else {
      idx_t nv = nc, ncon = 1, nparts = nranks, objval = 0;
      idx_t options[METIS_NOPTIONS];
      METIS_SetDefaultOptions(options);
      options[METIS_OPTION_NUMBERING] = 0;
      options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
      options[METIS_OPTION_CONTIG] = 1;
      options[METIS_OPTION_SEED] = 42;
      // Note: this METIS build uses the legacy 13-argument order with ncon as
      // the second argument (see metis.h), matching DNDSR usage.
      int rc = METIS_PartGraphKway(&nv, &ncon, xadj.data(), adjncy.data(),
                                   nullptr, nullptr, nullptr, &nparts, nullptr,
                                   nullptr, options, &objval, part.data());
      if (rc != METIS_OK)
        throw std::runtime_error("METIS_PartGraphKway failed");
      edge_cut = static_cast<int>(objval);
    }
  }

  // Broadcast the partition.
  int nc = static_cast<int>(global.cells.size());
  MPI_Bcast(&nc, 1, MPI_INT, 0, comm);
  MPI_Bcast(&edge_cut, 1, MPI_INT, 0, comm);
  if (rank != 0) part.resize(nc);
  MPI_Bcast(part.data(), nc, MPI_INT, 0, comm);

  // Every rank builds its local mesh from the broadcast partition.
  DistributedMesh m = build_local_mesh(global, part, rank, nranks, cfg, edge_cut);

  // Send lists: for each of my owned cells, find which ranks list it as a
  // ghost. Computed locally from the (broadcast) partition. The graph is
  // symmetric, so send and recv neighbors coincide.
  std::map<int, std::vector<int>> send_by_neighbor;
  {
    std::vector<int> owned_me;
    for (int c = 0; c < nc; ++c) if (part[c] == rank) owned_me.push_back(c);
    std::set<int> owned_me_set(owned_me.begin(), owned_me.end());
    std::set<int> ghost_me;
    for (int c : owned_me) {
      for (int k = global.cell_neighbor_offsets[c]; k < global.cell_neighbor_offsets[c + 1]; ++k) {
        int nb = global.cell_neighbors[k];
        if (nb >= 0 && !owned_me_set.count(nb)) ghost_me.insert(nb);
      }
    }
    std::vector<int> ghosts_me(ghost_me.begin(), ghost_me.end());
    // Local id of each ghost on this rank.
    std::unordered_map<int, int> ghost_local;
    for (size_t i = 0; i < ghosts_me.size(); ++i) ghost_local[ghosts_me[i]] = static_cast<int>(i) + m.n_owned;
    // Local id of each owned cell.
    std::unordered_map<int, int> owned_local;
    for (size_t i = 0; i < owned_me.size(); ++i) owned_local[owned_me[i]] = static_cast<int>(i);
    // For every other rank q, its ghost set must include some of my owned
    // cells; find them by scanning q's ghost set.
    for (int q = 0; q < nranks; ++q) {
      if (q == rank) continue;
      std::vector<int> owned_q;
      for (int c = 0; c < nc; ++c) if (part[c] == q) owned_q.push_back(c);
      std::set<int> owned_q_set(owned_q.begin(), owned_q.end());
      std::set<int> ghost_q;
      for (int c : owned_q) {
        for (int k = global.cell_neighbor_offsets[c]; k < global.cell_neighbor_offsets[c + 1]; ++k) {
          int nb = global.cell_neighbors[k];
          if (nb >= 0 && !owned_q_set.count(nb)) ghost_q.insert(nb);
        }
      }
      std::vector<int> to_send;
      for (int g : ghost_q) {
        auto it = owned_local.find(g);
        if (it != owned_local.end()) to_send.push_back(it->second);
      }
      if (!to_send.empty()) send_by_neighbor[q] = std::move(to_send);
    }
    // Also record neighbor ranks consistent with recv lists.
  }

  // Rebuild neighbor_ranks/send/recv consistently.
  std::vector<int> new_neighbors;
  std::vector<std::vector<int>> new_send, new_recv;
  for (int q : m.neighbor_ranks) {
    new_neighbors.push_back(q);
    auto sit = send_by_neighbor.find(q);
    new_send.push_back(sit != send_by_neighbor.end() ? sit->second : std::vector<int>());
    auto rit = std::find(m.neighbor_ranks.begin(), m.neighbor_ranks.end(), q);
    new_recv.push_back(m.recv_cells[std::distance(m.neighbor_ranks.begin(), rit)]);
  }
  // Add any send-only neighbors (should not happen for a symmetric graph).
  for (const auto& [q, cells] : send_by_neighbor) {
    if (std::find(new_neighbors.begin(), new_neighbors.end(), q) == new_neighbors.end()) {
      new_neighbors.push_back(q);
      new_send.push_back(cells);
      new_recv.push_back({});
    }
  }
  m.neighbor_ranks = std::move(new_neighbors);
  m.send_cells = std::move(new_send);
  m.recv_cells = std::move(new_recv);
  m.neighbor_rank_list = m.neighbor_ranks;
  m.send_count = 0;
  for (const auto& v : m.send_cells) m.send_count += static_cast<int>(v.size());

  local = std::move(m);
  return edge_cut;
}

PartitionSummary partition_summary(const DistributedMesh& local, MPI_Comm comm) {
  int rank = 0, nranks = 0;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);
  PartitionSummary s;
  std::vector<int> owned(nranks), ghosts(nranks), bfaces(nranks);
  MPI_Allgather(&local.n_owned, 1, MPI_INT, owned.data(), 1, MPI_INT, comm);
  MPI_Allgather(&local.n_ghost, 1, MPI_INT, ghosts.data(), 1, MPI_INT, comm);
  MPI_Allgather(&local.num_boundary_faces, 1, MPI_INT, bfaces.data(), 1, MPI_INT, comm);
  s.min_owned = *std::min_element(owned.begin(), owned.end());
  s.max_owned = *std::max_element(owned.begin(), owned.end());
  double sum = 0.0;
  for (int v : owned) sum += v;
  s.mean_owned = sum / nranks;
  s.load_balance = s.mean_owned / s.max_owned;
  s.total_boundary_faces = 0;
  for (int v : bfaces) s.total_boundary_faces += v;
  (void)rank;
  return s;
}

}  // namespace cfds
