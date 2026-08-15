// Implicit linear solve: matrix-free LU-SGS / symmetric Gauss-Seidel with a
// simplified (scalar, spectral-radius) Jacobian. The implicit system for the
// owned cells linearizes the Rusanov/Roe numerical flux as
//   (1/dtau_i) dU_i + (1/A_i) Sum_f 0.5*alpha_f*(dU_i - dU_nb) = R_i,
// i.e.  D_i dU_i - Sum_f c_f dU_nb = R_i  with
//   D_i = 1/dtau_i + (1/A_i) Sum_f alpha_f * len_f   (FULL spectral on diagonal)
//   c_f  = (1/A_i) * 0.5 * alpha_f * len_f            (off-diagonal)
// The FULL spectral on the diagonal makes the high-CFL limit dU -> -0.5 dU
// (stable, removes ~half the perturbation per step). The SGS topology (which
// owned cells neighbor which, via which face) is precomputed in setup as a flat
// CSR; only the per-face coefficients alpha are recomputed each call -- this
// avoids the per-call vector-of-vectors allocation churn that dominated runtime.
#include "solver.hpp"
#include <algorithm>
#include <cmath>

namespace cfd {

static inline double faceAlpha(const Solver& s, const LocalFace& f) {
  double len = std::max(f.len, 1e-30);
  double nx = f.Sx/len, ny = f.Sy/len;
  int lc = f.lc, rc = f.rc;
  double aL=0,unL=0,aR=0,unR=0;
  if (lc>=0){Prim w{s.Wrho[lc],s.Wu[lc],s.Wv[lc],s.Wp[lc]};aL=s.phys.gas.soundSpeed(w);unL=w.u*nx+w.v*ny;}
  if (rc>=0){Prim w{s.Wrho[rc],s.Wu[rc],s.Wv[rc],s.Wp[rc]};aR=s.phys.gas.soundSpeed(w);unR=w.u*nx+w.v*ny;}
  else { aR=aL; unR=unL; }
 double inv = std::max(std::fabs(unL)+aL, std::fabs(unR)+aR);
 if (s.phys.laminar && s.phys.mu > 0.0) {
    double Al=(lc>=0)?s.lm.area[lc]:1.0, Ar=(rc>=0)?s.lm.area[rc]:1.0;
    double Amin=std::max(std::min(Al,Ar),1e-12);
    inv += 4.0 * s.phys.mu * len * len / Amin;
  }
  // AUSM+-up low-Mach pressure-velocity coupling linearization: the M_p (Delta-p
  // -> mass) and p_u (Delta-Un -> pressure) terms are explicit-only in the
  // residual. A dual-time implicit can ONLY control modes present in its
  // Jacobian, so without this they cannot stabilize the cap-1.0 pressure
  // checkerboard (the inner solve diverges, ratio->1.0, then the state blows up
  // at t~2-3 -- the same failure as pstab, which was also never linearized).
  // Add the coupling's effective dissipation speed to the spectral radius:
  //   alpha_pu (momentum, from p_u) ~ Ku * P+ * P- * (rhoL+rhoR) * fa * a12
  //   alpha_mp (mass/pressure, from M_p) ~ (Kp/fa) * cut * a12
  // Both are Rusanov-like (proportional to the jump), so adding them to `inv`
  // is the correct scalar-implicit linearization. At low Mach fa~0.3-0.5 so
  // 1/fa amplifies M_p (strong pressure coupling in the Jacobian, as intended);
  // at high Mach P+*P->0 and fa->1 so the terms vanish (no over-dissipation).
  if (s.phys.use_ausmup) {
    const auto& gas = s.phys.gas;
    double rhoL = (lc>=0)? std::max(s.Wrho[lc],1e-30) : 1.0;
    double rhoR = (rc>=0)? std::max(s.Wrho[rc],1e-30) : rhoL;
    double pL = (lc>=0)? s.Wp[lc] : 1.0;
    double pR = (rc>=0)? s.Wp[rc] : pL;
    double a12 = 0.5*(aL + aR);            // interface sound speed (low-Mach approx)
    a12 = std::max(a12, 1e-12);
    double ML = unL/a12, MR = unR/a12;
    // P5+ (ML), P5- (MR) at the basic alpha=3/16 (high-Mach) value; the all-speed
    // correction is O(fa^2) and minor for the linearization magnitude.
    auto P5p = [](double M)->double{
      if (M>=1.0) return 1.0; if (M<=-1.0) return 0.0;
      double m1=M+1.0, m2=M*M-1.0; return 0.25*m1*m1*(2.0-M)+(3.0/16.0)*M*m2*m2; };
    auto P5m = [](double M)->double{
      if (M>=1.0) return 0.0; if (M<=-1.0) return 1.0;
      double m1=M-1.0, m2=M*M-1.0; return 0.25*m1*m1*(2.0+M)-(3.0/16.0)*M*m2*m2; };
    double PpL = P5p(ML), PmR = P5m(MR);
    double Mbar2 = 0.5*(unL*unL + unR*unR)/(a12*a12);
    double mcut = (s.phys.ausmup_mcut>0.0)? s.phys.ausmup_mcut : 0.3;
    double M0sq = std::min(1.0, std::max(Mbar2, mcut*mcut));
    double fa = std::sqrt(M0sq)*(2.0-std::sqrt(M0sq));
    fa = std::max(fa, 1e-6);
    // p_u momentum dissipation speed (scaled by ausmup_lscale for over-
    // linearization: the Jacobian can be more dissipative than the explicit
    // residual, which stabilizes the inner solve at large shedding amplitude
    // where the approximate linearization otherwise under-matches the residual).
    double lscale = std::max(s.phys.ausmup_lscale, 1.0);
    if (s.phys.ausmup_ku > 0.0)
      inv += lscale * s.phys.ausmup_ku * PpL * PmR * (rhoL+rhoR) * fa * a12;
    // M_p pressure-mass coupling speed (1/fa amplifies at low Mach)
    if (s.phys.ausmup_kp > 0.0) {
      double cut = std::max(1.0 - Mbar2, 0.0);
      inv += lscale * (s.phys.ausmup_kp/fa) * cut * a12;
    }
  }
  return inv;
}

void Solver::implicitSolve(int n_sweeps) {
  const int no = lm.n_owned;
  const int nloc = no + lm.n_ghost;
  dU.assign(nloc * NEQ, 0.0);
  diag.assign(no, 0.0);
  // AV linearization: per-cell sum of internal-face lengths (the Laplacian's
  // self-term magnitude). The live AV would otherwise not be linearized by the
  // scalar implicit (Newton stalls at ratio~1). With the correct dissipative
  // AV sign dR_av/dU_c < 0, so M_av = -dR_av/dU_c > 0 (stabilizing):
  // diag += k4*a*len^2*(len+sfl[c])/A_c; off-diag cf += k4*a*len^2*(len+sfl[nb])/A_c.
  // Owned-owned faces (sgs_inner) match the SGS coupling (consistent w/ inviscid).
  std::vector<double> sfl;
  if (shed_av) {
    sfl.assign(no, 0.0);
    for (int idx = 0; idx < (int)sgs_inner.size(); ++idx) {
      const LocalFace& f = lm.faces[sgs_inner[idx]];
      sfl[f.lc] += f.len; sfl[f.rc] += f.len;
    }
  }
  for (int c = 0; c < no; ++c) {
    double dtau = localDt(c, cfl_current);
    diag[c] = 1.0 / std::max(dtau, 1e-30);
    diag[c] += bdf_diag_coeff;   // dual-time BDF term (3/(2dt) BDF2, 1/dt BDF1)
    // Sponge layer diagonal (must match residual sponge source)
    if (const char* e = std::getenv("CFD2D_SPONGE")) {
      double sig_max = std::atof(e);
      double sr0 = 8.0, sr1 = 30.0;
      if (const char* e2 = std::getenv("CFD2D_SPONGE_R0")) sr0 = std::atof(e2);
      if (const char* e3 = std::getenv("CFD2D_SPONGE_R1")) sr1 = std::atof(e3);
      double r = std::sqrt(lm.center[c].x*lm.center[c].x + lm.center[c].y*lm.center[c].y);
      double sig = 0.0;
      if (r > sr0) sig = sig_max * std::min(1.0, (r - sr0) / std::max(sr1 - sr0, 1.0));
      if (sig > 0.0) diag[c] += sig / lm.area[c];
    }
  }
  // compute coefficients + diagonal contributions (internal faces)
  for (int idx = 0; idx < (int)sgs_inner.size(); ++idx) {
    const LocalFace& f = lm.faces[sgs_inner[idx]];
    double a = faceAlpha(*this, f); double len = f.len;
    diag[f.lc] += a*len/lm.area[f.lc];
    diag[f.rc] += a*len/lm.area[f.rc];
    if (shed_av) {
      double ll = len*len;
      // Spatially-scaled k4 for AV linearization (must match residual.cpp)
      // Precomputed per-face scaling (no sqrt+tanh in inner loop)
      double k4_av = av_k4 * face_av_scale[sgs_inner[idx]];
      diag[f.lc] += k4_av * a * ll * (len + sfl[f.lc]) / lm.area[f.lc];
      diag[f.rc] += k4_av * a * ll * (len + sfl[f.rc]) / lm.area[f.rc];
    }
  }
  // boundary faces: full spectral on the owner diagonal
  for (int idx = 0; idx < (int)sgs_bnd.size(); ++idx) {
    const LocalFace& f = lm.faces[sgs_bnd[idx]];
    double a = faceAlpha(*this, f); double len = f.len;
    if (f.lc>=0 && f.lc<no) diag[f.lc] += a*len/lm.area[f.lc];
    if (f.rc>=0 && f.rc<no) diag[f.rc] += a*len/lm.area[f.rc];
  }
  for (int c = 0; c < no; ++c) diag[c] = std::max(diag[c], 1e-12);
  // fill CSR coefficients (off-diagonal, half spectral)
  for (int c = 0; c < no; ++c) {
    for (int p = sgs_ptr[c]; p < sgs_ptr[c+1]; ++p) {
      const LocalFace& f = lm.faces[sgs_face[p]];
      double a = faceAlpha(*this, f); double len = f.len;
      sgs_coef[p] = 0.5 * a * len / lm.area[c];
      if (shed_av) {
        int nb = sgs_nb[p];
        double sfl_nb = (nb >= 0 && nb < no) ? sfl[nb] : 0.0;
        double k4_av2 = av_k4 * face_av_scale[sgs_face[p]];
        sgs_coef[p] += k4_av2 * a * len*len * (len + sfl_nb) / lm.area[c];
      }
    }
  }
  // symmetric Gauss-Seidel sweeps over the flat CSR
  for (int s = 0; s < n_sweeps; ++s) {
    for (int c = 0; c < no; ++c) {
      double rhs[NEQ]; for (int k=0;k<NEQ;++k) rhs[k]=R[c*NEQ+k];
      for (int p = sgs_ptr[c]; p < sgs_ptr[c+1]; ++p) {
        int j=sgs_nb[p]; double cf=sgs_coef[p];
        for (int k=0;k<NEQ;++k) rhs[k]+=cf*dU[j*NEQ+k];
      }
      double invd = 1.0/diag[c];
      for (int k=0;k<NEQ;++k) dU[c*NEQ+k]=rhs[k]*invd;
    }
    for (int c = no-1; c >= 0; --c) {
      double rhs[NEQ]; for (int k=0;k<NEQ;k++) rhs[k]=R[c*NEQ+k];
      for (int p = sgs_ptr[c]; p < sgs_ptr[c+1]; ++p) {
        int j=sgs_nb[p]; double cf=sgs_coef[p];
        for (int k=0;k<NEQ;k++) rhs[k]+=cf*dU[j*NEQ+k];
      }
      double invd = 1.0/diag[c];
      for (int k=0;k<NEQ;++k) dU[c*NEQ+k]=rhs[k]*invd;
    }
  }
}

}  // namespace cfd
