#include "solver/reconstruction.hpp"

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
// Owned cells store their face lists; ghost cells do not (partition.cpp only
// fills face_ids for owned cells), so for ghosts we scan the local faces.
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

// reconstruct_face_states takes no gamma parameter (see reconstruction.hpp),
// so the positivity fallback uses the standard-air value. This only decides
// whether to fall back to first order at a face; it does not affect the
// reconstructed values themselves.
constexpr double kPositivityGamma = 1.4;

} // namespace

std::vector<Vec2> compute_gradients(const DistributedMesh& mesh,
                                     const std::vector<double>& state,
                                     double gamma) {
    (void)gamma;  // reserved; gradients are a purely geometric least-squares fit

    const int num_total = num_total_cells(mesh);
    std::vector<Vec2> gradients(4 * num_total, Vec2::Zero());

    std::vector<int> faces;
    for (int c = 0; c < num_total; ++c) {
        cell_face_ids(mesh, c, faces);

        // Least-squares system: ATA * grad = ATb, accumulated over the
        // face-neighbor stencil (dr = neighbor centroid - cell centroid).
        Eigen::Matrix2d ATA = Eigen::Matrix2d::Zero();
        std::array<Eigen::Vector2d, 4> ATb;
        for (auto& b : ATb) b.setZero();

        for (int fid : faces) {
            const int j = other_cell(mesh, fid, c);
            if (j < 0) continue;  // boundary face: no neighbor
            const Vec2 dr = cell_centroid(mesh, j) - cell_centroid(mesh, c);
            ATA(0, 0) += dr.x() * dr.x();
            ATA(0, 1) += dr.x() * dr.y();
            ATA(1, 0) += dr.y() * dr.x();
            ATA(1, 1) += dr.y() * dr.y();
            for (int v = 0; v < 4; ++v) {
                const double dU = state[4 * j + v] - state[4 * c + v];
                ATb[v](0) += dr.x() * dU;
                ATb[v](1) += dr.y() * dU;
            }
        }

        // ATA is symmetric positive semi-definite (sum of outer products);
        // invert only when well conditioned. Degenerate stencils (no
        // neighbors, or all neighbors collinear) get zero gradients.
        const double det = ATA.determinant();
        if (det > 1e-30) {
            const Eigen::Matrix2d ATA_inv = ATA.inverse();
            for (int v = 0; v < 4; ++v) {
                gradients[c + v * num_total] = ATA_inv * ATb[v];
            }
        }
    }
    return gradients;
}

void reconstruct_face_states(const DistributedMesh& mesh,
                              const std::vector<double>& state,
                              const std::vector<Vec2>& gradients,
                              std::vector<Vec4>& face_UL,
                              std::vector<Vec4>& face_UR) {
    const int num_faces = static_cast<int>(mesh.local_faces.size());
    const int num_total = num_total_cells(mesh);

    face_UL.resize(num_faces);
    face_UR.resize(num_faces);

    for (int f = 0; f < num_faces; ++f) {
        const int left = mesh.face_left[f];
        const int right = mesh.face_right[f];
        const Vec2 fc = mesh.local_faces[f].centroid;

        // Left state: cell average + gradient . (face centroid - cell centroid).
        const Vec4 UL0 = cell_state(state, left);
        const Vec2 drL = fc - cell_centroid(mesh, left);
        Vec4 UL = UL0;
        for (int v = 0; v < 4; ++v) {
            UL[v] += gradients[left + v * num_total].dot(drL);
        }
        // Fall back to the first-order cell average if the reconstruction
        // produces negative density or pressure.
        face_UL[f] = positive_state(UL, kPositivityGamma) ? UL : UL0;

        if (right >= 0) {
            const Vec4 UR0 = cell_state(state, right);
            const Vec2 drR = fc - cell_centroid(mesh, right);
            Vec4 UR = UR0;
            for (int v = 0; v < 4; ++v) {
                UR[v] += gradients[right + v * num_total].dot(drR);
            }
            face_UR[f] = positive_state(UR, kPositivityGamma) ? UR : UR0;
        } else {
            // Boundary face: the right state is set by the BC implementation.
            face_UR[f] = Vec4::Zero();
        }
    }
}

} // namespace solver
