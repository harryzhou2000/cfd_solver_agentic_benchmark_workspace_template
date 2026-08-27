#include "parallel/distributed_mesh.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>

#include "core/exceptions.h"
#include "core/logging.h"
#include "mesh/cgns_reader.h"
#include "mesh/global_mesh.h"

namespace cns2d {
namespace {

// Wire format for one cell sent from the preprocessing rank to its owner.
// Node coordinates travel with the cell so no rank needs the global node array.
struct CellPacket {
  GlobalIndex global_id{0};
  std::int32_t num_nodes{0};
  std::int32_t shape{0};
  Real x[4]{};
  Real y[4]{};
};

// Wire format for one face.  Faces reference cells by GLOBAL id; each receiving
// rank translates them into its own local numbering.
struct FacePacket {
  GlobalIndex left_global{0};
  GlobalIndex right_global{0};  // -1 for a boundary face
  std::int32_t boundary_tag{-1};
  Real x0{0.0};
  Real y0{0.0};
  Real x1{0.0};
  Real y1{0.0};
};

void mpiCheck(int code, const char *what) {
  if (code != MPI_SUCCESS) {
    throw CnsError(std::string("MPI call failed: ") + what);
  }
}

}  // namespace

std::unique_ptr<DistributedMesh> DistributedMesh::build(const CaseInput &input, MPI_Comm comm) {
  auto mesh = std::unique_ptr<DistributedMesh>(new DistributedMesh());
  mesh->comm_ = comm;
  mpiCheck(MPI_Comm_rank(comm, &mesh->rank_), "MPI_Comm_rank");
  mpiCheck(MPI_Comm_size(comm, &mesh->size_), "MPI_Comm_size");
  const int rank = mesh->rank_;
  const int nranks = mesh->size_;

  // -----------------------------------------------------------------------
  // Stage 1 (rank 0 only): read CGNS, merge zones, build faces, partition.
  //
  // The global mesh lives in this scope and is destroyed at the end of the
  // stage, before any solver data is allocated.  Other ranks never allocate it.
  // -----------------------------------------------------------------------
  std::vector<std::string> boundary_names;
  GlobalIndex num_cells_global = 0;
  GlobalIndex num_faces_global = 0;
  GlobalIndex edge_cut = 0;
  std::string partitioner_name;

  // Per-destination-rank packets assembled on rank 0.
  std::vector<std::vector<CellPacket>> owned_packets;
  std::vector<std::vector<CellPacket>> ghost_packets;
  std::vector<std::vector<FacePacket>> face_packets;

  if (rank == 0) {
    const RawMesh raw = readCgnsMesh(input.mesh_file);
    logInfo(describeRawMesh(raw));

    GlobalMesh global = buildGlobalMesh(raw);
    logInfo(describeGlobalMesh(global));

    boundary_names = global.boundary_names;
    // Validate the case BC mapping against the mesh families before doing work.
    (void)mapBoundaryConditions(global, input.boundary_conditions);

    num_cells_global = global.numCells();
    num_faces_global = global.numFaces();

    const PartitionResult partition = partitionMesh(global, nranks);
    edge_cut = partition.edge_cut;
    partitioner_name = partition.partitioner;
    logInfo("partitioning: " + partitioner_name + " into " + std::to_string(nranks) +
            " part(s), edge cut " + std::to_string(edge_cut));

    // --- decide which cells each rank needs -----------------------------
    // Owned: cells assigned to the rank.  Ghost: off-rank cells appearing on
    // the far side of a face whose near side is owned by the rank.
    std::vector<std::set<Index>> ghost_sets(static_cast<std::size_t>(nranks));
    for (const GlobalFace &face : global.faces) {
      if (face.right_cell < 0) continue;
      const int lr = partition.cell_rank[static_cast<std::size_t>(face.left_cell)];
      const int rr = partition.cell_rank[static_cast<std::size_t>(face.right_cell)];
      if (lr == rr) continue;
      ghost_sets[static_cast<std::size_t>(lr)].insert(face.right_cell);
      ghost_sets[static_cast<std::size_t>(rr)].insert(face.left_cell);
    }

    // Owned cell lists in ascending global order (deterministic layout).
    std::vector<std::vector<Index>> owned_lists(static_cast<std::size_t>(nranks));
    for (Index c = 0; c < global.numCells(); ++c) {
      owned_lists[static_cast<std::size_t>(partition.cell_rank[static_cast<std::size_t>(c)])]
          .push_back(c);
    }

    auto makeCellPacket = [&](Index c) {
      const GlobalCell &cell = global.cells[static_cast<std::size_t>(c)];
      CellPacket p;
      p.global_id = static_cast<GlobalIndex>(c);
      p.num_nodes = cell.num_nodes;
      p.shape = static_cast<std::int32_t>(cell.shape);
      for (int k = 0; k < cell.num_nodes; ++k) {
        const Index n = cell.nodes[static_cast<std::size_t>(k)];
        p.x[k] = global.x[static_cast<std::size_t>(n)];
        p.y[k] = global.y[static_cast<std::size_t>(n)];
      }
      return p;
    };

    owned_packets.resize(static_cast<std::size_t>(nranks));
    ghost_packets.resize(static_cast<std::size_t>(nranks));
    face_packets.resize(static_cast<std::size_t>(nranks));

    for (int r = 0; r < nranks; ++r) {
      auto &op = owned_packets[static_cast<std::size_t>(r)];
      op.reserve(owned_lists[static_cast<std::size_t>(r)].size());
      for (const Index c : owned_lists[static_cast<std::size_t>(r)]) op.push_back(makeCellPacket(c));
      auto &gp = ghost_packets[static_cast<std::size_t>(r)];
      gp.reserve(ghost_sets[static_cast<std::size_t>(r)].size());
      for (const Index c : ghost_sets[static_cast<std::size_t>(r)]) gp.push_back(makeCellPacket(c));
    }

    // --- faces each rank must integrate ---------------------------------
    // A rank integrates every face that has at least one owned cell.  Faces
    // straddling a partition boundary are integrated by both adjacent ranks;
    // each rank keeps only the flux contribution to its own owned cell, so the
    // scheme stays conservative without extra flux communication.
    for (const GlobalFace &face : global.faces) {
      const int lr = partition.cell_rank[static_cast<std::size_t>(face.left_cell)];
      FacePacket p;
      p.left_global = static_cast<GlobalIndex>(face.left_cell);
      p.right_global = face.right_cell >= 0 ? static_cast<GlobalIndex>(face.right_cell) : -1;
      p.boundary_tag = face.boundary_tag;
      p.x0 = global.x[static_cast<std::size_t>(face.nodes[0])];
      p.y0 = global.y[static_cast<std::size_t>(face.nodes[0])];
      p.x1 = global.x[static_cast<std::size_t>(face.nodes[1])];
      p.y1 = global.y[static_cast<std::size_t>(face.nodes[1])];
      face_packets[static_cast<std::size_t>(lr)].push_back(p);
      if (face.right_cell >= 0) {
        const int rr = partition.cell_rank[static_cast<std::size_t>(face.right_cell)];
        if (rr != lr) face_packets[static_cast<std::size_t>(rr)].push_back(p);
      }
    }
  }

  // -----------------------------------------------------------------------
  // Stage 2: scatter global scalars and the boundary-family table.
  // -----------------------------------------------------------------------
  {
    GlobalIndex scalars[3] = {num_cells_global, num_faces_global, edge_cut};
    mpiCheck(MPI_Bcast(scalars, 3, MPI_INT64_T, 0, comm), "MPI_Bcast(global scalars)");
    num_cells_global = scalars[0];
    num_faces_global = scalars[1];
    edge_cut = scalars[2];

    // Boundary names as one packed buffer.
    std::string packed;
    if (rank == 0) {
      for (const std::string &n : boundary_names) {
        packed += n;
        packed.push_back('\0');
      }
      packed += partitioner_name;
      packed.push_back('\0');
    }
    int packed_size = static_cast<int>(packed.size());
    mpiCheck(MPI_Bcast(&packed_size, 1, MPI_INT, 0, comm), "MPI_Bcast(name buffer size)");
    packed.resize(static_cast<std::size_t>(packed_size));
    mpiCheck(MPI_Bcast(packed.data(), packed_size, MPI_CHAR, 0, comm), "MPI_Bcast(name buffer)");
    if (rank != 0) {
      boundary_names.clear();
      std::size_t start = 0;
      std::vector<std::string> tokens;
      for (std::size_t i = 0; i < packed.size(); ++i) {
        if (packed[i] == '\0') {
          tokens.emplace_back(packed.data() + start, i - start);
          start = i + 1;
        }
      }
      if (tokens.empty()) throw CnsError("internal error: empty boundary-name broadcast");
      partitioner_name = tokens.back();
      tokens.pop_back();
      boundary_names = tokens;
    }
  }

  mesh->num_cells_global_ = num_cells_global;
  mesh->num_faces_global_ = num_faces_global;
  mesh->edge_cut_ = edge_cut;
  mesh->partitioner_name_ = partitioner_name;
  mesh->boundary_names_ = boundary_names;
  mesh->boundary_types_.clear();
  for (const std::string &name : boundary_names) {
    auto it = input.boundary_conditions.find(name);
    if (it == input.boundary_conditions.end()) {
      throw CnsError("mesh boundary family '" + name + "' is not mapped in the case file");
    }
    mesh->boundary_types_.push_back(it->second);
  }

  // -----------------------------------------------------------------------
  // Stage 3: scatter the per-rank cell and face packets.
  // -----------------------------------------------------------------------
  std::vector<CellPacket> my_owned;
  std::vector<CellPacket> my_ghost;
  std::vector<FacePacket> my_faces;

  {
    // Byte-count based scatter: the packets are POD, so MPI_BYTE is enough and
    // avoids constructing derived datatypes.
    auto scatterVector = [&](auto &per_rank_source, auto &destination, const char *label) {
      using Packet = typename std::decay_t<decltype(destination)>::value_type;
      std::vector<int> counts(static_cast<std::size_t>(nranks), 0);
      std::vector<int> displs(static_cast<std::size_t>(nranks), 0);
      std::vector<Packet> flat;
      if (rank == 0) {
        int total = 0;
        for (int r = 0; r < nranks; ++r) {
          const std::size_t n = per_rank_source[static_cast<std::size_t>(r)].size();
          counts[static_cast<std::size_t>(r)] = static_cast<int>(n * sizeof(Packet));
          displs[static_cast<std::size_t>(r)] = total;
          total += counts[static_cast<std::size_t>(r)];
        }
        flat.reserve(static_cast<std::size_t>(total) / sizeof(Packet));
        for (int r = 0; r < nranks; ++r) {
          auto &src = per_rank_source[static_cast<std::size_t>(r)];
          flat.insert(flat.end(), src.begin(), src.end());
          src.clear();
          src.shrink_to_fit();
        }
      }
      int my_bytes = 0;
      mpiCheck(MPI_Scatter(counts.data(), 1, MPI_INT, &my_bytes, 1, MPI_INT, 0, comm),
               "MPI_Scatter(counts)");
      if (my_bytes % static_cast<int>(sizeof(Packet)) != 0) {
        throw CnsError(std::string("internal error: misaligned ") + label + " scatter payload");
      }
      destination.resize(static_cast<std::size_t>(my_bytes) / sizeof(Packet));
      mpiCheck(MPI_Scatterv(rank == 0 ? flat.data() : nullptr, counts.data(), displs.data(), MPI_BYTE,
                            destination.data(), my_bytes, MPI_BYTE, 0, comm),
               "MPI_Scatterv(payload)");
    };

    scatterVector(owned_packets, my_owned, "owned-cell");
    scatterVector(ghost_packets, my_ghost, "ghost-cell");
    scatterVector(face_packets, my_faces, "face");
  }
  owned_packets.clear();
  owned_packets.shrink_to_fit();
  ghost_packets.clear();
  ghost_packets.shrink_to_fit();
  face_packets.clear();
  face_packets.shrink_to_fit();

  if (my_owned.empty()) {
    throw CnsError("rank " + std::to_string(rank) + " received no owned cells from the partitioner");
  }

  // -----------------------------------------------------------------------
  // Stage 4: build the local numbering and geometry.
  // -----------------------------------------------------------------------
  mesh->num_owned_ = static_cast<Index>(my_owned.size());
  mesh->num_ghost_ = static_cast<Index>(my_ghost.size());
  const Index num_local = mesh->numLocal();

  mesh->global_cell_id_.resize(static_cast<std::size_t>(num_local));
  mesh->cell_nodes_.resize(static_cast<std::size_t>(num_local));
  mesh->cell_num_nodes_.resize(static_cast<std::size_t>(num_local));

  std::unordered_map<GlobalIndex, Index> local_of_global;
  local_of_global.reserve(static_cast<std::size_t>(num_local) * 2);

  auto installCell = [&](const CellPacket &p, Index local) {
    mesh->global_cell_id_[static_cast<std::size_t>(local)] = p.global_id;
    mesh->cell_num_nodes_[static_cast<std::size_t>(local)] = p.num_nodes;
    for (int k = 0; k < p.num_nodes; ++k) {
      mesh->cell_nodes_[static_cast<std::size_t>(local)][static_cast<std::size_t>(k)] = {p.x[k], p.y[k]};
    }
    local_of_global.emplace(p.global_id, local);
  };

  for (Index i = 0; i < mesh->num_owned_; ++i) {
    installCell(my_owned[static_cast<std::size_t>(i)], i);
  }
  for (Index i = 0; i < mesh->num_ghost_; ++i) {
    installCell(my_ghost[static_cast<std::size_t>(i)], mesh->num_owned_ + i);
  }
  my_owned.clear();
  my_owned.shrink_to_fit();
  my_ghost.clear();
  my_ghost.shrink_to_fit();

  // --- faces in local numbering ------------------------------------------
  mesh->faces_.reserve(my_faces.size());
  for (const FacePacket &p : my_faces) {
    auto lit = local_of_global.find(p.left_global);
    if (lit == local_of_global.end()) {
      throw CnsError("internal error: rank " + std::to_string(rank) +
                     " received a face whose left cell is not local");
    }
    LocalFace f;
    f.left = lit->second;
    f.node0 = {p.x0, p.y0};
    f.node1 = {p.x1, p.y1};
    f.boundary_tag = p.boundary_tag;
    if (p.right_global < 0) {
      f.kind = FaceKind::kBoundary;
      f.right = -1;
      if (p.boundary_tag < 0) {
        throw CnsError("internal error: untagged boundary face reached the solver");
      }
    } else {
      auto rit = local_of_global.find(p.right_global);
      if (rit == local_of_global.end()) {
        throw CnsError("internal error: rank " + std::to_string(rank) +
                       " received an interior face whose right cell is neither owned nor ghost");
      }
      f.kind = FaceKind::kInterior;
      f.right = rit->second;
    }
    // Orient the face so its left side is the cell the normal points away from.
    // If the packet's left cell is a ghost here while the right cell is owned,
    // swapping keeps owned cells on the left where possible, which simplifies
    // the residual scatter.
    if (f.kind == FaceKind::kInterior && f.left >= mesh->num_owned_ && f.right < mesh->num_owned_) {
      std::swap(f.left, f.right);
      std::swap(f.node0, f.node1);
    }
    mesh->faces_.push_back(f);
  }
  my_faces.clear();
  my_faces.shrink_to_fit();

  mesh->finalizeGeometry();
  mesh->buildCellFaceMaps();
  mesh->buildGradientStencils();

  // -----------------------------------------------------------------------
  // Stage 5: build the neighbour communication plans.
  //
  // For each ghost cell the owner rank must be known.  It is obtained with a
  // single all-to-all style exchange of ghost global ids: each rank asks the
  // others "do you own this cell?".  This happens once during setup; iteration
  // time communication is strictly neighbour-scoped.
  // -----------------------------------------------------------------------
  {
    // Which global ids do I need?
    std::vector<GlobalIndex> wanted;
    wanted.reserve(static_cast<std::size_t>(mesh->num_ghost_));
    for (Index i = 0; i < mesh->num_ghost_; ++i) {
      wanted.push_back(mesh->global_cell_id_[static_cast<std::size_t>(mesh->num_owned_ + i)]);
    }

    // Gather the request counts, then the requests themselves.  Setup-only.
    std::vector<int> want_counts(static_cast<std::size_t>(nranks), 0);
    int my_want = static_cast<int>(wanted.size());
    mpiCheck(MPI_Allgather(&my_want, 1, MPI_INT, want_counts.data(), 1, MPI_INT, comm),
             "MPI_Allgather(ghost request counts)");
    std::vector<int> want_displs(static_cast<std::size_t>(nranks), 0);
    int total_wanted = 0;
    for (int r = 0; r < nranks; ++r) {
      want_displs[static_cast<std::size_t>(r)] = total_wanted;
      total_wanted += want_counts[static_cast<std::size_t>(r)];
    }
    std::vector<GlobalIndex> all_wanted(static_cast<std::size_t>(std::max(total_wanted, 1)));
    mpiCheck(MPI_Allgatherv(wanted.data(), my_want, MPI_INT64_T, all_wanted.data(),
                            want_counts.data(), want_displs.data(), MPI_INT64_T, comm),
             "MPI_Allgatherv(ghost requests)");

    // Map my owned global ids for fast lookup.
    std::unordered_map<GlobalIndex, Index> owned_of_global;
    owned_of_global.reserve(static_cast<std::size_t>(mesh->num_owned_) * 2);
    for (Index i = 0; i < mesh->num_owned_; ++i) {
      owned_of_global.emplace(mesh->global_cell_id_[static_cast<std::size_t>(i)], i);
    }

    // For each requesting rank, the owned cells I must send, in the exact order
    // the requester listed them.  That shared ordering is what makes the
    // iteration-time exchange a plain contiguous buffer copy with no index
    // metadata on the wire.
    std::map<int, std::vector<Index>> send_map;
    std::map<int, std::vector<Index>> recv_map;
    for (int r = 0; r < nranks; ++r) {
      if (r == rank) continue;
      const int begin = want_displs[static_cast<std::size_t>(r)];
      const int count = want_counts[static_cast<std::size_t>(r)];
      for (int k = 0; k < count; ++k) {
        const GlobalIndex gid = all_wanted[static_cast<std::size_t>(begin + k)];
        auto it = owned_of_global.find(gid);
        if (it != owned_of_global.end()) {
          send_map[r].push_back(it->second);
        }
      }
    }

    // My own ghosts, grouped by owner.  The owner is whichever rank listed the
    // id among its owned cells; determined by asking every rank which of my
    // requests it can satisfy.
    {
      // Reply phase: each rank tells every other rank which of its requests it
      // owns, as a bitmask-free ordered list of positions.
      std::vector<std::vector<std::int32_t>> reply_positions(static_cast<std::size_t>(nranks));
      for (int r = 0; r < nranks; ++r) {
        if (r == rank) continue;
        const int begin = want_displs[static_cast<std::size_t>(r)];
        const int count = want_counts[static_cast<std::size_t>(r)];
        for (int k = 0; k < count; ++k) {
          const GlobalIndex gid = all_wanted[static_cast<std::size_t>(begin + k)];
          if (owned_of_global.count(gid) > 0) {
            reply_positions[static_cast<std::size_t>(r)].push_back(k);
          }
        }
      }

      // Exchange reply sizes, then the position lists.
      std::vector<int> send_sizes(static_cast<std::size_t>(nranks), 0);
      std::vector<int> recv_sizes(static_cast<std::size_t>(nranks), 0);
      for (int r = 0; r < nranks; ++r) {
        send_sizes[static_cast<std::size_t>(r)] =
            static_cast<int>(reply_positions[static_cast<std::size_t>(r)].size());
      }
      mpiCheck(MPI_Alltoall(send_sizes.data(), 1, MPI_INT, recv_sizes.data(), 1, MPI_INT, comm),
               "MPI_Alltoall(reply sizes)");

      std::vector<MPI_Request> requests;
      requests.reserve(static_cast<std::size_t>(2 * nranks));
      std::vector<std::vector<std::int32_t>> received(static_cast<std::size_t>(nranks));
      for (int r = 0; r < nranks; ++r) {
        if (recv_sizes[static_cast<std::size_t>(r)] > 0) {
          received[static_cast<std::size_t>(r)].resize(
              static_cast<std::size_t>(recv_sizes[static_cast<std::size_t>(r)]));
          MPI_Request req;
          mpiCheck(MPI_Irecv(received[static_cast<std::size_t>(r)].data(),
                             recv_sizes[static_cast<std::size_t>(r)], MPI_INT32_T, r, 7001, comm, &req),
                   "MPI_Irecv(reply positions)");
          requests.push_back(req);
        }
      }
      for (int r = 0; r < nranks; ++r) {
        if (send_sizes[static_cast<std::size_t>(r)] > 0) {
          MPI_Request req;
          mpiCheck(MPI_Isend(reply_positions[static_cast<std::size_t>(r)].data(),
                             send_sizes[static_cast<std::size_t>(r)], MPI_INT32_T, r, 7001, comm, &req),
                   "MPI_Isend(reply positions)");
          requests.push_back(req);
        }
      }
      if (!requests.empty()) {
        mpiCheck(MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE),
                 "MPI_Waitall(reply positions)");
      }

      for (int r = 0; r < nranks; ++r) {
        for (const std::int32_t pos : received[static_cast<std::size_t>(r)]) {
          if (pos < 0 || pos >= my_want) {
            throw CnsError("internal error: ghost-owner reply out of range");
          }
          recv_map[r].push_back(mesh->num_owned_ + static_cast<Index>(pos));
        }
      }
    }

    // Every ghost must have exactly one owner.
    {
      std::vector<int> owner_count(static_cast<std::size_t>(mesh->num_ghost_), 0);
      for (const auto &kv : recv_map) {
        for (const Index ghost_local : kv.second) {
          ++owner_count[static_cast<std::size_t>(ghost_local - mesh->num_owned_)];
        }
      }
      for (Index i = 0; i < mesh->num_ghost_; ++i) {
        if (owner_count[static_cast<std::size_t>(i)] != 1) {
          throw CnsError("internal error: ghost cell " + std::to_string(i) + " on rank " +
                         std::to_string(rank) + " has " +
                         std::to_string(owner_count[static_cast<std::size_t>(i)]) + " owners");
        }
      }
    }

    std::set<int> neighbor_ranks;
    for (const auto &kv : send_map) neighbor_ranks.insert(kv.first);
    for (const auto &kv : recv_map) neighbor_ranks.insert(kv.first);
    for (const int r : neighbor_ranks) {
      NeighborPlan plan;
      plan.rank = r;
      auto sit = send_map.find(r);
      if (sit != send_map.end()) plan.send_cells = sit->second;
      auto rit = recv_map.find(r);
      if (rit != recv_map.end()) plan.recv_cells = rit->second;
      mesh->neighbors_.push_back(std::move(plan));
    }
  }

