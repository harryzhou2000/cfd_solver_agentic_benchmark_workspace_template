#include "gradient.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cfd {

LsqStencil buildLsqStencil(const LocalMesh& mesh) {
  const int nLocal = mesh.numLocal();
  LsqStencil s;
  s.entries.resize(nLocal);
  s.a11a12.resize(nLocal);
  s.a21a22.resize(nLocal);
  s.det.resize(nLocal);

  for (int i = 0; i < nLocal; ++i) {
    const Vec2& xi = mesh.centroid[i];
    double a11 = 0.0, a12 = 0.0, a22 = 0.0;
    // Use face neighbors only (interior + halo), deduplicated.
    for (int nb : mesh.neighbors[i]) {
      double dx = mesh.centroid[nb][0] - xi[0];
      double dy = mesh.centroid[nb][1] - xi[1];
      double d2 = dx * dx + dy * dy;
      if (d2 <= 0.0) continue;
      // Inverse-distance-squared weighting: balances the contributions of
      // high-aspect-ratio cells so the tangential gradient is not polluted by
      // the (large) wall-normal gradient.
      double w = 1.0 / d2;
      a11 += w * dx * dx;
      a12 += w * dx * dy;
      a22 += w * dy * dy;
      s.entries[i].push_back({nb, dx, dy, w});
    }
    // Guard against duplicate neighbors (should not happen for a valid mesh).
    if (s.entries[i].size() > 1) {
      // deduplicate by neighbor id
      auto& e = s.entries[i];
      std::sort(e.begin(), e.end(), [](const auto& a, const auto& b) { return a.nbr < b.nbr; });
      size_t w = 1;
      for (size_t k = 1; k < e.size(); ++k) {
        if (e[k].nbr == e[w - 1].nbr) continue;
        e[w++] = e[k];
      }
      e.resize(w);
    }
    const double det = a11 * a22 - a12 * a12;
    s.a11a12[i] = {a11, a12};
    s.a21a22[i] = {a12, a22};
    s.det[i] = det;
  }
  return s;
}

void computeGradients(const LocalMesh& mesh, const LsqStencil& stencil,
                      const std::vector<Prim>& W, std::vector<Prim>& gradDx,
                      std::vector<Prim>& gradDy, int nOwned) {
  for (int i = 0; i < nOwned; ++i) {
    const double det = stencil.det[i];
    Prim bx = {0.0, 0.0, 0.0, 0.0};
    Prim by = {0.0, 0.0, 0.0, 0.0};
    for (const auto& e : stencil.entries[i]) {
      const double dw = e.w * (W[e.nbr][0] - W[i][0]);
      bx[0] += dw * e.dx;
      by[0] += dw * e.dy;
      const double du = e.w * (W[e.nbr][1] - W[i][1]);
      bx[1] += du * e.dx;
      by[1] += du * e.dy;
      const double dv = e.w * (W[e.nbr][2] - W[i][2]);
      bx[2] += dv * e.dx;
      by[2] += dv * e.dy;
      const double dp = e.w * (W[e.nbr][3] - W[i][3]);
      bx[3] += dp * e.dx;
      by[3] += dp * e.dy;
    }
    if (std::fabs(det) > 1e-30) {
      const double a11 = stencil.a11a12[i][0], a12 = stencil.a11a12[i][1];
      const double a21 = stencil.a21a22[i][0], a22 = stencil.a21a22[i][1];
      const double inv = 1.0 / det;
      // Solve [a11 a12; a21 a22] [gx; gy] = [b1; b2]
      for (int k = 0; k < 4; ++k) {
        const double b1 = bx[k], b2 = by[k];
        const double gx = (a22 * b1 - a12 * b2) * inv;
        const double gy = (-a21 * b1 + a11 * b2) * inv;
        bx[k] = gx;
        by[k] = gy;
      }
    } else {
      // degenerate stencil: first-order fallback (documented)
      bx = {0.0, 0.0, 0.0, 0.0};
      by = {0.0, 0.0, 0.0, 0.0};
    }
    gradDx[i] = bx;
    gradDy[i] = by;
  }
}

std::vector<double> computeLimiter(const LocalMesh& mesh, const std::vector<Prim>& W,
                                   const std::vector<Prim>& gradDx,
                                   const std::vector<Prim>& gradDy, int nOwned) {
  std::vector<double> lim(nOwned, 1.0);
  // min/max of each primitive over the cell and its face neighbors
  std::vector<std::array<double, 4>> wmin(nOwned), wmax(nOwned);
  for (int i = 0; i < nOwned; ++i) {
    wmin[i] = W[i];
    wmax[i] = W[i];
  }
  for (int i = 0; i < nOwned; ++i) {
    for (int nb : mesh.neighbors[i]) {
      for (int k = 0; k < 4; ++k) {
        wmin[i][k] = std::min(wmin[i][k], W[nb][k]);
        wmax[i][k] = std::max(wmax[i][k], W[nb][k]);
      }
    }
  }

  for (int i = 0; i < nOwned; ++i) {
    double phi = 1.0;
    // Gather the face reconstruction points of cell i (through all its faces).
    std::vector<int> faceIds;
    for (int fi : mesh.cellFaces[i]) {
      // each face appears once per cell; avoid duplicates (interior faces
      // appear once, boundary once)
      faceIds.push_back(fi);
    }
    for (int fi : faceIds) {
      const auto& f = mesh.faces[fi];
      const Vec2& xf = f.centroid;
      const double dxf = xf[0] - mesh.centroid[i][0];
      const double dyf = xf[1] - mesh.centroid[i][1];
      for (int k = 0; k < 4; ++k) {
        const double v0 = W[i][k];
        const double vf = v0 + gradDx[i][k] * dxf + gradDy[i][k] * dyf;
        double psi = 1.0;
        if (vf > v0) {
          if (vf > wmax[i][k]) psi = (wmax[i][k] - v0) / (vf - v0);
        } else if (vf < v0) {
          if (vf < wmin[i][k]) psi = (wmin[i][k] - v0) / (vf - v0);
        }
        psi = std::max(0.0, std::min(1.0, psi));
        phi = std::min(phi, psi);
      }
    }
    lim[i] = phi;
  }
  return lim;
}

}  // namespace cfd
