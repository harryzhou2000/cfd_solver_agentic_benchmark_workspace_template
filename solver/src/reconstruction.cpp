#include "solver.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {
namespace {

// Ghost primitive state at a boundary face (mirror / freestream).
inline bool boundary_ghost(const Solver& s, int cell, const LocalMesh::Face& f,
                           Prim& qg) {
  const Prim qc{s.Q[4 * cell], s.Q[4 * cell + 1], s.Q[4 * cell + 2],
                s.Q[4 * cell + 3]};
  return boundary_ghost_state(qc, f.nx, f.ny, f.bc, s.fs, s.gas, qg);
}

}  // namespace

void compute_primitives(Solver& s) {
  const int nc = s.nc;
  const double gm1 = s.gas.gamma - 1.0;
  for (int i = 0; i < nc; ++i) {
    const double rho = s.U[4 * i];
    const double u = s.U[4 * i + 1] / rho;
    const double v = s.U[4 * i + 2] / rho;
    const double ek = 0.5 * (u * u + v * v);
    double p = gm1 * (s.U[4 * i + 3] - rho * ek);
    if (p < 1e-6 * s.gas.p_ref) p = 1e-6 * s.gas.p_ref;
    s.Q[4 * i] = rho;
    s.Q[4 * i + 1] = u;
    s.Q[4 * i + 2] = v;
    s.Q[4 * i + 3] = p;
  }
}

void compute_gradients(Solver& s) {
  const int n_owned = s.mesh.n_owned;
  const int nc = s.nc;
  Prim qg;
  for (int i = 0; i < n_owned; ++i) {
    const auto& cell = s.mesh.cells[i];
    double gx[4] = {0, 0, 0, 0};
    double gy[4] = {0, 0, 0, 0};
    for (int fi : cell.faces) {
      const auto& f = s.mesh.faces[fi];
      const double* qj;
      double qjbuf[4];
      if (f.c1 >= 0) {
        qj = &s.Q[4 * f.c1];
      } else {
        if (!boundary_ghost(s, i, f, qg)) {
          qg = Prim{0, 0, 0, 0};
        }
        qjbuf[0] = qg.rho;
        qjbuf[1] = qg.u;
        qjbuf[2] = qg.v;
        qjbuf[3] = qg.p;
        qj = qjbuf;
      }
      const double na = f.area;
      const double nx = f.nx, ny = f.ny;
      for (int k = 0; k < 4; ++k) {
        const double qmid = 0.5 * (s.Q[4 * i + k] + qj[k]);
        gx[k] += qmid * nx * na;
        gy[k] += qmid * ny * na;
      }
    }
    const double inv = 1.0 / cell.vol;
    for (int k = 0; k < 4; ++k) {
      s.grad[(4 * i + k) * 2] = gx[k] * inv;
      s.grad[(4 * i + k) * 2 + 1] = gy[k] * inv;
    }
  }
  (void)nc;
}

void compute_limiters(Solver& s) {
  const int n_owned = s.mesh.n_owned;
  const int nc = s.nc;
  Prim qg;
  for (int i = 0; i < n_owned; ++i) {
    const auto& cell = s.mesh.cells[i];
    double qmax[4] = {s.Q[4 * i], s.Q[4 * i + 1], s.Q[4 * i + 2],
                      s.Q[4 * i + 3]};
    double qmin[4] = {s.Q[4 * i], s.Q[4 * i + 1], s.Q[4 * i + 2],
                      s.Q[4 * i + 3]};
    for (int fi : cell.faces) {
      const auto& f = s.mesh.faces[fi];
      const double* qj;
      double qjbuf[4];
      if (f.c1 >= 0) {
        qj = &s.Q[4 * f.c1];
      } else {
        if (!boundary_ghost(s, i, f, qg)) qg = Prim{0, 0, 0, 0};
        qjbuf[0] = qg.rho;
        qjbuf[1] = qg.u;
        qjbuf[2] = qg.v;
        qjbuf[3] = qg.p;
        qj = qjbuf;
      }
      for (int k = 0; k < 4; ++k) {
        qmax[k] = std::max(qmax[k], qj[k]);
        qmin[k] = std::min(qmin[k], qj[k]);
      }
    }
    double phi[4] = {1, 1, 1, 1};
    const bool venkat = getenv("CFD_LIMITER") &&
                        std::string(getenv("CFD_LIMITER")) == "vk";
    const bool vel_free = getenv("CFD_LIMITER") &&
                          std::string(getenv("CFD_LIMITER")) == "vel_free";
    const double h = std::min(std::sqrt(cell.vol), 0.25);
    // Venkatakrishnan epsilon: eps = (K*h)^3 with K configurable (CFD_VK_K).
    // Larger K keeps the limiter inactive in smooth but non-monotone regions
    // (e.g. the cylinder wake), which is required to retain the second-order
    // accuracy there and to let the physical instability grow into a vortex
    // street; the default K=1 matches the classical h^3 choice.
    const double vk_k =
        getenv("CFD_VK_K") ? std::max(0.0, std::atof(getenv("CFD_VK_K"))) : 1.0;
    const double eps2 = venkat ? (vk_k * h) * (vk_k * h) * (vk_k * h) : 0.0;
    for (int fi : cell.faces) {
      const auto& f = s.mesh.faces[fi];
      const double dx = f.fx - cell.cx;
      const double dy = f.fy - cell.cy;
      for (int k = 0; k < 4; ++k) {
        // "vel_free" limiter mode: leave the velocity components unlimited so
        // the smooth (but non-monotone) wake shear layers keep their
        // second-order accuracy; density and pressure stay Barth-limited.
        if (vel_free && (k == 1 || k == 2)) continue;
        const double qc = s.Q[4 * i + k];
        const double qf =
            qc + s.grad[(4 * i + k) * 2] * dx + s.grad[(4 * i + k) * 2 + 1] * dy;
        double r = 1.0;
        const double eps = 1e-14 * (std::abs(qc) + 1e-30);
        const double dq = qf - qc;
        if (venkat) {
          if (dq > eps) {
            const double dmax = qmax[k] - qc;
            const double num = dmax * dmax + eps2 + 2.0 * dmax * dq;
            const double den = dmax * dmax + 2.0 * dq * dq + dmax * dq + eps2;
            r = (den > 1e-300) ? std::max(0.0, num / den) : 1.0;
          } else if (dq < -eps) {
            const double dmin = qmin[k] - qc;
            const double num = dmin * dmin + eps2 + 2.0 * dmin * dq;
            const double den = dmin * dmin + 2.0 * dq * dq + dmin * dq + eps2;
            r = (den > 1e-300) ? std::max(0.0, num / den) : 1.0;
          }
        } else {
          if (dq > eps) {
            r = (qmax[k] - qc) / dq;
          } else if (dq < -eps) {
            r = (qmin[k] - qc) / dq;
          }
        }
        phi[k] = std::min(phi[k], std::max(0.0, std::min(1.0, r)));
      }
    }
    for (int k = 0; k < 4; ++k) s.phi[4 * i + k] = phi[k];
  }
  (void)nc;
}

