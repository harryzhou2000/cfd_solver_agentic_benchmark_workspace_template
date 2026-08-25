#pragma once
// METIS cell-graph partitioning and rank-local mesh construction.
// Serial preprocessing (rank 0) builds the global mesh once, calls METIS,
// and writes per-rank partition files. The solver stage loads only the
// rank-local owned cells plus one layer of ghost cells.

#include <string>
#include <vector>
#include "common.hpp"
#include "mesh.hpp"

namespace cfd {

struct LocalMesh {
  int rank = 0, n_ranks = 1;

  int n_owned = 0;                 // owned cells: indices [0, n_owned)
  int n_ghost = 0;                 // ghost cells: [n_owned, n_owned + n_ghost)
  int n_cells = 0;                 // owned + ghost
  std::vector<int> cell_global;    // global cell id per local cell
  std::vector<double> cx, cy, vol;
  // Polygon connectivity for output (local node ids).
  std::vector<int> cell_node_start, cell_node_list;

  int n_nodes = 0;
  std::vector<double> node_x, node_y;
  std::vector<int> node_global;

  // Faces incident to at least one owned cell. face_l is always owned.
  int n_faces = 0;
  std::vector<int> face_l, face_r;   // local cell ids; face_r == -1 -> boundary
  std::vector<double> face_nx, face_ny, face_area;
  std::vector<double> face_cx, face_cy;
  std::vector<int> face_bc;          // -1 internal, else family index
  std::vector<std::string> bc_names; // boundary family names (global ordering)

  // Cell -> faces CSR (owned cells only).
  std::vector<int> cell_face_start, cell_face_list;
  // LSQ stencil geometry per cell-face entry: vector to neighbor/virtual
  // center and the inverse-distance-squared weight.
  std::vector<double> cf_dx, cf_dy, cf_w;
  // Per-owned-cell symmetric 2x2 LSQ inverse (m00, m01, m11).
  std::vector<double> lsq_m00, lsq_m01, lsq_m11;

  // Neighbor-scoped halo description.
  std::vector<int> nbr_rank;
  std::vector<int> send_start, send_cells;  // owned local ids, sorted by global id
  std::vector<int> recv_start, recv_cells;  // ghost local ids, sorted by global id

  int numBoundaryFacesOwned() const;
};

struct PartitionInfo {
  int n_cells_global = 0;
  int n_faces_global = 0;   // internal + boundary
  int n_nodes_global = 0;
  int edge_cut = 0;
  std::vector<std::string> bc_names;
};

// Serial preprocessing on rank 0: read CGNS, partition with METIS, write
// per-rank partition files and a JSON summary. Returns false if a valid
// cache already exists.
bool preprocess_partition(const std::string& mesh_file, int n_ranks,
                          const std::string& out_dir, std::string* error);

// Loads this rank's partition file (written by preprocess_partition).
LocalMesh load_local_partition(const std::string& out_dir, int rank, int n_ranks,
                               PartitionInfo& info);

// Per-rank diagnostics gathered during the actual solver run (CSV text).
std::string partition_diagnostics_csv(const LocalMesh& lm, const PartitionInfo& info);

}  // namespace cfd
