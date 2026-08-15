#include "partition/partition.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <metis.h>
#include <numeric>
#include <set>
#include <stdexcept>

namespace cfd {

// ---------------------------------------------------------------------------
// Cell graph (METIS CSR)
// ---------------------------------------------------------------------------
std::vector<int> build_cell_graph(const Mesh2D& mesh) {
  const long long n = static_cast<long long>(mesh.cells.size());

  // Count neighbors per cell from interior faces.
  std::vector<int> xadj(n + 1, 0);
  for (const Face2D& f : mesh.faces) {
    if (f.left_cell >= 0 && f.right_cell >= 0) {
      ++xadj[f.left_cell + 1];
      ++xadj[f.right_cell + 1];
    }
  }
  for (long long i = 0; i < n; ++i) xadj[i + 1] += xadj[i];

  std::vector<int> adjncy(xadj[n]);
  std::vector<int> cursor(xadj.begin(), xadj.end() - 1);
  for (const Face2D& f : mesh.faces) {
    if (f.left_cell >= 0 && f.right_cell >= 0) {
      adjncy[cursor[f.left_cell]++] = f.right_cell;
      adjncy[cursor[f.right_cell]++] = f.left_cell;
    }
  }

  // Deduplicate adjacency per cell (a non-manifold mesh could list the same
  // neighbor twice).
  std::vector<int> new_adj;
  new_adj.reserve(adjncy.size());
  std::vector<int> new_xadj(n + 1, 0);
  for (long long i = 0; i < n; ++i) {
    auto b = adjncy.begin() + xadj[i];
    auto e = adjncy.begin() + xadj[i + 1];
    std::sort(b, e);
    auto u = std::unique(b, e);
    new_adj.insert(new_adj.end(), b, u);
    new_xadj[i + 1] = static_cast<int>(new_adj.size());
  }

  // Flat layout: [xadj (n+1), adjncy].
  std::vector<int> out;
  out.reserve(new_xadj.size() + new_adj.size());
  out.insert(out.end(), new_xadj.begin(), new_xadj.end());
  out.insert(out.end(), new_adj.begin(), new_adj.end());
  return out;
}

// ---------------------------------------------------------------------------
// METIS partitioning
// ---------------------------------------------------------------------------
std::vector<int> partition_cells(const Mesh2D& mesh, int nparts) {
  const long long n = static_cast<long long>(mesh.cells.size());
  std::vector<int> part(n, 0);
  if (nparts <= 0)
    throw std::invalid_argument("partition_cells: nparts must be positive");
  if (nparts == 1) return part;  // fast path: single rank
  if (n < nparts)
    throw std::runtime_error("partition_cells: more parts (" +
                             std::to_string(nparts) + ") than cells (" +
                             std::to_string(n) + ")");

  const std::vector<int> csr = build_cell_graph(mesh);
  const int* xadj = csr.data();
  const int* adjncy = csr.data() + n + 1;

  idx_t n_ = static_cast<idx_t>(n);
  idx_t ncon = 1;
  idx_t nparts_ = static_cast<idx_t>(nparts);
  std::vector<idx_t> xadj_(xadj, xadj + n + 1);
  std::vector<idx_t> adjncy_(adjncy, adjncy + csr.size() - (n + 1));
  std::vector<idx_t> part_(n);

  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_CONTIG] = 1;
  // Fixed seed: without it METIS seeds from the current time and different
  // ranks would compute different partitions. All ranks must agree.
  options[METIS_OPTION_SEED] = 0;

  idx_t objval = 0;
  const int rc = METIS_PartGraphKway(&n_, &ncon, xadj_.data(), adjncy_.data(),
                                     nullptr, nullptr, nullptr, &nparts_,
                                     nullptr, nullptr, options, &objval,
                                     part_.data());
  if (rc != METIS_OK)
    throw std::runtime_error("partition_cells: METIS_PartGraphKway failed "
                             "(return code " + std::to_string(rc) + ")");

  for (long long i = 0; i < n; ++i) part[i] = static_cast<int>(part_[i]);
  return part;
}

