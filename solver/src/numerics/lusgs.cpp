#include "numerics/lusgs.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "numerics/reconstruction.hpp"
#include "physics/gas_model.hpp"
#include "physics/inviscid_flux.hpp"

namespace cfd {

namespace {

// 4x4 (row-major) by 4-vector product: y = m * x.
inline void matvec4(const double* m, const double* x, double* y) {
  for (int r = 0; r < 4; ++r) {
    y[r] = m[r * 4 + 0] * x[0] + m[r * 4 + 1] * x[1] + m[r * 4 + 2] * x[2] +
           m[r * 4 + 3] * x[3];
  }
}

// Returns true when `cell` is the LEFT cell of the face with unified index
// `uf` (picks the correct block orientation).
bool is_face_left(const DistributedMesh& dmesh, int uf, int cell) {
  const int n_int = static_cast<int>(dmesh.interior_faces.size());
  const int n_bnd = static_cast<int>(dmesh.boundary_faces.size());
  const int n_snd = static_cast<int>(dmesh.send_faces.size());
  if (uf < n_int) return dmesh.interior_faces[uf].left_cell == cell;
  if (uf < n_int + n_bnd) return true;  // boundary faces carry no block
  if (uf < n_int + n_bnd + n_snd)
    return dmesh.send_faces[uf - n_int - n_bnd].left_cell == cell;
  return dmesh.recv_faces[uf - n_int - n_bnd - n_snd].left_cell == cell;
}

struct FaceData {
  double lam = 0.0;    // inviscid spectral radius (|vn| + a) * A
  double alpha = 0.0;  // |A_euler| = alpha*A_euler + beta*I
  double beta = 0.0;
  double Ae[16] = {0.0};  // convective Euler Jacobian along the face normal
};

// Classic Yoon-Jameson split of the convective Jacobian A_c = 0.5*A*Ae:
//   |A_c| = 0.5*A*(alpha*Ae + beta*I)
//   lower part (row left,  neighbor lower): 0.25A*((1+a)Ae + bI)
//   upper part (row left,  neighbor upper): 0.25A*((1-a)Ae - bI)
//   lower part (row right, neighbor lower): 0.25A*((a-1)Ae + bI)
//   upper part (row right, neighbor upper): -0.25A*((1+a)Ae + bI)
// out: the L or U block (row-major 4x4) for the row cell w.r.t. the neighbor.
inline void lusgs_block(int row_is_left, int neighbor_lower, double area,
                        const double* Ae, double alpha, double beta,
                        double* out) {
  const double s = 0.25 * area;
  if (row_is_left) {
    const double p = neighbor_lower ? (1.0 + alpha) : (1.0 - alpha);
    const double q = neighbor_lower ? beta : -beta;
    for (int r = 0; r < 4; ++r)
      for (int cc = 0; cc < 4; ++cc)
        out[r * 4 + cc] = s * (p * Ae[r * 4 + cc] + ((r == cc) ? q : 0.0));
  } else {
    const double p = neighbor_lower ? (alpha - 1.0) : -(1.0 + alpha);
    const double q = neighbor_lower ? beta : -beta;
    for (int r = 0; r < 4; ++r)
      for (int cc = 0; cc < 4; ++cc)
        out[r * 4 + cc] = s * (p * Ae[r * 4 + cc] + ((r == cc) ? q : 0.0));
  }
}

}  // namespace

void lusgs_sweep(const std::vector<double>& U,
                 const std::vector<double>& residual,
                 const DistributedMesh& dmesh,
                 const std::vector<double>& dt,
                 const std::vector<std::vector<int>>& cell_face_map,
                 double gamma, std::vector<double>& dU,
                 const std::vector<double>* diag_extra) {
  const long long n_owned = dmesh.n_owned;
  const int nuf = n_unified_faces(dmesh);
  dU.assign(static_cast<std::size_t>(n_owned) * NVARS, 0.0);
  if (n_owned == 0 || nuf == 0) return;

  // Block LU-SGS (Yoon-Jameson) for the Rusanov flux. The off-diagonal
  // blocks are the +/- split of the CONVECTIVE Jacobian (|A| from the
  // eigen-decomposition: |A_euler| = alpha*A_euler + beta*I), giving
  // upwind coupling in BOTH sweeps; the dissipation 0.5*lam*A*I and the
  // |A| smoothing live in the 4x4 diagonal, so the implicit operator is a
  // faithful J_tilde = J_true + V/dt*I and stays stable at high CFL.
  const int n_int = static_cast<int>(dmesh.interior_faces.size());
  const int n_bnd = static_cast<int>(dmesh.boundary_faces.size());
  const int n_snd = static_cast<int>(dmesh.send_faces.size());

  auto face_lr = [&](int uf, int& a, int& b, const Face2D*& f) {
    if (uf < n_int) {
      f = &dmesh.interior_faces[uf];
      a = f->left_cell;
      b = f->right_cell;
    } else if (uf < n_int + n_bnd) {
      f = &dmesh.boundary_faces[uf - n_int];
      a = f->left_cell >= 0 ? f->left_cell : f->right_cell;
      b = -1;
    } else if (uf < n_int + n_bnd + n_snd) {
      f = &dmesh.send_faces[uf - n_int - n_bnd];
      a = f->left_cell;
      b = f->right_cell;
    } else {
      f = &dmesh.recv_faces[uf - n_int - n_bnd - n_snd];
      a = f->left_cell;
      b = f->right_cell;
    }
  };

  std::vector<FaceData> fd(static_cast<std::size_t>(nuf));
  for (int uf = 0; uf < nuf; ++uf) {
    int a = -1, b = -1;
    const Face2D* f = nullptr;
    face_lr(uf, a, b, f);
    const PrimitiveState pA =
        cons_to_prim(U.data() + static_cast<std::size_t>(a) * NVARS, gamma,
                     1.0);
    FaceData& J = fd[static_cast<std::size_t>(uf)];
    if (b < 0) {
      // Boundary face: dissipation-only diagonal contribution.
      const double vn_f = pA.u * f->normal.x + pA.v * f->normal.y;
      J.lam = (std::fabs(vn_f) + pA.a) * f->area;
      continue;
    }
    const PrimitiveState pB =
        cons_to_prim(U.data() + static_cast<std::size_t>(b) * NVARS, gamma,
                     1.0);
    const double vn_f = 0.5 * ((pA.u + pB.u) * f->normal.x +
                               (pA.v + pB.v) * f->normal.y);
    const double a_f = 0.5 * (pA.a + pB.a);
    J.lam = (std::fabs(vn_f) + a_f) * f->area;

    double Uavg[NVARS];
    for (int k = 0; k < NVARS; ++k)
      Uavg[k] =
          0.5 * (U[static_cast<std::size_t>(a) * NVARS + k] +
                 U[static_cast<std::size_t>(b) * NVARS + k]);
    euler_flux_jacobian(Uavg, f->normal, gamma, J.Ae);
    // |A_euler| = alpha*A_euler + beta*I (eigenvalues |vn|, |vn|, |vn+-a|).
    const double av = std::fabs(vn_f);
    const double avp = std::fabs(vn_f + a_f);
    const double avm = std::fabs(vn_f - a_f);
    J.alpha = (avp - avm) / (2.0 * a_f);
    J.beta = av - (avp - av) * (vn_f - a_f) / (2.0 * a_f) +
             (avm - av) * (vn_f + a_f) / (2.0 * a_f);
  }

  // Assemble and invert the per-cell 4x4 diagonal.
  std::vector<double> Dinv(static_cast<std::size_t>(n_owned) * 16, 0.0);
  for (long long c = 0; c < n_owned; ++c) {
    const double V = dmesh.cells[static_cast<std::size_t>(c)].volume;
    const double pdt = V / dt[static_cast<std::size_t>(c)];
    Eigen::Matrix4d D = Eigen::Matrix4d::Zero();
    for (int uf : cell_face_map[static_cast<std::size_t>(c)]) {
      const FaceData& J = fd[static_cast<std::size_t>(uf)];
      const Face2D* f = nullptr;
      int a, b;
      face_lr(uf, a, b, f);
      const double area = f->area;
      const double sgn = (a == static_cast<int>(c)) ? 1.0 : -1.0;
      const double al = (a == static_cast<int>(c)) ? J.alpha : -J.alpha;
      for (int r = 0; r < 4; ++r) {
        for (int cc = 0; cc < 4; ++cc) {
          // |A| smoothing: 0.5*A*(alpha(n_if)*Ae(n_if) + beta*I), where
          // Ae(n_if) = sgn*Ae and alpha(n_if) = sgn*alpha for cell b.
          D(r, cc) += 0.5 * area * (al * sgn * J.Ae[r * 4 + cc] +
                                    ((r == cc) ? J.beta : 0.0));
        }
        D(r, r) += 0.5 * J.lam;  // dissipation 0.5*lam*A*I (lam includes A)
      }
    }
    for (int r = 0; r < 4; ++r) D(r, r) += pdt;
    if (diag_extra != nullptr) {
      const double e = (*diag_extra)[static_cast<std::size_t>(c)];
      for (int r = 0; r < 4; ++r) D(r, r) += e;
    }

    Eigen::Matrix4d Di = D.inverse();
    bool ok = true;
    for (int r = 0; r < 4; ++r)
      for (int cc = 0; cc < 4; ++cc)
        if (!std::isfinite(Di(r, cc))) ok = false;
    if (!ok) {
      // Degenerate: fall back to the scalar spectral-radius inverse.
      double dscalar = pdt;
      for (int uf : cell_face_map[static_cast<std::size_t>(c)])
        dscalar += 0.5 * fd[static_cast<std::size_t>(uf)].lam;
      if (diag_extra != nullptr)
        dscalar += (*diag_extra)[static_cast<std::size_t>(c)];
      for (int r = 0; r < 4; ++r) Di(r, r) = 1.0 / dscalar;
    }
    for (int r = 0; r < 4; ++r)
      for (int cc = 0; cc < 4; ++cc)
        Dinv[static_cast<std::size_t>(c) * 16 + r * 4 + cc] = Di(r, cc);
  }

  double tmp[4], blk[16];

  // Forward sweep (lower triangle): owned neighbors with index < i.
  for (long long i = 0; i < n_owned; ++i) {
    double* dui = dU.data() + static_cast<std::size_t>(i) * NVARS;
    const double* r = residual.data() + static_cast<std::size_t>(i) * NVARS;
    for (int k = 0; k < NVARS; ++k) dui[k] = -r[k];
    for (int uf : cell_face_map[static_cast<std::size_t>(i)]) {
      const int j = face_other_cell(dmesh, uf, static_cast<int>(i));
      if (j < 0 || j >= static_cast<int>(n_owned) || j >= static_cast<int>(i))
        continue;
      const FaceData& J = fd[static_cast<std::size_t>(uf)];
      const int row_left = is_face_left(dmesh, uf, static_cast<int>(i)) ? 1 : 0;
      const Face2D* f = nullptr;
      int a, b;
      face_lr(uf, a, b, f);
      lusgs_block(row_left, /*neighbor_lower=*/1, f->area, J.Ae, J.alpha,
                  J.beta, blk);
      const double* duj = dU.data() + static_cast<std::size_t>(j) * NVARS;
      matvec4(blk, duj, tmp);
      for (int k = 0; k < NVARS; ++k) dui[k] -= tmp[k];
    }
    const double* Di = Dinv.data() + static_cast<std::size_t>(i) * 16;
    matvec4(Di, dui, tmp);
    for (int k = 0; k < NVARS; ++k) dui[k] = tmp[k];
  }

  // Backward sweep (upper triangle): owned neighbors with index > i.
  for (long long i = n_owned - 1; i >= 0; --i) {
    double* dui = dU.data() + static_cast<std::size_t>(i) * NVARS;
    const double* Di = Dinv.data() + static_cast<std::size_t>(i) * 16;
    for (int uf : cell_face_map[static_cast<std::size_t>(i)]) {
      const int j = face_other_cell(dmesh, uf, static_cast<int>(i));
      if (j < 0 || j >= static_cast<int>(n_owned) || j <= static_cast<int>(i))
        continue;
      const FaceData& J = fd[static_cast<std::size_t>(uf)];
      const int row_left = is_face_left(dmesh, uf, static_cast<int>(i)) ? 1 : 0;
      const Face2D* f = nullptr;
      int a, b;
      face_lr(uf, a, b, f);
      lusgs_block(row_left, /*neighbor_lower=*/0, f->area, J.Ae, J.alpha,
                  J.beta, blk);
      const double* duj = dU.data() + static_cast<std::size_t>(j) * NVARS;
      double contrib[4];
      matvec4(blk, duj, tmp);    // U_ij * du_j
      matvec4(Di, tmp, contrib); // Di^-1 * U_ij * du_j
      for (int k = 0; k < NVARS; ++k) dui[k] -= contrib[k];
    }
  }
}

void limit_update_positivity(const std::vector<double>& U,
                             std::vector<double>& dU, long long n_owned,
                             double gamma, double floor_frac) {
  for (long long c = 0; c < n_owned; ++c) {
    const double* u = U.data() + static_cast<std::size_t>(c) * NVARS;
    double* du = dU.data() + static_cast<std::size_t>(c) * NVARS;
    const double rho = u[0];
    if (!(rho > 0.0)) continue;  // already broken; do not mask it
    const double rho_u = u[1], rho_v = u[2], E = u[3];
    const double p = (gamma - 1.0) *
                     (E - 0.5 * (rho_u * rho_u + rho_v * rho_v) / rho);
    if (!(p > 0.0)) continue;  // already broken; do not mask it

    double alpha_max = 1.0;
    if (du[0] < 0.0 && rho + du[0] < floor_frac * rho)
      alpha_max = std::min(alpha_max, (floor_frac - 1.0) * rho / du[0]);

    auto p_at = [&](double a) {
      const double r_a = rho + a * du[0];
      const double ru_a = rho_u + a * du[1];
      const double rv_a = rho_v + a * du[2];
      const double E_a = E + a * du[3];
      return (gamma - 1.0) *
             (E_a - 0.5 * (ru_a * ru_a + rv_a * rv_a) / r_a);
    };
    if (alpha_max > 0.0 && p_at(alpha_max) < floor_frac * p) {
      double lo = 0.0, hi = alpha_max;
      for (int it = 0; it < 50; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (p_at(mid) < floor_frac * p)
          hi = mid;
        else
          lo = mid;
      }
      alpha_max = lo;
    }
    if (alpha_max < 1.0)
      for (int k = 0; k < NVARS; ++k) du[k] *= alpha_max;
  }
}

}  // namespace cfd
