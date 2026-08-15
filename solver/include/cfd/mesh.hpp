#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "cfd/common.hpp"

namespace cfd {

struct Cell {
  int global_id{-1};
  std::vector<int> vertices;
  Vec2 center;
  Real area{0.0};
};

struct Face {
  int global_id{-1};
  int v0{-1};
  int v1{-1};
  int left_cell{-1};
  int right_cell{-1};
  Vec2 center;
  Vec2 normal;  // unit normal from left_cell to right_cell, or outward for boundary faces
  Real length{0.0};
  std::string tag;
};

struct GlobalMesh {
  std::vector<Vec2> vertices;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::map<std::string, int> boundary_face_counts;
};

GlobalMesh read_cgns_mesh(const std::string& file_name);
void build_geometry(GlobalMesh& mesh);

struct LocalCell {
  int global_id{-1};
  int owner_rank{-1};
  bool owned{false};
  std::vector<int> vertices;
  Vec2 center;
  Real area{0.0};
};

struct LocalFace {
  int global_id{-1};
  int left{-1};
  int right{-1};
  int global_left{-1};
  int global_right{-1};
  Vec2 center;
  Vec2 normal;
  Real length{0.0};
  std::string tag;
};

struct LocalMesh {
  std::vector<Vec2> vertices;
  std::vector<LocalCell> cells;
  std::vector<LocalFace> faces;
  std::vector<std::vector<int>> neighbor_cells;
  std::vector<std::vector<int>> faces_by_cell;
  std::vector<int> owned_local_indices;
  std::unordered_map<int, int> local_index_by_global_cell;
  int global_num_cells{0};
  int global_num_faces{0};
};

}  // namespace cfd