  mesh->fillDiagnostics();
  return mesh;
}

void DistributedMesh::finalizeGeometry() {
  const Index num_local = numLocal();
  cells_.assign(static_cast<std::size_t>(num_local), CellGeometry{});

  for (Index c = 0; c < num_local; ++c) {
    const int n = cell_num_nodes_[static_cast<std::size_t>(c)];
    Real xs[4];
    Real ys[4];
    for (int k = 0; k < n; ++k) {
      xs[k] = cell_nodes_[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)].x;
      ys[k] = cell_nodes_[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)].y;
    }
    Vec2 centroid{};
    const Real area = polygonAreaCentroid(xs, ys, n, centroid);
    if (!(area > 0.0)) {
      throw CnsError("rank " + std::to_string(rank_) + " received a cell with non-positive area " +
                     std::to_string(area));
    }
    CellGeometry &g = cells_[static_cast<std::size_t>(c)];
    g.centroid = centroid;
    g.volume = area;
    g.inv_volume = 1.0 / area;
  }

  // Face metrics.  The normal is rotated from the edge tangent and then
  // oriented to point away from the left cell centroid, which guarantees a
  // consistent outward normal for the left cell on every face including
  // boundary faces.
  local_wall_length_ = 0.0;
  for (LocalFace &f : faces_) {
    const Vec2 t = f.node1 - f.node0;
    const Real len = norm(t);
    if (!(len > 0.0)) {
      throw CnsError("degenerate zero-length face encountered on rank " + std::to_string(rank_));
    }
    f.geom.area = len;
    f.geom.centroid = 0.5 * (f.node0 + f.node1);
    Vec2 nrm{t.y / len, -t.x / len};
    const Vec2 d = f.geom.centroid - cells_[static_cast<std::size_t>(f.left)].centroid;
    if (dot(nrm, d) < 0.0) {
      nrm = -1.0 * nrm;
      std::swap(f.node0, f.node1);
    }
    f.geom.normal = nrm;

    if (f.kind == FaceKind::kBoundary && isWall(boundaryTypeOfTag(f.boundary_tag))) {
      local_wall_length_ += len;
    }
  }

  // Characteristic cell length: volume divided by half the total face
  // perimeter, i.e. the mean distance from the centroid to the faces.  This is
  // the length scale used by the local time step.
  std::vector<Real> perimeter(static_cast<std::size_t>(num_local), 0.0);
  for (const LocalFace &f : faces_) {
    perimeter[static_cast<std::size_t>(f.left)] += f.geom.area;
    if (f.right >= 0) perimeter[static_cast<std::size_t>(f.right)] += f.geom.area;
  }
  for (Index c = 0; c < num_local; ++c) {
    const Real p = perimeter[static_cast<std::size_t>(c)];
    cells_[static_cast<std::size_t>(c)].characteristic_length =
        p > 0.0 ? cells_[static_cast<std::size_t>(c)].volume / p : 0.0;
  }

  // Boundary-face index table.
  boundary_face_slot_.assign(static_cast<std::size_t>(faces_.size()), -1);
  for (Index i = 0; i < numFaces(); ++i) {
    if (faces_[static_cast<std::size_t>(i)].kind == FaceKind::kBoundary) {
      boundary_face_slot_[static_cast<std::size_t>(i)] = static_cast<Index>(boundary_faces_.size());
      boundary_faces_.push_back(i);
    }
  }
}

