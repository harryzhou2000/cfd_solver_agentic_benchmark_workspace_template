// Implicit linear solve: matrix-free LU-SGS / symmetric Gauss-Seidel with a
// simplified (scalar, spectral-radius) Jacobian. The implicit system for the
// owned cells linearizes the Rusanov/Roe numerical flux as
//   (1/dtau_i) dU_i + (1/A_i) Sum_f 0.5*alpha_f*(dU_i - dU_nb) = R_i,
// i.e.  D_i dU_i - Sum_f c_f dU_nb = R_i  with
//   D_i = 1/dtau_i + (1/A_i) Sum_f alpha_f * len_f      (FULL spectral)
//   c_f  = (1/A_i) * 0.5 * alpha_f * len_f               (off-diagonal)
// Using the FULL spectral alpha on the diagonal (not 0.5 alpha) is essential:
// it makes the high-CFL limit dU -> -dU (removes the perturbation in one step),
// giving unconditional stability and fast convergence. With only 0.5*alpha on
// the diagonal the update would be dU ~ -2 dU (unstable). Multiple SGS sweeps
// over the 0.5*alpha off-diagonal provide the linear-solver iterations.
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
  return inv;
}

void Solver::implicitSolve(int n_sweeps) {
  const int no = lm.n_owned;
  const int nloc = no + lm.n_ghost;
  dU.assign(nloc * NEQ, 0.0);
  diag.assign(no, 0.0);
  for (int c = 0; c < no; ++c) {
    double dtau = localDt(c, cfl_current);
    diag[c] = 1.0 / std::max(dtau, 1e-30);
  }
  std::vector<std::vector<std::pair<int,double>>> nb(no);
  // internal faces (both owned): full spectral on diagonal, half on off-diag
  for (int idx = 0; idx < (int)sgs_inner.size(); ++idx) {
    const LocalFace& f = lm.faces[sgs_inner[idx]];
    double a = faceAlpha(*this, f); double len = f.len;
    diag[f.lc] += a*len/lm.area[f.lc];
    diag[f.rc] += a*len/lm.area[f.rc];
    nb[f.lc].push_back({f.rc, 0.5*a*len/lm.area[f.lc]});
    nb[f.rc].push_back({f.lc, 0.5*a*len/lm.area[f.rc]});
  }
  // boundary faces: full spectral on the owner diagonal
  for (int idx = 0; idx < (int)sgs_bnd.size(); ++idx) {
    const LocalFace& f = lm.faces[sgs_bnd[idx]];
    double a = faceAlpha(*this, f); double len = f.len;
    if (f.lc>=0 && f.lc<no) diag[f.lc] += a*len/lm.area[f.lc];
    if (f.rc>=0 && f.rc<no) diag[f.rc] += a*len/lm.area[f.rc];
  }
  for (int c = 0; c < no; ++c) diag[c] = std::max(diag[c], 1e-12);
  for (int s = 0; s < n_sweeps; ++s) {
    for (int c = 0; c < no; ++c) {
      double rhs[NEQ]; for (int k=0;k<NEQ;++k) rhs[k]=R[c*NEQ+k];
      for (const auto& pr : nb[c]){int j=pr.first;double cf=pr.second;
        for (int k=0;k<NEQ;++k) rhs[k]+=cf*dU[j*NEQ+k];}
      for (int k=0;k<NEQ;++k) dU[c*NEQ+k]=rhs[k]/diag[c];
    }
    for (int c = no-1; c >= 0; --c) {
      double rhs[NEQ]; for (int k=0;k<NEQ;k++) rhs[k]=R[c*NEQ+k];
      for (const auto& pr : nb[c]){int j=pr.first;double cf=pr.second;
        for (int k=0;k<NEQ;++k) rhs[k]+=cf*dU[j*NEQ+k];}
      for (int k=0;k<NEQ;++k) dU[c*NEQ+k]=rhs[k]/diag[c];
    }
  }
}

}  // namespace cfd
