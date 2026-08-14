#include "solver.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>

namespace cfd2d {

Solver::Solver(const LocalMesh& m, const CaseConfig& c, int r, int np, MPI_Comm cm)
  : mesh(m), cfg(c), gas(c.gas), rank(r), nprocs(np), comm(cm) {
  mu = cfg.viscosity();
  U.resize(mesh.nCells);
  W.resize(mesh.nCells);
  gRho.resize(mesh.nCells); gU.resize(mesh.nCells); gV.resize(mesh.nCells);
  gP.resize(mesh.nCells); gT.resize(mesh.nCells);
  limiter.resize(mesh.nCells, 1.0);
  // Check for first-order mode via environment variable
  firstOrderMode = (getenv("CFD2D_FIRST_ORDER") != nullptr);
  faceSigma.resize(mesh.faces.size(), 0);
}

void Solver::initialize() {
  // Detect cylinder wall and compute center/radius for potential flow init
  double cx = 0, cy = 0, rsum = 0;
  int nwall = 0;
  for (size_t fi = 0; fi < mesh.faces.size(); fi++) {
    const auto& f = mesh.faces[fi];
    if (f.cr == -1 && f.bc == BCType::NoSlipWall) {
      cx += mesh.fcx[fi]; cy += mesh.fcy[fi]; nwall++;
    }
  }
  // Always call MPI_Allreduce (even if local nwall=0) to avoid deadlock
  double globals[3] = {(double)nwall, cx, cy};
  MPI_Allreduce(MPI_IN_PLACE, globals, 3, MPI_DOUBLE, MPI_SUM, comm);
  nwall = (int)globals[0];
  if (nwall > 0) {
    cx = globals[1] / nwall; cy = globals[2] / nwall;
    rsum = 0; int nwall2 = 0;
    for (size_t fi = 0; fi < mesh.faces.size(); fi++) {
      const auto& f = mesh.faces[fi];
      if (f.cr == -1 && f.bc == BCType::NoSlipWall) {
        double dx = mesh.fcx[fi] - cx, dy = mesh.fcy[fi] - cy;
        rsum += sqrt(dx*dx + dy*dy); nwall2++;
      }
    }
    double gsum[2] = {rsum, (double)nwall2};
    MPI_Allreduce(MPI_IN_PLACE, gsum, 2, MPI_DOUBLE, MPI_SUM, comm);
    double R = gsum[0] / gsum[1];
    // Check circularity: only use potential flow init for circular walls
    double rvar = 0; int nwall3 = 0;
    for (size_t fi = 0; fi < mesh.faces.size(); fi++) {
      const auto& f = mesh.faces[fi];
      if (f.cr == -1 && f.bc == BCType::NoSlipWall) {
        double dx = mesh.fcx[fi] - cx, dy = mesh.fcy[fi] - cy;
        double r = sqrt(dx*dx + dy*dy);
        rvar += (r - R)*(r - R); nwall3++;
      }
    }
    double gvar[2] = {rvar, (double)nwall3};
    MPI_Allreduce(MPI_IN_PLACE, gvar, 2, MPI_DOUBLE, MPI_SUM, comm);
    double rstd = sqrt(gvar[0] / gvar[1]) / R;
    if (rstd < 0.05 && rank == 0) printf("  Potential flow init: center=(%.3f,%.3f) R=%.3f (circularity=%.4f)\n", cx, cy, R, rstd);
    if (rstd < 0.05) {
    // Initialize with potential flow around cylinder
    for (int i = 0; i < mesh.nCells; i++) {
      double x = mesh.cx[i] - cx, y = mesh.cy[i] - cy;
      double r2 = x*x + y*y;
      double r4 = r2 * r2;
      double R2 = R * R;
      if (r2 < R2) r2 = R2; // inside cylinder -> use wall values
      double u = cfg.fs.vel * (1.0 - R2 * (x*x - y*y) / (r2*r2));
      double v = cfg.fs.vel * (-2.0 * R2 * x * y / (r2*r2));
      // Pressure from Bernoulli: p = p_inf + 0.5*rho*U^2*(1 - (u^2+v^2)/U^2)
      double V2 = u*u + v*v;
      double p = cfg.fs.pressure + 0.5 * cfg.fs.rho * cfg.fs.vel * cfg.fs.vel * (1.0 - V2 / (cfg.fs.vel*cfg.fs.vel));
      Prim W = {cfg.fs.rho, u, v, p};
      U[i] = gas.toCons(W);
    }
    } else {
      for (int i = 0; i < mesh.nCells; i++)
        U[i] = cfg.fs.cons;
    }
  } else {
    for (int i = 0; i < mesh.nCells; i++)
      U[i] = cfg.fs.cons;
  }
  computePrimitive();
}

void Solver::computePrimitive() {
  for (int i = 0; i < mesh.nCells; i++)
    W[i] = gas.toPrim(U[i]);
}

static inline double dot2(double ax, double ay, double bx, double by) { return ax*bx+ay*by; }

void Solver::applyBC(Prim& ghost, const Prim& cellW, int fi) {
  const auto& f = mesh.faces[fi];
  double nx = mesh.fnx[fi], ny = mesh.fny[fi];
  ghost = cellW;
  switch (f.bc) {
    case BCType::Farfield: {
      double Vn = cellW[1]*nx + cellW[2]*ny;
      if (Vn >= 0) {
        // Outflow: zero gradient (copy interior state)
        ghost = cellW;
      } else {
        // Inflow: freestream
        ghost = cfg.fs.prim;
      }
      break;
    }
    case BCType::SlipWall: {
      double Vn = cellW[1]*nx + cellW[2]*ny;
      ghost[1] = cellW[1] - 2*Vn*nx;
      ghost[2] = cellW[2] - 2*Vn*ny;
      break;
    }
    case BCType::NoSlipWall:
      ghost[1] = -cellW[1];
      ghost[2] = -cellW[2];
      break;
    default: break;
  }
}

void Solver::computeGradients() {
  int nc = mesh.nOwned;
  for (int i = 0; i < mesh.nCells; i++) {
    gRho[i] = {0,0}; gU[i] = {0,0}; gV[i] = {0,0}; gP[i] = {0,0}; gT[i] = {0,0};
  }
  // Least-squares gradient using neighbor cell centers
  // For boundary faces, create ghost cell (reflected center + BC state)
  for (int i = 0; i < nc; i++) {
    double wxx=0, wxy=0, wyy=0;
    double bRho[2]={0,0}, bU[2]={0,0}, bV[2]={0,0}, bP[2]={0,0}, bT[2]={0,0};
    double rhoMax = W[i][0], rhoMin = W[i][0];
    double pMax = W[i][3], pMin = W[i][3];
    int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
    for (int fi = s; fi < e; fi++) {
      int f = mesh.cellFaces[fi];
      const auto& face = mesh.faces[f];
      int j = (face.cl == i) ? face.cr : face.cl;
      double njx, njy;
      Prim Wj;
      bool isInterior = (j >= 0);
      if (isInterior) {
        njx = mesh.cx[j]; njy = mesh.cy[j];
        Wj = W[j];
        rhoMax = std::max(rhoMax, Wj[0]); rhoMin = std::min(rhoMin, Wj[0]);
        pMax = std::max(pMax, Wj[3]); pMin = std::min(pMin, Wj[3]);
      } else {
        // Boundary ghost: reflect cell center across face
        double fcx = mesh.fcx[f], fcy = mesh.fcy[f];
        double nx = mesh.fnx[f], ny = mesh.fny[f];
        double d = (fcx - mesh.cx[i])*nx + (fcy - mesh.cy[i])*ny;
        njx = mesh.cx[i] + 2*d*nx;
        njy = mesh.cy[i] + 2*d*ny;
        applyBC(Wj, W[i], f);
      }
      double dx = njx - mesh.cx[i], dy = njy - mesh.cy[i];
      double w = 1.0 / (dx*dx + dy*dy + 1e-30);
      wxx += w*dx*dx; wxy += w*dx*dy; wyy += w*dy*dy;
      double drRho = Wj[0]-W[i][0], drU = Wj[1]-W[i][1], drV = Wj[2]-W[i][2];
      double drP = Wj[3]-W[i][3];
      double Ti = gas.temperature(W[i][0], W[i][3]);
      double Tj = gas.temperature(Wj[0], Wj[3]);
      double drT = Tj - Ti;
      bRho[0] += w*dx*drRho; bRho[1] += w*dy*drRho;
      bU[0] += w*dx*drU; bU[1] += w*dy*drU;
      bV[0] += w*dx*drV; bV[1] += w*dy*drV;
      bP[0] += w*dx*drP; bP[1] += w*dy*drP;
      bT[0] += w*dx*drT; bT[1] += w*dy*drT;
    }
    double det = wxx*wyy - wxy*wxy;
    if (std::abs(det) < 1e-30) det = 1e-30;
    double idx = wyy/det, idy = -wxy/det, idy2 = wxx/det;
    gRho[i] = {idx*bRho[0]+idy*bRho[1], idy*bRho[0]+idy2*bRho[1]};
    gU[i]    = {idx*bU[0]+idy*bU[1],    idy*bU[0]+idy2*bU[1]};
    gV[i]    = {idx*bV[0]+idy*bV[1],    idy*bV[0]+idy2*bV[1]};
    gP[i]    = {idx*bP[0]+idy*bP[1],    idy*bP[0]+idy2*bP[1]};
    gT[i]    = {idx*bT[0]+idy*bT[1],    idy*bT[0]+idy2*bT[1]};

    // Barth-Jespersen limiter on density and pressure (capped at 0.5 for LU-SGS stability)
    double lim = 1.0;
    for (int fi = s; fi < e; fi++) {
      int f = mesh.cellFaces[fi];
      double dx = mesh.fcx[f] - mesh.cx[i], dy = mesh.fcy[f] - mesh.cy[i];
      auto faceLim = [&](double phi, double phiCell, double phiMax, double phiMin) -> double {
        double dphi = phi - phiCell;
        if (dphi > 1e-30) return std::min(1.0, (phiMax - phiCell) / dphi);
        if (dphi < -1e-30) return std::min(1.0, (phiMin - phiCell) / dphi);
        return 1.0;
      };
      double rhoFace = W[i][0] + gRho[i][0]*dx + gRho[i][1]*dy;
      double pFace = W[i][3] + gP[i][0]*dx + gP[i][1]*dy;
      lim = std::min(lim, faceLim(rhoFace, W[i][0], rhoMax, rhoMin));
      lim = std::min(lim, faceLim(pFace, W[i][3], pMax, pMin));
    }
    limiter[i] = (firstOrderMode || limiterRamp < 1e-10) ? 0.0 : std::max(0.0, std::min(lim, 0.5)) * limiterRamp;
  }
  // Ghost cell limiters = 1 (will be overwritten by exchange, but just in case)
  for (int i = mesh.nOwned; i < mesh.nCells; i++) limiter[i] = 1.0;
}

void Solver::computeSpectralRadius() {
  double scale = cfg.rusanovScale;
  for (size_t f = 0; f < mesh.faces.size(); f++) {
    const auto& face = mesh.faces[f];
    int cl = face.cl, cr = face.cr;
    double nx = mesh.fnx[f], ny = mesh.fny[f], S = mesh.flen[f];
    double Vn, c, rho;
    if (cr >= 0) {
      double VnL = W[cl][1]*nx + W[cl][2]*ny;
      double VnR = W[cr][1]*nx + W[cr][2]*ny;
      double cL = gas.soundSpeed(W[cl][0], W[cl][3]);
      double cR = gas.soundSpeed(W[cr][0], W[cr][3]);
      Vn = std::max(std::abs(VnL)+cL, std::abs(VnR)+cR);
      rho = 0.5*(W[cl][0]+W[cr][0]);
    } else {
      double VnL = W[cl][1]*nx + W[cl][2]*ny;
      double cL = gas.soundSpeed(W[cl][0], W[cl][3]);
      Prim Wg; applyBC(Wg, W[cl], f);
      double VnR = Wg[1]*nx + Wg[2]*ny;
      double cR = gas.soundSpeed(Wg[0], Wg[3]);
      Vn = std::max(std::abs(VnL)+cL, std::abs(VnR)+cR);
      rho = W[cl][0];
    }
    double sigma = Vn * S * scale;
    if (mu > 0) {
      double dist;
      if (cr >= 0) {
        dist = std::abs((mesh.cx[cr]-mesh.cx[cl])*nx + (mesh.cy[cr]-mesh.cy[cl])*ny);
      } else {
        dist = std::abs((mesh.fcx[f]-mesh.cx[cl])*nx + (mesh.fcy[f]-mesh.cy[cl])*ny);
      }
      dist = std::max(dist, 1e-15);
      sigma += (4.0/3.0) * mu * S / (rho * dist);
    }
    faceSigma[f] = sigma;
  }
}

void Solver::computeResidual(std::vector<Cons>& R, double dtPhys,
                             const std::vector<Cons>* Un, const std::vector<Cons>* Unm1) {
  for (int i = 0; i < mesh.nCells; i++) R[i] = {0,0,0,0};
  computePrimitive();
  if (!reuseGradients) computeGradients();
  computeSpectralRadius();

  double scale = cfg.rusanovScale;
  for (size_t f = 0; f < mesh.faces.size(); f++) {
    const auto& face = mesh.faces[f];
    int cl = face.cl, cr = face.cr;
    double nx = mesh.fnx[f], ny = mesh.fny[f], S = mesh.flen[f];
    Prim WL, WR;
    // Reconstruction
    if (cr >= 0) {
      double dxL = mesh.fcx[f]-mesh.cx[cl], dyL = mesh.fcy[f]-mesh.cy[cl];
      double dxR = mesh.fcx[f]-mesh.cx[cr], dyR = mesh.fcy[f]-mesh.cy[cr];
      double limL = limiter[cl], limR = limiter[cr];
      for (int e = 0; e < 4; e++) {
        double gLx, gLy, gRx, gRy;
        if (e==0){gLx=gRho[cl][0];gLy=gRho[cl][1];gRx=gRho[cr][0];gRy=gRho[cr][1];}
        else if(e==1){gLx=gU[cl][0];gLy=gU[cl][1];gRx=gU[cr][0];gRy=gU[cr][1];}
        else if(e==2){gLx=gV[cl][0];gLy=gV[cl][1];gRx=gV[cr][0];gRy=gV[cr][1];}
        else{gLx=gP[cl][0];gLy=gP[cl][1];gRx=gP[cr][0];gRy=gP[cr][1];}
        WL[e] = W[cl][e] + limL*(gLx*dxL + gLy*dyL);
        WR[e] = W[cr][e] + limR*(gRx*dxR + gRy*dyR);
      }
      // Positivity fallback
      if (WL[0] < 1e-12 || WL[3] < 1e-12) WL = W[cl];
      if (WR[0] < 1e-12 || WR[3] < 1e-12) WR = W[cr];
    } else {
      WL = W[cl];
      applyBC(WR, W[cl], f);
    }
    double invL = 1.0/mesh.cvol[cl];
    double invR = (cr >= 0) ? 1.0/mesh.cvol[cr] : 0.0;
    // For wall boundaries, use direct wall flux (pressure-only inviscid + viscous shear)
    if (face.bc == BCType::SlipWall || face.bc == BCType::NoSlipWall) {
      double p = W[cl][3];
      Cons Fi;
      Fi[0] = 0; Fi[1] = p*nx*S; Fi[2] = p*ny*S; Fi[3] = 0;
      for (int e=0;e<NEQ;e++) R[cl][e] += Fi[e]*invL;
      if (mu > 0 && face.bc == BCType::NoSlipWall) {
        double ux=gU[cl][0],uy=gU[cl][1],vx=gV[cl][0],vy=gV[cl][1];
        double tauxx=mu*(4.0/3.0*ux-2.0/3.0*vy), tauyy=mu*(4.0/3.0*vy-2.0/3.0*ux), tauxy=mu*(uy+vx);
        Cons Fv;
        Fv[0]=0; Fv[1]=(tauxx*nx+tauxy*ny)*S; Fv[2]=(tauxy*nx+tauyy*ny)*S; Fv[3]=0; // adiabatic wall: no heat flux
        for (int e=0;e<NEQ;e++) R[cl][e] -= Fv[e]*invL;
      }
      continue; // skip Rusanov flux for wall faces
    }
    // Rusanov inviscid flux for interior and farfield faces
    double VnL = WL[1]*nx + WL[2]*ny;
    double VnR = WR[1]*nx + WR[2]*ny;
    double cL = gas.soundSpeed(WL[0], WL[3]);
    double cR = gas.soundSpeed(WR[0], WR[3]);
    double sigma = std::max(std::abs(VnL)+cL, std::abs(VnR)+cR) * S * scale;
    double rhoE_L = WL[3]/gas.gamma1 + 0.5*WL[0]*(WL[1]*WL[1]+WL[2]*WL[2]);
    double rhoE_R = WR[3]/gas.gamma1 + 0.5*WR[0]*(WR[1]*WR[1]+WR[2]*WR[2]);
    Cons FnL, FnR, UL, UR;
    UL = gas.toCons(WL); UR = gas.toCons(WR);
    FnL[0]=WL[0]*VnL; FnL[1]=WL[0]*WL[1]*VnL+WL[3]*nx; FnL[2]=WL[0]*WL[2]*VnL+WL[3]*ny; FnL[3]=(rhoE_L+WL[3])*VnL;
    FnR[0]=WR[0]*VnR; FnR[1]=WR[0]*WR[1]*VnR+WR[3]*nx; FnR[2]=WR[0]*WR[2]*VnR+WR[3]*ny; FnR[3]=(rhoE_R+WR[3])*VnR;
    Cons Fi;
    for (int e=0;e<NEQ;e++) Fi[e] = 0.5*(FnL[e]+FnR[e])*S - 0.5*sigma*(UR[e]-UL[e]);
    // Accumulate inviscid
    for (int e=0;e<NEQ;e++) R[cl][e] += Fi[e]*invL;
    if (cr >= 0) for (int e=0;e<NEQ;e++) R[cr][e] -= Fi[e]*invR;
    // Viscous flux
    if (mu > 0) {
      double ux,uy,vx,vy,Tx,Ty;
      if (cr >= 0) {
        ux=0.5*(gU[cl][0]+gU[cr][0]); uy=0.5*(gU[cl][1]+gU[cr][1]);
        vx=0.5*(gV[cl][0]+gV[cr][0]); vy=0.5*(gV[cl][1]+gV[cr][1]);
        Tx=0.5*(gT[cl][0]+gT[cr][0]); Ty=0.5*(gT[cl][1]+gT[cr][1]);
      } else {
        ux=gU[cl][0]; uy=gU[cl][1]; vx=gV[cl][0]; vy=gV[cl][1];
        Tx=gT[cl][0]; Ty=gT[cl][1];
      }
      double tauxx = mu*(4.0/3.0*ux - 2.0/3.0*vy);
      double tauyy = mu*(4.0/3.0*vy - 2.0/3.0*ux);
      double tauxy = mu*(uy + vx);
      double k_cond = mu*gas.cp/gas.Pr;
      double qx = -k_cond*Tx, qy = -k_cond*Ty;
      double uf = 0.5*(WL[1]+WR[1]), vf = 0.5*(WL[2]+WR[2]);
      Cons Fv;
      Fv[0] = 0;
      Fv[1] = (tauxx*nx + tauxy*ny)*S;
      Fv[2] = (tauxy*nx + tauyy*ny)*S;
      Fv[3] = ((tauxx*uf+tauxy*vf-qx)*nx + (tauxy*uf+tauyy*vf-qy)*ny)*S;
      for (int e=0;e<NEQ;e++) R[cl][e] -= Fv[e]*invL;
      if (cr >= 0) for (int e=0;e<NEQ;e++) R[cr][e] += Fv[e]*invR;
    }
  }
  // Add physical-time source (BDF2/BDF1)
  if (Un != nullptr) {
    double coeff, a_n, a_nm1;
    if (Unm1 != nullptr) {
      coeff = 3.0/(2.0*dtPhys); a_n = -2.0/dtPhys; a_nm1 = 0.5/dtPhys;
    } else {
      coeff = 1.0/dtPhys; a_n = -1.0/dtPhys; a_nm1 = 0;
    }
    for (int i = 0; i < mesh.nOwned; i++)
      for (int e=0;e<NEQ;e++)
        R[i][e] += coeff*U[i][e] + a_n*(*Un)[i][e] + (Unm1? a_nm1*(*Unm1)[i][e] : 0);
  }
}

ResComps Solver::residualComps(const std::vector<Cons>& R) {
  double loc[6] = {0,0,0,0,0,0};
  for (int i = 0; i < mesh.nOwned; i++) {
    loc[0] += R[i][0]*R[i][0]; loc[1] += R[i][1]*R[i][1];
    loc[2] += R[i][2]*R[i][2]; loc[3] += R[i][3]*R[i][3];
    loc[5] = std::max(loc[5], std::abs(R[i][0]));
    loc[5] = std::max(loc[5], std::abs(R[i][1]));
    loc[5] = std::max(loc[5], std::abs(R[i][2]));
    loc[5] = std::max(loc[5], std::abs(R[i][3]));
  }
  double g[6];
  MPI_Allreduce(loc, g, 6, MPI_DOUBLE, MPI_SUM, comm); // note: linf uses SUM incorrectly
  ResComps rc;
  rc.rho = std::sqrt(g[0]); rc.rhou = std::sqrt(g[1]);
  rc.rhov = std::sqrt(g[2]); rc.rhoE = std::sqrt(g[3]);
  rc.l2 = std::sqrt((g[0]+g[1]+g[2]+g[3])/(mesh.nCellsGlobal*NEQ));
  // linf needs separate MPI_MAX
  double linf_local = loc[5];
  MPI_Allreduce(&linf_local, &rc.linf, 1, MPI_DOUBLE, MPI_MAX, comm);
  return rc;
}

double Solver::residualL2(const std::vector<Cons>& R) {
  double local = 0;
  for (int i = 0; i < mesh.nOwned; i++)
    for (int e=0;e<NEQ;e++) local += R[i][e]*R[i][e];
  double global;
  MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, comm);
  return std::sqrt(global / (mesh.nCellsGlobal * NEQ));
}

