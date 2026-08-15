#pragma once

#include "common.h"

namespace cfd {

enum class BCType { Interior = 0, Farfield, SlipWall, NoSlipAdiabaticWall, Interface };

const char* bc_type_name(BCType t);

struct Face {
  int ca = -1;   // local cell index (or -1 for unused)
  int cb = -1;   // -1 => boundary face
  double nx = 0.0, ny = 0.0;  // normal scaled by face length; points ca -> cb;
                              // boundary faces: points out of the fluid domain
  double len = 0.0;           // face length
  BCType bc = BCType::Interior;
  int bface = -1;             // index into boundary_faces, or -1
};

struct BoundaryFace {
  int face = -1;      // index into Mesh::faces
  int cell = -1;      // owner cell (local index)
  BCType bc = BCType::Interior;
  std::string family;
  double xm = 0.0, ym = 0.0;  // face midpoint
};

struct Cell {
  std::array<int, 4> nodes = {-1, -1, -1, -1};
  int nverts = 0;
  double vol = 0.0;
  double cx = 0.0, cy = 0.0;
};

struct Mesh {
  std::vector<Vec2> nodes;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<std::vector<int>> cell_faces;       // per cell: face indices
  std::vector<std::vector<int>> cell_face_sign;   // per cell: +1 if face.ca==cell else -1
  std::vector<BoundaryFace> boundary_faces;
  int num_cells_global = 0;
  int num_faces_global = 0;
  int num_nodes_global = 0;
};

// Reads a CGNS 2-D unstructured mesh (possibly multi-zone with 1-to-1
// abutting interfaces), builds geometry and the face/cell adjacency.
// `bc_map` maps CGNS boundary family names to solver BC types.
bool read_mesh(const std::string& cgns_path,
               const std::map<std::string, std::string>& bc_map,
               Mesh& mesh, std::string& err);

}  // namespace cfd
