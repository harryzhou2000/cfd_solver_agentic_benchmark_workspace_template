#pragma once

#include "core.hpp"

#include <array>
#include <string>
#include <vector>

namespace aerofv {

// The indices in GlobalMesh are stable, zero-based identifiers.  A cell is
// always stored counter-clockwise; this makes Face::normal point out of left.
struct Cell {
  std::vector<int> vertices;
  std::vector<int> faces;
  Vec2 centroid{};
  double area{0.0};
};

struct Face {
  std::array<int, 2> vertices{{-1, -1}};
  int left_cell{-1};
  int right_cell{-1}; // -1 for a physical boundary
  Vec2 center{};
  Vec2 normal{}; // unit normal directed from left_cell to right_cell/outside
  double length{0.0};
  std::string boundary_family; // empty for an interior face
};

struct GlobalMesh {
  std::vector<Vec2> vertices;
  std::vector<Cell> cells;
  std::vector<Face> faces;

  [[nodiscard]] bool is_boundary_face(int face) const;
  // Throws std::runtime_error for invalid indices, non-manifold edges, bad
  // geometry, or an untagged physical boundary.
  void validate_topology() const;
};

// Reads all unstructured 2-D zones in a CGNS file.  TRI_3 and QUAD_4 are
// volume cells; BAR_2 sections supply physical-boundary family names.  Vertex
// coordinates shared by zones are welded with the supplied absolute tolerance.
GlobalMesh read_cgns_unstructured_2d(const std::string &path,
                                     double stitch_tolerance = 1.0e-10);

} // namespace aerofv
