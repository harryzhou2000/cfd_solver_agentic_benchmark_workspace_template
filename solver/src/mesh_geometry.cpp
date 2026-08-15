#include "cfd/mesh.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace cfd {

namespace {

uint64_t edge_key(int a, int b) {
  const uint32_t lo = static_cast<uint32_t>(std::min(a, b));
  const uint32_t hi = static_cast<uint32_t>(std::max(a, b));
  return (static_cast<uint64_t>(lo) << 32u) | hi;
}

Real signed_area(const std::vector<Vec2>& points, const std::vector<int>& verts) {
  Real a = 0.0;
  for (std::size_t i = 0; i < verts.size(); ++i) {
    const Vec2& p = points[static_cast<std::size_t>(verts[i])];
    const Vec2& q = points[static_cast<std::size_t>(verts[(i + 1) % verts.size()])];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * a;
}

Vec2 centroid(const std::vector<Vec2>& points, const std::vector<int>& verts, Real area) {
  Real cx = 0.0;
  Real cy = 0.0;
  for (std::size_t i = 0; i < verts.size(); ++i) {
    const Vec2& p = points[static_cast<std::size_t>(verts[i])];
    const Vec2& q = points[static_cast<std::size_t>(verts[(i + 1) % verts.size()])];
    const Real cr = p.x * q.y - q.x * p.y;
    cx += (p.x + q.x) * cr;
    cy += (p.y + q.y) * cr;
  }
  const Real denom = 6.0 * area;
  return {cx / denom, cy / denom};
}

}  // namespace

void build_geometry(GlobalMesh& mesh) {
  mesh.faces.clear();
  mesh.boundary_face_counts.clear();

  for (Cell& cell : mesh.cells) {
    if (cell.vertices.size() < 3) throw CfdError("cell has fewer than three vertices");
    Real a = signed_area(mesh.vertices, cell.vertices);
    if (std::abs(a) < 1.0e-20) throw CfdError("degenerate cell area");
    if (a < 0.0) {
      std::reverse(cell.vertices.begin(), cell.vertices.end());
      a = -a;
    }
    cell.area = a;
    cell.center = centroid(mesh.vertices, cell.vertices, a);
  }

  std::unordered_map<uint64_t, int> face_by_edge;
  for (const Cell& cell : mesh.cells) {
    const int n = static_cast<int>(cell.vertices.size());
    for (int i = 0; i < n; ++i) {
      const int a = cell.vertices[static_cast<std::size_t>(i)];
      const int b = cell.vertices[static_cast<std::size_t>((i + 1) % n)];
      const uint64_t key = edge_key(a, b);
      auto it = face_by_edge.find(key);
      if (it == face_by_edge.end()) {
        Face f;
        f.global_id = static_cast<int>(mesh.faces.size());
        f.v0 = a;
        f.v1 = b;
        f.left_cell = cell.global_id;
        const Vec2 pa = mesh.vertices[static_cast<std::size_t>(a)];
        const Vec2 pb = mesh.vertices[static_cast<std::size_t>(b)];
        const Vec2 edge = pb - pa;
        f.length = norm(edge);
        if (f.length <= 0.0) throw CfdError("zero-length face");
        f.center = 0.5 * (pa + pb);
        f.normal = {edge.y / f.length, -edge.x / f.length};
        face_by_edge.emplace(key, f.global_id);
        mesh.faces.push_back(f);
      } else {
        Face& f = mesh.faces[static_cast<std::size_t>(it->second)];
        if (f.right_cell >= 0) throw CfdError("non-manifold face with more than two cells");
        f.right_cell = cell.global_id;
      }
    }
  }

  for (Face& f : mesh.faces) {
    if (f.right_cell >= 0) {
      const Vec2 dr = mesh.cells[static_cast<std::size_t>(f.right_cell)].center -
                      mesh.cells[static_cast<std::size_t>(f.left_cell)].center;
      if (dot(f.normal, dr) < 0.0) {
        f.normal *= -1.0;
      }
    } else {
      const Vec2 dr = f.center - mesh.cells[static_cast<std::size_t>(f.left_cell)].center;
      if (dot(f.normal, dr) < 0.0) {
        f.normal *= -1.0;
      }
    }
  }
}

}  // namespace cfd