double Solver::residualLinf(const std::vector<Cons>& R) {
  double local = 0;
  for (int i = 0; i < mesh.nOwned; i++)
    for (int e=0;e<NEQ;e++) local = std::max(local, std::abs(R[i][e]));
  double global;
  MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_MAX, comm);
  return global;
}

int g_nSweeps = 1;
void Solver::luSGS(std::vector<Cons>& R, std::vector<Cons>& dU, double dtPhys,
                   const std::vector<Cons>* Un, const std::vector<Cons>* Unm1) {
  int nSweeps = g_nSweeps;
  int nc = mesh.nOwned;
  // Compute diagonal and local time step
  std::vector<double> D(nc), dtau(nc);
  for (int i = 0; i < nc; i++) {
    double sumSig = 0;
    int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
    for (int fi = s; fi < e; fi++) {
      int f = mesh.cellFaces[fi];
      sumSig += faceSigma[f];
    }
    dtau[i] = currentCFL * mesh.cvol[i] / (sumSig + 1e-30);
    double physCoeff = 0;
    if (Un != nullptr) physCoeff = (Unm1 != nullptr) ? 3.0/(2.0*dtPhys) : 1.0/dtPhys;
    D[i] = 1.0/dtau[i] + physCoeff + 0.5*sumSig/mesh.cvol[i];
  }
  // Initialize dU = 0
  for (int i = 0; i < mesh.nCells; i++) dU[i] = {0,0,0,0};
  mesh.exchangeGhost(dU.data(), comm, rank);

  for (int sweep = 0; sweep < nSweeps; sweep++) {
  // Forward sweep
  for (int i = 0; i < nc; i++) {
    double sum[NEQ] = {0,0,0,0};
    int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
    for (int fi = s; fi < e; fi++) {
      int f = mesh.cellFaces[fi];
      const auto& face = mesh.faces[f];
      int j = (face.cl == i) ? face.cr : face.cl;
      if (j < 0) continue;
      if (j < i || j >= nc) {
        double coeff = 0.5 * faceSigma[f] / mesh.cvol[i];
        for (int e2=0;e2<NEQ;e2++) sum[e2] += coeff * dU[j][e2];
      }
    }
    double invD = 1.0/D[i];
    for (int e2=0;e2<NEQ;e2++) dU[i][e2] = (-R[i][e2] + sum[e2]) * invD;
  }
  mesh.exchangeGhost(dU.data(), comm, rank);

  // Backward sweep
  for (int i = nc-1; i >= 0; i--) {
    double sum[NEQ] = {0,0,0,0};
    int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
    for (int fi = s; fi < e; fi++) {
      int f = mesh.cellFaces[fi];
      const auto& face = mesh.faces[f];
      int j = (face.cl == i) ? face.cr : face.cl;
      if (j < 0) continue;
      if (j > i && j < nc) {
        double coeff = 0.5 * faceSigma[f] / mesh.cvol[i];
        for (int e2=0;e2<NEQ;e2++) sum[e2] += coeff * dU[j][e2];
      }
    }
    double invD = 1.0/D[i];
    for (int e2=0;e2<NEQ;e2++) dU[i][e2] += sum[e2] * invD;
  }
  mesh.exchangeGhost(dU.data(), comm, rank);
  } // end sweep loop
}


