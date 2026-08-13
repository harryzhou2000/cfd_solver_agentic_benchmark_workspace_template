// Phase 2: METIS partitioning and rank-local mesh construction.
// See partition.h for the API contract.

#include "partition.h"

#include <metis.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <utility>

namespace cfd {

// ---------------------------------------------------------------------------
// METIS partitioning
// ---------------------------------------------------------------------------

PartitionResult partition_mesh(const Mesh& mesh, const PartitionConfig& cfg) {
  PartitionResult res;
  const int n_cells = static_cast<int>(mesh.cells.size());
  res.cell_part.assign(n_cells, 0);
  res.edge_cut = 0;

  if (cfg.nparts <= 1 || n_cells == 0) {
    // Trivial partition: every cell on rank 0, no cut edges.
    return res;
  }

  // METIS idx_t is int32_t in this build (IDXTYPEWIDTH 32), matching int on
  // this platform; copy the CSR into idx_t vectors anyway so the code stays
  // correct if METIS is rebuilt with 64-bit indices.
  std::vector<idx_t> xadj(n_cells + 1);
  for (int i = 0; i <= n_cells; ++i) {
    xadj[i] = static_cast<idx_t>(mesh.cell_neighbors_offsets[i]);
  }
  std::vector<idx_t> adjncy(mesh.cell_neighbors_data.size());
  for (size_t i = 0; i < mesh.cell_neighbors_data.size(); ++i) {
    adjncy[i] = static_cast<idx_t>(mesh.cell_neighbors_data[i]);
  }

  idx_t nvtxs = static_cast<idx_t>(n_cells);
  idx_t ncon = 1;
  // METIS requires nparts <= nvtxs; clamp defensively (unused in practice).
  idx_t nparts = static_cast<idx_t>(
      std::min(cfg.nparts, std::max(1, n_cells)));

  std::vector<idx_t> options(METIS_NOPTIONS);
  METIS_SetDefaultOptions(options.data());
  options[METIS_OPTION_NUMBERING] = 0;  // 0-based CSR (C-style)
  options[METIS_OPTION_CONTIG] = 1;     // contiguous partitions
  options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
  options[METIS_OPTION_DBGLVL] = 0;

  idx_t objval = 0;
  std::vector<idx_t> part(n_cells);

  // Unweighted graph: vwgt/vsize/adjwgt/tpwgts/ubvec all NULL -> unit vertex
  // weights, uniform target partition weights, default imbalance tolerance.
  const int status =
      METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr,
                          nullptr, nullptr, &nparts, nullptr, nullptr,
                          options.data(), &objval, part.data());
  if (status != METIS_OK) {
    throw std::runtime_error("partition_mesh: METIS_PartGraphKway failed "
                             "(status " + std::to_string(status) + ")");
  }

  res.cell_part.assign(part.begin(), part.end());
  res.edge_cut = static_cast<int>(objval);
  return res;
}

// ---------------------------------------------------------------------------
// Rank-local mesh
// ---------------------------------------------------------------------------

