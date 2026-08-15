// Unstructured 2-D mesh: CGNS import, cell/face geometry, boundary-family
// extraction, and multi-zone interface stitching. The solver works on a
// global cell/face array built here; partitioning then distributes it.
//
// Reader strategy (matches the supplied NACA0012_H2 / CylinderB1 meshes):
//   read every zone's coordinates and volume Elements_t sections (TRI_3,
//   QUAD_4, ...); each volume element becomes one cell with a global id.
//   per zone, build an edge hash (sorted zone-local vertex pairs) so every
//   internal edge shared by two cells becomes one internal face, and every
//   edge seen once becomes a boundary edge. Tag boundary edges by looking up
//   their section name in the case boundary-condition map; names not in the
//   map are inter-zone interfaces ("con-N") paired across zones by edge
//   midpoint. Face normals point from the left/owner cell toward the right
//   cell (and outward for real boundary faces), using CCW cell orientation.
#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace cfd {

struct Cell {
  Vec2 center;
  double area = 0.0;
  std::vector<Vec2> verts;     // CCW corner coordinates (for output)
  std::vector<int> face_ids;  // faces touching this cell
  int global_id = -1;
};

struct Face {
  Vec2 p1, p2;        // endpoints, oriented for the left/owner cell
  Vec2 center;
  double Sx = 0, Sy = 0;  // area-weighted normal (lc -> rc), |S| = length
  double len = 0;
  int lc = -1;        // left/owner cell (global id)
  int rc = -1;        // right cell (global id), -1 if real boundary
  BCType bctype = BCType::Internal;
  std::string family; // family name for boundary faces (empty for internal)
  int lc_owner = -1;  // owning rank of lc/rc (filled after partitioning)
  int rc_owner = -1;
};

struct Mesh {
  std::vector<Cell> cells;       // global cells
  std::vector<Face> faces;       // global faces
  std::vector<int> wall_face_ids;   // boundary faces on no-slip/slip walls
  std::vector<int> farfield_face_ids;
  int num_internal_faces = 0;
  int num_boundary_faces = 0;
  Vec2 bbox_min{0,0}, bbox_max{0,0};
  double diag = 1.0;
  std::string mesh_file;
};

// Load a CGNS mesh and build the global cell/face array. bc_map maps mesh
// boundary-family names (e.g. "bc-2","WALL") to solver BC types; families not
// present in bc_map are treated as inter-zone interfaces and stitched.
Mesh load_cgns(const std::string& path,
               const std::unordered_map<std::string, BCType>& bc_map);

}  // namespace cfd
