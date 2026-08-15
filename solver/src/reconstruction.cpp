// Green-Gauss cell gradients (rho,u,v,p,T) and Barth-Jespersen limiter.
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cfd {

// helper: ghost primitive at a boundary face (uses the stored outward unit normal)
static inline Prim faceGhost(const Solver& s, const LocalFace& f) {
  double len = std::max(f.len, 1e-30);
  double nx = f.Sx / len, ny = f.Sy / len;
  // the interior cell is the one that is owned (lc or rc); find it
  Prim Wc;
  int c = (f.lc >= 0 && f.lc < s.lm.n_owned) ? f.lc : f.rc;
  if (c < 0) return Wc;
  Wc.rho = s.Wrho[c]; Wc.u = s.Wu[c]; Wc.v = s.Wv[c]; Wc.p = s.Wp[c];
  return bcGhostState(s.phys, f.bctype, Wc, nx, ny);
}

void Solver::computeGradients() {
  const int no = lm.n_owned;
  auto zero2 = [&](std::vector<double>& a) { a.assign(no * 2, 0.0); };
  zero2(gRhoX); zero2(gRhoY); zero2(gUX); zero2(gUY); zero2(gVX); zero2(gVY);
  zero2(gPX); zero2(gPY); zero2(gTX); zero2(gTY);
  int nloc = no + lm.n_ghost;
  for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
    const LocalFace& f = lm.faces[fi];
    double rf_rho, rf_u, rf_v, rf_p, rf_T;
    if (f.rc >= 0) {
      // internal: face value = average of L and R primitives
      rf_rho = 0.5 * (Wrho[f.lc] + Wrho[f.rc]);
      rf_u   = 0.5 * (Wu[f.lc] + Wu[f.rc]);
      rf_v   = 0.5 * (Wv[f.lc] + Wv[f.rc]);
      rf_p   = 0.5 * (Wp[f.lc] + Wp[f.rc]);
      rf_T   = 0.5 * (WT[f.lc] + WT[f.rc]);
    } else {
      // boundary: face value = 0.5*(cell + ghost)
      Prim Wc, g;
      int c = f.lc;
      if (c < 0 || c >= nloc) c = f.rc;
      Wc.rho = Wrho[c]; Wc.u = Wu[c]; Wc.v = Wv[c]; Wc.p = Wp[c];
      double len = std::max(f.len, 1e-30);
      g = bcGhostState(phys, f.bctype, Wc, f.Sx / len, f.Sy / len);
      rf_rho = 0.5 * (Wc.rho + g.rho);
      rf_u   = 0.5 * (Wc.u + g.u);
      rf_v   = 0.5 * (Wc.v + g.v);
      rf_p   = 0.5 * (Wc.p + g.p);
      Prim gT = g; gT.rho = g.rho; // T handled below
      double Tc = WT[c];
      double Tg = phys.gas.temperature(g);
      rf_T = 0.5 * (Tc + Tg);
    }
    double Sx = f.Sx, Sy = f.Sy;
    // L cell (lc): outward = +S
    if (f.lc >= 0 && f.lc < no) {
      double inv = 1.0 / lm.area[f.lc];
      gRhoX[f.lc] += rf_rho * Sx * inv; gRhoY[f.lc] += rf_rho * Sy * inv;
      gUX[f.lc] += rf_u * Sx * inv;     gUY[f.lc] += rf_u * Sy * inv;
      gVX[f.lc] += rf_v * Sx * inv;     gVY[f.lc] += rf_v * Sy * inv;
      gPX[f.lc] += rf_p * Sx * inv;     gPY[f.lc] += rf_p * Sy * inv;
      gTX[f.lc] += rf_T * Sx * inv;     gTY[f.lc] += rf_T * Sy * inv;
    }
    // R cell (rc): outward = -S
    if (f.rc >= 0 && f.rc < no) {
      double inv = 1.0 / lm.area[f.rc];
      gRhoX[f.rc] -= rf_rho * Sx * inv; gRhoY[f.rc] -= rf_rho * Sy * inv;
      gUX[f.rc] -= rf_u * Sx * inv;     gUY[f.rc] -= rf_u * Sy * inv;
      gVX[f.rc] -= rf_v * Sx * inv;     gVY[f.rc] -= rf_v * Sy * inv;
      gPX[f.rc] -= rf_p * Sx * inv;     gPY[f.rc] -= rf_p * Sy * inv;
      gTX[f.rc] -= rf_T * Sx * inv;     gTY[f.rc] -= rf_T * Sy * inv;
    }
  }
  if (std::getenv("CFD2D_DEBUG")) {
    double mg = 0; double sumSx = 0, sumSy = 0;
    for (int c = 0; c < no; ++c) {
      double g = std::sqrt(gUX[c]*gUX[c]+gUY[c]*gUY[c]+gVX[c]*gVX[c]+gVY[c]*gVY[c]);
      if (g > mg) mg = g;
    }
    for (int fi : lm.cell_faces[0]) {
      const LocalFace& ff = lm.faces[fi];
      int sgn = (ff.lc == 0) ? +1 : -1;
      sumSx += sgn * ff.Sx; sumSy += sgn * ff.Sy;
    }
    std::fprintf(stderr, "[r%d] max|gradUV|=%.6e cell0 sumS=(%.6e,%.6e)\n",
                 rank, mg, sumSx, sumSy);
  }
}

