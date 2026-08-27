#include "grad.hpp"

namespace fv {

void computeGrads(const LocalMesh& m, const vector<Prim>& W, const Gas& gas,
                  const GhostFn& ghost, vector<Grads>& grads) {
  if ((int)grads.size() != m.nAll) grads.assign(m.nAll, Grads{});
  for (int c = 0; c < m.nOwn; ++c) {
    const Prim& wc = W[c];
    double Tc = wc.T(gas);
    double br[5] = {0, 0, 0, 0, 0}, by[5] = {0, 0, 0, 0, 0};
    for (int k = m.lsqOff[c]; k < m.lsqOff[c + 1]; ++k) {
      const LsqEntry& e = m.lsqEntries[k];
      Prim ws;
      if (e.idx >= 0) {
        ws = W[e.idx];
      } else {
        const LocalFace& f = m.faces[~e.idx];
        ws = ghost(f.bc, wc, f.nx, f.ny);
      }
      double d[5] = {ws.rho - wc.rho, ws.u - wc.u, ws.v - wc.v, ws.p - wc.p, ws.T(gas) - Tc};
      for (int v = 0; v < 5; ++v) {
        br[v] += e.w * d[v] * e.dx;
        by[v] += e.w * d[v] * e.dy;
      }
    }
    double a00 = m.lsqInv00[c], a01 = m.lsqInv01[c], a11 = m.lsqInv11[c];
    Grads& g = grads[c];
    g.rho = {a00 * br[0] + a01 * by[0], a01 * br[0] + a11 * by[0]};
    g.u   = {a00 * br[1] + a01 * by[1], a01 * br[1] + a11 * by[1]};
    g.v   = {a00 * br[2] + a01 * by[2], a01 * br[2] + a11 * by[2]};
    g.p   = {a00 * br[3] + a01 * by[3], a01 * br[3] + a11 * by[3]};
    g.T   = {a00 * br[4] + a01 * by[4], a01 * br[4] + a11 * by[4]};
  }
}

void computeLimiters(const LocalMesh& m, const vector<Prim>& W, const vector<Grads>& grads,
                     const GhostFn& ghost, double rhoFloor, double pFloor, double venkatK,
                     vector<Limiters>& lim) {
  if ((int)lim.size() != m.nAll) lim.assign(m.nAll, Limiters{});
  for (int c = 0; c < m.nOwn; ++c) {
    const Prim& wc = W[c];
    double vc[4] = {wc.rho, wc.u, wc.v, wc.p};
    double vmin[4] = {vc[0], vc[1], vc[2], vc[3]};
    double vmax[4] = {vc[0], vc[1], vc[2], vc[3]};
    for (int k = m.lsqOff[c]; k < m.lsqOff[c + 1]; ++k) {
      const LsqEntry& e = m.lsqEntries[k];
      Prim ws;
      if (e.idx >= 0) {
        ws = W[e.idx];
      } else {
        const LocalFace& f = m.faces[~e.idx];
        ws = ghost(f.bc, wc, f.nx, f.ny);
      }
      double vs[4] = {ws.rho, ws.u, ws.v, ws.p};
      for (int v = 0; v < 4; ++v) {
        vmin[v] = std::min(vmin[v], vs[v]);
        vmax[v] = std::max(vmax[v], vs[v]);
      }
    }
    const Grads& g = grads[c];
    const Vec2 gr[4] = {g.rho, g.u, g.v, g.p};
    double alpha[4] = {1, 1, 1, 1};
    double h = std::sqrt(m.vol[c]);
    double eps2 = (venkatK > 0.0) ? std::pow(venkatK * h, 3.0) : 0.0;
    for (int k = m.cellFaceOff[c]; k < m.cellFaceOff[c + 1]; ++k) {
      const LocalFace& f = m.faces[m.cellFaceIdx[k]];
      double dx = f.fx - m.xc[c], dy = f.fy - m.yc[c];
      for (int v = 0; v < 4; ++v) {
        double delta = gr[v].x * dx + gr[v].y * dy;
        if (venkatK > 0.0) {
          // Venkatakrishnan smooth limiter
          double d1, d2 = delta;
          if (d2 > 0.0) d1 = vmax[v] - vc[v];
          else if (d2 < 0.0) d1 = vmin[v] - vc[v];
          else continue;
          double sig =
              (d1 * d1 + 2.0 * d1 * d2 + eps2) / (d1 * d1 + d1 * d2 + 2.0 * d2 * d2 + eps2);
          alpha[v] = std::min(alpha[v], sig);
        } else {
          if (delta > 0.0) {
            double room = vmax[v] - vc[v];
            if (delta > room && delta > 0.0) alpha[v] = std::min(alpha[v], room / delta);
          } else if (delta < 0.0) {
            double room = vmin[v] - vc[v];
            if (delta < room && delta < 0.0) alpha[v] = std::min(alpha[v], room / delta);
          }
        }
      }
      // positivity floors on density and pressure
      for (int v : {0, 3}) {
        double floor_ = (v == 0) ? rhoFloor : pFloor;
        double delta = gr[v].x * dx + gr[v].y * dy;
        if (delta < 0.0 && vc[v] + delta < floor_) {
          alpha[v] = std::min(alpha[v], (floor_ - vc[v]) / delta);
        }
      }
    }
    Limiters& L = lim[c];
    L.rho = std::max(0.0, alpha[0]);
    L.u = std::max(0.0, alpha[1]);
    L.v = std::max(0.0, alpha[2]);
    L.p = std::max(0.0, alpha[3]);
  }
}

}  // namespace fv