void DistributedMesh::buildCellFaceMaps() {
  const Index num_local = numLocal();
  std::vector<Index> degree(static_cast<std::size_t>(num_local), 0);
  for (const LocalFace &f : faces_) {
    ++degree[static_cast<std::size_t>(f.left)];
    if (f.right >= 0) ++degree[static_cast<std::size_t>(f.right)];
  }
  cell_face_range_.assign(static_cast<std::size_t>(num_local), CellFaceRange{});
  Index total = 0;
  for (Index c = 0; c < num_local; ++c) {
    cell_face_range_[static_cast<std::size_t>(c)].begin = total;
    cell_face_range_[static_cast<std::size_t>(c)].count = 0;
    total += degree[static_cast<std::size_t>(c)];
  }
  cell_faces_.assign(static_cast<std::size_t>(total), -1);
  cell_face_sign_.assign(static_cast<std::size_t>(total), 0);
  for (Index i = 0; i < numFaces(); ++i) {
    const LocalFace &f = faces_[static_cast<std::size_t>(i)];
    {
      CellFaceRange &r = cell_face_range_[static_cast<std::size_t>(f.left)];
      const Index slot = r.begin + r.count++;
      cell_faces_[static_cast<std::size_t>(slot)] = i;
      cell_face_sign_[static_cast<std::size_t>(slot)] = 1;
    }
    if (f.right >= 0) {
      CellFaceRange &r = cell_face_range_[static_cast<std::size_t>(f.right)];
      const Index slot = r.begin + r.count++;
      cell_faces_[static_cast<std::size_t>(slot)] = i;
      cell_face_sign_[static_cast<std::size_t>(slot)] = -1;
    }
  }
}

