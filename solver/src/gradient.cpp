// Phase 4: gradient reconstruction implementation (see gradient.h).

#include "gradient.h"

#include <cmath>

#include "physics.h"

namespace cfd {

namespace {

constexpr int kConsGradComps = 4;  // rho, rhou, rhov, rhoE
constexpr double kLstsqDetFloor = 1e-24;   // least-squares matrix singularity
constexpr double kLstsqRelCond = 1e-10;    // relative conditioning of the
                                           // least-squares matrix (det vs.
                                           // mxx*myy); below this the stencil
                                           // is treated as degenerate
constexpr double kStencilEps = 1e-14;      // face-length guard in Green-Gauss

// Accessor: raw gradient of component k (0..3) of owned cell i.
inline Vec2 raw_grad(const CellGradients& g, int k, int i) {
  switch (k) {
    case 0: return g.grad_rho[static_cast<size_t>(i)];
    case 1: return g.grad_rhou[static_cast<size_t>(i)];
    case 2: return g.grad_rhov[static_cast<size_t>(i)];
    default: return g.grad_rhoE[static_cast<size_t>(i)];
  }
}

inline void set_grad(CellGradients& g, int k, int i, const Vec2& v) {
  switch (k) {
    case 0: g.grad_rho[static_cast<size_t>(i)] = v; break;
    case 1: g.grad_rhou[static_cast<size_t>(i)] = v; break;
    case 2: g.grad_rhov[static_cast<size_t>(i)] = v; break;
    default: g.grad_rhoE[static_cast<size_t>(i)] = v; break;
  }
}

// Cell value accessor for the Green-Gauss face average.
inline double cell_comp(const ConsState& U, int k) {
  switch (k) {
    case 0: return U.rho;
    case 1: return U.rhou;
    case 2: return U.rhov;
    default: return U.rhoE;
  }
}

}  // namespace

void compute_gradients(const std::vector<ConsState>& U_local,
                       const LocalMesh& lm, CellGradients& grads) {
  grads.resize(static_cast<size_t>(lm.n_owned));

  for (int i = 0; i < lm.n_owned; ++i) {
    // ---- Weighted least-squares over the face-neighbor stencil ----
    double mxx = 0.0, mxy = 0.0, myy = 0.0;
    double rhs_x[kConsGradComps] = {0.0, 0.0, 0.0, 0.0};
    double rhs_y[kConsGradComps] = {0.0, 0.0, 0.0, 0.0};

    const int begin = lm.cell_faces_offsets[i];
    const int end = lm.cell_faces_offsets[i + 1];
    for (int kf = begin; kf < end; ++kf) {
      const LocalMesh::LocalFace& f = lm.faces[lm.cell_faces_data[kf]];
      // Neighbor cell: the other side of the face (owned or ghost).
      int j = -1;
      if (f.left == i) {
        j = (f.right >= 0) ? f.right : f.right_local;  // -1 for boundary
      } else {
        j = f.left;  // face listed from its right side: left is owned
      }
      if (j < 0) {
        continue;  // boundary face: no neighbor cell in the stencil
      }

      const double dx = lm.cells[j].centroid.x - lm.cells[i].centroid.x;
      const double dy = lm.cells[j].centroid.y - lm.cells[i].centroid.y;
      const double dist = std::sqrt(dx * dx + dy * dy);
      if (!(dist > 0.0)) {
        continue;  // degenerate geometry; skip (never weights a zero offset)
      }
      const double w = 1.0 / dist;  // inverse-distance weight

      mxx += w * dx * dx;
      mxy += w * dx * dy;
      myy += w * dy * dy;
      for (int k = 0; k < kConsGradComps; ++k) {
        const double du = cell_comp(U_local[j], k) - cell_comp(U_local[i], k);
        rhs_x[k] += w * dx * du;
        rhs_y[k] += w * dy * du;
      }
    }

    // ---- Solve the 2x2 system per variable; Green-Gauss fallback ----
    const double det = mxx * myy - mxy * mxy;
    // Relative conditioning: the absolute determinant can pass even for
    // nearly collinear (skinny wall-adjacent) stencils, whose least-squares
    // gradients are then numerically huge and force the limiter to clip the
    // reconstruction everywhere. Compare det against the diagonal product.
    const bool well_conditioned =
        det > kLstsqDetFloor &&
        det > kLstsqRelCond * (mxx * myy);
    if (well_conditioned) {
      for (int k = 0; k < kConsGradComps; ++k) {
        const Vec2 grad((myy * rhs_x[k] - mxy * rhs_y[k]) / det,
                        (mxx * rhs_y[k] - mxy * rhs_x[k]) / det);
        if (std::isfinite(grad.x) && std::isfinite(grad.y)) {
          set_grad(grads, k, i, grad);
        } else {
          set_grad(grads, k, i, Vec2(0.0, 0.0));
        }
      }
    } else {
      // Poorly conditioned stencil (empty, collinear, or degenerate): fall
      // back to the Green-Gauss average over the cell's faces:
      //   grad = (1/V) * sum_faces U_face * n_outward
      // with U_face the average of the two adjacent cell values (the cell's
      // own value for boundary faces) and n the outward area vector.
      Vec2 gg[kConsGradComps];
      for (int k = 0; k < kConsGradComps; ++k) {
        gg[k] = Vec2(0.0, 0.0);
      }
      for (int kf = begin; kf < end; ++kf) {
        const LocalMesh::LocalFace& f = lm.faces[lm.cell_faces_data[kf]];
        // Outward area vector of cell i (normal points left -> right).
        Vec2 n_out = (f.left == i) ? f.normal : (f.normal * -1.0);
        const double area = n_out.norm();
        if (area <= kStencilEps) {
          continue;
        }
        // Neighbor value (or the cell's own value on boundary faces).
        int j = -1;
        if (f.left == i) {
          j = (f.right >= 0) ? f.right : f.right_local;
        } else {
          j = f.left;
        }
        for (int k = 0; k < kConsGradComps; ++k) {
          const double u_face =
              j >= 0 ? 0.5 * (cell_comp(U_local[i], k) + cell_comp(U_local[j], k))
                     : cell_comp(U_local[i], k);
          gg[k].x += u_face * n_out.x;
          gg[k].y += u_face * n_out.y;
        }
      }
      const double vol = lm.cells[i].volume;
      const double inv_vol = std::abs(vol) > kStencilEps ? 1.0 / vol : 0.0;
      for (int k = 0; k < kConsGradComps; ++k) {
        set_grad(grads, k, i, gg[k] * inv_vol);
      }
    }
  }
}

PrimGradients cons_grad_to_prim(const ConsState& U, const Vec2 grad[4],
                                const GasConfig& gas) {
  const double rho = U.rho;
  const double u = U.rhou / rho;
  const double v = U.rhov / rho;
  const double rho2 = rho * rho;

  PrimGradients P;
  // grad u = (grad(rhou) - u * grad(rho)) / rho
  P.grad_u = (grad[1] - grad[0] * u) / rho;
  P.grad_v = (grad[2] - grad[0] * v) / rho;

  // grad ke for ke = 0.5 * (rhou^2 + rhov^2) / rho:
  //   grad ke = (rhou grad(rhou) + rhov grad(rhov)) / rho
  //            - 0.5 * (rhou^2 + rhov^2) / rho^2 * grad(rho)
  const double kin2 = U.rhou * U.rhou + U.rhov * U.rhov;
  const Vec2 grad_ke = (grad[1] * U.rhou + grad[2] * U.rhov) / rho -
                       grad[0] * (0.5 * kin2 / rho2);

  // grad p = (gamma - 1) * (grad(rhoE) - grad ke)
  const Vec2 grad_p = (grad[3] - grad_ke) * (gas.gamma - 1.0);

  // grad T = (grad p - (p / rho) * grad rho) / (rho * R)
  const double p = pressure_from_cons(U, gas);
  P.grad_T = (grad_p - grad[0] * (p / rho)) / (rho * gas.R);
  return P;
}

}  // namespace cfd
