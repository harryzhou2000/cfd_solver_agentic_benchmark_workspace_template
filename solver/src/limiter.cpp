// Phase 4: Barth-Jespersen limiter implementation (see limiter.h).

#include "limiter.h"

#include <algorithm>
#include <cmath>

#include "gradient.h"

namespace cfd {

namespace {

constexpr int kConsLims = 4;  // rho, rhou, rhov, rhoE

inline double comp(const ConsState& U, int k) {
  switch (k) {
    case 0: return U.rho;
    case 1: return U.rhou;
    case 2: return U.rhov;
    default: return U.rhoE;
  }
}

}  // namespace

void compute_limiters(const std::vector<ConsState>& U_local,
                      const LocalMesh& lm, const CellGradients& grads,
                      Limiters& limiters) {
  limiters.resize(static_cast<size_t>(lm.n_owned));

  for (int i = 0; i < lm.n_owned; ++i) {
    // Local min/max of U over cell i and its face neighbors (owned or
    // ghost). Boundary faces have no neighbor and contribute nothing.
    double u_min[kConsLims] = {0.0, 0.0, 0.0, 0.0};
    double u_max[kConsLims] = {0.0, 0.0, 0.0, 0.0};
    for (int k = 0; k < kConsLims; ++k) {
      u_min[k] = comp(U_local[i], k);
      u_max[k] = comp(U_local[i], k);
    }

    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    for (int kf = begin; kf < end; ++kf) {
      const LocalMesh::LocalFace& f = lm.faces[lm.cell_faces_data[kf]];
      // The neighbor is the other cell of the face: an owned cell, a ghost
      // cell (inter-rank face, right == -2 -> right_local), or no cell at
      // all (boundary face, right == -1). Ghost neighbors must participate
      // in the min/max so the bounding range is the same on both sides of
      // an inter-rank face.
      int j = -1;
      if (f.left == i) {
        j = (f.right >= 0) ? f.right : f.right_local;
      } else {
        j = f.left;  // f.left is always owned
      }
      if (j < 0) {
        continue;  // boundary face: no neighbor
      }
      for (int k = 0; k < kConsLims; ++k) {
        const double uj = comp(U_local[j], k);
        u_min[k] = std::min(u_min[k], uj);
        u_max[k] = std::max(u_max[k], uj);
      }
    }

    // Per-face limiter ratio, minimized over the faces with a neighbor.
    // The smooth Venkatakrishnan form is used instead of the raw
    // Barth-Jespersen ratio: the raw min-over-faces limiter is non-smooth in
    // the state and flaps between pseudo-time steps, which stalls the
    // implicit iteration at a residual floor. The Venkatakrishnan limiter
    // has the same bounding behavior (phi -> 0 for large overshoots, phi -> 1
    // for vanishing increments) but is C1-smooth, so the implicit iteration
    // converges to the steady state of the limited operator.
    double phi[kConsLims] = {1.0, 1.0, 1.0, 1.0};
    for (int kf = begin; kf < end; ++kf) {
      const LocalMesh::LocalFace& f = lm.faces[lm.cell_faces_data[kf]];
      int j = -1;
      if (f.left == i) {
        j = (f.right >= 0) ? f.right : f.right_local;
      } else {
        j = f.left;  // f.left is always owned
      }
      if (j < 0) {
        continue;  // boundary faces do not enter the min
      }
      const Vec2 dr = lm.cells[j].centroid - lm.cells[i].centroid;
      for (int k = 0; k < kConsLims; ++k) {
        const double u_i = comp(U_local[i], k);
        // Unconstrained reconstructed value at the neighbor centroid,
        // built from the RAW gradient (the limiter itself is what we are
        // computing here).
        const double d_face = u_i + raw_grad(grads, k, i).dot(dr);
        const double delta = d_face - u_i;
        // Smoothing parameter: the limiter stays inactive while the
        // reconstruction increment is below this fraction of the state
        // magnitude (the truncation/noise scale of a smooth flow), and
        // becomes active for the large increments of shocks and steep
        // gradients. The smooth Venkatakrishnan form (instead of the raw
        // ratio y/delta) keeps the limiter C1 in the state, which is what
        // lets the implicit iteration converge instead of stalling on the
        // non-smooth min-over-faces.
        const double eps = 1e-4 * std::abs(u_i);
        double phi_f = 1.0;
        if (delta > eps) {
          // Overshoot above the local maximum: smooth bounding.
          // Standard Venkatakrishnan form (verified against SU2):
          //   phi = (y^2 + 2*y*delta + eps^2) / (y^2 + y*delta + 2*delta^2 + eps^2)
          // with y the available range (u_max - u_i) and delta the face
          // increment. The coefficients on the |delta|*y and delta^2 terms
          // in the denominator are 1 and 2 respectively.
          const double y = u_max[k] - u_i;
          const double y_denom =
              y * y + delta * y + 2.0 * delta * delta + eps * eps;
          phi_f = (y * y + 2.0 * delta * y + eps * eps) / y_denom;
        } else if (delta < -eps) {
          // Undershoot below the local minimum: smooth bounding (same form).
          const double y = u_min[k] - u_i;
          const double y_denom =
              y * y + delta * y + 2.0 * delta * delta + eps * eps;
          phi_f = (y * y + 2.0 * delta * y + eps * eps) / y_denom;
        }
        phi[k] = std::min(phi[k], phi_f);
      }
    }
    for (int k = 0; k < kConsLims; ++k) {
      phi[k] = std::max(0.0, std::min(1.0, phi[k]));
    }

    limiters.phi_rho[static_cast<size_t>(i)] = phi[0];
    limiters.phi_rhou[static_cast<size_t>(i)] = phi[1];
    limiters.phi_rhov[static_cast<size_t>(i)] = phi[2];
    limiters.phi_rhoE[static_cast<size_t>(i)] = phi[3];
  }
}

}  // namespace cfd
