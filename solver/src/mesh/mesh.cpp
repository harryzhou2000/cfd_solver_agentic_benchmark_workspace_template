#include "mesh/mesh_types.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace cfd {

namespace {

// --- Helper: cross product in 2D (z-component of 3D cross) ---
Real cross2d(const Vec2& a, const Vec2& b) {
    return a[0] * b[1] - a[1] * b[0];
}

// --- Compute centroid of polygon using fan triangulation ---
// True area-weighted centroid: centroid = sum(tri_area * tri_centroid) /
// total_area, where triangle i is (v0, vi, v_{i+1}) for i = 1..n-2.
// Exact for triangles and quads; the plain vertex average is only correct
// for triangles, so this removes the quad bias.
static Vec2 polygon_centroid(const std::vector<Vec2>& coords,
                             const std::vector<Int>& vert_ids) {
    const Int n = static_cast<Int>(vert_ids.size());
    if (n == 0) return {0, 0};

    Vec2 c{0, 0};
    Real total_area = 0;
    const Vec2& a = coords[vert_ids[0]];
    for (Int i = 1; i + 1 < n; ++i) {
        const Vec2& b = coords[vert_ids[i]];
        const Vec2& d = coords[vert_ids[i + 1]];
        const Real tri_area = 0.5 * std::abs(cross2d(b - a, d - a));
        total_area += tri_area;
        c += tri_area * ((a + b + d) / 3.0);
    }

    if (total_area > 0) {
        return c / total_area;
    }

    // Degenerate (zero-area) polygon: fall back to the vertex average so we
    // never return NaN.
    for (auto vid : vert_ids) {
        c += coords[vid];
    }
    return c / static_cast<Real>(n);
}

// --- Compute area of polygon using shoelace formula ---
// polygon_area() takes std::abs of the shoelace sum: we do not need the
// winding orientation from it, only the magnitude.
static Real polygon_area(const std::vector<Vec2>& coords,
                         const std::vector<Int>& vert_ids) {
    Real a = 0;
    const Int n = static_cast<Int>(vert_ids.size());
    for (Int i = 0; i < n; ++i) {
        const Int j = (i + 1) % n;
        a += cross2d(coords[vert_ids[i]], coords[vert_ids[j]]);
    }
    return 0.5 * std::abs(a);
}

// --- Normalize a face normal in place; returns false if degenerate ---
static bool normalize_normal(Face& face) {
    const Real len = std::sqrt(face.normal.nx * face.normal.nx +
                               face.normal.ny * face.normal.ny);
    if (len <= 0) return false;
    face.normal.nx /= len;
    face.normal.ny /= len;
    return true;
}

} // namespace

// --- Mesh::finalize ---
// Computes centroids (from vertex_ids, which are always vertex indices),
// volumes, fixes/enforces face normals, builds cell face lists, cell
// neighbors, and the flat boundary face list.
void Mesh::finalize() {
    // Idempotency guard: face_ids holds face indices after the first run,
    // so a second run would misread them as vertex indices.
    if (finalized) return;

    stats.n_cells = cells.size();
    stats.n_faces = faces.size();
    stats.n_boundary_faces = 0;
    for (auto& f : faces) {
        if (f.is_boundary) stats.n_boundary_faces++;
    }
    stats.n_vertices = vertices.size();

    // Compute cell centroids (true area-weighted) and volumes from vertex_ids
    for (auto& cell : cells) {
        cell.centroid = polygon_centroid(vertices, cell.vertex_ids);
        cell.volume = polygon_area(vertices, cell.vertex_ids);
    }

    // Internal faces: verify the normal is unit length, then enforce the
    // left -> right orientation using the cell centroids. The reader stores
    // a raw normal candidate; any wrong winding is fixed here.
    for (auto& face : faces) {
        if (face.is_boundary) continue;
        if (!normalize_normal(face)) {
            throw std::runtime_error(
                "finalize: internal face " + std::to_string(&face - faces.data()) +
                " has a zero-length normal (degenerate edge)");
        }
        const Vec2 dir = cells[face.right_cell].centroid -
                         cells[face.left_cell].centroid;
        if (dir.dot(Vec2{face.normal.nx, face.normal.ny}) < 0) {
            // Normal points right -> left; flip it so the invariant
            // "normal points from left cell to right cell" holds.
            // Note: do NOT also swap left/right here - the two operations
            // would cancel out and the orientation would never be fixed.
            face.normal.nx = -face.normal.nx;
            face.normal.ny = -face.normal.ny;
        }
    }

    // Boundary faces: enforce an outward normal. The raw normal from the
    // reader assumes CCW winding, which CGNS does not guarantee; this check
    // is winding-agnostic and self-validating: for a valid cell the vector
    // (face_centroid - cell_centroid) points outward, so the outward normal
    // is the one with a positive dot product against it.
    for (auto& face : faces) {
        if (!face.is_boundary) continue;
        if (!normalize_normal(face)) {
            throw std::runtime_error(
                "finalize: boundary face " + std::to_string(&face - faces.data()) +
                " has a zero-length normal (degenerate edge)");
        }
        const Vec2 out = face.centroid - cells[face.left_cell].centroid;
        if (out.dot(Vec2{face.normal.nx, face.normal.ny}) < 0) {
            face.normal.nx = -face.normal.nx;
            face.normal.ny = -face.normal.ny;
        }
    }

    // Build cell-face connectivity: cell.face_ids now stores actual face
    // indices (vertex connectivity stays in cell.vertex_ids).
    std::vector<std::vector<Int>> cell_face_list(stats.n_cells);
    for (Int fi = 0; fi < faces.size(); ++fi) {
        auto& f = faces[fi];
        cell_face_list[f.left_cell].push_back(fi);
        if (!f.is_boundary && f.right_cell != INVALID_INDEX) {
            cell_face_list[f.right_cell].push_back(fi);
        }
    }

    // Update cells with face ids and build neighbors
    cell_neighbors.resize(stats.n_cells);
    for (Int ci = 0; ci < stats.n_cells; ++ci) {
        cells[ci].face_ids = cell_face_list[ci];
        // Build neighbors: for each face of this cell, the other cell
        for (auto fi : cell_face_list[ci]) {
            auto& f = faces[fi];
            if (f.left_cell == ci && f.right_cell != INVALID_INDEX) {
                cell_neighbors[ci].push_back(f.right_cell);
            } else if (f.right_cell == ci && !f.is_boundary) {
                cell_neighbors[ci].push_back(f.left_cell);
            }
        }
    }

    // Build flat boundary face list
    boundary_face_ids.clear();
    for (Int fi = 0; fi < faces.size(); ++fi) {
        if (faces[fi].is_boundary) {
            boundary_face_ids.push_back(fi);
        }
    }

    finalized = true;

    std::cout << "Mesh finalized:" << std::endl;
    std::cout << "  Cells: " << stats.n_cells << std::endl;
    std::cout << "  Faces: " << stats.n_faces << " (boundary: "
              << stats.n_boundary_faces << ")" << std::endl;
    std::cout << "  Vertices: " << stats.n_vertices << std::endl;
    std::cout << "  Boundary patches: " << boundary_patches.size() << std::endl;
}

} // namespace cfd
