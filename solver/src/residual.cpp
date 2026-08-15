// Conservative FV residual assembly: face reconstruction (2nd-order limited
// + positivity fallback), inviscid Roe/Rusanov flux, viscous flux from Green-
// Gauss gradients, boundary ghost states, BDF2 physical-time source, and
// wall-face data collection for force/surface output.
//
// NOTE: the local left/right primitive states are named Wl/Wr (not L/R) so
// they do not shadow the member residual vector R.
#include "solver.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cfd {

static const double RHO_FLOOR = 1e-9;
static const double P_FLOOR = 1e-9;

static inline Prim reconAt(const Solver& s, int c, const Vec2& fc) {
  Prim w;
  double dx = fc.x - s.lm.center[c].x, dy = fc.y - s.lm.center[c].y;
  w.rho = s.Wrho[c] + s.limRho[c] * (s.gRhoX[c]*dx + s.gRhoY[c]*dy);
  w.u   = s.Wu[c]   + s.limU[c]   * (s.gUX[c]*dx   + s.gUY[c]*dy);
  w.v   = s.Wv[c]   + s.limV[c]   * (s.gVX[c]*dx   + s.gVY[c]*dy);
  w.p   = s.Wp[c]   + s.limP[c]   * (s.gPX[c]*dx   + s.gPY[c]*dy);
  if (w.rho <= RHO_FLOOR || w.p <= P_FLOOR) {
    w.rho = s.Wrho[c]; w.u = s.Wu[c]; w.v = s.Wv[c]; w.p = s.Wp[c];
  }
  return w;
}

void Solver::clearWallData() {
  wcx.clear(); wcy.clear(); wSx.clear(); wSy.clear();
  wlen.clear(); wp.clear(); wtx.clear(); wty.clear(); wRho.clear();
}