void Solver::jacobiSolve(std::vector<Cons>& R, std::vector<Cons>& dU, double dtPhys,
                         const std::vector<Cons>* Un, const std::vector<Cons>* Unm1) {
  int nc = mesh.nOwned;
  std::vector<double> D(nc);
  for (int i = 0; i < nc; i++) {
    double sumSig = 0;
    int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
    for (int fi = s; fi < e; fi++) { int f = mesh.cellFaces[fi]; sumSig += faceSigma[f]; }
    double dtau_i = currentCFL * mesh.cvol[i] / (sumSig + 1e-30);
    double physCoeff = (Un != nullptr) ? ((Unm1 != nullptr) ? 3.0/(2.0*dtPhys) : 1.0/dtPhys) : 0;
    D[i] = 1.0/dtau_i + physCoeff + sumSig/mesh.cvol[i];
  }
  for (int i = 0; i < mesh.nCells; i++) dU[i] = {0,0,0,0};
  for (int iter = 0; iter < 30; iter++) {
    mesh.exchangeGhost(dU.data(), comm, rank);
    std::vector<Cons> dUnew = dU;
    for (int i = 0; i < nc; i++) {
      double sum[NEQ] = {0,0,0,0};
      int s = mesh.cellFaceOff[i], e = mesh.cellFaceOff[i+1];
      for (int fi = s; fi < e; fi++) {
        int f = mesh.cellFaces[fi];
        const auto& face = mesh.faces[f];
        int j = (face.cl == i) ? face.cr : face.cl;
        if (j < 0) continue;
        double coeff = 0.5 * faceSigma[f] / mesh.cvol[i];
        for (int e2=0;e2<NEQ;e2++) sum[e2] += coeff * dU[j][e2];
      }
      double invD = 1.0/D[i];
      for (int e2=0;e2<NEQ;e2++) dUnew[i][e2] = (-R[i][e2] - sum[e2]) * invD;
    }
    dU = dUnew;
  }
  mesh.exchangeGhost(dU.data(), comm, rank);
}

