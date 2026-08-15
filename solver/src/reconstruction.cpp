// Green-Gauss cell gradients (rho,u,v,p,T) and Barth-Jespersen limiter.
#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
 
 namespace cfd {
void Solver::computeConsLaplacians() {
  // Undivided Laplacian of the conserved variables (rho, rhou, rhov, rhoE),
  // over internal and halo faces (rc>=0). Boundary faces skipped (ghost not
  // stored; the AV targets the interior wake). Length-weighted for k4.
  const int no = lm.n_owned;
  const int nloc = no + lm.n_ghost;
  lapU.assign(no * NEQ, 0.0);
  for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
    const LocalFace& f = lm.faces[fi];
    int lc = f.lc, rc = f.rc;
    if (rc < 0) continue;
    if (lc < 0 || lc >= nloc || rc >= nloc) continue;
    double len = std::max(f.len, 1e-30);
    const double* Ulc = &U[lc * NEQ];
    const double* Urc = &U[rc * NEQ];
    if (lc < no) {
      double* lap = &lapU[lc * NEQ];
      for (int k = 0; k < NEQ; ++k) lap[k] += (Urc[k] - Ulc[k]) * len;
    }
    if (rc < no) {
      double* lap = &lapU[rc * NEQ];
      for (int k = 0; k < NEQ; ++k) lap[k] += (Ulc[k] - Urc[k]) * len;
    }
  }
}
 
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

// Venkatakrishnan smooth limiter function: psi = (d2^2 + 2 d2 |d| + eps2) /
// (d2^2 + 2 d2 |d| + 2 d^2 + eps2), with d2 the (>=0) allowable excursion in
// the direction of the signed increment d. C1-smooth (no Barth kinks/cycling);
// psi->1 in smooth regions (d2>>d), psi->0 at extrema (d2~0, d!=0).
static inline double venkPsi(double d, double d2, double eps2) {
  if (d2 < 0.0) d2 = 0.0;
  double ad = std::fabs(d);
  double num = d2*d2 + 2.0*d2*ad + eps2;
  double den = d2*d2 + 2.0*d2*ad + 2.0*d*d + eps2;
  return (den > 1e-30) ? num/den : 1.0;
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
  // Venkatakrishnan (smooth, less-dissipative) limiter as an alternative to
  // Barth-Jespersen for the Re200 transient: Barth's min/kink non-smoothness
  // excites high-frequency modes and causes limiter cycling with no-freeze,
  // which (with the cap) over-damps the von Karman mode. Venkat is C1-smooth
  // (no cycling) and fuller 2nd-order, so it may reach the shedding Hopf
  // without the odd-en blowup. Inactive unless CFD2D_LIMITER=venkat.
  bool venkat = false;
  double venk_eps = 0.05;
  if (const char* lv = std::getenv("CFD2D_LIMITER")) venkat = (std::string(lv) == "venkat");
  if (const char* ev = std::getenv("CFD2D_VENK_EPS")) venk_eps = std::atof(ev);
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
    // Venkat regularization eps^2 per variable (per cell), relative to the
    // variable magnitude so uniform regions (increment d~0) keep psi->1
    // (full 2nd order) instead of collapsing to first order.
    double eR2 = venk_eps*venk_eps*std::max(Wrho[c]*Wrho[c],1.0);
    double eU2 = venk_eps*venk_eps*std::max(Wu[c]*Wu[c],1.0);
    double eV2 = venk_eps*venk_eps*std::max(Wv[c]*Wv[c],1.0);
    double eP2 = venk_eps*venk_eps*std::max(Wp[c]*Wp[c],1.0);
    for (int fi : lm.cell_faces[c]) {
      const LocalFace& f = lm.faces[fi];
      Vec2 rf = f.center;
      double dx = rf.x - rc.x, dy = rf.y - rc.y;
      // increments
      double dR = gRhoX[c]*dx + gRhoY[c]*dy;
      double dU = gUX[c]*dx + gUY[c]*dy;
      double dV = gVX[c]*dx + gVY[c]*dy;
      double dP = gPX[c]*dx + gPY[c]*dy;
      if (venkat) {
        // allowable excursion (>=0) in the direction of the signed increment
        double d2R = (dR >= 0.0) ? (mxR - Wrho[c]) : (Wrho[c] - mnR);
        double d2U = (dU >= 0.0) ? (mxU - Wu[c])   : (Wu[c] - mnU);
        double d2V = (dV >= 0.0) ? (mxV - Wv[c])   : (Wv[c] - mnV);
        double d2P = (dP >= 0.0) ? (mxP - Wp[c])   : (Wp[c] - mnP);
        lR = std::min(lR, venkPsi(dR, d2R, eR2));
        lU = std::min(lU, venkPsi(dU, d2U, eU2));
        lV = std::min(lV, venkPsi(dV, d2V, eV2));
        lP = std::min(lP, venkPsi(dP, d2P, eP2));
      } else {
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
 else if (venkat) cap = 1.0;   // Venkat is already a smooth bounded limiter in [0,1]; do not clip it (would re-add dissipation).
 else if (shed_av) {
  // Ramp cap 0.5 -> 1.0 over [ramp_start, ramp_start+ramp_len] (default
   // 200..500, i.e. t=2..5). Before the ramp the run is the stable cap-0.5
   // config; the full Barth + 4th-order AV backstop (active over the same
   // window) lower the low-frequency dissipation so the von Karman shear mode
   // can grow from the developed wake. CFD2D_LIMCAP overrides (forces a
   // fixed cap, skipping the ramp) for experiments.
   int rstart = 200, rlen = 300;
   if (const char* es = std::getenv("CFD2D_AV_RAMP_START")) rstart = std::atoi(es);
   double frac = (cur_step <= rstart) ? 0.0
               : (cur_step >= rstart + rlen) ? 1.0
               : double(cur_step - rstart) / double(rlen);
   cap = 0.5 + 0.5 * frac;
 }
// CFD2D_CAP_RAMP: time-dependent cap schedule for sustainable shedding.
  // The cap=0.8 AUSM+-up run develops real shedding (cl~0.13) but blows up at
  // t~8.5 when the large-amplitude oscillation triggers a pressure/overshoot
  // instability. This ramp lets the shedding develop at the high cap (cl grows
  // to ~0.05-0.08 by t~6) then ramps the cap DOWN to add dissipation so the
  // shedding saturates below the blowup threshold (~0.13) instead of diverging.
  // Env: CFD2D_CAP_RAMP_T1 (start step, default off), CFD2D_CAP_RAMP_T2 (end
  // step), CFD2D_CAP_RAMP_C2 (end cap). cap stays at CFD2D_LIMCAP before T1,
  // ramps linearly to C2 over [T1,T2], stays at C2 after T2.
  if (const char* e1 = std::getenv("CFD2D_CAP_RAMP_T1")) {
    int t1 = std::atoi(e1);
    int t2 = t1 + 300;
    if (const char* e2 = std::getenv("CFD2D_CAP_RAMP_T2")) t2 = std::atoi(e2);
    double c2 = cap;
    if (const char* e3 = std::getenv("CFD2D_CAP_RAMP_C2")) c2 = std::atof(e3);
    if (cur_step <= t1) {
      // keep cap as-is (CFD2D_LIMCAP)
    } else if (cur_step >= t2) {
      cap = c2;
    } else {
      double frac = double(cur_step - t1) / double(t2 - t1);
      cap = cap + (c2 - cap) * frac;
    }
  }
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
