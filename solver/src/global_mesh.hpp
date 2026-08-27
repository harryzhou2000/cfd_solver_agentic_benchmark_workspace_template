#pragma once
// Global (serial, preprocessing-stage) unstructured mesh read from CGNS.
// Used on rank 0 only, before partitioning. Handles multi-zone CGNS files
// with 1-to-1 abutting connectivity by merging duplicate interface nodes.
#include "common.hpp"
#include "case_config.hpp"

namespace fv {

struct GlobalMesh {
  // nodes (2-D; z dropped)
  vector<double> x, y;
  // cells: tri (3 nodes) or quad (4 nodes)
  vector<array<int, 4>> cell_nodes;
  vector<int> cell_nnodes;
  // boundary faces (bars) with family names
  vector<array<int, 2>> bface_nodes;
  vector<int> bface_family;  // index into families
  vector<string> families;

  // built face list (cell-face adjacency)
  vector<array<int, 2>> face_nodes;
  vector<int> face_c0, face_c1;      // face_c1 == -1 for boundary faces
  vector<int> face_bc_family;        // -1 for interior faces

  int numNodes() const { return (int)x.size(); }
  int numCells() const { return (int)cell_nodes.size(); }
  int numFaces() const { return (int)face_nodes.size(); }
};

// Read a CGNS file into a GlobalMesh (cells + boundary bars). Merges zones
// connected with 1-to-1 connectivity by unifying interface nodes.
GlobalMesh readCgnsMesh(const string& path);

// Build cell-face adjacency and match boundary bars to faces. Interior bar
// elements (e.g. inter-zone interface markers) that coincide with interior
// faces are dropped.
void buildFaces(GlobalMesh& m);

}  // namespace fv