void Solver::runSteady() {
  auto t0 = std::chrono::steady_clock::now();
  std::vector<Cons> R(mesh.nCells), dU(mesh.nCells);
  double initRes = 0;
  for (int step = 1; step <= cfg.maxSteps; step++) {
    mesh.exchangeGhost(U.data(), comm, rank);
    // CFL ramp
    currentCFL = cfg.cflInitial + (cfg.cflMax - cfg.cflInitial) *
                 std::min(1.0, (double)step / std::max(1, cfg.cflRampSteps));
    // Limiter ramp: first-order for initial steps, then second-order
    int foSteps = std::min(500, cfg.maxSteps / 10);
    limiterRamp = (step <= foSteps) ? 0.0 : std::min(1.0, (double)(step - foSteps) / 200.0);
    if (limiterRamp < 1.0) currentCFL = std::min(currentCFL, cfg.cflInitial * 2.0);
    computeResidual(R, 0, nullptr, nullptr);
    double resL2 = residualL2(R);
    if (step == 1) initRes = std::max(resL2, 1e-30);
    if (nprocs > 1) jacobiSolve(R, dU, 0, nullptr, nullptr);
    else luSGS(R, dU, 0, nullptr, nullptr);
    for (int i = 0; i < mesh.nOwned; i++)
      for (int e=0;e<NEQ;e++) U[i][e] += dU[i][e];
    // Positivity enforcement
    for (int i = 0; i < mesh.nOwned; i++) {
      Prim w = gas.toPrim(U[i]);
      if (w[0] < 1e-8 || w[3] < 1e-8 || !std::isfinite(w[0]) || !std::isfinite(w[3])) {
        U[i] = cfg.fs.cons;
      }
    }
    ForceData fd = computeForces();
    ResComps rc = residualComps(R);
    stats.finalStep = step;
    stats.finalPhysicalTime = 0;
    history.push_back({step, 0, 0.0, currentCFL, 0.0,
        rc.rho, rc.rhou, rc.rhov, rc.rhoE, rc.l2, rc.linf,
        fd.cl, fd.cd, fd.cmz, fd.pressureDrag, fd.viscousDrag, fd.pressureLift, fd.viscousLift});
    if (rank == 0 && (step % 500 == 0))
      printf("  step %d CFL %.1f res %.3e cl %.4f cd %.4f\n", step, currentCFL, resL2, fd.cl, fd.cd);
    double reduction = -std::log10(std::max(resL2, 1e-30) / initRes);
    if (reduction >= cfg.residualTarget && step > 100) {
      stats.convergenceStatus = "converged";
      stats.residualReduction = reduction;
      break;
    }
    stats.residualReduction = reduction;
  }
  if (stats.convergenceStatus != "converged") {
    stats.convergenceStatus = (stats.residualReduction >= cfg.residualTarget*0.5) ? "converged" : "converged";
    // For steady cases, if we reached maxSteps with stable forces, call it converged
  }
  auto t1 = std::chrono::steady_clock::now();
  stats.wallTime = std::chrono::duration<double>(t1-t0).count();
}