void DistributedMesh::buildGradientStencils() {
  // Inverse-distance-weighted least-squares gradient.
  //
  // For cell i with neighbours j the gradient g minimises
  //   sum_j w_j^2 * ( g . (x_j - x_i) - (phi_j - phi_i) )^2 ,
  // whose solution is g = sum_j W_j (phi_j - phi_i) with the 2x2 normal-matrix
  // inverse folded into the per-neighbour weight vector W_j.  Because the
  // weights depend only on geometry they are precomputed once here, so the
  // per-iteration cost is one dot product per neighbour.
  //
  // Boundary faces contribute a stencil entry at the face centroid using the
  // imposed boundary state, which is what makes wall-normal gradients (and thus
  // skin friction) accurate in the first cell off the wall.
  const Index num_owned = num_owned_;
  grad_stencil_.assign(static_cast<std::size_t>(num_owned), GradientStencil{});
  grad_entries_.clear();
  grad_entries_.reserve(static_cast<std::size_t>(num_owned) * 4);

  for (Index c = 0; c < num_owned; ++c) {
    const CellFaceRange range = cell_face_range_[static_cast<std::size_t>(c)];
    const Vec2 xc = cells_[static_cast<std::size_t>(c)].centroid;

    // Collect offsets and weights.
    struct Sample {
      Index index{-1};
      StencilKind kind{StencilKind::kCell};
      Vec2 delta{};
      Real weight{1.0};
    };
    std::vector<Sample> samples;
    samples.reserve(static_cast<std::size_t>(range.count));

    for (Index k = 0; k < range.count; ++k) {
      const Index face_id = cell_faces_[static_cast<std::size_t>(range.begin + k)];
      const LocalFace &f = faces_[static_cast<std::size_t>(face_id)];
      Sample s;
      if (f.kind == FaceKind::kBoundary) {
        s.index = face_id;
        s.kind = StencilKind::kBoundaryFace;
        s.delta = f.geom.centroid - xc;
      } else {
        const Index other = (f.left == c) ? f.right : f.left;
        s.index = other;
        s.kind = StencilKind::kCell;
        s.delta = cells_[static_cast<std::size_t>(other)].centroid - xc;
      }
      const Real d = norm(s.delta);
      if (!(d > 0.0)) continue;
      s.weight = 1.0 / d;  // inverse-distance weighting
      samples.push_back(s);
    }

    // Assemble and invert the 2x2 weighted normal matrix.
    Real a11 = 0.0;
    Real a12 = 0.0;
    Real a22 = 0.0;
    for (const Sample &s : samples) {
      const Real w2 = s.weight * s.weight;
      a11 += w2 * s.delta.x * s.delta.x;
      a12 += w2 * s.delta.x * s.delta.y;
      a22 += w2 * s.delta.y * s.delta.y;
    }
    Real det = a11 * a22 - a12 * a12;
    // Regularise a rank-deficient stencil (possible for a cell with two nearly
    // collinear neighbours) instead of producing an unbounded gradient.
    const Real scale = std::max(a11 + a22, kTiny);
    if (std::abs(det) < 1.0e-12 * scale * scale) {
      a11 += 1.0e-9 * scale;
      a22 += 1.0e-9 * scale;
      det = a11 * a22 - a12 * a12;
    }
    const Real inv_det = (std::abs(det) > 0.0) ? 1.0 / det : 0.0;
    const Real i11 = a22 * inv_det;
    const Real i12 = -a12 * inv_det;
    const Real i22 = a11 * inv_det;

    grad_stencil_[static_cast<std::size_t>(c)].begin = static_cast<Index>(grad_entries_.size());
    grad_stencil_[static_cast<std::size_t>(c)].count = static_cast<Index>(samples.size());
    for (const Sample &s : samples) {
      const Real w2 = s.weight * s.weight;
      StencilEntry e;
      e.index = s.index;
      e.kind = s.kind;
      e.weight.x = w2 * (i11 * s.delta.x + i12 * s.delta.y);
      e.weight.y = w2 * (i12 * s.delta.x + i22 * s.delta.y);
      grad_entries_.push_back(e);
    }
  }
}

void DistributedMesh::fillDiagnostics() {
  diagnostics_.rank = rank_;
  diagnostics_.num_cells_owned = num_owned_;
  diagnostics_.num_cells_ghost = num_ghost_;
  diagnostics_.num_boundary_faces = static_cast<Index>(boundary_faces_.size());
  diagnostics_.num_neighbor_ranks = static_cast<int>(neighbors_.size());
  diagnostics_.neighbor_ranks.clear();
  diagnostics_.send_cells = 0;
  diagnostics_.recv_cells = 0;
  for (const NeighborPlan &p : neighbors_) {
    diagnostics_.neighbor_ranks.push_back(p.rank);
    diagnostics_.send_cells += static_cast<Index>(p.send_cells.size());
    diagnostics_.recv_cells += static_cast<Index>(p.recv_cells.size());
  }
}

std::string DistributedMesh::describe() const {
  std::ostringstream os;
  os << "rank " << rank_ << "/" << size_ << ": " << num_owned_ << " owned cells, " << num_ghost_
     << " ghost cells, " << faces_.size() << " faces, " << boundary_faces_.size()
     << " boundary faces, " << neighbors_.size() << " neighbour rank(s)";
  return os.str();
}

}  // namespace cns2d
