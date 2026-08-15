#include "mesh/geometry.hpp"

#include <fmt/format.h>

#include <cmath>
#include <stdexcept>

namespace cfd {

void compute_geometry(Mesh2D& mesh, const std::vector<Vector3>& vertices) {
  // ------------------------------------------------------------------
  // Cells: centroid and area (shoelace formula on the cyclic vertex ring)
  // ------------------------------------------------------------------
  for (Cell2D& c : mesh.cells) {
    const std::size_t n = c.vertex_indices.size();
    if (n < 3) {
      throw std::runtime_error("geometry: cell " + std::to_string(c.cell_id) +
                               " has fewer than 3 vertices");
    }

    Vector3 cnt(0.0, 0.0, 0.0);
    for (std::size_t k = 0; k < n; ++k)
      cnt += vertices[c.vertex_indices[k]];
    c.cell_center = cnt * (1.0 / static_cast<double>(n));

    double cross_sum = 0.0;
    for (std::size_t k = 0; k < n; ++k) {
      const Vector3& a = vertices[c.vertex_indices[k]];
      const Vector3& b = vertices[c.vertex_indices[(k + 1) % n]];
      cross_sum += a.x * b.y - b.x * a.y;
    }
    c.volume = 0.5 * std::fabs(cross_sum);

    if (c.volume <= 0.0)
      fmt::print(stderr, "[warning] cell {} has non-positive area {:.6e}\n",
                 c.cell_id, c.volume);
  }

  // ------------------------------------------------------------------
  // Faces: center, length (area), and outward/left-right oriented normal
  // ------------------------------------------------------------------
  for (Face2D& f : mesh.faces) {
    f.center = 0.5 * (f.nodes[0] + f.nodes[1]);

    const Vector3 e = f.nodes[1] - f.nodes[0];
    f.area = e.norm();

    // Raw unit perpendicular (2D): (dy, -dx) / |e|
    Vector3 n(0.0, 0.0, 0.0);
    if (f.area > 0.0) {
      n = Vector3(e.y, -e.x, 0.0) * (1.0 / f.area);
    } else {
      fmt::print(stderr, "[warning] face {} has zero length\n", f.face_id);
    }

    // Orient: interior faces point from left to right cell; boundary faces
    // point outward from the owning cell.
    const bool has_left = f.left_cell >= 0;
    const bool has_right = f.right_cell >= 0;
    if (!has_left && !has_right) {
      // Orphan face (only possible for non-conforming boundary edges).
      fmt::print(stderr,
                 "[warning] face {} has no adjacent cell; normal left "
                 "unoriented\n",
                 f.face_id);
      f.normal = n;
      continue;
    }

    Vector3 ref;
    if (has_left && has_right) {
      ref = mesh.cells[f.right_cell].cell_center -
            mesh.cells[f.left_cell].cell_center;
    } else if (has_left) {
      ref = f.center - mesh.cells[f.left_cell].cell_center;
    } else {
      ref = f.center - mesh.cells[f.right_cell].cell_center;
    }
    if (ref.dot(n) < 0.0) n = n * -1.0;
    f.normal = n;
  }
}

}  // namespace cfd