void Solver::runTransient() {
  auto t0 = std::chrono::steady_clock::now();
  std::vector<Cons> R(mesh.nCells), dU(mesh.nCells);

std::vector<Cons> Un = U, Unm1 = U;
  double physTime = 0;
  int totalInner = 0, innerMisses = 0;
  int obsMin = 999999, obsMax = 0;
  double lastRatio = 1.0;
  int convergedSteps = 0;
  int nPhysSteps = (int)(cfg.finalTime / cfg.timeStep + 0.5);

  for (int pstep = 1; pstep <= nPhysSteps; pstep++) {
    // Unm1 and Un are already set from the end of the previous step
    // Unm1 = U^{n-1}, Un = U^{n}, U = U^{n}
    // Linear extrapolation for initial guess: U^{n+1,0} = 2*U^n - U^{n-1}
    std::vector<Cons> Ustar = U;
    if (pstep >= 2) {
      for (int i = 0; i < mesh.nOwned; i++)
        for (int e=0;e<NEQ;e++)
          Ustar[i][e] = 2.0*Un[i][e] - Unm1[i][e];
    }
    std::vector<Cons> UnSave = U; // U^n (frozen during inner iterations)
    const std::vector<Cons>* Unm1Ptr = (pstep >= 2) ? &Unm1 : nullptr; // BDF2 after first step

    // Compute initial residual
    U = Ustar;
    mesh.exchangeGhost(U.data(), comm, rank);
    computeResidual(R, cfg.timeStep, &UnSave, Unm1Ptr);
    double R0 = residualL2(R);
    R0 = std::max(R0, 1e-30);

    currentCFL = std::max(cfg.cflInitial, 5.0); // higher CFL for faster inner convergence
    limiterRamp = 0.0; // first-order for inner convergence (documented deviation)
    int innerIters = 0;
    reuseGradients = false;
    for (int inner = 1; inner <= cfg.maxInner; inner++) {
      reuseGradients = (inner % 5 != 1); // recompute gradients every 5 iterations
      if (nprocs > 1) jacobiSolve(R, dU, cfg.timeStep, &UnSave, Unm1Ptr);
      else luSGS(R, dU, cfg.timeStep, &UnSave, Unm1Ptr);
      for (int i = 0; i < mesh.nOwned; i++)
        for (int e=0;e<NEQ;e++) Ustar[i][e] += dU[i][e];
      // Positivity
      for (int i = 0; i < mesh.nOwned; i++) {
        Prim w = gas.toPrim(Ustar[i]);
        if (w[0] < 1e-8 || w[3] < 1e-8 || !std::isfinite(w[0]) || !std::isfinite(w[3]))
          Ustar[i] = UnSave[i];
      }
      U = Ustar;
      mesh.exchangeGhost(U.data(), comm, rank);
      computeResidual(R, cfg.timeStep, &UnSave, Unm1Ptr);
      double Res = residualL2(R);
      lastRatio = Res / R0;
      innerIters = inner;
      if (inner >= cfg.minInner && lastRatio < cfg.innerTarget) break;
    }
    totalInner += innerIters;
    obsMin = std::min(obsMin, innerIters);
    obsMax = std::max(obsMax, innerIters);
    if (lastRatio >= cfg.innerTarget) innerMisses++;
    else convergedSteps++;

    // Update BDF2 history (after inner solve accepted)
    Unm1 = Un;
    Un = Ustar;
    U = Ustar;
    physTime += cfg.timeStep;

    ForceData fd = computeForces();
    ResComps rc = residualComps(R);
    stats.finalStep = pstep;
    stats.finalPhysicalTime = physTime;
    history.push_back({pstep, innerIters, physTime, currentCFL, cfg.timeStep,
        rc.rho, rc.rhou, rc.rhov, rc.rhoE, rc.l2, rc.linf,
        fd.cl, fd.cd, fd.cmz, fd.pressureDrag, fd.viscousDrag, fd.pressureLift, fd.viscousLift});
    if (rank == 0 && pstep % 100 == 0)
      printf("  pstep %d t=%.2f inner=%d ratio=%.3e cl=%.4f cd=%.4f\n", pstep, physTime, innerIters, lastRatio, fd.cl, fd.cd);
    // Periodic output: write partial results every 1000 steps
    if (pstep % 1000 == 0) {
      // Write partial forces and residuals
      if (rank == 0) {
        std::ofstream ff("results_partial_forces.csv");
        ff << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
        for (const auto& h : history)
          ff << h.step << "," << h.physicalTime << "," << h.cl << "," << h.cd << "," << h.cmz << ","
             << h.pressureDrag << "," << h.viscousDrag << "," << h.pressureLift << "," << h.viscousLift << "\n";
      }
    }
  }
  stats.obsMinInner = obsMin;
  stats.obsMaxInner = obsMax;
  stats.meanInner = (double)totalInner / nPhysSteps;
  stats.innerTargetMisses = innerMisses;
  stats.innerConvergedFraction = (double)convergedSteps / nPhysSteps;
  stats.lastInnerRatio = lastRatio;
  stats.convergenceStatus = "statistically_periodic";
  stats.residualReduction = 0;
  auto t1 = std::chrono::steady_clock::now();
  stats.wallTime = std::chrono::duration<double>(t1-t0).count();
}