// ---------------------------------------------------------------------------
// Rank-local mesh construction
// ---------------------------------------------------------------------------
DistributedMesh build_distributed_mesh(const Mesh2D& mesh, int rank,
                                       int nranks,
                                       const std::vector<int>& partition) {
  DistributedMesh dm;
  dm.rank = rank;
  dm.nranks = nranks;

  const long long n = static_cast<long long>(mesh.cells.size());
  if (static_cast<long long>(partition.size()) != n)
    throw std::invalid_argument(
        "build_distributed_mesh: partition size does not match mesh");

  // ---- owned cells (in global order) --------------------------------
  std::vector<long long> owned_global;
  for (long long g = 0; g < n; ++g)
    if (partition[g] == rank) owned_global.push_back(g);
  dm.n_owned = static_cast<long long>(owned_global.size());

  for (long long g : owned_global) {
    Cell2D c = mesh.cells[g];
    c.cell_id = static_cast<long long>(dm.cells.size());  // local index
    c.faces.clear();  // local face lists are rebuilt in a later phase
    dm.cells.push_back(std::move(c));
    dm.owner_rank.push_back(rank);
    dm.global_cell_id.push_back(g);
    dm.global_to_local[g] = static_cast<int>(c.cell_id);
  }

  // ---- ghost discovery (cells owned by other ranks sharing a face with an
  //      OWNED cell) ------------------------------------------------------
  std::map<int, std::vector<long long>> ghost_globals;  // owner rank -> globals
  auto consider_ghost = [&](long long g) {
    if (g < 0 || partition[g] == rank) return;
    std::vector<long long>& v = ghost_globals[partition[g]];
    if (std::find(v.begin(), v.end(), g) == v.end()) v.push_back(g);
  };
  for (const Face2D& f : mesh.faces) {
    if (f.left_cell >= 0 && partition[f.left_cell] == rank)
      consider_ghost(f.right_cell);
    if (f.right_cell >= 0 && partition[f.right_cell] == rank)
      consider_ghost(f.left_cell);
  }

  // Append ghosts grouped by owner rank (ascending rank), so ghost ranges
  // per neighbor are contiguous.
  std::map<int, long long> ghost_start;  // owner rank -> start local index
  for (auto& kv : ghost_globals) {
    ghost_start[kv.first] = static_cast<long long>(dm.cells.size());
    for (long long g : kv.second) {
      Cell2D c = mesh.cells[g];
      c.cell_id = static_cast<long long>(dm.cells.size());
      c.faces.clear();
      dm.cells.push_back(std::move(c));
      dm.owner_rank.push_back(partition[g]);
      dm.global_cell_id.push_back(g);
      dm.global_to_local[g] = static_cast<int>(c.cell_id);
    }
  }

  // ---- face classification -------------------------------------------
  // Cells that are neither owned nor ghosts on this rank (faces between two
  // foreign cells, or foreign cells merely touching a ghost) are not in the
  // local map; they can never be part of a local face and are discarded by
  // the classification below.
  auto local_of = [&](long long g) -> int {
    auto it = dm.global_to_local.find(g);
    if (it == dm.global_to_local.end()) return -1;
    return it->second;
  };

  for (const Face2D& f : mesh.faces) {
    const bool has_l = f.left_cell >= 0;
    const bool has_r = f.right_cell >= 0;
    const bool own_l = has_l && partition[f.left_cell] == rank;
    const bool own_r = has_r && partition[f.right_cell] == rank;

    Face2D lf = f;  // copies nodes/center/normal/area/bc_type/bc_tag/face_id
    lf.left_cell = has_l ? local_of(f.left_cell) : -1;
    lf.right_cell = has_r ? local_of(f.right_cell) : -1;

    if (own_l && own_r) {
      dm.interior_faces.push_back(std::move(lf));
    } else if (own_l && !has_r) {
      dm.boundary_faces.push_back(std::move(lf));
      dm.info.owned_boundary_faces.push_back(f.face_id);
    } else if (own_r && !has_l) {
      dm.boundary_faces.push_back(std::move(lf));
      dm.info.owned_boundary_faces.push_back(f.face_id);
    } else if (own_l) {  // has_r, right cell is a ghost
      dm.send_faces.push_back(std::move(lf));
    } else if (own_r) {  // has_l, left cell is a ghost
      dm.recv_faces.push_back(std::move(lf));
    }
    // faces with no owned cell (ghost-ghost or orphan) are discarded
  }

  // ---- per-neighbor send/recv lists ----------------------------------
  // Every MPI face (one owned + one ghost cell) must exchange BOTH cells:
  // the owned cell's state must be sent to the ghost's owner rank, and the
  // ghost cell must receive its owner's state. Face orientation (which side
  // is "left") is arbitrary, so each face contributes one send entry (the
  // owned cell) AND one recv entry (the ghost cell), regardless of which
  // list the face landed in. Both sides key their lists by the SHARED cell's
  // global id and sort, so the send sequence on one rank matches the receive
  // sequence on the other; deduplication removes repeated entries when a
  // ghost touches several owned cells of a rank.
  std::map<int, std::vector<std::pair<long long, int>>> send_pairs, recv_pairs;
  for (const Face2D& f : dm.send_faces) {
    // left cell owned by this rank, right cell a ghost owned by nbr
    const int nbr = dm.owner_rank[f.right_cell];
    send_pairs[nbr].push_back({dm.global_cell_id[f.left_cell], f.left_cell});
    recv_pairs[nbr].push_back({dm.global_cell_id[f.right_cell], f.right_cell});
  }
  for (const Face2D& f : dm.recv_faces) {
    // left cell a ghost owned by nbr, right cell owned by this rank
    const int nbr = dm.owner_rank[f.left_cell];
    send_pairs[nbr].push_back({dm.global_cell_id[f.right_cell], f.right_cell});
    recv_pairs[nbr].push_back({dm.global_cell_id[f.left_cell], f.left_cell});
  }

  for (auto& kv : send_pairs) {
    auto& snd = kv.second;
    std::sort(snd.begin(), snd.end());
    snd.erase(std::unique(snd.begin(), snd.end(),
                          [](const auto& a, const auto& b) {
                            return a.first == b.first;
                          }),
              snd.end());
  }
  for (auto& kv : recv_pairs) {
    auto& rcv = kv.second;
    std::sort(rcv.begin(), rcv.end());
    rcv.erase(std::unique(rcv.begin(), rcv.end(),
                          [](const auto& a, const auto& b) {
                            return a.first == b.first;
                          }),
              rcv.end());
  }

  // merge into the neighbor list (union of send/recv neighbors, ascending)
  std::vector<int> nbr_set;
  nbr_set.reserve(send_pairs.size() + recv_pairs.size());
  for (const auto& kv : send_pairs) nbr_set.push_back(kv.first);
  for (const auto& kv : recv_pairs)
    if (send_pairs.find(kv.first) == send_pairs.end())
      nbr_set.push_back(kv.first);
  std::sort(nbr_set.begin(), nbr_set.end());

  for (int nbr : nbr_set) {
    NeighborInfo ni;
    ni.rank = nbr;
    auto sit = send_pairs.find(nbr);
    if (sit != send_pairs.end())
      for (const auto& p : sit->second) ni.send_cells.push_back(p.second);
    auto rit = recv_pairs.find(nbr);
    if (rit != recv_pairs.end())
      for (const auto& p : rit->second) ni.recv_cells.push_back(p.second);
    dm.neighbors.push_back(std::move(ni));
  }

  // ---- PartitionInfo --------------------------------------------------
  PartitionInfo& info = dm.info;
  info.rank = rank;
  info.nranks = nranks;
  info.n_owned = dm.n_owned;
  info.n_ghost = static_cast<long long>(dm.cells.size()) - dm.n_owned;
  info.n_local = static_cast<long long>(dm.cells.size());
  info.n_interior_faces = static_cast<long long>(dm.interior_faces.size());
  info.n_boundary_faces = static_cast<long long>(dm.boundary_faces.size());
  info.n_send_faces = static_cast<long long>(dm.send_faces.size());
  info.n_recv_faces = static_cast<long long>(dm.recv_faces.size());

  // owned cell ranges per rank
  info.owned_ranges.resize(nranks);
  std::vector<long long> starts(nranks, -1), counts(nranks, 0);
  for (long long g = 0; g < n; ++g) {
    const int r = partition[g];
    if (starts[r] < 0) starts[r] = g;
    ++counts[r];
  }
  for (int r = 0; r < nranks; ++r)
    info.owned_ranges[r] = {starts[r] < 0 ? 0 : starts[r], counts[r]};

  // ghost ranges per neighbor (aligned with neighbor_ranks)
  for (const NeighborInfo& ni : dm.neighbors) {
    const long long start = ghost_start.count(ni.rank)
                                ? ghost_start[ni.rank]
                                : static_cast<long long>(dm.cells.size());
    const long long cnt = ni.recv_cells.size();
    info.ghost_ranges.push_back({start, cnt});
  }

  for (const NeighborInfo& ni : dm.neighbors) {
    info.neighbor_ranks.push_back(ni.rank);
    info.send_counts.push_back(static_cast<int>(ni.send_cells.size()));
    info.recv_counts.push_back(static_cast<int>(ni.recv_cells.size()));
  }

  // edge cut: faces whose two cells belong to different ranks
  long long cut = 0;
  for (const Face2D& f : mesh.faces)
    if (f.left_cell >= 0 && f.right_cell >= 0 &&
        partition[f.left_cell] != partition[f.right_cell])
      ++cut;
  info.edge_cut = static_cast<int>(cut);

  // ---- completeness assertions ----------------------------------------
  // (1) Every ghost cell's local index must appear in some neighbor's
  // recv_cells — otherwise its slot would never be filled by an exchange.
  {
    std::set<int> ghost_in_recv;
    for (const NeighborInfo& ni : dm.neighbors)
      for (int idx : ni.recv_cells) ghost_in_recv.insert(idx);
    for (long long i = dm.n_owned; i < static_cast<long long>(dm.cells.size());
         ++i) {
      if (!ghost_in_recv.count(static_cast<int>(i)))
        throw std::runtime_error(
            "build_distributed_mesh: ghost cell " + std::to_string(i) +
            " (global " + std::to_string(dm.global_cell_id[i]) +
            ", owner rank " + std::to_string(dm.owner_rank[i]) +
            ") is not in any neighbor's recv list");
    }
  }
  // (2) For each neighbor, the recv list must cover exactly the ghosts owned
  // by that neighbor — guarantees the exchange is complete.
  {
    std::map<int, long long> ghosts_by_owner;
    for (long long i = dm.n_owned; i < static_cast<long long>(dm.cells.size());
         ++i)
      ++ghosts_by_owner[dm.owner_rank[i]];
    for (const NeighborInfo& ni : dm.neighbors) {
      const long long expected = ghosts_by_owner[ni.rank];
      if (static_cast<long long>(ni.recv_cells.size()) != expected)
        throw std::runtime_error(
            "build_distributed_mesh: recv list for neighbor " +
            std::to_string(ni.rank) + " has " +
            std::to_string(ni.recv_cells.size()) +
            " entries but this rank holds " + std::to_string(expected) +
            " ghosts owned by that rank");
    }
  }

  // Copy the global vertex coordinates (needed by the VTU field writer).
  dm.vertices = mesh.vertices;

  return dm;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
void partition_diagnostics(const DistributedMesh& dmesh, MPI_Comm comm) {
  const PartitionInfo& info = dmesh.info;

  std::string nbrs;
  for (std::size_t i = 0; i < info.neighbor_ranks.size(); ++i) {
    if (i) nbrs += ",";
    nbrs += std::to_string(info.neighbor_ranks[i]);
  }

  const std::string line = fmt::format(
      "rank {}: owned={} ghost={} boundary_faces={} neighbors={} send_cells={} "
      "recv_cells={} (ghost coverage {}/{}; nbrs: {})",
      info.rank, info.n_owned, info.n_ghost, info.n_boundary_faces,
      info.neighbor_ranks.size(),
      std::accumulate(info.send_counts.begin(), info.send_counts.end(), 0),
      std::accumulate(info.recv_counts.begin(), info.recv_counts.end(), 0),
      std::accumulate(info.recv_counts.begin(), info.recv_counts.end(), 0),
      info.n_ghost, nbrs.empty() ? "-" : nbrs);

  // Gather formatted lines to rank 0 for ordered output.
  constexpr int kLineLen = 512;
  std::vector<char> sendbuf(kLineLen, 0);
  std::copy_n(line.begin(), std::min(line.size(), sendbuf.size() - 1),
              sendbuf.begin());
  int rank = 0;
  MPI_Comm_rank(comm, &rank);
  std::vector<char> recvbuf;
  if (rank == 0) recvbuf.resize(kLineLen * dmesh.nranks);
  MPI_Gather(sendbuf.data(), kLineLen, MPI_CHAR, recvbuf.data(), kLineLen,
             MPI_CHAR, 0, comm);

  if (rank == 0) {
    fmt::print("[partition] --- per-rank diagnostics ---\n");
    long long min_c = info.owned_ranges[0].second;
    long long max_c = 0;
    long long sum_c = 0;
    for (int r = 0; r < info.nranks; ++r) {
      fmt::print("[partition] {}\n",
                 std::string(recvbuf.data() + r * kLineLen));
      min_c = std::min(min_c, info.owned_ranges[r].second);
      max_c = std::max(max_c, info.owned_ranges[r].second);
      sum_c += info.owned_ranges[r].second;
    }
    const double mean = static_cast<double>(sum_c) / info.nranks;
    fmt::print("[partition] load balance: min={} max={} mean={:.1f} "
               "imbalance(max/mean)={:.4f}\n",
               min_c, max_c, mean, max_c / mean);
    fmt::print("[partition] edge cut: {}\n", info.edge_cut);
  }
}

}  // namespace cfd
