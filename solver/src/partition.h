#pragma once
// Phase 2: METIS partitioning and rank-local mesh construction.
//
// partition_mesh() runs serial METIS (METIS_PartGraphKway) over the full
// global mesh's cell adjacency CSR, producing a cell -> rank mapping.
//
// build_local_mesh() then builds, on every rank, the LocalMesh: the cells
// owned by this rank plus the ghost cells needed for the finite-volume
// stencil, the subset of faces touching owned cells, and the halo exchange
// pattern (which cells to send to / receive from each neighbor rank).
//
// The solver iterations (Phase 3+) run exclusively on the LocalMesh; the
// global Mesh is only used for this preprocessing.

#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace cfd {

// ---------------------------------------------------------------------------
// Partitioning
// ---------------------------------------------------------------------------

struct PartitionConfig {
  int nparts = 1;  // number of MPI ranks (partitions)
  // (imbalance tolerance, objective weighting, ... can be added later)
};

struct PartitionResult {
  std::vector<int> cell_part;  // cell_part[global_cell_id] = owning rank
  int edge_cut = 0;            // total weight of edges crossing partitions
};

// Partition the global mesh cells among cfg.nparts ranks using METIS
// (serial k-way graph partitioning). Returns the cell -> rank mapping and the
// edge cut. For nparts == 1 (or an empty mesh) the trivial partition is
// returned (all cells on rank 0, edge cut 0) without calling METIS.
//
// Throws std::runtime_error if METIS fails.
PartitionResult partition_mesh(const Mesh& mesh, const PartitionConfig& cfg);

// ---------------------------------------------------------------------------
// Rank-local mesh
// ---------------------------------------------------------------------------

struct LocalMesh {
  int rank = 0;
  int nranks = 1;

  // Cell data. Local index order: the first n_owned cells are owned by this
  // rank, the following n_ghost cells are ghosts (owned by other ranks).
  std::vector<Cell> cells;
  int n_owned = 0;  // first n_owned cells are owned
  int n_ghost = 0;

  // Global cell id of each local cell (index-aligned with `cells`).
  std::vector<int> local_to_global;
  // Reverse map: global cell id -> local index (present for owned + ghosts).
  std::unordered_map<int, int> global_to_local;

  // Node coordinates referenced by local cells. Cell::nodes in `cells` hold
  // LOCAL node indices into this vector (remapped from the global mesh).
  std::vector<Vec2> nodes;
  // Reverse map: global node id -> local node index (only for nodes used by
  // local cells).
  std::unordered_map<int, int> global_node_to_local;

  // Faces touching the owned cells of this rank. Each global face appears on
  // every rank that owns one of its adjacent cells. For inter-rank faces,
  // both sides store the face and compute the flux; antisymmetry of the
  // numerical flux guarantees conservation. Boundary faces appear on exactly
  // one rank. `left` is always a local owned cell index.
  struct LocalFace {
    int left = -1;  // local cell index (owned)
    // right: >= 0  internal face, local cell index of the right cell
    //        -1    boundary face (right_local/right_global unused;
    //              bc_type/bc_family hold the boundary condition)
    //        -2    ghost cell owned by another rank (right_global is its
    //              global id, right_local its local index in `cells`)
    int right = -1;
    int right_global = -1;  // global cell id if right is a ghost cell
    int right_local = -1;   // local cell index if right is a ghost cell
    Vec2 centroid;
    Vec2 normal;              // area vector, oriented left -> right
    double area = 0.0;
    BCType bc_type = BCType::Invalid;  // only set for boundary faces
    std::string bc_family;             // boundary family name if applicable
  };
  std::vector<LocalFace> faces;
  int n_boundary_faces = 0;  // count of faces with right == -1

  // Per-cell face CSR over the OWNED cells only (indices 0..n_owned-1):
  //   cell_faces_offsets[i]..cell_faces_offsets[i+1] -> cell_faces_data
  //     = local face indices touching owned cell i.
  // A face is listed for every owned cell it touches: `left`, plus `right`
  // for same-rank internal faces (mirroring the serial mesh's cell_faces
  // CSR, where each face is referenced from both adjacent cells).
  // Ghost cells are excluded because gradients are reconstructed on owned
  // cells only (Phase 3). The offsets array has size n_owned + 1.
  std::vector<int> cell_faces_offsets;
  std::vector<int> cell_faces_data;

  // Neighbor ranks (ascending), with the halo exchange pattern for each.
  std::vector<int> neighbor_ranks;
  struct HaloExchange {
    int neighbor_rank = -1;
    std::vector<int> send_cells;  // local indices of owned cells to send
    std::vector<int> recv_cells;  // local indices of ghost cells to receive
  };
  std::vector<HaloExchange> halo_exchanges;
};

// Build this rank's LocalMesh from the full global mesh. Every rank calls
// this with the complete serial mesh (preprocessing only; the solver never
// replicates the global mesh during iterations).
//
// Ordering contract (important for halo exchanges): owned cells are added in
// ascending global id order, and ghost cells are added in ascending global id
// order. Consequently send_cells/recv_cells of any halo exchange are sorted
// by global cell id on both communicating ranks, so message payloads line up
// positionally.
LocalMesh build_local_mesh(const Mesh& global_mesh,
                           const PartitionResult& part, int rank, int nranks);

}  // namespace cfd