void Solver::computeResidual(bool add_bdf2_source, double dt_phys) {
  const int no = lm.n_owned;
  const int nloc = no + lm.n_ghost;
  R.assign(no * NEQ, 0.0);
  clearWallData();
  for (int fi = 0; fi < (int)lm.faces.size(); ++fi) {
    const LocalFace& f = lm.faces[fi];
    double len = std::max(f.len, 1e-30);
    double nx = f.Sx / len, ny = f.Sy / len;
    Vec2 fc = f.center;
    Prim Wl, Wr;
    int lc = f.lc, rc = f.rc;
    bool lcOwn = (lc >= 0 && lc < no);
    bool rcOwn = (rc >= 0 && rc < no);
    if (lcOwn) Wl = reconAt(*this, lc, fc);
    else if (lc >= 0 && lc < nloc) { Wl.rho=Wrho[lc]; Wl.u=Wu[lc]; Wl.v=Wv[lc]; Wl.p=Wp[lc]; }
    if (rc >= 0) {
      if (rcOwn) Wr = reconAt(*this, rc, fc);
      else { Wr.rho=Wrho[rc]; Wr.u=Wu[rc]; Wr.v=Wv[rc]; Wr.p=Wp[rc]; }
    } else {
      Prim Wint = lcOwn ? Wl : Prim{Wrho[lc],Wu[lc],Wv[lc],Wp[lc]};
      Wr = bcGhostState(phys, f.bctype, Wint, nx, ny);
    }
    Wl.rho = std::max(Wl.rho, RHO_FLOOR); Wl.p = std::max(Wl.p, P_FLOOR);
    Wr.rho = std::max(Wr.rho, RHO_FLOOR); Wr.p = std::max(Wr.p, P_FLOOR);

    Cons F = numericalInviscidFlux(phys, Wl, Wr, nx, ny);
    Cons flux; for (int k=0;k<NEQ;++k) flux.v[k] = F.v[k] * len;

    if (phys.laminar && phys.mu > 0.0) {
      double gUx[2]={0,0}, gUy[2]={0,0}, gVx[2]={0,0}, gVy[2]={0,0},
             gTx[2]={0,0}, gTy[2]={0,0};
      Prim side[2]; int have=0;
      if (lcOwn) {
        gUx[0]=gUX[lc]; gUy[0]=gUY[lc]; gVx[0]=gVX[lc]; gVy[0]=gVY[lc];
        gTx[0]=gTX[lc]; gTy[0]=gTY[lc]; side[0]=Wl; have|=1;
      }
      if (rcOwn) {
        gUx[1]=gUX[rc]; gUy[1]=gUY[rc]; gVx[1]=gVX[rc]; gVy[1]=gVY[rc];
        gTx[1]=gTX[rc]; gTy[1]=gTY[rc]; side[1]=Wr; have|=2;
      }
      double ux=0,uy=0,vx=0,vy=0,Tx=0,Ty=0,u=0,v=0,T=0;
      if (have==3) {
        ux=0.5*(gUx[0]+gUx[1]); uy=0.5*(gUy[0]+gUy[1]);
        vx=0.5*(gVx[0]+gVx[1]); vy=0.5*(gVy[0]+gVy[1]);
        Tx=0.5*(gTx[0]+gTx[1]); Ty=0.5*(gTy[0]+gTy[1]);
        u=0.5*(Wl.u+Wr.u); v=0.5*(Wl.v+Wr.v);
        T=0.5*(phys.gas.temperature(Wl)+phys.gas.temperature(Wr));
      } else if (have==1) {
        ux=gUx[0]; uy=gUy[0]; vx=gVx[0]; vy=gVy[0]; Tx=gTx[0]; Ty=gTy[0];
        u=Wl.u; v=Wl.v; T=phys.gas.temperature(Wl);
      } else if (have==2) {
        ux=gUx[1]; uy=gUy[1]; vx=gVx[1]; vy=gVy[1]; Tx=gTx[1]; Ty=gTy[1];
        u=Wr.u; v=Wr.v; T=phys.gas.temperature(Wr);
      }
      if (have) {
        Cons Fv = viscousNormalFlux(phys, u, v, T, ux,uy,vx,vy,Tx,Ty, nx,ny);
        for (int k=0;k<NEQ;++k) flux.v[k] += Fv.v[k] * len;
      }
    }

    if (lc >= 0 && lc < no) {
      double inv = 1.0 / lm.area[lc];
      for (int k=0;k<NEQ;++k) R[lc*NEQ+k] -= flux.v[k] * inv;
    }
    if (rc >= 0 && rc < no) {
      double inv = 1.0 / lm.area[rc];
      for (int k=0;k<NEQ;++k) R[rc*NEQ+k] += flux.v[k] * inv;
    }

    if (f.bctype == BCType::NoSlipAdiabaticWall || f.bctype == BCType::SlipWall) {
      double pwall = lcOwn ? Wl.p : (rcOwn ? Wr.p : Wp[lc]);
      double txx=0,txy=0,tyy=0;
      int cg = lcOwn ? lc : (rcOwn ? rc : -1);
      if (phys.laminar && phys.mu>0.0 && cg >= 0 && cg < no) {
        double ux=gUX[cg],uy=gUY[cg],vx=gVX[cg],vy=gVY[cg];
        double div = ux+vy; double mu = phys.mu;
        txx = 2.0*mu*ux - (2.0/3.0)*mu*div;
        tyy = 2.0*mu*vy - (2.0/3.0)*mu*div;
        txy = mu*(uy+vx);
      }
      wcx.push_back(fc.x); wcy.push_back(fc.y);
      wSx.push_back(f.Sx); wSy.push_back(f.Sy); wlen.push_back(len);
      wp.push_back(pwall);
      // Wall viscous traction stored for force integration. The face normal S
      // points out of the fluid (into the body); the tangential traction the
      // fluid exerts on the body is -tau.n projected tangentially, so we store
      // -tau.S to match the pressure-force sign convention (p-p_inf)*S which
      // yields positive drag for a body in freestream.
      wtx.push_back(-(txx*f.Sx + txy*f.Sy));
      wty.push_back(-(txy*f.Sx + tyy*f.Sy));
      wRho.push_back((cg>=0 && cg<no) ? Wrho[cg] : Wrho[lc]);
    }
  }

  if (add_bdf2_source && dt_phys > 0.0) {
    double coef = 1.0 / (2.0 * dt_phys);
    for (int c = 0; c < no; ++c)
      for (int k = 0; k < NEQ; ++k) {
        double u_cur = U[c*NEQ+k], u_n = Un[c*NEQ+k], u_nm1 = Unm1[c*NEQ+k];
        R[c*NEQ+k] -= coef * (3.0*u_cur - 4.0*u_n + u_nm1);
      }
  }
  if (std::getenv("CFD2D_DEBUG")) {
    int nbad = 0; double maxR = 0; int imax = 0;
    for (int c = 0; c < no; ++c) {
      if (lm.area[c] < 1e-6) ++nbad;
      double rr = 0; for (int k=0;k<NEQ;++k) rr += R[c*NEQ+k]*R[c*NEQ+k];
      if (rr > maxR) { maxR = rr; imax = c; }
    }
    std::fprintf(stderr, "[r%d] nbad(area<1e-6)=%d maxR cell=%d area=%.3e R=(%.3e,%.3e,%.3e,%.3e)\n",
      rank, nbad, imax, lm.area[imax], R[imax*NEQ], R[imax*NEQ+1], R[imax*NEQ+2], R[imax*NEQ+3]);
  }
  (void)nloc;
}

