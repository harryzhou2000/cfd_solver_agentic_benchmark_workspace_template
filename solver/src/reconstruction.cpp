#include "reconstruction.hpp"

#include <algorithm>
#include <cmath>

namespace cfd2d {

LsqStencil buildLsqStencil(const LocalMesh& lm) {
  LsqStencil ls;
  ls.neighbors.assign(lm.nOwned, {});
  ls.coef.assign(lm.nOwned, {});

  // Collect face neighbors for each owned cell.
  for (int f = 0; f < lm.nFaces; ++f) {
    const int L = lm.faceCellL[f];
    const int R = lm.faceCellR[f];
    if (R < 0) continue;
    if (L < lm.nOwned) ls.neighbors[L].push_back(R);
    if (R < lm.nOwned) ls.neighbors[R].push_back(L);
  }

  // Weighted least squares: minimize sum_j w_j (gx dxj + gy dyj - dphi_j)^2.
  // w_j = 1/|d_j|^2 (inverse-distance-squared weighting). 2x2 normal equations.
  for (int i = 0; i < lm.nOwned; ++i) {
    const auto& nbrs = ls.neighbors[i];
    auto& coef = ls.coef[i];
    coef.assign(nbrs.size(), {0.0, 0.0});
    double a11 = 0.0, a12 = 0.0, a22 = 0.0;
    for (size_t k = 0; k < nbrs.size(); ++k) {
      const int j = nbrs[k];
      const double dx = lm.cellCx[j] - lm.cellCx[i];
      const double dy = lm.cellCy[j] - lm.cellCy[i];
      const double w = 1.0 / std::max(dx * dx + dy * dy, 1e-300);
      a11 += w * dx * dx;
      a12 += w * dx * dy;
      a22 += w * dy * dy;
    }
    double det = a11 * a22 - a12 * a12;
    if (!(det > 1e-30) || nbrs.size() < 2) {
      // Degenerate stencil: leave zero coefficients (zero gradient).
      std::fill(coef.begin(), coef.end(), std::array<double, 2>{0.0, 0.0});
      continue;
    }
    const double inv = 1.0 / det;
    for (size_t k = 0; k < nbrs.size(); ++k) {
      const int j = nbrs[k];
      const double dx = lm.cellCx[j] - lm.cellCx[i];
      const double dy = lm.cellCy[j] - lm.cellCy[i];
      const double w = 1.0 / (dx * dx + dy * dy);
      // grad = A^{-1} sum_j w_j d_j dphi_j,  A = [[a11,a12],[a12,a22]]
      coef[k][0] = inv * (a22 * dx - a12 * dy) * w;
      coef[k][1] = inv * (-a12 * dx + a11 * dy) * w;
    }
  }
  return ls;
}

void computeScalarGradient(const LsqStencil& ls, const std::vector<double>& phi,
                           std::vector<double>& grad) {
  const size_t n = ls.neighbors.size();
  grad.assign(2 * n, 0.0);
  for (size_t i = 0; i < n; ++i) {
    double gx = 0.0, gy = 0.0;
    const double pi = phi[i];
    for (size_t k = 0; k < ls.neighbors[i].size(); ++k) {
      const double dp = phi[ls.neighbors[i][k]] - pi;
      gx += ls.coef[i][k][0] * dp;
      gy += ls.coef[i][k][1] * dp;
    }
    grad[2 * i] = gx;
    grad[2 * i + 1] = gy;
  }
}

void computePrimitiveGradients(const LsqStencil& ls, const std::vector<double>& W,
                               std::vector<double>& grad) {
  const size_t n = ls.neighbors.size();
  grad.assign(8 * n, 0.0);
  for (size_t i = 0; i < n; ++i) {
    double g[4][2] = {{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}};
    for (size_t k = 0; k < ls.neighbors[i].size(); ++k) {
      const size_t j = ls.neighbors[i][k];
      const double cx = ls.coef[i][k][0], cy = ls.coef[i][k][1];
      for (int q = 0; q < 4; ++q) {
        const double dp = W[4 * j + q] - W[4 * i + q];
        g[q][0] += cx * dp;
        g[q][1] += cy * dp;
      }
    }
    for (int q = 0; q < 4; ++q) {
      grad[8 * i + 2 * q] = g[q][0];
      grad[8 * i + 2 * q + 1] = g[q][1];
    }
  }
}

void computeLimiter(const LocalMesh& lm, const LsqStencil& ls, LimiterType type,
                    const std::vector<double>& W, const std::vector<double>& grad,
                    int gradStride, std::vector<double>& limiter) {
  const size_t n = ls.neighbors.size();
  limiter.assign(4 * n, 1.0);
  if (type == LimiterType::None) return;
  // Venkatakrishnan smoothing parameter eps^2 = (K * h)^3 with K = 3.
  const double K = 3.0;
  for (size_t i = 0; i < n; ++i) {
    // Neighbor min/max per variable (including the cell itself).
    double wmin[4], wmax[4];
    for (int q = 0; q < 4; ++q) {
      wmin[q] = W[4 * i + q];
      wmax[q] = W[4 * i + q];
    }
    for (int j : ls.neighbors[i]) {
      for (int q = 0; q < 4; ++q) {
        wmin[q] = std::min(wmin[q], W[4 * j + q]);
        wmax[q] = std::max(wmax[q], W[4 * j + q]);
      }
    }
    const double h = std::sqrt(lm.cellVol[i]);
    const double eps2 = K * K * K * h * h * h;  // (K h)^3
    for (int q = 0; q < 4; ++q) {
      const double wi = W[4 * i + q];
      const double gx = grad[gradStride * i + 2 * q];
      const double gy = grad[gradStride * i + 2 * q + 1];
      double phi = 1.0;
      for (int f : lm.cellFaces[i]) {
        const double dx = lm.faceCx[f] - lm.cellCx[i];
        const double dy = lm.faceCy[f] - lm.cellCy[i];
        const double d2 = gx * dx + gy * dy;  // unreconstructed jump
        if (d2 > 0.0) {
          const double dp = wmax[q] - wi;
          if (type == LimiterType::BarthJespersen) {
            phi = std::min(phi, (dp > 0.0) ? std::min(1.0, dp / d2) : 0.0);
          } else {
            const double num = dp * dp + 2.0 * d2 * dp + eps2;
            const double den = dp * dp + 2.0 * d2 * d2 + d2 * dp + eps2;
            phi = std::min(phi, (den > 0.0) ? std::min(1.0, num / den) : 1.0);
          }
        } else if (d2 < 0.0) {
          const double dm = wmin[q] - wi;
          if (type == LimiterType::BarthJespersen) {
            phi = std::min(phi, (dm < 0.0) ? std::min(1.0, dm / d2) : 0.0);
          } else {
            const double num = dm * dm + 2.0 * d2 * dm + eps2;
            const double den = dm * dm + 2.0 * d2 * d2 + d2 * dm + eps2;
            phi = std::min(phi, (den > 0.0) ? std::min(1.0, num / den) : 1.0);
          }
        }
      }
      limiter[4 * i + q] = std::max(0.0, std::min(1.0, phi));
    }
  }
}

}  // namespace cfd2d
