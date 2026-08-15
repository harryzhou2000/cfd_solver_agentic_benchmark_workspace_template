#pragma once

// Core 2D unstructured mesh data structures. The mesh stores global (serial)
// data; partitioning happens in a later phase.

#include <map>
#include <string>
#include <vector>

#include "common/types.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// Boundary condition types
// ---------------------------------------------------------------------------
enum class BCType {
  Interior,
  Farfield,
  SlipWall,
  NoSlipAdiabaticWall,
};

// Human-readable name (lowercase, matches case-file strings)
const char* bc_type_name(BCType t);

// Maps a case-file BC type string ("farfield", "slip_wall",
// "no_slip_adiabatic_wall") to a BCType. Throws std::invalid_argument for
// unknown names.
BCType bc_type_from_string(const std::string& name);

// ---------------------------------------------------------------------------
// Cell types
// ---------------------------------------------------------------------------
enum class CellType {
  Triangle = 0,
  Quadrilateral = 1,
};

// ---------------------------------------------------------------------------
// Cell2D
// ---------------------------------------------------------------------------
struct Cell2D {
  long long cell_id = -1;  // global (serial) cell id, 0-based
  Vector3 cell_center;     // centroid (computed by compute_geometry)
  double volume = 0.0;     // area in 2D (computed by compute_geometry)
  std::vector<int> faces;  // indices into Mesh2D::faces, in cyclic order
  CellType type = CellType::Triangle;
  std::vector<int> vertex_indices;  // global vertex ids, cyclic order (0-based)
};

// ---------------------------------------------------------------------------
// Face2D
// ---------------------------------------------------------------------------
struct Face2D {
  long long face_id = -1;  // global (serial) face id, 0-based
  Vector3 nodes[2];        // endpoint coordinates
  Vector3 center;          // midpoint (computed by compute_geometry)
  Vector3 normal;          // unit normal, points from left to right cell;
                           // points outward on boundary faces
  double area = 0.0;       // face length in 2D (computed by compute_geometry)
  int left_cell = -1;      // index into Mesh2D::cells; -1 if none
  int right_cell = -1;     // index into Mesh2D::cells; -1 if boundary
  BCType bc_type = BCType::Interior;
  std::string bc_tag;      // boundary family/tag name; empty for interior
};

// ---------------------------------------------------------------------------
// Mesh2D
// ---------------------------------------------------------------------------
struct Mesh2D {
  std::string zone_name;
  long long num_vertices = 0;        // global vertex count
  long long num_cells = 0;           // global cell count
  long long num_faces = 0;           // global face count
  long long num_boundary_faces = 0;  // global boundary face count

  // Global vertex coordinates (index-aligned with the vertex ids used by
  // Cell2D::vertex_indices). Populated by read_cgns_mesh; may be empty for
  // hand-built meshes (field writers require it).
  std::vector<Vector3> vertices;

  std::vector<Cell2D> cells;
  std::vector<Face2D> faces;
  // boundary face indices grouped by bc_tag (family name)
  std::map<std::string, std::vector<int>> boundary_faces;

  // Recomputes num_cells, num_faces and num_boundary_faces from the actual
  // vectors. Call after any structural modification (e.g. interface
  // stitching) so the counters never drift from the data. num_vertices is not
  // derivable from the mesh alone (vertex coordinates live outside Mesh2D)
  // and is left untouched.
  void finalize();
};

}  // namespace cfd