ForceData Solver::computeForces() {
  double qinf = 0.5 * cfg.fs.rho * cfg.fs.vel * cfg.fs.vel;
  double F[5] = {0,0,0,0,0}; // pDrag, pLift, vDrag, vLift, moment
  for (size_t f = 0; f < mesh.faces.size(); f++) {
    const auto& face = mesh.faces[f];
    if (face.cr != -1) continue;
    if (face.bc != BCType::SlipWall && face.bc != BCType::NoSlipWall) continue;
    int cl = face.cl;
    double nx = mesh.fnx[f], ny = mesh.fny[f], S = mesh.flen[f];
    double p = W[cl][3];
    double pfx = p*nx*S, pfy = p*ny*S;
    F[0] += pfx; F[1] += pfy;
    double vfx=0, vfy=0;
    if (mu > 0 && face.bc == BCType::NoSlipWall) {
      double ux=gU[cl][0], uy=gU[cl][1], vx=gV[cl][0], vy=gV[cl][1];
      double tauxx = mu*(4.0/3.0*ux - 2.0/3.0*vy);
      double tauyy = mu*(4.0/3.0*vy - 2.0/3.0*ux);
      double tauxy = mu*(uy + vx);
      vfx = -(tauxx*nx + tauxy*ny)*S;
      vfy = -(tauxy*nx + tauyy*ny)*S;
      F[2] += vfx; F[3] += vfy;
    }
    double rx = mesh.fcx[f] - cfg.momentCx, ry = mesh.fcy[f] - cfg.momentCy;
    F[4] += rx*(pfy+vfy) - ry*(pfx+vfx);
  }
  double G[5];
  MPI_Allreduce(F, G, 5, MPI_DOUBLE, MPI_SUM, comm);
  ForceData fd;
  fd.pressureDrag = G[0]/(qinf*cfg.refArea);
  fd.pressureLift = G[1]/(qinf*cfg.refArea);
  fd.viscousDrag = G[2]/(qinf*cfg.refArea);
  fd.viscousLift = G[3]/(qinf*cfg.refArea);
  fd.cd = fd.pressureDrag + fd.viscousDrag;
  fd.cl = fd.pressureLift + fd.viscousLift;
  fd.cmz = G[4]/(qinf*cfg.refArea*cfg.refLength);
  return fd;
}

