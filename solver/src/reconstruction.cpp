#include "reconstruction.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace cfd2d {

void computeGradients(const LocalMesh& lm, const std::vector<PrimState>& P,
                      std::vector<std::array<double,4>>& gradX,
                      std::vector<std::array<double,4>>& gradY,
                      const GasPhysics& gas) {
  int n = lm.numLocal;
  gradX.assign(n, {0,0,0,0});
  gradY.assign(n, {0,0,0,0});

  // Green-Gauss: grad(phi) = (1/V) * sum_faces (phi_face * n * area)
  // phi_face = average of left/right cell values
  // We use primitive [rho, u, v, T]
  for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
    const Face& f = lm.faces[fi];
    double nx = f.nx, ny = f.ny, len = f.len;
    int l = f.l, r = f.r;

    std::array<double,4> phiFace;
    if (l >= 0 && r >= 0) {
      // interior face
      const PrimState& PL = P[l];
      const PrimState& PR = P[r];
      for (int k = 0; k < 4; ++k)
        phiFace[k] = 0.5 * (PL[k] + PR[k]);
    } else if (l >= 0) {
      // boundary face, left cell
      const PrimState& PL = P[l];
      // Use boundary state approximation = cell value (will be corrected in BC)
      for (int k = 0; k < 4; ++k) phiFace[k] = PL[k];
    } else if (r >= 0) {
      const PrimState& PR = P[r];
      for (int k = 0; k < 4; ++k) phiFace[k] = PR[k];
    } else continue;

    if (l >= 0 && l < lm.numOwned) {
      double vol = lm.area[l];
      for (int k = 0; k < 4; ++k) {
        gradX[l][k] += phiFace[k] * nx * len / vol;
        gradY[l][k] += phiFace[k] * ny * len / vol;
      }
    }
    if (r >= 0 && r < lm.numOwned) {
      double vol = lm.area[r];
      for (int k = 0; k < 4; ++k) {
        gradX[r][k] -= phiFace[k] * nx * len / vol;
        gradY[r][k] -= phiFace[k] * ny * len / vol;
      }
    }
  }

  // Convert 4th component from T gradient (we stored T in P[3]? No, P[3]=p)
  // Actually we stored primitive [rho,u,v,p]. We need T gradient for viscous.
  // Convert p gradient to T gradient: T = p/(rho*R), but for viscous flux
  // we'll handle conversion in the viscous flux computation.
  // For reconstruction we use [rho,u,v,p].
}