void Solver::computeLimiters() {
  const int no = lm.n_owned;
  limRho.assign(no, 1.0); limU.assign(no, 1.0);
  limV.assign(no, 1.0); limP.assign(no, 1.0);
  if (std::getenv("CFD2D_FIRSTORDER")) {
    std::fill(limRho.begin(),limRho.end(),0.0); std::fill(limU.begin(),limU.end(),0.0);
    std::fill(limV.begin(),limV.end(),0.0); std::fill(limP.begin(),limP.end(),0.0);
    return;  // limiter=0 -> first order (cell-centered, no reconstruction)
  }
  // Thin/sliver cells (tiny area) use first order: their 1/area-scaled gradients
  // are unreliable and their reconstructed face states can trigger non-physical
  // fluxes. This keeps second order in the smooth bulk of the domain.
  for (int c = 0; c < no; ++c) {
    if (lm.area[c] < 1e-7) { limRho[c]=limU[c]=limV[c]=limP[c]=0.0; }
  }
  int nloc = no + lm.n_ghost;
  for (int c = 0; c < no; ++c) {
    Vec2 rc = lm.center[c];
    // gather neighbor primitive envelope (cell-center values incl. ghosts)
    double mnR = Wrho[c], mxR = Wrho[c], mnU = Wu[c], mxU = Wu[c];
    double mnV = Wv[c], mxV = Wv[c], mnP = Wp[c], mxP = Wp[c];
    for (int fi : lm.cell_faces[c]) {
      const LocalFace& f = lm.faces[fi];
      int nb = (f.lc == c) ? f.rc : f.lc;
      Prim nbc;
      if (nb >= 0 && nb < nloc) { nbc.rho = Wrho[nb]; nbc.u = Wu[nb]; nbc.v = Wv[nb]; nbc.p = Wp[nb]; }
      else { nbc = faceGhost(*this, f); }
      mnR = std::min(mnR, nbc.rho); mxR = std::max(mxR, nbc.rho);
      mnU = std::min(mnU, nbc.u);   mxU = std::max(mxU, nbc.u);
      mnV = std::min(mnV, nbc.v);   mxV = std::max(mxV, nbc.v);
      mnP = std::min(mnP, nbc.p);   mxP = std::max(mxP, nbc.p);
    }
    double lR = 1, lU = 1, lV = 1, lP = 1;
    for (int fi : lm.cell_faces[c]) {
      const LocalFace& f = lm.faces[fi];
      Vec2 rf = f.center;
      double dx = rf.x - rc.x, dy = rf.y - rc.y;
      // increments
      double dR = gRhoX[c]*dx + gRhoY[c]*dy;
      double dU = gUX[c]*dx + gUY[c]*dy;
      double dV = gVX[c]*dx + gVY[c]*dy;
      double dP = gPX[c]*dx + gPY[c]*dy;
      double eps = 1e-12;
      if (dR >  eps) lR = std::min(lR, (mxR - Wrho[c]) / dR);
      if (dR < -eps) lR = std::min(lR, (mnR - Wrho[c]) / dR);
      if (dU >  eps) lU = std::min(lU, (mxU - Wu[c]) / dU);
      if (dU < -eps) lU = std::min(lU, (mnU - Wu[c]) / dU);
      if (dV >  eps) lV = std::min(lV, (mxV - Wv[c]) / dV);
      if (dV < -eps) lV = std::min(lV, (mnV - Wv[c]) / dV);
      if (dP >  eps) lP = std::min(lP, (mxP - Wp[c]) / dP);
      if (dP < -eps) lP = std::min(lP, (mnP - Wp[c]) / dP);
    }
  limRho[c] = std::max(0.0, std::min(1.0, lR));
  limU[c] = std::max(0.0, std::min(1.0, lU));
  limV[c] = std::max(0.0, std::min(1.0, lV));
  limP[c] = std::max(0.0, std::min(1.0, lP));
  }
  // Conservative limiter cap (default 0.5, overridable via CFD2D_LIMCAP) for
  // robustness: the full Barth limiter (up to 1.0) can drive a slow
  // anti-diffusive instability with the simplified implicit at high CFL; the
  // cap keeps the piecewise-linear reconstruction active (second-order in
  // smooth regions up to the cap) while remaining stable.
  double cap = 0.5;
  if (const char* e = std::getenv("CFD2D_LIMCAP")) cap = std::atof(e);
  for (int i = 0; i < no; ++i) {
    limRho[i]=std::min(limRho[i],cap); limU[i]=std::min(limU[i],cap);
    limV[i]=std::min(limV[i],cap); limP[i]=std::min(limP[i],cap);
  }
  if (std::getenv("CFD2D_DEBUG")) {
    double mn=1,mx=0,sum=0; int n0=0;
    for (int c=0;c<no;++c){ mn=std::min(mn,limRho[c]); mx=std::max(mx,limRho[c]); sum+=limRho[c]; if(limRho[c]<1e-6)++n0; }
    std::fprintf(stderr,"[r%d] limiter rho min=%.3e max=%.3f mean=%.3f nZero=%d\n",rank,mn,mx,sum/no,n0);
  }
}

}  // namespace cfd