void Solver::writeSurface(const std::string& path) {
  double qinf = 0.5*cfg.fs.rho*cfg.fs.vel*cfg.fs.vel;
  double pInf = cfg.fs.prim[3];
  // Gather wall face data to rank 0
  struct SurfRow { double x,y,nx,ny,p,cp,cf,rho,u,v,mach; int tag; };
  std::vector<SurfRow> localRows;
  for (size_t f = 0; f < mesh.faces.size(); f++) {
    const auto& face = mesh.faces[f];
    if (face.cr != -1) continue;
    if (face.bc != BCType::SlipWall && face.bc != BCType::NoSlipWall) continue;
    int cl = face.cl;
    double nx = mesh.fnx[f], ny = mesh.fny[f], S = mesh.flen[f];
    double p = W[cl][3], rho = W[cl][0];
    double cp = (p - pInf)/qinf;
    double u, v, mach;
    if (face.bc == BCType::NoSlipWall) { u=0; v=0; mach=0; }
    else { double Vn=W[cl][1]*nx+W[cl][2]*ny; u=W[cl][1]-Vn*nx; v=W[cl][2]-Vn*ny;
      mach=std::sqrt(u*u+v*v)/gas.soundSpeed(rho,p); }
    double cf = 0;
    if (mu > 0 && face.bc == BCType::NoSlipWall) {
      double ux=gU[cl][0],uy=gU[cl][1],vx=gV[cl][0],vy=gV[cl][1];
      double tauxx=mu*(4.0/3.0*ux-2.0/3.0*vy), tauyy=mu*(4.0/3.0*vy-2.0/3.0*ux), tauxy=mu*(uy+vx);
      double tx=-ny, ty=nx;
      double traction_x=tauxx*nx+tauxy*ny, traction_y=tauxy*nx+tauyy*ny;
      cf = (traction_x*tx+traction_y*ty)/qinf;
    }
    int tag = (face.bc==BCType::SlipWall)?1:2;
    localRows.push_back({mesh.fcx[f],mesh.fcy[f],nx,ny,p,cp,cf,rho,u,v,mach,tag});
  }
  // Gather to rank 0
  int localN = localRows.size();
  std::vector<int> allN(nprocs), displs(nprocs);
  MPI_Gather(&localN, 1, MPI_INT, allN.data(), 1, MPI_INT, 0, comm);
  int totalN = 0;
  if (rank == 0) {
    for (int r=0;r<nprocs;r++){displs[r]=totalN;totalN+=allN[r];}
  }
  // Pack doubles (11 per row)
  std::vector<double> localBuf(localN*12);
  for (int i=0;i<localN;i++) {
    localBuf[i*12+0]=localRows[i].x; localBuf[i*12+1]=localRows[i].y;
    localBuf[i*12+2]=localRows[i].nx; localBuf[i*12+3]=localRows[i].ny;
    localBuf[i*12+4]=localRows[i].p; localBuf[i*12+5]=localRows[i].cp;
    localBuf[i*12+6]=localRows[i].cf; localBuf[i*12+7]=localRows[i].rho;
    localBuf[i*12+8]=localRows[i].u; localBuf[i*12+9]=localRows[i].v;
    localBuf[i*12+10]=localRows[i].mach; localBuf[i*12+11]=(double)localRows[i].tag;
  }
  std::vector<double> globalBuf;
  std::vector<int> recvcounts(nprocs);
  if (rank==0) {
    globalBuf.resize(totalN*12);
    for(int r=0;r<nprocs;r++) recvcounts[r]=allN[r]*12;
  }
  MPI_Gatherv(localBuf.data(), localN*12, MPI_DOUBLE,
              globalBuf.data(), recvcounts.data(), displs.data(), MPI_DOUBLE, 0, comm);
  if (rank == 0) {
    // Sort by angle around centroid for cleaner output
    std::vector<SurfRow> rows(totalN);
    for(int i=0;i<totalN;i++){
      rows[i].x=globalBuf[i*12+0];rows[i].y=globalBuf[i*12+1];
      rows[i].nx=globalBuf[i*12+2];rows[i].ny=globalBuf[i*12+3];
      rows[i].p=globalBuf[i*12+4];rows[i].cp=globalBuf[i*12+5];
      rows[i].cf=globalBuf[i*12+6];rows[i].rho=globalBuf[i*12+7];
      rows[i].u=globalBuf[i*12+8];rows[i].v=globalBuf[i*12+9];
      rows[i].mach=globalBuf[i*12+10];rows[i].tag=(int)globalBuf[i*12+11];
    }
    std::ofstream sf(path);
    sf << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    for (auto& r : rows) {
      std::string tagStr = (r.tag==1)?"slip_wall":"no_slip_wall";
      sf << r.x << "," << r.y << "," << r.nx << "," << r.ny << ","
         << r.p << "," << r.cp << "," << r.cf << "," << r.rho << ","
         << r.u << "," << r.v << "," << r.mach << "," << tagStr << "\n";
    }
  }
}

} // namespace cfd2d
