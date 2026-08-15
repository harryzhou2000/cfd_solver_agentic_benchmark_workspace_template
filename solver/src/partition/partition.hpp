#pragma once

// MPI partitioning: METIS cell-graph partitioning and rank-local mesh
// construction. Serial preprocessing is used (all ranks read the serial mesh,
// all ranks run METIS deterministically, each rank builds its own local
// data), which is acceptable per TASK.md section 49.

#include <mpi.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mesh/mesh.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// Per-neighbor halo exchange lists.
// ---------------------------------------------------------------------------
struct NeighborInfo {
  int rank = -1;                    // neighbor rank
  std::vector<int> send_cells;      // local indices of owned cells whose
                                    // conservative state the neighbor needs
                                    // (as ghosts)
  std::vector<int> recv_cells;      // local indices of ghost cells to receive
                                    // state into (owned by this neighbor)
};

// ---------------------------------------------------------------------------
// Per-rank partition diagnostics (filled by build_distributed_mesh).
// ---------------------------------------------------------------------------
struct PartitionInfo {
  int rank = 0;
  int nranks = 1;

  long long n_owned = 0;              // owned cells on this rank
  long long n_ghost = 0;              // ghost cells on this rank
  long long n_local = 0;              // owned + ghost
  long long n_interior_faces = 0;     // both cells owned by this rank
  long long n_boundary_faces = 0;     // one owned cell, other side -1
  long long n_send_faces = 0;         // owned (left) <-> ghost (right)
  long long n_recv_faces = 0;         // ghost (left) <-> owned (right)
  int edge_cut = 0;                   // global interface faces (partition edges)

  // owned cell ranges per rank: (start global cell index, count)
  std::vector<std::pair<long long, long long>> owned_ranges;

  // ghost cell ranges on this rank: (start local index, count) per neighbor
  // rank, index-aligned with neighbor_ranks
  std::vector<std::pair<long long, long long>> ghost_ranges;

  // global ids of boundary faces owned by this rank
  std::vector<long long> owned_boundary_faces;

  // per-neighbor diagnostics (index-aligned)
  std::vector<int> neighbor_ranks;
  std::vector<int> send_counts;  // cells sent to each neighbor
  std::vector<int> recv_counts;  // cells received from each neighbor
};

// ---------------------------------------------------------------------------
// DistributedMesh: rank-local view of the partitioned mesh.
// Cell layout: [0, n_owned) owned cells, then ghost cells appended (grouped
// by owner rank). Cell2D::cell_id is the LOCAL index; the global serial
// index is available via global_cell_id.
// ---------------------------------------------------------------------------
struct DistributedMesh {
  int rank = 0;
  int nranks = 1;

  std::vector<Cell2D> cells;       // owned then ghosts (geometry copied)
  long long n_owned = 0;
  std::vector<int> owner_rank;         // per local cell
  std::vector<long long> global_cell_id;  // per local cell
  // global serial cell index -> local index (owned AND ghost cells)
  std::map<long long, int> global_to_local;

  // Face lists (left/right cells remapped to LOCAL indices; face_id keeps the
  // global serial face id; normals point from left to right as in the serial
  // mesh):
  //  - interior: both cells owned by this rank
  //  - boundary: one owned cell, the other side is -1 (bc_type/bc_tag kept)
  //  - send:     left owned, right ghost (data must be sent to the neighbor)
  //  - recv:     left ghost, right owned (data must be received)
  // Faces touching two ghosts are not stored locally.
  std::vector<Face2D> interior_faces;
  std::vector<Face2D> boundary_faces;
  std::vector<Face2D> send_faces;
  std::vector<Face2D> recv_faces;

  std::vector<NeighborInfo> neighbors;  // sorted by neighbor rank

  PartitionInfo info;

  // Global vertex coordinates (index-aligned with the vertex ids used by
  // Cell2D::vertex_indices), copied from the serial mesh. Used by the VTU
  // field writer; may be empty for hand-built meshes.
  std::vector<Vector3> vertices;
};

// ---------------------------------------------------------------------------
// Cell graph + METIS partitioning
// ---------------------------------------------------------------------------

// Builds the cell-cell adjacency graph (interior faces only) in METIS CSR
// format and returns it as one flat vector: xadj (n+1 entries) followed by
// adjncy. Adjacency lists are sorted and deduplicated per cell.
std::vector<int> build_cell_graph(const Mesh2D& mesh);

// Partitions the cell graph into `nparts` parts with METIS_PartGraphKway
// (contiguity on, fixed seed for cross-rank determinism). Returns the cell ->
// rank map (0-based). A fixed seed guarantees that every rank computing the
// same serial mesh obtains the identical partition.
std::vector<int> partition_cells(const Mesh2D& mesh, int nparts);

// Builds the rank-local DistributedMesh for `rank` from the serial mesh and
// the global partition. Pure serial function (no MPI calls).
DistributedMesh build_distributed_mesh(const Mesh2D& mesh, int rank,
                                       int nranks,
                                       const std::vector<int>& partition);

// Prints per-rank diagnostics (gathered to rank 0) plus the global load
// balance summary: min/max/mean owned cells, imbalance ratio (max/mean) and
// the partition edge cut.
void partition_diagnostics(const DistributedMesh& dmesh, MPI_Comm comm);

}  // namespace cfd