void reconstructFaces(const LocalMesh& lm,
                       const std::vector<PrimState>& P,
                       const std::vector<std::array<double,4>>& gradX,
                       const std::vector<std::array<double,4>>& gradY,
                       std::vector<FaceRecon>& recon,
                       const GasPhysics& gas) {
  int nf = (int)lm.faces.size();
  recon.resize(nf);

  // First compute Barth-Jespersen limiter per cell.
  // phi_limited = min(1, (phi_max - phi_cell)/(dphi), (phi_min - phi_cell)/(-dphi))
  // where dphi = grad . d (d = face midpoint - cell center)
  // We limit on density and pressure (positivity).
  std::vector<double> limRho(lm.numLocal, 1.0), limP(lm.numLocal, 1.0);

  // Find min/max of neighbors (including self) for rho and p
  std::vector<double> rhoMin(lm.numLocal, std::numeric_limits<double>::max());
  std::vector<double> rhoMax(lm.numLocal, std::numeric_limits<double>::lowest());
  std::vector<double> pMin(lm.numLocal, std::numeric_limits<double>::max());
  std::vector<double> pMax(lm.numLocal, std::numeric_limits<double>::lowest());

  for (int i = 0; i < lm.numOwned; ++i) {
    rhoMin[i] = std::min(rhoMin[i], P[i][0]);
    rhoMax[i] = std::max(rhoMax[i], P[i][0]);
    pMin[i] = std::min(pMin[i], P[i][3]);
    pMax[i] = std::max(pMax[i], P[i][3]);
  }

  for (int fi = 0; fi < nf; ++fi) {
    const Face& f = lm.faces[fi];
    int l = f.l, r = f.r;
    if (l >= 0 && r >= 0) {
      if (l < lm.numOwned) {
        rhoMin[l] = std::min(rhoMin[l], P[r][0]);
        rhoMax[l] = std::max(rhoMax[l], P[r][0]);
        pMin[l] = std::min(pMin[l], P[r][3]);
        pMax[l] = std::max(pMax[l], P[r][3]);
      }
      if (r < lm.numOwned) {
        rhoMin[r] = std::min(rhoMin[r], P[l][0]);
        rhoMax[r] = std::max(rhoMax[r], P[l][0]);
        pMin[r] = std::min(pMin[r], P[l][3]);
        pMax[r] = std::max(pMax[r], P[l][3]);
      }
    }
  }

  // Compute limiter using face deltas (only for owned cells)
  for (int fi = 0; fi < nf; ++fi) {
    const Face& f = lm.faces[fi];
    int l = f.l, r = f.r;
    double dx = f.mx, dy = f.my;

    if (l >= 0 && l < lm.numOwned) {
      double ddx = dx - lm.cx[l], ddy = dy - lm.cy[l];
      double dphiRho = gradX[l][0] * ddx + gradY[l][0] * ddy;
      double dphiP = gradX[l][3] * ddx + gradY[l][3] * ddy;
      // Barth limiter
      if (dphiRho > 0) {
        double phi = (rhoMax[l] - P[l][0]) / std::max(dphiRho, 1e-30);
        limRho[l] = std::min(limRho[l], phi);
      } else if (dphiRho < 0) {
        double phi = (rhoMin[l] - P[l][0]) / std::max(-dphiRho, 1e-30);
        limRho[l] = std::min(limRho[l], phi);
      }
      if (dphiP > 0) {
        double phi = (pMax[l] - P[l][3]) / std::max(dphiP, 1e-30);
        limP[l] = std::min(limP[l], phi);
      } else if (dphiP < 0) {
        double phi = (pMin[l] - P[l][3]) / std::max(-dphiP, 1e-30);
        limP[l] = std::min(limP[l], phi);
      }
    }
    if (r >= 0 && r < lm.numOwned) {
      double ddx = dx - lm.cx[r], ddy = dy - lm.cy[r];
      double dphiRho = gradX[r][0] * ddx + gradY[r][0] * ddy;
      double dphiP = gradX[r][3] * ddx + gradY[r][3] * ddy;
      if (dphiRho > 0) {
        double phi = (rhoMax[r] - P[r][0]) / std::max(dphiRho, 1e-30);
        limRho[r] = std::min(limRho[r], phi);
      } else if (dphiRho < 0) {
        double phi = (rhoMin[r] - P[r][0]) / std::max(-dphiRho, 1e-30);
        limRho[r] = std::min(limRho[r], phi);
      }
      if (dphiP > 0) {
        double phi = (pMax[r] - P[r][3]) / std::max(dphiP, 1e-30);
        limP[r] = std::min(limP[r], phi);
      } else if (dphiP < 0) {
        double phi = (pMin[r] - P[r][3]) / std::max(-dphiP, 1e-30);
        limP[r] = std::min(limP[r], phi);
      }
    }
  }

  // Clamp limiters to [0,1] (only owned cells)
  for (int i = 0; i < lm.numOwned; ++i) {
    limRho[i] = std::max(0.0, std::min(1.0, limRho[i]));
    limP[i] = std::max(0.0, std::min(1.0, limP[i]));
  }

  // Now reconstruct face states
  for (int fi = 0; fi < nf; ++fi) {
    const Face& f = lm.faces[fi];
    int l = f.l, r = f.r;
    double dx = f.mx, dy = f.my;

    if (l >= 0) {
      double ddx = dx - lm.cx[l], ddy = dy - lm.cy[l];
      double lim = std::min(limRho[l], limP[l]);
      PrimState PL;
      for (int k = 0; k < 4; ++k) {
        PL[k] = P[l][k] + lim * (gradX[l][k] * ddx + gradY[l][k] * ddy);
      }
      // Positivity check
      if (PL[0] < 0.1 * P[l][0] || PL[3] < 0.1 * P[l][3]) {
        PL = P[l]; // fallback to first order
      }
      recon[fi].left = PL;
    } else {
      recon[fi].left = P[r]; // shouldn't happen
    }

    if (r >= 0) {
      double ddx = dx - lm.cx[r], ddy = dy - lm.cy[r];
      double lim = std::min(limRho[r], limP[r]);
      PrimState PR;
      for (int k = 0; k < 4; ++k) {
        PR[k] = P[r][k] + lim * (gradX[r][k] * ddx + gradY[r][k] * ddy);
      }
      if (PR[0] < 0.1 * P[r][0] || PR[3] < 0.1 * P[r][3]) {
        PR = P[r];
      }
      recon[fi].right = PR;
    } else {
      recon[fi].right = P[l];
    }
  }
}

} // namespace cfd2d
