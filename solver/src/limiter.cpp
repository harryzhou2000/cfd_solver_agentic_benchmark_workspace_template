#include "solver/limiter.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace solver {

namespace {

// Combined owned+ghost cell count.
inline int num_total_cells(const DistributedMesh& mesh) {
    return static_cast<int>(mesh.owned_cells.size()) +
           static_cast<int>(mesh.ghost_cells.size());
}

// Centroid of the cell with local index lid (owned+ghost combined).
inline const Vec2& cell_centroid(const DistributedMesh& mesh, int lid) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    return (lid < num_owned) ? mesh.owned_cells[lid].centroid
                             : mesh.ghost_cells[lid - num_owned].centroid;
}

// Face ids (indices into mesh.local_faces) of the cell with local index lid.
// Owned cells store their face lists; ghost cells do not, so for ghosts we
// scan the local faces.
inline void cell_face_ids(const DistributedMesh& mesh, int lid,
                          std::vector<int>& out) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    out.clear();
    if (lid < num_owned) {
        out = mesh.owned_cells[lid].face_ids;
        return;
    }
    const int num_faces = static_cast<int>(mesh.local_faces.size());
    for (int f = 0; f < num_faces; ++f) {
        if (mesh.face_left[f] == lid || mesh.face_right[f] == lid) {
            out.push_back(f);
        }
    }
}

// The cell on the other side of face f, or -1 for a boundary face.
inline int other_cell(const DistributedMesh& mesh, int f, int cell) {
    return (mesh.face_left[f] == cell) ? mesh.face_right[f]
                                       : mesh.face_left[f];
}

// Conservative state of cell lid as a Vec4.
inline Vec4 cell_state(const std::vector<double>& state, int lid) {
    Vec4 U;
    for (int v = 0; v < 4; ++v) U[v] = state[4 * lid + v];
    return U;
}

// A state is physically valid iff both density and pressure are positive.
inline bool positive_state(const Vec4& U, double gamma) {
    const double rho = U[0];
    if (rho <= 0.0) return false;
    const double p = (gamma - 1.0) *
                     (U[3] - 0.5 * (U[1] * U[1] + U[2] * U[2]) / rho);
    return p > 0.0;
}

} // namespace

void apply_barth_jespersen_limiter(const DistributedMesh& mesh,
                                    const std::vector<double>& state,
                                    std::vector<Vec2>& gradients,
                                    double gamma) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_total = num_total_cells(mesh);

    std::vector<int> faces;
    for (int c = 0; c < num_owned; ++c) {
        cell_face_ids(mesh, c, faces);

        // 1. Min/max of each conservative variable over the cell and its
        //    face neighbors (ghost cells included via the combined indexing).
        std::array<double, 4> Umin, Umax;
        for (int v = 0; v < 4; ++v) {
            Umin[v] = state[4 * c + v];
            Umax[v] = state[4 * c + v];
        }
        for (int fid : faces) {
            const int j = other_cell(mesh, fid, c);
            if (j < 0) continue;  // boundary face
            for (int v = 0; v < 4; ++v) {
                const double Uj = state[4 * j + v];
                Umin[v] = std::min(Umin[v], Uj);
                Umax[v] = std::max(Umax[v], Uj);
            }
        }

        // 2-4. Limiter factor: minimum over all faces and all four variables
        //      of the per-component slope constraint.
        double phi = 1.0;
        for (int fid : faces) {
            const Vec2 dr =
                mesh.local_faces[fid].centroid - cell_centroid(mesh, c);
            for (int v = 0; v < 4; ++v) {
                const double dU = gradients[c + v * num_total].dot(dr);
                if (dU > 0.0) {
                    phi = std::min(phi, (Umax[v] - state[4 * c + v]) /
                                            std::max(dU, 1e-15));
                } else if (dU < 0.0) {
                    phi = std::min(phi, (Umin[v] - state[4 * c + v]) /
                                            std::min(dU, -1e-15));
                }
                // dU == 0: no slope, factor 1 for this component.
            }
        }
        // phi is in [0,1] by construction; clamp guards FP edge cases.
        phi = std::min(1.0, std::max(0.0, phi));

        // 5. Scale the gradients of all four components.
        for (int v = 0; v < 4; ++v) {
            gradients[c + v * num_total] *= phi;
        }

        // 6. Positivity check: if the limited reconstruction still yields
        //    negative density or pressure at any face, zero the gradients
        //    (first-order fallback).
        for (int fid : faces) {
            const Vec2 dr =
                mesh.local_faces[fid].centroid - cell_centroid(mesh, c);
            Vec4 Uf = cell_state(state, c);
            for (int v = 0; v < 4; ++v) {
                Uf[v] += gradients[c + v * num_total].dot(dr);
            }
            if (!positive_state(Uf, gamma)) {
                for (int v = 0; v < 4; ++v) {
                    gradients[c + v * num_total] = Vec2::Zero();
                }
                break;
            }
        }
    }
}

} // namespace solver
