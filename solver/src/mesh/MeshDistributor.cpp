#include "mesh/MeshDistributor.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "core/Exception.hpp"
#include "core/Log.hpp"

namespace cfd {
namespace {

constexpr int kTagInts = 5101;
constexpr int kTagReals = 5102;
constexpr int kTagSizes = 5103;
constexpr int kTagHaloGids = 5104;

struct PackedMesh {
  std::vector<long long> ints;
  std::vector<double> reals;
};

// Header layout of PackedMesh::ints.
enum : int {
  kHNumOwned = 0,
  kHNumGhost,
  kHNumNodes,
  kHNumBoundaryEdges,
  kHCellNodeTotal,
  kHGlobalCells,
  kHGlobalFaces,
  kHGlobalBoundaryFaces,
  kHEdgeCut,
  kHNumPatches,
  kHeaderSize
};

PackedMesh packRank(const GlobalMesh& mesh, const PartitionResult& pr, int r) {
  const std::vector<Index>& owned = pr.ordered_cells[r];

  // Ghost cells: face neighbours of owned cells that belong to another rank.
  std::vector<Index> ghosts;
  {
    std::vector<std::pair<long long, Index>> tmp;   // (owner, gid) for stable order
    std::unordered_map<Index, char> seen;
    seen.reserve(owned.size());
    for (Index c : owned) seen[c] = 1;
    for (Index c : owned) {
      for (Index k = mesh.adj_offset[c]; k < mesh.adj_offset[c + 1]; ++k) {
        const Index d = mesh.adj_cells[k];
        if (pr.part[d] == r) continue;
        auto it = seen.find(d);
        if (it != seen.end()) continue;
        seen[d] = 1;
        tmp.emplace_back(pr.part[d], d);
      }
    }
    std::sort(tmp.begin(), tmp.end());
    ghosts.reserve(tmp.size());
    for (const auto& t : tmp) ghosts.push_back(t.second);
  }

  const std::size_t n_owned = owned.size();
  const std::size_t n_ghost = ghosts.size();
  const std::size_t n_total = n_owned + n_ghost;

  std::vector<Index> local_cells;
  local_cells.reserve(n_total);
  local_cells.insert(local_cells.end(), owned.begin(), owned.end());
  local_cells.insert(local_cells.end(), ghosts.begin(), ghosts.end());

  // Local node numbering: first appearance order over the local cell list.
  std::unordered_map<Index, Index> local_node;
  local_node.reserve(n_total * 4);
  std::vector<Index> node_gid;
  std::vector<Index> cell_nodes;
  std::vector<Index> cell_node_offset(n_total + 1, 0);
  for (std::size_t i = 0; i < n_total; ++i) {
    const Index c = local_cells[i];
    const int n = mesh.cellSize(c);
    const Index* nodes = mesh.cellNodePtr(c);
    for (int k = 0; k < n; ++k) {
      auto it = local_node.find(nodes[k]);
      Index ln;
      if (it == local_node.end()) {
        ln = static_cast<Index>(node_gid.size());
        local_node.emplace(nodes[k], ln);
        node_gid.push_back(nodes[k]);
      } else {
        ln = it->second;
      }
      cell_nodes.push_back(ln);
    }
    cell_node_offset[i + 1] = static_cast<Index>(cell_nodes.size());
  }

  // Boundary edges of owned cells, in local node numbering.
  std::vector<long long> bedges;
  {
    std::unordered_map<Index, char> is_owned;
    is_owned.reserve(n_owned * 2);
    for (Index c : owned) is_owned[c] = 1;
    for (Index f = 0; f < mesh.numFaces(); ++f) {
      if (mesh.face_patch[f] < 0) continue;
      if (!is_owned.count(mesh.face_cell_l[f])) continue;
      bedges.push_back(local_node.at(mesh.face_n0[f]));
      bedges.push_back(local_node.at(mesh.face_n1[f]));
      bedges.push_back(mesh.face_patch[f]);
    }
  }

  PackedMesh pm;
  pm.ints.resize(kHeaderSize);
  pm.ints[kHNumOwned] = static_cast<long long>(n_owned);
  pm.ints[kHNumGhost] = static_cast<long long>(n_ghost);
  pm.ints[kHNumNodes] = static_cast<long long>(node_gid.size());
  pm.ints[kHNumBoundaryEdges] = static_cast<long long>(bedges.size() / 3);
  pm.ints[kHCellNodeTotal] = static_cast<long long>(cell_nodes.size());
  pm.ints[kHGlobalCells] = static_cast<long long>(mesh.numCells());
  pm.ints[kHGlobalFaces] = static_cast<long long>(mesh.numFaces());
  {
    long long nbf = 0;
    for (const auto& p : mesh.patches) nbf += p.num_faces;
    pm.ints[kHGlobalBoundaryFaces] = nbf;
  }
  pm.ints[kHEdgeCut] = static_cast<long long>(pr.edge_cut);
  pm.ints[kHNumPatches] = static_cast<long long>(mesh.patches.size());

  pm.ints.reserve(pm.ints.size() + n_total * 2 + n_total + 1 + cell_nodes.size() +
                  node_gid.size() + bedges.size());
  for (std::size_t i = 0; i < n_total; ++i) pm.ints.push_back(local_cells[i]);      // cell gid
  for (std::size_t i = 0; i < n_total; ++i) pm.ints.push_back(pr.part[local_cells[i]]);
  for (std::size_t i = 0; i <= n_total; ++i) pm.ints.push_back(cell_node_offset[i]);
  for (Index v : cell_nodes) pm.ints.push_back(v);
  for (Index v : node_gid) pm.ints.push_back(v);
  for (long long v : bedges) pm.ints.push_back(v);

  pm.reals.reserve(node_gid.size() * 2);
  for (Index g : node_gid) pm.reals.push_back(mesh.x[g]);
  for (Index g : node_gid) pm.reals.push_back(mesh.y[g]);
  return pm;
}

void unpack(const PackedMesh& pm, LocalMesh& lm) {
  const long long* h = pm.ints.data();
  lm.num_owned = static_cast<Index>(h[kHNumOwned]);
  lm.num_ghost = static_cast<Index>(h[kHNumGhost]);
  const Index nn = static_cast<Index>(h[kHNumNodes]);
  const Index nbe = static_cast<Index>(h[kHNumBoundaryEdges]);
  const Index ncn = static_cast<Index>(h[kHCellNodeTotal]);
  lm.global_num_cells = h[kHGlobalCells];
  lm.global_num_faces = h[kHGlobalFaces];
  lm.global_num_boundary_faces = h[kHGlobalBoundaryFaces];
  lm.partition_edge_cut = h[kHEdgeCut];

  const Index nt = lm.numTotalCells();
  std::size_t p = kHeaderSize;
  lm.cell_gid.assign(pm.ints.begin() + p, pm.ints.begin() + p + nt); p += nt;
  lm.cell_owner.resize(nt);
  for (Index i = 0; i < nt; ++i) lm.cell_owner[i] = static_cast<int>(pm.ints[p + i]);
  p += nt;
  lm.cell_node_offset.resize(nt + 1);
  for (Index i = 0; i <= nt; ++i) lm.cell_node_offset[i] = static_cast<Index>(pm.ints[p + i]);
  p += nt + 1;
  lm.cell_nodes.resize(ncn);
  for (Index i = 0; i < ncn; ++i) lm.cell_nodes[i] = static_cast<Index>(pm.ints[p + i]);
  p += ncn;
  lm.node_gid.assign(pm.ints.begin() + p, pm.ints.begin() + p + nn); p += nn;
  std::vector<std::array<Index, 3>> bedges(nbe);
  for (Index i = 0; i < nbe; ++i) {
    bedges[i] = {static_cast<Index>(pm.ints[p + 3 * i]),
                 static_cast<Index>(pm.ints[p + 3 * i + 1]),
                 static_cast<Index>(pm.ints[p + 3 * i + 2])};
  }
  p += 3 * static_cast<std::size_t>(nbe);

  lm.x.assign(pm.reals.begin(), pm.reals.begin() + nn);
  lm.y.assign(pm.reals.begin() + nn, pm.reals.begin() + 2 * nn);

  lm.buildTopologyAndGeometry(bedges);
}

}  // namespace

void buildHaloDescriptors(LocalMesh& lm, MPI_Comm comm) {
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  // Receive side: ghosts grouped by owner (they arrive already sorted by
  // (owner, global id) from the packer, but do not rely on it).
  std::map<int, std::vector<Index>> by_owner;
  for (Index c = lm.num_owned; c < lm.numTotalCells(); ++c) {
    by_owner[lm.cell_owner[c]].push_back(c);
  }
  lm.neighbors.clear();
  lm.recv_cells.clear();
  for (auto& kv : by_owner) {
    std::sort(kv.second.begin(), kv.second.end(),
              [&](Index a, Index b) { return lm.cell_gid[a] < lm.cell_gid[b]; });
    lm.neighbors.push_back(kv.first);
    lm.recv_cells.push_back(kv.second);
  }

  // Tell every rank how many cells we want from it (setup-time only collective
  // on scalar counts, never on state).
  std::vector<int> want(size, 0), give(size, 0);
  for (std::size_t k = 0; k < lm.neighbors.size(); ++k)
    want[lm.neighbors[k]] = static_cast<int>(lm.recv_cells[k].size());
  MPI_Alltoall(want.data(), 1, MPI_INT, give.data(), 1, MPI_INT, comm);

  // Exchange the requested global ids.
  std::vector<int> senders;
  for (int r = 0; r < size; ++r) if (give[r] > 0) senders.push_back(r);
  CFD_CHECK(senders.size() == lm.neighbors.size(),
            "halo neighbour sets are not symmetric (rank " << rank << ")");

  std::vector<std::vector<long long>> req_out(lm.neighbors.size());
  std::vector<std::vector<long long>> req_in(senders.size());
  std::vector<MPI_Request> reqs;
  for (std::size_t k = 0; k < senders.size(); ++k) {
    req_in[k].resize(give[senders[k]]);
    reqs.emplace_back();
    MPI_Irecv(req_in[k].data(), give[senders[k]], MPI_LONG_LONG, senders[k], kTagHaloGids, comm,
              &reqs.back());
  }
  for (std::size_t k = 0; k < lm.neighbors.size(); ++k) {
    req_out[k].reserve(lm.recv_cells[k].size());
    for (Index c : lm.recv_cells[k]) req_out[k].push_back(lm.cell_gid[c]);
    reqs.emplace_back();
    MPI_Isend(req_out[k].data(), static_cast<int>(req_out[k].size()), MPI_LONG_LONG,
              lm.neighbors[k], kTagHaloGids, comm, &reqs.back());
  }
  MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);

