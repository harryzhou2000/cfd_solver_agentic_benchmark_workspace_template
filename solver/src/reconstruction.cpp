#include "reconstruction.h"

namespace cfd {

Prim boundary_stencil_value(BCType bc, const Prim& w_cell,
                            double nwx, double nwy, const CaseConfig& cfg) {
  Prim wb = w_cell;
  switch (bc) {
    case BCType::Farfield: {
      wb.rho = cfg.rho_inf;
      wb.u = cfg.u_inf;
      wb.v = cfg.v_inf;
      wb.p = cfg.p_inf;
      break;
    }
    case BCType::SlipWall: {
      double un = w_cell.u * nwx + w_cell.v * nwy;
      wb.u = w_cell.u - 2.0 * un * nwx;
      wb.v = w_cell.v - 2.0 * un * nwy;
      break;
    }
    case BCType::NoSlipAdiabaticWall: {
      wb.u = 0.0;
      wb.v = 0.0;
      break;
    }
    default:
      break;
  }
  return wb;
}

void compute_gradient_weights(const LocalMesh& lm, const CaseConfig& cfg,
                              GradWeights& gw) {
  gw.wx.resize(lm.nowned + lm.nghost);
  gw.wy.resize(lm.nowned + lm.nghost);
  for (int i = 0; i < lm.nowned; ++i) {
    const auto& faces = lm.cell_faces[i];
    int nf = (int)faces.size();
    std::vector<double>& wx = gw.wx[i];
    std::vector<double>& wy = gw.wy[i];
    wx.assign(nf, 0.0);
    wy.assign(nf, 0.0);
    double Axx = 1.0e-16, Axy = 0.0, Ayy = 1.0e-16;
    std::vector<double> dx(nf), dy(nf), w(nf);
    for (int k = 0; k < nf; ++k) {
      int f = faces[k];
      double px, py;
      if (lm.face_cb[f] >= 0) {
        int j = (lm.face_ca[f] == i) ? lm.face_cb[f] : lm.face_ca[f];
        px = lm.cx[j];
        py = lm.cy[j];
      } else {
        const BoundaryFace& bf = lm.boundary_faces[lm.face_bface[f]];
        px = bf.xm;
        py = bf.ym;
      }
      dx[k] = px - lm.cx[i];
      dy[k] = py - lm.cy[i];
      double d2 = dx[k] * dx[k] + dy[k] * dy[k];
      w[k] = 1.0 / (d2 + 1.0e-16);
      Axx += w[k] * dx[k] * dx[k];
      Axy += w[k] * dx[k] * dy[k];
      Ayy += w[k] * dy[k] * dy[k];
    }
    // Regularize near-singular least-squares systems.
    double scale = std::max(Axx, Ayy);
    double reg = 1.0e-12 * scale;
    Axx += reg;
    Ayy += reg;
    double det = Axx * Ayy - Axy * Axy;
    double inv00, inv01, inv10, inv11;
    if (det > 1.0e-24 * scale * scale) {
      inv00 = Ayy / det; inv01 = -Axy / det;
      inv10 = -Axy / det; inv11 = Axx / det;
    } else {
      inv00 = 1.0 / Axx; inv01 = 0.0;
      inv10 = 0.0; inv11 = 1.0 / Ayy;
    }
    for (int k = 0; k < nf; ++k) {
      wx[k] = w[k] * (inv00 * dx[k] + inv01 * dy[k]);
      wy[k] = w[k] * (inv10 * dx[k] + inv11 * dy[k]);
    }
  }
}

PrimGrad compute_gradient(const LocalMesh& lm, const GradWeights& gw,
                          const std::vector<Prim>& W, int cell,
                          const CaseConfig& cfg) {
  if (getenv("CFD_DEBUG_WALL") && lm.local_to_global[cell] == 1506) {
    g_log.logf("DBG cg cell %d W %.6e %.6e %.6e %.6e nfaces %zu\n",
               cell, W[cell].rho, W[cell].u, W[cell].v, W[cell].p,
               lm.cell_faces[cell].size());
    for (size_t k = 0; k < lm.cell_faces[cell].size(); ++k) {
      int f = lm.cell_faces[cell][k];
      int j = (lm.face_cb[f] >= 0) ? ((lm.face_ca[f] == cell) ? lm.face_cb[f] : lm.face_ca[f]) : -1;
      g_log.logf("   k %zu face %d j %d Wj %.6e %.6e %.6e %.6e wx %.6e wy %.6e\n",
                 k, f, j, j >= 0 ? W[j].rho : -1.0, j >= 0 ? W[j].u : -1.0,
                 j >= 0 ? W[j].v : -1.0, j >= 0 ? W[j].p : -1.0,
                 gw.wx[cell][k], gw.wy[cell][k]);
    }
  }
  const auto& faces = lm.cell_faces[cell];
  const auto& wx = gw.wx[cell];
  const auto& wy = gw.wy[cell];
  PrimGrad g;
  for (size_t k = 0; k < faces.size(); ++k) {
    int f = faces[k];
    Prim wb;
    if (lm.face_cb[f] >= 0) {
      int j = (lm.face_ca[f] == cell) ? lm.face_cb[f] : lm.face_ca[f];
      wb = W[j];
    } else {
      const BoundaryFace& bf = lm.boundary_faces[lm.face_bface[f]];
      double len = lm.face_len[f];
      double nwx = -lm.face_nx[f] / len;
      double nwy = -lm.face_ny[f] / len;
      wb = boundary_stencil_value(lm.face_bc[f], W[cell], nwx, nwy, cfg);
    }
    double dr = wb.rho - W[cell].rho;
    double du = wb.u - W[cell].u;
    double dv = wb.v - W[cell].v;
    double dp = wb.p - W[cell].p;
    g.rx += wx[k] * dr;
    g.ry += wy[k] * dr;
    g.ux += wx[k] * du;
    g.uy += wy[k] * du;
    g.vx += wx[k] * dv;
    g.vy += wy[k] * dv;
    g.px += wx[k] * dp;
    g.py += wy[k] * dp;
  }
  return g;
}

namespace {

double bj_limiter(const std::vector<double>& phi_face, double phi_i,
                  double phi_min, double phi_max) {
  double psi = 1.0;
  for (double phif : phi_face) {
    double d = phif - phi_i;
    if (d > 1.0e-14) {
      double num = phi_max - phi_i;
      psi = std::min(psi, num > 0.0 ? num / d : 0.0);
    } else if (d < -1.0e-14) {
      double num = phi_min - phi_i;
      psi = std::min(psi, num < 0.0 ? num / d : 0.0);
    }
  }
  return std::max(0.0, std::min(1.0, psi));
}

}  // namespace

std::array<double, 4> limit_gradient(const LocalMesh& lm, const CaseConfig& cfg,
                                     const std::vector<Prim>& W, int cell,
                                     const PrimGrad& grad, long long& fallback_count) {
  const auto& faces = lm.cell_faces[cell];
  int nf = (int)faces.size();
  // Stencil extrema per primitive.
  double rho_min = W[cell].rho, rho_max = W[cell].rho;
  double u_min = W[cell].u, u_max = W[cell].u;
  double v_min = W[cell].v, v_max = W[cell].v;
  double p_min = W[cell].p, p_max = W[cell].p;
  std::vector<double> face_rho(nf), face_u(nf), face_v(nf), face_p(nf);
  for (int k = 0; k < nf; ++k) {
    int f = faces[k];
    double px, py;
    Prim wb;
    if (lm.face_cb[f] >= 0) {
      int j = (lm.face_ca[f] == cell) ? lm.face_cb[f] : lm.face_ca[f];
      px = lm.cx[j];
      py = lm.cy[j];
      wb = W[j];
    } else {
      const BoundaryFace& bf = lm.boundary_faces[lm.face_bface[f]];
      px = bf.xm;
      py = bf.ym;
      double len = lm.face_len[f];
      double nwx = -lm.face_nx[f] / len;
      double nwy = -lm.face_ny[f] / len;
      wb = boundary_stencil_value(lm.face_bc[f], W[cell], nwx, nwy, cfg);
    }
    rho_min = std::min(rho_min, wb.rho); rho_max = std::max(rho_max, wb.rho);
    u_min = std::min(u_min, wb.u); u_max = std::max(u_max, wb.u);
    v_min = std::min(v_min, wb.v); v_max = std::max(v_max, wb.v);
    p_min = std::min(p_min, wb.p); p_max = std::max(p_max, wb.p);

    double ddx = px - lm.cx[cell], ddy = py - lm.cy[cell];
    face_rho[k] = W[cell].rho + grad.rx * ddx + grad.ry * ddy;
    face_u[k] = W[cell].u + grad.ux * ddx + grad.uy * ddy;
    face_v[k] = W[cell].v + grad.vx * ddx + grad.vy * ddy;
    face_p[k] = W[cell].p + grad.px * ddx + grad.py * ddy;
  }
  std::array<double, 4> psi;
  psi[0] = bj_limiter(face_rho, W[cell].rho, rho_min, rho_max);
  psi[1] = bj_limiter(face_u, W[cell].u, u_min, u_max);
  psi[2] = bj_limiter(face_v, W[cell].v, v_min, v_max);
  psi[3] = bj_limiter(face_p, W[cell].p, p_min, p_max);
  // Positivity fallback: any reconstructed face state with nonpositive
  // density or pressure degrades this cell to first order.
  const double rho_tol = 1.0e-12, p_tol = 1.0e-12;
  bool bad = false;
  for (int k = 0; k < nf; ++k) {
    if (!(face_rho[k] > rho_tol) || !(face_p[k] > p_tol)) { bad = true; break; }
  }
  if (bad) {
    psi = {0.0, 0.0, 0.0, 0.0};
    ++fallback_count;
  }
  return psi;
}

Prim face_state(const LocalMesh& lm, const std::vector<Prim>& W,
                const std::vector<PrimGrad>& grads,
                int cell, int face) {
  double px, py;
  if (lm.face_cb[face] >= 0) {
    int j = (lm.face_ca[face] == cell) ? lm.face_cb[face] : lm.face_ca[face];
    px = 0.5 * (lm.cx[cell] + lm.cx[j]);
    py = 0.5 * (lm.cy[cell] + lm.cy[j]);
  } else {
    const BoundaryFace& bf = lm.boundary_faces[lm.face_bface[face]];
    px = bf.xm;
    py = bf.ym;
  }
  double ddx = px - lm.cx[cell], ddy = py - lm.cy[cell];
  const PrimGrad& g = grads[cell];
  Prim w;
  w.rho = W[cell].rho + (g.rx * ddx + g.ry * ddy);
  w.u = W[cell].u + (g.ux * ddx + g.uy * ddy);
  w.v = W[cell].v + (g.vx * ddx + g.vy * ddy);
  w.p = W[cell].p + (g.px * ddx + g.py * ddy);
  return w;
}

double wall_pressure(const LocalMesh& lm, const std::vector<Prim>& W,
                     const std::vector<PrimGrad>& grads,
                     int cell, int face) {
  const BoundaryFace& bf = lm.boundary_faces[lm.face_bface[face]];
  double ddx = bf.xm - lm.cx[cell], ddy = bf.ym - lm.cy[cell];
  const PrimGrad& g = grads[cell];
  double pw = W[cell].p + (g.px * ddx + g.py * ddy);
  return std::max(pw, 1.0e-10);
}

}  // namespace cfd