// Piecewise-linear, Barth-limited reconstruction of the primitive state of
// `cell` at the face midpoint `f`. Returns false if a positivity fallback
// was applied (first-order state).
bool reconstruct_face(const Solver& s, int cell, const LocalMesh::Face& f,
                      Prim& q) {
  // Cells adjacent to a solid wall: first-order face states. The wall flux
  // itself is exact, and the wall-adjacent layer on these benchmark meshes
  // is thin enough that the cell-centered values are the robust choice.
  const bool wall_adjacent =
      getenv("CFD_WALL_FIRST") && s.wall_cell[cell];
  // Faces connecting cells of very different sizes (e.g. the fine/coarse
  // zone interface of a patched mesh) pollute the Green-Gauss gradients of
  // the small cell, whose interface face dominates its volume-weighted sum.
  // Use the plain cell-center states there (first-order) for robustness.
  bool unbalanced = false;
  if (f.c1 >= 0) {
    const double v0 = s.mesh.cells[f.c0].vol;
    const double v1 = s.mesh.cells[f.c1].vol;
    const double r = std::max(v0, v1) / std::min(v0, v1);
    const double ratio_lim =
        getenv("CFD_RATIO") ? std::atof(getenv("CFD_RATIO")) : 8.0;
    unbalanced = r > ratio_lim;
  }
  if (getenv("CFD_FIRST_ORDER") || wall_adjacent || unbalanced) {
    q.rho = s.Q[4 * cell];
    q.u = s.Q[4 * cell + 1];
    q.v = s.Q[4 * cell + 2];
    q.p = s.Q[4 * cell + 3];
    return true;
  }
  const double dx = f.fx - s.mesh.cells[cell].cx;
  const double dy = f.fy - s.mesh.cells[cell].cy;
  q.rho = s.Q[4 * cell] +
          s.phi[4 * cell] *
              (s.grad[(4 * cell) * 2] * dx + s.grad[(4 * cell) * 2 + 1] * dy);
  q.u = s.Q[4 * cell + 1] +
        s.phi[4 * cell + 1] *
            (s.grad[(4 * cell + 1) * 2] * dx +
             s.grad[(4 * cell + 1) * 2 + 1] * dy);
  q.v = s.Q[4 * cell + 2] +
        s.phi[4 * cell + 2] *
            (s.grad[(4 * cell + 2) * 2] * dx +
             s.grad[(4 * cell + 2) * 2 + 1] * dy);
  q.p = s.Q[4 * cell + 3] +
        s.phi[4 * cell + 3] *
            (s.grad[(4 * cell + 3) * 2] * dx +
             s.grad[(4 * cell + 3) * 2 + 1] * dy);

  if (q.rho <= s.rho_min || q.p <= s.p_min) {
    // Positivity fallback: first-order cell values.
    q.rho = s.Q[4 * cell];
    q.u = s.Q[4 * cell + 1];
    q.v = s.Q[4 * cell + 2];
    q.p = s.Q[4 * cell + 3];
    return false;
  }
  return true;
}

}  // namespace cfd
