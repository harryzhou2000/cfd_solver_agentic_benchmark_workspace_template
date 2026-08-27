// cns2d -- mesh data structures.
//
// Two levels of mesh exist:
//   * RawMesh      -- exactly what the CGNS file contains, per zone.
//   * GlobalMesh   -- one merged, zone-joined, face-based unstructured mesh
//                     built on the preprocessing rank only.
// The solver itself never touches either; it works on the DistributedMesh
// (see parallel/distributed_mesh.h), which stores only rank-local cells.
//
// Element topology is described through a small table (kElementTable) instead
// of hard-coded switch statements, so adding 3-D element types later means
// adding table rows rather than editing algorithms.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "core/types.h"

namespace cns2d {

// Supported element shapes.  The list is intentionally open-ended: 3-D shapes
// can be appended without changing consuming code.
enum class ElementShape {
  kBar2,   // 1-D line, used for 2-D boundary faces
  kTri3,
  kQuad4,
  kUnsupported,
};

struct ElementTopology {
  ElementShape shape{ElementShape::kUnsupported};
  int num_nodes{0};
  int dimension{0};  // topological dimension: 1 = edge, 2 = face/cell in 2-D
  const char *name{"unsupported"};
};

ElementTopology topologyForShape(ElementShape shape);
// Map a CGNS ElementType_t integer onto a supported shape.
ElementShape shapeFromCgnsElementType(int cgns_element_type);
const char *elementShapeName(ElementShape shape);

// ---------------------------------------------------------------------------
// RawMesh: verbatim CGNS content.
// ---------------------------------------------------------------------------

// A CGNS Elements_t section.
struct RawSection {
  std::string name;
  ElementShape shape{ElementShape::kUnsupported};
  int cgns_element_type{0};
  GlobalIndex first_element{0};  // 1-based CGNS element range
  GlobalIndex last_element{0};
  // Zone-local 0-based node indices, num_nodes per element, row-major.
  std::vector<Index> connectivity;
  Index num_elements{0};
  int num_nodes_per_element{0};
};

// A boundary condition patch as stored in CGNS ZoneBC.
struct RawBoco {
  std::string name;
  std::string family_name;  // FamilyName if present, else name
  std::string grid_location;
  std::string point_set_type;
  // Element range (inclusive, 1-based) when the boco addresses element indices.
  GlobalIndex first_element{0};
  GlobalIndex last_element{0};
  bool has_element_range{false};
  // Explicit element/point list when a range is not used.
  std::vector<GlobalIndex> point_list;
};

// A 1-to-1 zone connectivity patch (abutting matched interface).
struct RawConnectivity {
  std::string name;
  std::string donor_zone_name;
  std::string connectivity_type;  // e.g. "Abutting1to1"
  std::string grid_location;      // "Vertex" for matched node lists
  // Matched NODE indices, 1-based, in this zone and in the donor zone.
  std::vector<GlobalIndex> point_list;
  std::vector<GlobalIndex> point_list_donor;
};

struct RawZone {
  std::string name;
  std::string family_name;
  Index num_nodes{0};
  Index num_cells{0};
  std::vector<Real> x;  // node coordinates, size num_nodes
  std::vector<Real> y;
  std::vector<RawSection> sections;
  std::vector<RawBoco> bocos;
  std::vector<RawConnectivity> connectivities;
};

struct RawMesh {
  std::string file_path;
  std::string base_name;
  int cell_dimension{2};
  int physical_dimension{2};
  std::vector<RawZone> zones;
  // Base-level family names (used to validate the case BC mapping).
  std::vector<std::string> family_names;
};

// ---------------------------------------------------------------------------
// GlobalMesh: merged face-based mesh used for partitioning only.
// ---------------------------------------------------------------------------

struct GlobalCell {
  ElementShape shape{ElementShape::kUnsupported};
  std::array<Index, 4> nodes{{-1, -1, -1, -1}};  // 4 = max nodes per 2-D cell
  int num_nodes{0};
  int zone{0};  // originating CGNS zone, retained for diagnostics
};

// A face (edge in 2-D) of the merged mesh.
struct GlobalFace {
  std::array<Index, 2> nodes{{-1, -1}};
  Index left_cell{-1};
  Index right_cell{-1};   // -1 for a boundary face
  int boundary_tag{-1};   // index into GlobalMesh::boundary_names, -1 interior
};

struct GlobalMesh {
  // Merged node coordinates (zone-interface duplicates fused).
  std::vector<Real> x;
  std::vector<Real> y;
  std::vector<GlobalCell> cells;
  std::vector<GlobalFace> faces;

  // Boundary family names, indexed by boundary_tag.
  std::vector<std::string> boundary_names;

  Index numNodes() const { return static_cast<Index>(x.size()); }
  Index numCells() const { return static_cast<Index>(cells.size()); }
  Index numFaces() const { return static_cast<Index>(faces.size()); }

  // Diagnostics filled during construction.
  Index num_merged_node_pairs{0};
  Real max_merge_distance{0.0};
  Index num_boundary_faces{0};
  Index num_interior_faces{0};
};

}  // namespace cns2d
