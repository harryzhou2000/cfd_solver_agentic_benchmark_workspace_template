#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include "partitioner.hpp"
#include "gas.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

// Gradients: [ncells * 5 * 2] for (rho, u, v, p, T) x (dx, dy)
// Index: grad[(cell * 5 + var) * 2 + dir]

class Reconstruction {
public:
    const DistMesh& dm;
    const GasModel& gas;
    std::vector<Real> grad;   // gradients
    std::vector<Real> limiter; // per-cell limiter (0 to 1)
    Real maxLimiter = 1.0; // cap on limiter value (for debugging)

    bool useLimiter;
    bool firstOrder = false;

    Reconstruction(const DistMesh& mesh, const GasModel& g, bool lim = true)
        : dm(mesh), gas(g), useLimiter(lim) {
        grad.resize(dm.nLocalCells() * 5 * 2, 0.0);
        limiter.resize(dm.nLocalCells(), 1.0);
    }

    // Compute least-squares gradients from primitive variables
    void computeGradients(const std::vector<PrimState>& W) {
        int nc = dm.nLocalCells();
        std::fill(grad.begin(), grad.end(), 0.0);

        for (int c = 0; c < nc; c++) {
            Real wxx = 0, wxy = 0, wyy = 0;
            Real rhs[5][2] = {};

            for (int fi : dm.cellFaces[c]) {
                int nbr = (dm.faceLc[fi] == c) ? dm.faceRc[fi] : dm.faceLc[fi];
                if (nbr < 0) continue;

                Real dx = dm.cellCx[nbr] - dm.cellCx[c];
                Real dy = dm.cellCy[nbr] - dm.cellCy[c];
                Real w = 1.0 / (dx*dx + dy*dy + 1e-20); // inverse distance weight

                wxx += dx*dx*w;
                wxy += dx*dy*w;
                wyy += dy*dy*w;

                for (int v = 0; v < 5; v++) {
                    Real dphi = W[nbr][v] - W[c][v];
                    rhs[v][0] += dx*dphi*w;
                    rhs[v][1] += dy*dphi*w;
                }
            }

            // Solve 2x2 system
            Real det = wxx*wyy - wxy*wxy;
            if (std::abs(det) < 1e-20) {
                for (int v = 0; v < 5; v++) {
                    grad[(c*5+v)*2+0] = 0;
                    grad[(c*5+v)*2+1] = 0;
                }
            } else {
                Real invDet = 1.0/det;
                for (int v = 0; v < 5; v++) {
                    Real gx = (wyy*rhs[v][0] - wxy*rhs[v][1]) * invDet;
                    Real gy = (-wxy*rhs[v][0] + wxx*rhs[v][1]) * invDet;
                    grad[(c*5+v)*2+0] = gx;
                    grad[(c*5+v)*2+1] = gy;
                }
            }
        }
    }