LocalMesh build_local_mesh(const Mesh& global_mesh,
                           const PartitionResult& part, int rank, int nranks) {
  LocalMesh lm;
  lm.rank = rank;
  lm.nranks = nranks;

  const int n_global = static_cast<int>(global_mesh.cells.size());
  if (n_global == 0) {
    return lm;  // empty mesh: empty local mesh
  }

  // ---- 1. Owned cells: ascending global id order (== vector order) ----
  std::vector<int> owned;
  owned.reserve(n_global / std::max(1, nranks) + 1);
  for (int gid = 0; gid < n_global; ++gid) {
    if (part.cell_part[gid] == rank) {
      owned.push_back(gid);
    }
  }

  // ---- 2. Ghost cells ----
  // A ghost of this rank is a cell owned by another rank that is adjacent
  // (via the cell-neighbor CSR) to at least one owned cell.
  std::vector<int> ghost_ids;                      // ascending global id order
  std::vector<int> ghost_owner;                    // owning rank per ghost
  std::vector<char> is_ghost(n_global, 0);         // visited marker
  {
    std::vector<int> seen_ghost;  // ghost ids in discovery order
    seen_ghost.reserve(owned.size());
    for (const int gid : owned) {
      const int begin = global_mesh.cell_neighbors_offsets[gid];
      const int end = global_mesh.cell_neighbors_offsets[gid + 1];
      for (int k = begin; k < end; ++k) {
        const int nbr = global_mesh.cell_neighbors_data[k];
        const int nbr_rank = part.cell_part[nbr];
        if (nbr_rank != rank && !is_ghost[nbr]) {
          is_ghost[nbr] = 1;
          seen_ghost.push_back(nbr);
        }
      }
    }
    // Canonical order: ascending global id. This is essential so that both
    // communicating ranks enumerate the same cells in the same order when
    // building halo send/recv lists.
    std::sort(seen_ghost.begin(), seen_ghost.end());
    ghost_ids.reserve(seen_ghost.size());
    ghost_owner.reserve(seen_ghost.size());
    for (const int gid : seen_ghost) {
      ghost_ids.push_back(gid);
      ghost_owner.push_back(part.cell_part[gid]);
    }
  }

  lm.n_owned = static_cast<int>(owned.size());
  lm.n_ghost = static_cast<int>(ghost_ids.size());

  // ---- 3. Local cell list (owned first, then ghosts) and maps ----
  lm.cells.reserve(owned.size() + ghost_ids.size());
  lm.local_to_global.reserve(owned.size() + ghost_ids.size());
  lm.global_to_local.reserve(owned.size() + ghost_ids.size());

  auto add_cell = [&](int gid) {
    const int local = static_cast<int>(lm.cells.size());
    lm.cells.push_back(global_mesh.cells[gid]);
    lm.local_to_global.push_back(gid);
    lm.global_to_local[gid] = local;
    return local;
  };
  for (const int gid : owned) {
    add_cell(gid);
  }
  for (const int gid : ghost_ids) {
    add_cell(gid);
  }

  // ---- 4. Local node subset + remap Cell::nodes to local indices ----
  for (Cell& cell : lm.cells) {
    for (int k = 0; k < cell.n_nodes; ++k) {
      const int gnode = cell.nodes[k];
      auto it = lm.global_node_to_local.find(gnode);
      if (it == lm.global_node_to_local.end()) {
        const int local = static_cast<int>(lm.nodes.size());
        lm.global_node_to_local[gnode] = local;
        lm.nodes.push_back(global_mesh.nodes[gnode]);
        cell.nodes[k] = local;
      } else {
        cell.nodes[k] = it->second;
      }
    }
  }

  // ---- 5. Face list ----
  // Include every global face with at least one adjacent owned cell. The
  // LocalFace invariant is "left is always an owned local cell": when the
  // global face's left cell is not owned but its right cell is, the face is
  // flipped (normal negated to keep the left -> right orientation).
  //
  // Inter-rank faces must be kept by BOTH adjacent ranks (one side flipped)
  // for conservation: each rank computes the flux on its own copy of the
  // face, and antisymmetry of the numerical flux (F(U_L, U_R) against the
  // normal equals -F(U_R, U_L) against the negated normal) guarantees that
  // the two ranks contribute exactly cancelling fluxes. Dropping the face on
  // either side would leak flux across the partition boundary. (The Phase 1
  // mesh orients internal faces left = smaller global cell id, right = larger
  // id, so without the flip each rank would keep only one side of each
  // inter-rank face.)
  std::unordered_map<int, std::pair<BCType, std::string>> bf_tags;
  bf_tags.reserve(global_mesh.boundary_faces.size());
  for (const BoundaryFace& bf : global_mesh.boundary_faces) {
    bf_tags[bf.face_id] = {bf.bc_type, bf.family};
  }

  lm.faces.reserve(global_mesh.faces.size() / std::max(1, nranks) + 16);
  for (const Face& f : global_mesh.faces) {
    const bool left_owned =
        f.left_cell >= 0 && part.cell_part[f.left_cell] == rank;
    const bool right_owned =
        f.right_cell >= 0 && part.cell_part[f.right_cell] == rank;
    if (!left_owned && !right_owned) {
      continue;  // face touches no owned cell of this rank
    }

    LocalMesh::LocalFace lf;
    lf.centroid = f.centroid;
    lf.area = f.area;
    lf.bc_type = BCType::Invalid;

    if (left_owned) {
      lf.left = lm.global_to_local.at(f.left_cell);
      lf.normal = f.normal;
      if (f.right_cell >= 0) {
        if (part.cell_part[f.right_cell] == rank) {
          lf.right = lm.global_to_local.at(f.right_cell);
          lf.right_global = -1;
        } else {
          lf.right = -2;  // ghost owned by another rank
          lf.right_global = f.right_cell;
          lf.right_local = lm.global_to_local.at(f.right_cell);
        }
      } else {
        lf.right = -1;  // boundary
        const auto it = bf_tags.find(f.id);
        if (it != bf_tags.end()) {
          lf.bc_type = it->second.first;
          lf.bc_family = it->second.second;
        }
        ++lm.n_boundary_faces;
      }
    } else {
      // right is owned, left belongs to another rank: flip the face so the
      // owned cell is `left`, and negate the normal (area vector) so it still
      // points from left to right.
      lf.left = lm.global_to_local.at(f.right_cell);
      lf.normal = f.normal * -1.0;
      lf.right = -2;              // the original left cell is a ghost
      lf.right_global = f.left_cell;
      lf.right_local = lm.global_to_local.at(f.left_cell);
    }
    lm.faces.push_back(std::move(lf));
  }

  // ---- 6. Per-cell face CSR (owned cells only) ----
  // Local face indices touching each owned cell, mirroring the serial mesh's
  // cell_faces CSR (Phase 1): a face is listed for BOTH adjacent owned cells
  // (its `left` and, for same-rank internal faces, its `right`) because
  // gradient reconstruction and residual accumulation over cell i need every
  // face of i. Ghost (right == -2) and boundary (right == -1) faces
  // contribute only through `left`, the owned cell; ghost cells are outside
  // the owned range 0..n_owned-1 and are skipped.
  std::vector<int> face_counts(lm.n_owned, 0);
  for (const LocalMesh::LocalFace& lf : lm.faces) {
    ++face_counts[lf.left];  // left is always an owned cell
    if (lf.right >= 0) {
      ++face_counts[lf.right];  // same-rank internal face
    }
  }
  lm.cell_faces_offsets.resize(lm.n_owned + 1);
  lm.cell_faces_offsets[0] = 0;
  for (int i = 0; i < lm.n_owned; ++i) {
    lm.cell_faces_offsets[i + 1] =
        lm.cell_faces_offsets[i] + face_counts[i];
  }
  lm.cell_faces_data.assign(lm.cell_faces_offsets.back(), 0);
  std::vector<int> fill_pos = lm.cell_faces_offsets;
  for (size_t i = 0; i < lm.faces.size(); ++i) {
    const LocalMesh::LocalFace& lf = lm.faces[i];
    lm.cell_faces_data[fill_pos[lf.left]++] = static_cast<int>(i);
    if (lf.right >= 0) {
      lm.cell_faces_data[fill_pos[lf.right]++] = static_cast<int>(i);
    }
  }

  // ---- 7. Halo exchange pattern ----
  // recv: group ghosts by owning rank. ghost_ids is ascending by global id,
  // so recv_cells end up ascending by local index (== ascending global id).
  std::map<int, std::vector<int>> recv_by_rank;
  for (size_t i = 0; i < ghost_ids.size(); ++i) {
    recv_by_rank[ghost_owner[i]].push_back(lm.global_to_local.at(ghost_ids[i]));
  }

  // send: for each owned cell, note the neighbor ranks it touches; an owned
  // cell is sent to a rank iff at least one of its neighbors is owned there.
  // std::set keeps send_cells sorted by local index (== ascending global id),
  // matching the order used by the receiving rank's recv_cells.
  std::map<int, std::set<int>> send_by_rank;
  for (const int gid : owned) {
    const int begin = global_mesh.cell_neighbors_offsets[gid];
    const int end = global_mesh.cell_neighbors_offsets[gid + 1];
    for (int k = begin; k < end; ++k) {
      const int nbr = global_mesh.cell_neighbors_data[k];
      const int nbr_rank = part.cell_part[nbr];
      if (nbr_rank != rank) {
        send_by_rank[nbr_rank].insert(lm.global_to_local.at(gid));
      }
    }
  }

  lm.neighbor_ranks.reserve(send_by_rank.size());
  lm.halo_exchanges.reserve(send_by_rank.size());
  for (const auto& [nbr_rank, send_set] : send_by_rank) {
    lm.neighbor_ranks.push_back(nbr_rank);
    LocalMesh::HaloExchange ex;
    ex.neighbor_rank = nbr_rank;
    ex.send_cells.assign(send_set.begin(), send_set.end());
    auto recv_it = recv_by_rank.find(nbr_rank);
    if (recv_it != recv_by_rank.end()) {
      ex.recv_cells = recv_it->second;
    }
    lm.halo_exchanges.push_back(std::move(ex));
  }

  return lm;
}

}  // namespace cfd
