#pragma once

#include "util/common.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cfds {

// Boundary condition kinds understood by the solver.
enum class BcType { Farfield, SlipWall, NoSlipAdiabaticWall, Interface, Count };

// A cell of the global (pre-partition) mesh.
struct Cell {
  std::vector<int> nodes;   // node ids in global node numbering
  double volume = 0.0;
  Vec2 centroid{0.0, 0.0};
};

// A face of the global mesh.
struct Face {
  int n0 = -1;              // node ids (global)
  int n1 = -1;
  int cellL = -1;           // cell on the left of the oriented edge (v0->v1)
  int cellR = -1;           // cell on the right; -1 for boundary faces
  double length = 0.0;      // edge length
  Vec2 centroid{0.0, 0.0};
  Vec2 normal{0.0, 0.0};    // unit normal pointing from cellL side to cellR side
  BcType bc = BcType::Count;  // meaningful for boundary faces
  std::string family;         // boundary family name for boundary faces
};

// Whole unstructured mesh assembled from one or more CGNS zones.
struct GlobalMesh {
  std::vector<Vec2> nodes;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<int> cell_face_offsets;   // CSR: cell -> faces
  std::vector<int> cell_faces;
  std::vector<int> cell_neighbor_offsets;  // CSR: cell -> neighbor cells
  std::vector<int> cell_neighbors;
  std::map<std::string, int> family_bc_face_count;  // family -> #boundary faces
};

// Load a CGNS mesh (one or more unstructured zones, mixed tri/quad elements,
// 1-to-1 zone interfaces, boundary families) into a single global mesh.
// Throws std::runtime_error on malformed or unsupported input.
GlobalMesh load_cgns_mesh(const std::string& path);

// Serialize / deserialize a global mesh (used to broadcast the mesh from
// rank 0 to all ranks during serial preprocessing).
std::vector<char> serialize_global_mesh(const GlobalMesh& mesh);
GlobalMesh deserialize_global_mesh(const char* data, size_t size);

// Build cell->face and cell->neighbor CSR adjacency after cells/faces exist.
void build_adjacency(GlobalMesh& mesh);

// Resolve a boundary family name to a solver BC kind; returns false if the
// family is not mapped (unknown boundary).
bool bc_from_string(const std::string& name, BcType& out);

const char* bc_to_string(BcType bc);

}  // namespace cfds