    // Barth-Jespersen limiter on primitive variables (rho, u, v, p)
    void computeLimiter(const std::vector<PrimState>& W) {
        if (!useLimiter) {
            std::fill(limiter.begin(), limiter.end(), 1.0);
            return;
        }
        // First-order fallback: all limiters = 0
        if (firstOrder) {
            std::fill(limiter.begin(), limiter.end(), 0.0);
            return;
        }

        int nc = dm.nOwned; // only for owned cells
        for (int c = 0; c < nc; c++) {
            Real phiMin[4] = {W[c][0], W[c][1], W[c][2], W[c][3]};
            Real phiMax[4] = {W[c][0], W[c][1], W[c][2], W[c][3]};

            for (int fi : dm.cellFaces[c]) {
                int nbr = (dm.faceLc[fi] == c) ? dm.faceRc[fi] : dm.faceLc[fi];
                if (nbr < 0) continue;
                for (int v = 0; v < 4; v++) {
                    phiMin[v] = std::min(phiMin[v], W[nbr][v]);
                    phiMax[v] = std::max(phiMax[v], W[nbr][v]);
                }
            }

            Real cellLim = 1.0;
            for (int fi : dm.cellFaces[c]) {
                Real fx = dm.faceFx[fi], fy = dm.faceFy[fi];
                Real dx = fx - dm.cellCx[c];
                Real dy = fy - dm.cellCy[c];

                for (int v = 0; v < 4; v++) {
                    Real gx = grad[(c*5+v)*2+0];
                    Real gy = grad[(c*5+v)*2+1];
                    Real dphi = gx*dx + gy*dy;
                    Real phiFace = W[c][v] + dphi;

                    Real faceLim = 1.0;
                    if (dphi > 1e-20) {
                        faceLim = (phiMax[v] - W[c][v]) / dphi;
                    } else if (dphi < -1e-20) {
                        faceLim = (phiMin[v] - W[c][v]) / dphi;
                    }
                    faceLim = std::max(0.0, std::min(1.0, faceLim));
                    cellLim = std::min(cellLim, faceLim);
                }
            }
        // Cap limiter for stability
        cellLim = std::min(cellLim, maxLimiter);
        limiter[c] = cellLim;
        }
        // Ghost cells: limiter = 0 (first-order at partition boundaries)
        for (int c = dm.nOwned; c < dm.nLocalCells(); c++) {
            limiter[c] = 0.0;
        }
    }

    // Reconstruct left/right primitive states at a face
    // Returns left state (from lc) and right state (from rc or boundary)
    void reconstructFace(int fi, const std::vector<PrimState>& W,
                         PrimState& WL, PrimState& WR) const {
        int lc = dm.faceLc[fi];
        int rc = dm.faceRc[fi];
        Real fx = dm.faceFx[fi], fy = dm.faceFy[fi];

        // Left state
        if (limiter[lc] > 0.0) {
            Real dx = fx - dm.cellCx[lc];
            Real dy = fy - dm.cellCy[lc];
            Real lim = limiter[lc];
            for (int v = 0; v < 5; v++) {
                Real gx = grad[(lc*5+v)*2+0];
                Real gy = grad[(lc*5+v)*2+1];
                WL[v] = W[lc][v] + lim * (gx*dx + gy*dy);
            }
        } else {
            WL = W[lc];
        }

        // Right state
        if (rc >= 0) {
            if (limiter[rc] > 0.0) {
                Real dx = fx - dm.cellCx[rc];
                Real dy = fy - dm.cellCy[rc];
                Real lim = limiter[rc];
                for (int v = 0; v < 5; v++) {
                    Real gx = grad[(rc*5+v)*2+0];
                    Real gy = grad[(rc*5+v)*2+1];
                    WR[v] = W[rc][v] + lim * (gx*dx + gy*dy);
                }
            } else {
                WR = W[rc];
            }
        }
    }

    // Enforce positivity on reconstructed states
    // Enforce positivity on reconstructed states - check conservative state
    void enforcePositivity(PrimState& W) const {
        W[0] = std::max(W[0], 1e-8);
        W[3] = std::max(W[3], 1e-8);
        // Ensure internal energy is positive: p/(gamma-1) > 0
        // If velocity is too large relative to energy, clip velocity
        Real rhoE = W[3] / gas.gamma_m1 + 0.5 * W[0] * (W[1]*W[1] + W[2]*W[2]);
        Real e = rhoE / W[0] - 0.5 * (W[1]*W[1] + W[2]*W[2]);
        if (e < 1e-8) {
            // Clip velocity to maintain positive internal energy
            Real eMin = 1e-8;
            Real eKinMax = rhoE / W[0] - eMin;
            if (eKinMax > 0) {
                Real vmag = std::sqrt(W[1]*W[1] + W[2]*W[2]);
                if (vmag > 1e-12) {
                    Real scale = std::sqrt(2.0 * eKinMax) / vmag;
                    scale = std::min(scale, 1.0);
                    W[1] *= scale;
                    W[2] *= scale;
                }
            } else {
                W[1] = 0;
                W[2] = 0;
            }
        }
        W[4] = W[3] / (W[0] * gas.R);
    }
};

} // namespace cfd
