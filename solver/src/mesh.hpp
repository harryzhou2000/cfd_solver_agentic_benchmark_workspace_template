#pragma once

#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// Global (preprocessing-time) unstructured mesh, built on rank 0 from CGNS.
// Freed on every rank before solver iterations begin.
// ---------------------------------------------------------------------------
struct GlobalMesh {
  struct Cell {
    int nv = 0;
    int v[4] = {-1, -1, -1, -1};  // global node ids (representatives)
    double cx = 0.0, cy = 0.0, vol = 0.0;
  };
  struct Face {
    int v0 = -1, v1 = -1;  // global node ids
    double fx = 0.0, fy = 0.0;
    double nx = 0.0, ny = 0.0;  // unit normal, outward from c0
    double area = 0.0;
    int c0 = -1, c1 = -1;  // -1 = boundary
    int bc = -1;           // BcKind index or -1 for internal
    int section = -1;      // index into gm.sections for boundary faces
  };
  struct Section {
    std::string family;
    int bc = -1;  // BcKind
    std::vector<int> face_ids;
  };

  int n_nodes = 0;
  std::vector<double> x, y;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<Section> sections;
  int num_boundary_faces = 0;
  int n_faces_global = 0;
  int edge_cut = 0;

  void clear() {
    x.clear(); y.clear(); cells.clear(); faces.clear(); sections.clear();
    x.shrink_to_fit(); y.shrink_to_fit(); cells.shrink_to_fit();
    faces.shrink_to_fit(); sections.shrink_to_fit();
  }
};

// ---------------------------------------------------------------------------
// Rank-local mesh: owned cells plus one layer of ghost cells, local faces
// (owned-owned and owned-ghost), boundary faces, and halo exchange lists.
// ---------------------------------------------------------------------------
struct LocalMesh {
  struct Cell {
    int global_id = -1;
    int owner = -1;    // -1 for owned, else owning rank
    int remote_id = -1;  // index in owner's owned list (ghosts)
    int nv = 0;
    int v[4] = {-1, -1, -1, -1};  // local node ids
    double cx = 0.0, cy = 0.0, vol = 0.0;
  };
  struct Face {
    int c0 = -1, c1 = -1;  // local cell ids; c1 == -1 for boundary
    int bc = -1;           // BcKind or -1
    int global_id = -1;
    int v0 = -1, v1 = -1;  // local node ids of the edge
    std::string tag;       // boundary family name (boundary faces only)
    double fx = 0.0, fy = 0.0;
    double nx = 0.0, ny = 0.0;
    double tx = 0.0, ty = 0.0;  // tangent, freestream-aligned (wall output)
    double area = 0.0;
  };

  int n_owned = 0;
  int n_cells = 0;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<std::vector<int>> cell_faces;
  std::vector<double> node_x, node_y;
  std::vector<int> node_global_id;
  int n_nodes = 0;

  // Halo exchange topology (neighbor-scoped).
  std::vector<int> neighbor_ranks;
  std::vector<std::vector<int>> send_cells;   // per neighbor: owned local ids
  std::vector<std::vector<int>> recv_cells;   // per neighbor: ghost local ids
  std::vector<std::vector<int>> recv_remote;  // per neighbor: remote owned ids

  // Wall boundary faces (slip + no-slip) for surface output / forces.
  std::vector<int> wall_faces;
  std::vector<int> far_faces;
  int num_boundary_faces = 0;

  // Sweep ordering for the LU-SGS relaxation (owned cells only), ordered along
  // the dominant flow direction (x) to accelerate Gauss-Seidel convergence.
  std::vector<int> sweep_order;
  std::vector<int> sweep_pos;  // owned cell -> position in sweep_order

  bool is_ghost(int c) const { return c >= n_owned; }
  int neighbor_index(int rank) const;  // -1 if not a neighbor
};

// Reads a CGNS mesh and builds the stitched global mesh.
GlobalMesh read_cgns_mesh(const std::string& path, const std::vector<BcSpec>& bcs);

// METIS-partitions the global cell graph on rank 0 and distributes rank-local
// meshes to every rank. After this call the GlobalMesh is destroyed everywhere.
void partition_and_scatter(GlobalMesh& gm, const CaseConfig& cfg, LocalMesh& lm,
                           int& edge_cut, int& n_faces_global);

// Prints a compact mesh summary (rank 0).
void print_mesh_summary(const GlobalMesh& gm, const CaseConfig& cfg);

}  // namespace cfd
