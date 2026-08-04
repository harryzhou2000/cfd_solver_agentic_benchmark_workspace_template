#pragma once

#include "cfd/case_config.hpp"
#include "cfd/types.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cfd {

struct GlobalNode {
  Vec2 position{};
};

struct GlobalCell {
  std::int64_t global_id = -1;
  std::vector<std::int64_t> nodes;
  Vec2 center{};
  double area = 0.0;
};

struct GlobalFace {
  std::int64_t global_id = -1;
  std::array<std::int64_t, 2> nodes{{-1, -1}};
  std::int64_t left = -1;
  std::int64_t right = -1;
  Vec2 center{};
  Vec2 normal{};  // unit normal directed out of the left cell
  double length = 0.0;
  BoundaryType boundary_type = BoundaryType::interior;
  std::string boundary_tag;
};

struct GlobalMesh {
  std::vector<GlobalNode> nodes;
  std::vector<GlobalCell> cells;
  std::vector<GlobalFace> faces;
  int source_zone_count = 0;
  std::size_t source_vertex_count = 0;
  std::size_t merged_interface_vertices = 0;
  double maximum_abs_z = 0.0;
};

GlobalMesh read_cgns_mesh(const std::filesystem::path& path, const CaseConfig& config);
void validate_global_mesh(const GlobalMesh& mesh, const CaseConfig& config);

}  // namespace cfd
