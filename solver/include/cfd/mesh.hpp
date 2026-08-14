#pragma once

#include "cfd/types.hpp"

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cfd {

// A nominally 2-D CGNS mesh stored with PhysicalDimension=3 is projected to XY
// only when its Z span is at most this fraction of its XY bounding-box diagonal.
// 1e-7 admits ordinary export/roundoff noise (including the supplied NACA mesh)
// while rejecting geometry whose out-of-plane scale can affect a 2-D solution.
inline constexpr double cgns_projection_planarity_relative_tolerance = 1.0e-7;

struct Vertex {
  GlobalId id{invalid_global_id};
  Vec2 position{};
};

struct Cell {
  GlobalId id{invalid_global_id};
  std::vector<GlobalId> vertices;
  std::vector<GlobalId> faces;
  Vec2 center{};
  double area{};
  int source_zone{};
};

struct Face {
  GlobalId id{invalid_global_id};
  std::array<GlobalId, 2> vertices{{invalid_global_id, invalid_global_id}};
  GlobalId left{invalid_global_id};
  GlobalId right{invalid_global_id};
  Vec2 center{};
  double length{};
  Vec2 normal{};  // Unit normal from left to right, or outward at a boundary.
  std::string boundary;
};

struct BoundaryMetadata {
  std::string bc_name;
  std::string family;
  bool family_specified{};
  bool family_name_node_present{};
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<std::string> zone_names;
  std::vector<std::string> family_names;
  std::vector<BoundaryMetadata> boundary_metadata;
  std::map<std::string, std::size_t> boundary_face_counts;
};

bool is_nearly_planar_for_xy_projection(double xy_extent, double z_span) noexcept;
std::string resolve_cgns_boundary_family(
    std::string_view bc_name,
    std::optional<std::string_view> family_name,
    bool family_specified);
Mesh read_cgns_mesh(const std::filesystem::path& path);
void validate_mesh(const Mesh& mesh);

}  // namespace cfd
