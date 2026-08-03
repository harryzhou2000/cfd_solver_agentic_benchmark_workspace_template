#include "solver/geometry.hpp"

#include <algorithm>
#include <cmath>

namespace solver {

namespace {

// Collect the distinct vertices of a cell (from its faces), de-duplicated by
// position.
std::vector<Vec2> cell_vertices(const Mesh& mesh, const Cell& cell) {
    std::vector<Vec2> pts;
    for (int fid : cell.face_ids) {
        for (const Vec2& n : mesh.faces[fid].nodes) {
            const double tol = 1e-12 * (1.0 + n.norm());
            bool dup = false;
            for (const Vec2& p : pts) {
                if ((p - n).norm() < tol) {
                    dup = true;
                    break;
                }
            }
            if (!dup) pts.push_back(n);
        }
    }
    return pts;
}

// Order vertices counter-clockwise around their mean (valid for convex
// cells: triangles and quadrilaterals).
std::vector<Vec2> order_vertices(std::vector<Vec2> pts) {
    Vec2 center = Vec2::Zero();
    for (const Vec2& p : pts) center += p;
    center /= static_cast<Scalar>(pts.size());
    std::sort(pts.begin(), pts.end(),
              [&center](const Vec2& a, const Vec2& b) {
                  return std::atan2(a.y() - center.y(), a.x() - center.x()) <
                         std::atan2(b.y() - center.y(), b.x() - center.x());
              });
    return pts;
}

// Signed area (shoelace formula); abs() applied by the caller.
Scalar polygon_area(const std::vector<Vec2>& pts) {
    Scalar area2 = 0.0;
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i < n; ++i) {
        const Vec2& a = pts[i];
        const Vec2& b = pts[(i + 1) % n];
        area2 += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * area2;
}

} // namespace

void compute_geometry(Mesh& mesh) {
    // 1. Cell centroids (average of the cell's vertices) and volumes (areas).
    for (Cell& cell : mesh.cells) {
        std::vector<Vec2> pts = cell_vertices(mesh, cell);
        if (pts.empty()) {
            cell.centroid = Vec2::Zero();
            cell.volume = 0.0;
            continue;
        }
        Vec2 avg = Vec2::Zero();
        for (const Vec2& p : pts) avg += p;
        avg /= static_cast<Scalar>(pts.size());
        cell.centroid = avg;

        pts = order_vertices(std::move(pts));
        cell.volume = std::abs(polygon_area(pts));
    }

    // 2. Face centroids, lengths, and normals.
    for (Face& face : mesh.faces) {
        face.centroid = Vec2::Zero();
        for (const Vec2& n : face.nodes) face.centroid += n;
        face.centroid /= static_cast<Scalar>(face.nodes.size());

        const Vec2 edge = face.nodes[1] - face.nodes[0];
        const Scalar len = edge.norm();
        face.area = len;

        if (len < 1e-300) {
            // Degenerate face: leave the normal zero.
            face.normal = Vec2::Zero();
            continue;
        }

        // Candidate normal from the (nodes[0] -> nodes[1]) edge direction:
        // n = (dy, -dx) / |edge|.
        Vec2 normal(edge.y() / len, -edge.x() / len);

        if (face.cell_ids[1] == -1) {
            // Boundary face: normal must point away from the cell.
            const int c0 = face.cell_ids[0];
            const Vec2 outward = face.centroid - mesh.cells[c0].centroid;
            if (normal.dot(outward) < 0.0) {
                normal = -normal;
                std::swap(face.nodes[0], face.nodes[1]);
            }
        } else {
            // Interior face: normal must point from cell 0 to cell 1.
            const int c0 = face.cell_ids[0];
            const int c1 = face.cell_ids[1];
            const Vec2 dir = mesh.cells[c1].centroid - mesh.cells[c0].centroid;
            if (normal.dot(dir) < 0.0) {
                normal = -normal;
                std::swap(face.nodes[0], face.nodes[1]);
            }
        }
        face.normal = normal;
    }

    // 3. Classify faces into interior / boundary index lists.
    mesh.boundary_face_ids.clear();
    mesh.interior_face_ids.clear();
    const int nfaces = static_cast<int>(mesh.faces.size());
    for (int i = 0; i < nfaces; ++i) {
        if (mesh.faces[i].cell_ids[1] == -1) {
            mesh.boundary_face_ids.push_back(i);
        } else {
            mesh.interior_face_ids.push_back(i);
        }
    }

    mesh.num_cells = static_cast<int>(mesh.cells.size());
    mesh.num_faces = nfaces;
    mesh.num_bnd_faces = static_cast<int>(mesh.boundary_face_ids.size());
}

} // namespace solver
