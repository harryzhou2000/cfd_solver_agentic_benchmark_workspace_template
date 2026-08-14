#pragma once

#include "io/Input.hpp"
#include "mesh/GlobalMesh.hpp"

#include <string>
#include <vector>

#include <mpi.h>

namespace cfds {

struct LocalFace {
  int cellL = -1;
  int cellR = -1;         // -1 => boundary face
  double length = 0.0;
  Vec2 centroid{0.0, 0.0};
  Vec2 normal{0.0, 0.0};  // unit normal: cellL -> cellR (boundary: outward of cellL)
  int bc = -1;            // BcType for boundary faces, -1 for internal
  int bnd_nodes[2] = {-1, -1};  // global node ids (boundary faces only)
  int family_id = -1;     // index into family_names (boundary faces only)
};

// Rank-local mesh: owned cells first, then ghost cells. All geometry and
// connectivity needed by the solver iterations lives here; no global mesh
// arrays are kept in the iteration path (except on rank 0 for output).
struct DistributedMesh {
  int rank = 0;
  int n_owned = 0;
  int n_ghost = 0;
  int n_local = 0;

  std::vector<Vec2> cell_centroid;   // owned + ghost
  std::vector<double> cell_volume;   // owned + ghost
  std::vector<std::vector<int>> cell_nodes;  // owned only (global ids)
  std::vector<int> owned_global_ids;          // owned only (global cell ids)

  std::vector<LocalFace> faces;
  std::vector<int> cell_face_offsets;      // n_local+1 CSR
  std::vector<int> cell_faces;
  std::vector<int> cell_neighbor_offsets;  // n_local+1 CSR (local cell ids)
  std::vector<int> cell_neighbors;

  std::vector<std::string> family_names;

  // Halo exchange bookkeeping.
  std::vector<int> neighbor_ranks;
  std::vector<std::vector<int>> send_cells;  // per neighbor: owned local ids
  std::vector<std::vector<int>> recv_cells;  // per neighbor: ghost local ids

  // Per-rank partition diagnostics (filled on every rank).
  int num_boundary_faces = 0;
  std::vector<int> neighbor_rank_list;
  int send_count = 0;
  int recv_count = 0;

  int n_owned_local() const { return n_owned; }
  int n_ghost_local() const { return n_ghost; }
};

// Partition the global mesh with METIS k-way, build rank-local meshes with
// ghost cells and halo bookkeeping, and distribute to all ranks.
// The full global mesh is retained only on rank 0 (for output files).
// Returns the partition edge cut (global).
int distribute_mesh(const GlobalMesh& global, const CaseConfig& cfg,
                    DistributedMesh& local, MPI_Comm comm);

// Broadcast a full global mesh from rank 0 to all ranks (serial
// preprocessing; the mesh is freed after the local mesh is built).
GlobalMesh broadcast_global_mesh(const GlobalMesh& local_mesh, MPI_Comm comm);

// Global partition summaries (called on every rank with the local mesh).
struct PartitionSummary {
  int edge_cut = 0;
  int min_owned = 0, max_owned = 0;
  double mean_owned = 0.0, load_balance = 1.0;
  int total_boundary_faces = 0;
};
PartitionSummary partition_summary(const DistributedMesh& local, MPI_Comm comm);

}  // namespace cfds