void Solver::computeResidualNorms() {
  // Residual norm uses the cell flux SUM (area * R), i.e. the true conservative
  // imbalance per cell, NOT the flux/area. This avoids the L2 being dominated
  // by high-aspect-ratio boundary-layer cells (tiny area -> huge flux/area) and
  // gives a well-behaved convergence metric driven by the bulk of the domain.
  double lsq[4] = {0,0,0,0}, linf[4] = {0,0,0,0};
  int no = lm.n_owned;
  for (int c = 0; c < no; ++c)
    for (int k = 0; k < NEQ; ++k) {
      double v = lm.area[c] * R[c*NEQ+k];
      lsq[k] += v*v;
      double av = std::fabs(v);
      if (av > linf[k]) linf[k] = av;
    }
  double gsq[4], ginf[4];
  MPI_Allreduce(lsq, gsq, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(linf, ginf, 4, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int nc = std::max(lm.num_cells_global, 1);
  double tot = 0.0, mx = 0.0;
  for (int k = 0; k < 4; ++k) {
    compL2[k] = std::sqrt(gsq[k] / nc);
    tot += gsq[k];
    if (ginf[k] > mx) mx = ginf[k];
  }
  residual_l2_last = std::sqrt(tot / nc);
  residual_linf_last = mx;
}

double Solver::residualL2() const { return residual_l2_last; }

void Solver::residualComponentsL2(double out[4]) const {
  for (int k = 0; k < 4; ++k) out[k] = compL2[k];
}

double Solver::localDt(int c, double cfl) const {
  double a = phys.gas.soundSpeed(Prim{Wrho[c], Wu[c], Wv[c], Wp[c]});
  double u = Wu[c], v = Wv[c];
  double spec = std::sqrt(u*u+v*v) + a;
  double visc = 0.0;
  if (phys.laminar && phys.mu > 0.0) {
    double h = std::sqrt(std::max(lm.area[c], 1e-30));
    double rho = std::max(Wrho[c], RHO_FLOOR);
    visc = 4.0 * phys.mu / (rho * h * h);
  }
  double denom = spec + visc * lm.area[c];
  return cfl * lm.area[c] / std::max(denom, 1e-30);
}

}  // namespace cfd
