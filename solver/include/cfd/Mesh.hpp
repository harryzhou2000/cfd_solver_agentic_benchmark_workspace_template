#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace cfd {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(double s, Vec2 a) { return {s * a.x, s * a.y}; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x / s, a.y / s}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

struct Cell {
  std::vector<int> nodes;
  Vec2 center;
  double volume = 0.0;
  std::vector<int> faces;
};

struct Face {
  std::array<int, 2> nodes{};
  int left = -1;
  int right = -1;
  Vec2 center;
  Vec2 normal;  // unit normal pointing out of left
  double area = 0.0;
  std::string boundary_tag;
};

struct GlobalMesh {
  std::vector<Vec2> nodes;
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<int> adjacency_offsets;
  std::vector<int> adjacency;

  static GlobalMesh read_cgns(const std::string& path);
  void build_topology_and_geometry(
      const std::unordered_map<unsigned long long, std::string>& boundary_edges);
  void validate(const std::unordered_map<std::string, std::string>& boundary_conditions) const;
};

}  // namespace cfd