  std::unordered_map<GlobalIndex, Index> local_of_gid;
  local_of_gid.reserve(lm.num_owned * 2);
  for (Index c = 0; c < lm.num_owned; ++c) local_of_gid[lm.cell_gid[c]] = c;

  lm.send_cells.assign(lm.neighbors.size(), {});
  for (std::size_t k = 0; k < senders.size(); ++k) {
    auto it = std::find(lm.neighbors.begin(), lm.neighbors.end(), senders[k]);
    CFD_CHECK(it != lm.neighbors.end(), "halo neighbour mismatch on rank " << rank);
    const std::size_t slot = static_cast<std::size_t>(it - lm.neighbors.begin());
    lm.send_cells[slot].reserve(req_in[k].size());
    for (long long g : req_in[k]) {
      auto lit = local_of_gid.find(g);
      CFD_CHECK(lit != local_of_gid.end(),
                "rank " << rank << " was asked for global cell " << g << " which it does not own");
      lm.send_cells[slot].push_back(lit->second);
    }
  }
}

void distributeMesh(GlobalMesh& global, const PartitionResult& pr, MPI_Comm comm, LocalMesh& lm) {
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  // Patch names are metadata: broadcast once as a packed string.
  std::string patch_blob;
  if (rank == 0) {
    for (const auto& p : global.patches) { patch_blob += p.name; patch_blob.push_back('\0'); }
  }
  int blob_size = static_cast<int>(patch_blob.size());
  MPI_Bcast(&blob_size, 1, MPI_INT, 0, comm);
  patch_blob.resize(blob_size);
  MPI_Bcast(patch_blob.data(), blob_size, MPI_CHAR, 0, comm);
  lm.patch_names.clear();
  for (int i = 0; i < blob_size;) {
    std::string s(patch_blob.c_str() + i);
    lm.patch_names.push_back(s);
    i += static_cast<int>(s.size()) + 1;
  }

  PackedMesh mine;
  if (rank == 0) {
    for (int r = size - 1; r >= 0; --r) {
      PackedMesh pm = packRank(global, pr, r);
      if (r == 0) {
        mine = std::move(pm);
      } else {
        long long sizes[2] = {static_cast<long long>(pm.ints.size()),
                              static_cast<long long>(pm.reals.size())};
        MPI_Send(sizes, 2, MPI_LONG_LONG, r, kTagSizes, comm);
        MPI_Send(pm.ints.data(), static_cast<int>(pm.ints.size()), MPI_LONG_LONG, r, kTagInts, comm);
        MPI_Send(pm.reals.data(), static_cast<int>(pm.reals.size()), MPI_DOUBLE, r, kTagReals, comm);
      }
    }
  } else {
    long long sizes[2] = {0, 0};
    MPI_Recv(sizes, 2, MPI_LONG_LONG, 0, kTagSizes, comm, MPI_STATUS_IGNORE);
    mine.ints.resize(static_cast<std::size_t>(sizes[0]));
    mine.reals.resize(static_cast<std::size_t>(sizes[1]));
    MPI_Recv(mine.ints.data(), static_cast<int>(sizes[0]), MPI_LONG_LONG, 0, kTagInts, comm,
             MPI_STATUS_IGNORE);
    MPI_Recv(mine.reals.data(), static_cast<int>(sizes[1]), MPI_DOUBLE, 0, kTagReals, comm,
             MPI_STATUS_IGNORE);
  }

  // The global mesh is no longer needed anywhere: release it before the solver
  // allocates its state so that no rank holds a replicated mesh.
  global.release();

  unpack(mine, lm);
  PackedMesh().ints.swap(mine.ints);
  PackedMesh().reals.swap(mine.reals);

  buildHaloDescriptors(lm, comm);
}

