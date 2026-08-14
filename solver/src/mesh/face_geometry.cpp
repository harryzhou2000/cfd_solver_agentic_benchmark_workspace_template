#include "mesh/face_geometry.hpp"
#include <iostream>
#include <cmath>

namespace omo {

void FaceGeometry::compute(MeshData& mesh) {
    int nc = mesh.n_cells_global;
    int nf = mesh.n_faces_global;

    // First compute cell centroids as average of face centroids
    std::vector<Vector2> cell_centroids(nc, Vector2::Zero());
    std::vector<int> cell_face_count(nc, 0);
    for (auto& face : mesh.faces) {
        if (face.left_cell >= 0) {
            cell_centroids[face.left_cell] += face.centroid;
            cell_face_count[face.left_cell]++;
        }
        if (face.right_cell >= 0) {
            cell_centroids[face.right_cell] += face.centroid;
            cell_face_count[face.right_cell]++;
        }
    }
    for (int c = 0; c < nc; c++) {
        if (cell_face_count[c] > 0) {
            cell_centroids[c] /= static_cast<Real>(cell_face_count[c]);
        }
    }

    // Reorient face normals to point from left cell to right cell
    for (auto& face : mesh.faces) {
        // Recompute face area and unit normal from face centroid and orientation
        // The normal should point from left to right (or out of domain for boundary)

        if (face.right_cell >= 0) {
            // Interior face: normal should point from left to right
            Vector2 vec_lr = cell_centroids[face.right_cell] - cell_centroids[face.left_cell];
            Real dot = face.normal.dot(vec_lr);
            if (dot < 0) {
                face.normal = -face.normal;
            }
        } else {
            // Boundary face: normal should point outward from domain
            Vector2 to_face = face.centroid - cell_centroids[face.left_cell];
            Real dot = face.normal.dot(to_face);
            if (dot < 0) {
                face.normal = -face.normal;
            }
        }
    }

    // Now compute cell volumes using Green's theorem
    // For a 2D cell: V = (1/2) * sum((x_f*nx + y_f*ny) * area_f) over faces, with outward normal
    for (auto& cell : mesh.cells) {
        cell.vol = 0.0;
    }

    for (auto& face : mesh.faces) {
        Real contrib = face.centroid(0) * face.normal(0) + face.centroid(1) * face.normal(1);
        contrib *= face.area;

        if (face.left_cell >= 0) {
            mesh.cells[face.left_cell].vol += 0.5 * contrib;
        }
        if (face.right_cell >= 0) {
            // For right cell, normal points INTO the cell (from left), so contribution is negative
            mesh.cells[face.right_cell].vol -= 0.5 * contrib;
        }
    }

    // Sanity checks
    Real min_vol = 1e100, max_vol = 0, total_vol = 0;
    int neg_count = 0;
    for (auto& cell : mesh.cells) {
        Real v = cell.vol;
        if (v <= 0) neg_count++;
        min_vol = std::min(min_vol, std::abs(v));
        max_vol = std::max(max_vol, v);
        total_vol += v;
    }

    std::cout << "[FaceGeometry] Cell volumes: min=" << min_vol
              << " max=" << max_vol << " total=" << total_vol
              << " negatives=" << neg_count << std::endl;
}

void FaceGeometry::compute_local(LocalMeshData& local, const MeshData* global_mesh) {
    for (auto& cell : local.cells) {
        cell.vol = 1.0; // placeholder
    }
}

} // namespace omo
