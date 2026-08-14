#include "implicit.h"
#include <cmath>
#include <algorithm>

namespace cfd2d {

void LUSGSSolver::setup(const LocalMesh& lm, double dt, const GasPhysics& gas,
                        const std::vector<PrimState>& P) {
  int n = lm.numLocal;
  diag.assign(n, 0.0);
  lower.assign(n, {});
  upper.assign(n, {});

  // Diagonal: 1/dt + sum of spectral radii / vol
  // Off-diagonal: -spectral_radius_face * len / vol (for each neighbor)
  for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
    const Face& f = lm.faces[fi];
    int l = f.l, r = f.r;
    if (l < 0 && r < 0) continue;

    // Spectral radius at face
    double a, un;
    if (l >= 0 && r >= 0) {
      double aL = gas.soundSpeed(P[l]);
      double aR = gas.soundSpeed(P[r]);
      a = std::max(aL, aR);
      double unL = P[l][1] * f.nx + P[l][2] * f.ny;
      double unR = P[r][1] * f.nx + P[r][2] * f.ny;
      un = std::max(std::abs(unL), std::abs(unR));
    } else {
      int c = (l >= 0) ? l : r;
      a = gas.soundSpeed(P[c]);
      un = std::abs(P[c][1] * f.nx + P[c][2] * f.ny);
    }
    double spec = (std::abs(un) + a) * f.len;
    double specScaled = spec * gas.rusanovScale;
    // Add viscous spectral radius for stability
    double specVisc = 0.0;
    if (gas.viscous && gas.mu > 0) {
      // Viscous spectral radius ~ mu * len^2 / (rho * vol) per face
      // Approximate: 2*mu/(rho) * len^2 / vol
      int c = (l >= 0) ? l : r;
      if (c >= 0) {
        double rho = std::max(P[c][0], 1e-12);
        specVisc = 4.0 * gas.mu / rho * f.len * f.len / lm.area[c];
      }
    }
    double specTotal = specScaled + specVisc;

    if (l >= 0) {
      double vol = lm.area[l];
      diag[l] += specTotal / vol;
      if (r >= 0) {
        // off-diagonal contribution
        lower[l].push_back({r, -specTotal / vol});
        upper[l].push_back({r, -specTotal / vol});
      }
    }
    if (r >= 0) {
      double vol = lm.area[r];
      diag[r] += specTotal / vol;
      if (l >= 0) {
        lower[r].push_back({l, -specTotal / vol});
        upper[r].push_back({l, -specTotal / vol});
      }
    }
  }

  // Add 1/dt to diagonal (for owned cells; ghost cells get large diag)
  // Use a safety factor to ensure diagonal dominance
  for (int i = 0; i < n; ++i) {
    if (i < lm.numOwned) {
      diag[i] += 1.0 / std::max(dt, 1e-30);
      // Ensure diagonal dominance: diag >= sum of |off-diagonal|
      // The off-diagonal sum is already included in diag from the spectral radius,
      // so diag is already diagonally dominant by 1/dt.
    } else {
      diag[i] = 1e20; // ghost cells don't update
    }
  }
}

void LUSGSSolver::solve(const LocalMesh& lm, const std::vector<ConsState>& R,
                        std::vector<ConsState>& dU, int nSweeps, double relax) {
  int n = lm.numLocal;
  dU.assign(n, ConsState{0,0,0,0});

  for (int sweep = 0; sweep < nSweeps; ++sweep) {
    // Forward sweep: (D + L) w = -R
    for (int i = 0; i < n; ++i) {
      ConsState rhs;
      for (int k = 0; k < NEQ; ++k) rhs[k] = -R[i][k] * relax;
      for (auto& [j, coeff] : lower[i]) {
        if (j < i) {
          for (int k = 0; k < NEQ; ++k)
            rhs[k] -= coeff * dU[j][k];
        }
      }
      for (int k = 0; k < NEQ; ++k)
        dU[i][k] = rhs[k] / diag[i];
    }
    // Backward sweep: (D + U) dU = D w
    for (int i = n - 1; i >= 0; --i) {
      ConsState rhs;
      for (int k = 0; k < NEQ; ++k)
        rhs[k] = diag[i] * dU[i][k];
      for (auto& [j, coeff] : upper[i]) {
        if (j > i) {
          for (int k = 0; k < NEQ; ++k)
            rhs[k] -= coeff * dU[j][k];
        }
      }
      for (int k = 0; k < NEQ; ++k)
        dU[i][k] = rhs[k] / diag[i];
    }
  }

  // Zero out ghost cell updates
  for (int i = lm.numOwned; i < n; ++i) {
    dU[i] = {0, 0, 0, 0};
  }
}

} // namespace cfd2d