std::vector<PartitionDiagnostics> gatherPartitionDiagnostics(const LocalMesh& lm, MPI_Comm comm) {
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  // Fixed-size scalar record + variable-length neighbour list.
  std::vector<long long> rec;
  rec.push_back(lm.num_owned);
  rec.push_back(lm.num_ghost);
  rec.push_back(lm.numBoundaryFaces());
  rec.push_back(static_cast<long long>(lm.neighbors.size()));
  long long ns = 0, nr = 0;
  for (const auto& v : lm.send_cells) ns += static_cast<long long>(v.size());
  for (const auto& v : lm.recv_cells) nr += static_cast<long long>(v.size());
  rec.push_back(ns);
  rec.push_back(nr);
  for (int n : lm.neighbors) rec.push_back(n);

  int my_len = static_cast<int>(rec.size());
  std::vector<int> lens(size, 0);
  MPI_Gather(&my_len, 1, MPI_INT, lens.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(size, 0);
  int total = 0;
  if (rank == 0) {
    for (int r = 0; r < size; ++r) { displs[r] = total; total += lens[r]; }
  }
  std::vector<long long> all(static_cast<std::size_t>(std::max(total, 1)));
  MPI_Gatherv(rec.data(), my_len, MPI_LONG_LONG, all.data(), lens.data(), displs.data(),
              MPI_LONG_LONG, 0, comm);

  std::vector<PartitionDiagnostics> out;
  if (rank == 0) {
    out.resize(size);
    for (int r = 0; r < size; ++r) {
      const long long* d = all.data() + displs[r];
      PartitionDiagnostics& pd = out[r];
      pd.rank = r;
      pd.num_cells_owned = static_cast<Index>(d[0]);
      pd.num_cells_ghost = static_cast<Index>(d[1]);
      pd.num_boundary_faces = static_cast<Index>(d[2]);
      pd.num_neighbor_ranks = static_cast<int>(d[3]);
      pd.send_cells = static_cast<Index>(d[4]);
      pd.recv_cells = static_cast<Index>(d[5]);
      for (int k = 0; k < pd.num_neighbor_ranks; ++k)
        pd.neighbor_ranks.push_back(static_cast<int>(d[6 + k]));
    }
  }
  return out;
}

}  // namespace cfd
