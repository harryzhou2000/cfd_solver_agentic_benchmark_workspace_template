#pragma once
// Global (preprocessing-stage) unstructured mesh: CGNS import, multi-zone
// merge through 1-to-1 interfaces, and FV geometry construction.

#include <string>
#include <vector>
#include "common.hpp"

namespace cfd {

struct GlobalMesh {
  // Nodes (after zone merge).
  int n_nodes = 0;
  std::vector<double> node_x, node_y;

  // Cells: triangles and quads, CCW node order.
  int n_cells = 0;
  std::vector<std::array<int, 4>> cell_nodes;  // padded with -1
  std::vector<int> cell_nnodes;                // 3 or 4
  std::vector<int> cell_zone;                  // origin zone (informational)

  // Faces. Internal faces come first, boundary faces after.
  int n_faces_internal = 0;
  int n_faces_boundary = 0;
  std::vector<int> face_cell_l;   // owner cell (normal points out of L)
  std::vector<int> face_cell_r;   // neighbor cell, -1 for boundary
  std::vector<int> face_n0, face_n1;
  std::vector<int> face_bc_family;  // -1 internal, else index into bc_family_names
  std::vector<std::string> bc_family_names;

  // Geometry.
  std::vector<double> cell_cx, cell_cy, cell_vol;
  std::vector<double> face_nx, face_ny, face_area;  // unit normal from L to R
  std::vector<double> face_cx, face_cy;

  // Cell adjacency (through internal faces), CSR.
  std::vector<int> adj_start, adj_list;

  int family_index(const std::string& name) const;
};

// Reads a CGNS file (all unstructured zones), merges conformal zone
// interfaces, builds faces/geometry/adjacency. Throws on malformed input.
GlobalMesh read_cgns_mesh(const std::string& path);

// Computes volumes, centroids, face normals/areas/centers, adjacency.
void build_geometry(GlobalMesh& m);

// Human-readable summary (used by the meshinfo debug subcommand).
std::string mesh_summary(const GlobalMesh& m);

}  // namespace cfd
