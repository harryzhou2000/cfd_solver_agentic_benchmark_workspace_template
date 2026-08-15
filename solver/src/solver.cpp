#include "solver.hpp"
#include "output.hpp"
#include <mpi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cfd {
namespace {

std::string fmtTime(double t) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(3) << t;
  return os.str();
}

struct Solver {
  const Case& c;
  const LocalMesh& m;
  const GasModel& gas;
  const int rank, nRanks;
  const LsqStencil stencil;
  std::vector<State> U, dU, R;
  std::vector<Prim> W, gradDx, gradDy;
  std::vector<double> limiter, dtPseudo, convSpec, viscSpec;
  std::vector<Vec2> netNormal;
  State UInf; Prim WInf;
  double mu = 0.0, kappa = 0.0, qInf = 0.0;
  bool laminar = false;
  std::vector<double> wallGradUx, wallGradUy, wallGradVx, wallGradVy;
  std::vector<double> wallGradTx, wallGradTy;
  std::vector<double> wallP, wallRho, wallU, wallV;
  std::vector<State> Un, Unm1;
  double physTime = 0.0, dtPhys = 0.0;
  long physStep = 0;
  SolveStats stats;
  std::vector<std::vector<double>> sendBufs, recvBufs;
  std::vector<MPI_Request> reqs;
  struct ForceAccum { double pDrag=0,vDrag=0,pLift=0,vLift=0,moment=0; void reset() { pDrag=vDrag=pLift=vLift=moment=0; } };

  Solver(const Case& cIn, const LocalMesh& mIn, int rankIn, int nRanksIn)
      : c(cIn), m(mIn), gas(cIn.gas), rank(rankIn), nRanks(nRanksIn),
        stencil(buildLsqStencil(mIn)) {
    const int n = m.numLocal();
    U.assign(n, State{}); W.assign(n, Prim{});
    gradDx.assign(n, Prim{}); gradDy.assign(n, Prim{});
    limiter.assign(n, 1.0); dU.assign(n, State{});
    R.resize(m.nOwned); dtPseudo.resize(m.nOwned);
    convSpec.resize(m.nOwned); viscSpec.resize(m.nOwned);
    netNormal.assign(m.nOwned, Vec2{0,0});
    WInf = {c.freestream.rho, c.freestream.u, c.freestream.v, c.freestream.pressure};
    UInf = toConservative(WInf, gas); qInf = c.qInf();
    laminar = c.isLaminar();
    if (laminar) {
      mu = c.freestream.rho * c.freestream.velocity_magnitude *
           c.reference.reynolds_length / c.reynolds;
      kappa = mu * gas.cp() / gas.prandtl;
    }
    dtPhys = c.run.time_step;
    for (int i = 0; i < m.nOwned; ++i)
      for (size_t k = 0; k < m.cellFaces[i].size(); ++k) {
        const int fi = m.cellFaces[i][k];
        const double s = (m.cellFaceSign[i][k] > 0) ? 1.0 : -1.0;
        netNormal[i][0] += s * m.faces[fi].normal[0] * m.faces[fi].len;
        netNormal[i][1] += s * m.faces[fi].normal[1] * m.faces[fi].len;
      }
    const size_t nw = m.wallFaceIdx.size();
    wallGradUx.assign(nw,0); wallGradUy.assign(nw,0);
    wallGradVx.assign(nw,0); wallGradVy.assign(nw,0);
    wallGradTx.assign(nw,0); wallGradTy.assign(nw,0);
    wallP.assign(nw,0); wallRho.assign(nw,0); wallU.assign(nw,0); wallV.assign(nw,0);
  }

  void exchangeData(const std::vector<double>& data, int ncomp, int tag,
                    std::vector<double>& out) {
    out.assign(m.numLocal() * ncomp, 0.0);
    reqs.clear(); 
    const size_t nbr = m.neighborRanks.size();
    sendBufs.resize(nbr);
    recvBufs.resize(nbr);
    for (size_t k = 0; k < nbr; ++k) {
      const auto& sc = m.sendCells[k];
      const auto& rc = m.recvCells[k];
      sendBufs[k].assign(sc.size() * ncomp, 0.0);
      recvBufs[k].assign(rc.size() * ncomp, 0.0);
      double* snd = sendBufs[k].data(), *rcv = recvBufs[k].data();
      for (size_t i = 0; i < sc.size(); ++i)
        for (int j = 0; j < ncomp; ++j)
          snd[i*ncomp+j] = data[sc[i]*ncomp+j];
      MPI_Request r1, r2;
      MPI_Isend(snd, (int)sendBufs[k].size(), MPI_DOUBLE,
                m.neighborRanks[k], tag, MPI_COMM_WORLD, &r1);
      MPI_Irecv(rcv, (int)recvBufs[k].size(), MPI_DOUBLE,
                m.neighborRanks[k], tag, MPI_COMM_WORLD, &r2);
      reqs.push_back(r1); reqs.push_back(r2);
    }
    if (!reqs.empty())
      MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (size_t k = 0; k < nbr; ++k)
      for (size_t i = 0; i < m.recvCells[k].size(); ++i)
        for (int j = 0; j < ncomp; ++j)
          out[m.recvCells[k][i]*ncomp+j] = recvBufs[k][i*ncomp+j];
  }

  void updateGhosts() {
    std::vector<double> buf(m.numLocal()*4);
    for (int i = 0; i < m.numLocal(); ++i)
      for (int j = 0; j < 4; ++j) buf[i*4+j] = U[i][j];
    std::vector<double> outbuf(m.numLocal()*4, 0.0);
    exchangeData(buf, 4, 100, outbuf);
    for (int i = m.nOwned; i < m.numLocal(); ++i) {
      U[i] = {outbuf[i*4], outbuf[i*4+1], outbuf[i*4+2], outbuf[i*4+3]};
      W[i] = toPrimitive(U[i], gas);
    }
  }

  void updateGradGhosts() {
    std::vector<double> buf(m.numLocal()*9);
    for (int i = 0; i < m.numLocal(); ++i) {
      for (int j = 0; j < 4; ++j) buf[i*9+j] = gradDx[i][j];
      for (int j = 0; j < 4; ++j) buf[i*9+4+j] = gradDy[i][j];
      buf[i*9+8] = limiter[i];
    }
    std::vector<double> outbuf2(m.numLocal()*9, 0.0);
    exchangeData(buf, 9, 101, outbuf2);
    for (int i = m.nOwned; i < m.numLocal(); ++i) {
      for (int j = 0; j < 4; ++j) gradDx[i][j] = outbuf2[i*9+j];
      for (int j = 0; j < 4; ++j) gradDy[i][j] = outbuf2[i*9+4+j];
      limiter[i] = outbuf2[i*9+8];
    }
  }

  void updateDUGhosts() {
    std::vector<double> buf(m.numLocal()*4);
    for (int i = 0; i < m.numLocal(); ++i)
      for (int j = 0; j < 4; ++j) buf[i*4+j] = dU[i][j];
    std::vector<double> outbuf3(m.numLocal()*4, 0.0);
    exchangeData(buf, 4, 102, outbuf3);
    for (int i = m.nOwned; i < m.numLocal(); ++i)
      dU[i] = {outbuf3[i*4], outbuf3[i*4+1], outbuf3[i*4+2], outbuf3[i*4+3]};
  }

  void updatePrimitives() {
    for (int i = 0; i < m.nOwned; ++i) W[i] = toPrimitive(U[i], gas);
  }

  void initFreestream() {
    for (int i = 0; i < m.numLocal(); ++i) { U[i] = UInf; W[i] = WInf; }
    updateGhosts();
  }

  Prim reconFace(int i, const Vec2& xf) const {
    double dx = xf[0] - m.centroid[i][0], dy = xf[1] - m.centroid[i][1];
    double l = limiter[i];
    Prim w = {W[i][0] + l*(gradDx[i][0]*dx+gradDy[i][0]*dy),
              W[i][1] + l*(gradDx[i][1]*dx+gradDy[i][1]*dy),
              W[i][2] + l*(gradDx[i][2]*dx+gradDy[i][2]*dy),
              W[i][3] + l*(gradDx[i][3]*dx+gradDy[i][3]*dy)};
    if (w[0] <= 1e-10 || w[3] <= 1e-10) w = W[i];
    return w;
  }

  void assembleResidual(ForceAccum& forces, bool wallData) {
    const double diss = c.run.rusanov_dissipation_scale;
    std::fill(R.begin(), R.end(), State{0,0,0,0});
    forces.reset();
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
      const auto& f = m.faces[fi];
      if (f.c1 >= 0) {
        const Vec2& n = f.normal;
        State UL = toConservative(reconFace(f.c0, f.centroid), gas);
        State UR = toConservative(reconFace(f.c1, f.centroid), gas);
        double lam; State F = roeFlux(UL, UR, gas, n[0], n[1], diss, lam);
        if (laminar) {
          Prim wl = toPrimitive(UL,gas), wr = toPrimitive(UR,gas);
          Prim gx = {0.5*(gradDx[f.c0][0]+gradDx[f.c1][0]),
                     0.5*(gradDx[f.c0][1]+gradDx[f.c1][1]),
                     0.5*(gradDx[f.c0][2]+gradDx[f.c1][2]),
                     0.5*(gradDx[f.c0][3]+gradDx[f.c1][3])};
          Prim gy = {0.5*(gradDy[f.c0][0]+gradDy[f.c1][0]),
                     0.5*(gradDy[f.c0][1]+gradDy[f.c1][1]),
                     0.5*(gradDy[f.c0][2]+gradDy[f.c1][2]),
                     0.5*(gradDy[f.c0][3]+gradDy[f.c1][3])};
          double dx = m.centroid[f.c1][0]-m.centroid[f.c0][0];
          double dy = m.centroid[f.c1][1]-m.centroid[f.c0][1];
          double dl = std::sqrt(dx*dx+dy*dy);
          if (dl > 1e-30) { dx /= dl; dy /= dl;
            for (int k = 0; k < 4; ++k) {
              double gn = gx[k]*dx+gy[k]*dy;
              double corr = (wr[k]-wl[k])/dl-gn;
              gx[k] += corr*dx; gy[k] += corr*dy;
            }
          }
          double rhoL = wl[0];
          double Tx = (gx[3]*rhoL-wl[3]*gx[0])/(rhoL*rhoL*gas.R);
          double Ty = (gy[3]*rhoL-wl[3]*gy[0])/(rhoL*rhoL*gas.R);
          State Fv = viscousFlux(wl[1],wl[2],gx[1],gy[1],gx[2],gy[2],Tx,Ty,mu,kappa,n[0],n[1]);
          for (int j = 0; j < 4; ++j) F[j] += Fv[j];
        }
        if (f.c0 < m.nOwned) for (int j = 0; j < 4; ++j) R[f.c0][j] += F[j]*f.len;
        if (f.c1 < m.nOwned) for (int j = 0; j < 4; ++j) R[f.c1][j] -= F[j]*f.len;
      } else {
        const Vec2& n = f.normal; const int i = f.c0;
        const Prim wf = reconFace(i, f.centroid);
        if (f.bc == (int)BcType::Farfield) {
          Prim ghost = farfieldGhost(wf, c.freestream, gas, n[0], n[1]);
          double lam; State F = roeFlux(toConservative(wf,gas),
                                        toConservative(ghost,gas), gas, n[0], n[1], diss, lam);
          for (int j = 0; j < 4; ++j) R[i][j] += F[j]*f.len;
        } else {
          double pw = wf[3]; State F = {0, pw*n[0], pw*n[1], 0};
          if (i < m.nOwned) for (int j = 0; j < 4; ++j) R[i][j] += F[j]*f.len;
          size_t wi = wallIndex((int)fi);
          if (f.bc == (int)BcType::NoSlipAdiabaticWall && laminar) {
            double ux,uy,vx,vy,Tx,Ty; ux=uy=vx=vy=Tx=Ty=0;
            double h = std::fabs((f.centroid[0]-m.centroid[i][0])*n[0]+
                                 (f.centroid[1]-m.centroid[i][1])*n[1]);
            if (h > 1e-30) {
              double uf = wf[1], vf = wf[2];
              double gUn = gradDx[i][1]*n[0]+gradDy[i][1]*n[1];
              double gVn = gradDx[i][2]*n[0]+gradDy[i][2]*n[1];
              ux = gradDx[i][1]-(uf/h+gUn)*n[0];
              uy = gradDy[i][1]-(uf/h+gUn)*n[1];
              vx = gradDx[i][2]-(vf/h+gVn)*n[0];
              vy = gradDy[i][2]-(vf/h+gVn)*n[1];
              double rhoL = wf[0];
              double gTx = (gradDx[i][3]*rhoL-wf[3]*gradDx[i][0])/(rhoL*rhoL*gas.R);
              double gTy = (gradDy[i][3]*rhoL-wf[3]*gradDy[i][0])/(rhoL*rhoL*gas.R);
              double gTn = gTx*n[0]+gTy*n[1]; Tx = gTx-gTn*n[0]; Ty = gTy-gTn*n[1];
            }
            State Fv = viscousFlux(0,0,ux,uy,vx,vy,Tx,Ty,mu,kappa,n[0],n[1]);
            for (int j = 0; j < 4; ++j) R[i][j] += Fv[j]*f.len;
            if (wallData && wi < wallGradUx.size()) {
              wallGradUx[wi]=ux; wallGradUy[wi]=uy;
              wallGradVx[wi]=vx; wallGradVy[wi]=vy;
              wallGradTx[wi]=Tx; wallGradTy[wi]=Ty;
            }
          }
          if (wallData && wi < wallP.size()) {
            wallP[wi]=pw; wallRho[wi]=wf[0];
            if (f.bc == (int)BcType::NoSlipAdiabaticWall) { wallU[wi]=0; wallV[wi]=0; }
            else { double vn = wf[1]*n[0]+wf[2]*n[1];
                   wallU[wi]=wf[1]-vn*n[0]; wallV[wi]=wf[2]-vn*n[1]; }
          }
          forces.pDrag += -pw*n[0]*f.len; forces.pLift += -pw*n[1]*f.len;
          forces.moment += (f.centroid[0]-c.reference.moment_center[0])*(-pw*n[1]*f.len) -
                           (f.centroid[1]-c.reference.moment_center[1])*(-pw*n[0]*f.len);
          if (laminar && f.bc == (int)BcType::NoSlipAdiabaticWall && wi < wallGradUx.size()) {
            double txx,txy,tyy;
            stress(wallGradUx[wi],wallGradUy[wi],wallGradVx[wi],wallGradVy[wi],mu,txx,txy,tyy);
            double tx = txx*n[0]+txy*n[1], ty = txy*n[0]+tyy*n[1], tn = tx*n[0]+ty*n[1];
            tx -= tn*n[0]; ty -= tn*n[1];
            forces.vDrag += tx*f.len; forces.vLift += ty*f.len;
            forces.moment += (f.centroid[0]-c.reference.moment_center[0])*(ty*f.len) -
                             (f.centroid[1]-c.reference.moment_center[1])*(tx*f.len);
          }
        }
      }
    }
  }

  size_t wallIndex(int fi) const {
    for (size_t k = 0; k < m.wallFaceIdx.size(); ++k)
      if (m.wallFaceIdx[k] == fi) return k;
    return (size_t)-1;
  }

  void residualNorms(double& l2, double& linf, std::array<double,4>& comp) {
    std::array<double,4> loc = {0,0,0,0}; double linfLoc = 0;
    for (int i = 0; i < m.nOwned; ++i)
      for (int k = 0; k < 4; ++k) {
        loc[k] += R[i][k]*R[i][k];
        linfLoc = std::max(linfLoc, std::fabs(R[i][k]));
      }
    MPI_Allreduce(MPI_IN_PLACE, loc.data(), 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, &linfLoc, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    double s = 0; for (int k = 0; k < 4; ++k) { comp[k] = std::sqrt(loc[k]); s += loc[k]; }
    l2 = std::sqrt(s); linf = linfLoc;
  }

  void computeSpectralRadii(double cfl) {
    const double cvisc = std::max(4.0/3.0, gas.gamma/gas.prandtl);
    for (int i = 0; i < m.nOwned; ++i) {
      double lc = 0, lv = 0;
      double rhoInv = 1.0/std::max(W[i][0],1e-30);
      double a = soundSpeed(W[i],gas);
      for (size_t k = 0; k < m.cellFaces[i].size(); ++k) {
        const int fi = m.cellFaces[i][k]; const auto& f = m.faces[fi];
        const double s = (m.cellFaceSign[i][k] > 0) ? 1.0 : -1.0;
        const double vn = s*(W[i][1]*f.normal[0]+W[i][2]*f.normal[1]);
        lc += (std::fabs(vn)+a)*f.len;
        if (laminar) lv += cvisc*mu*rhoInv*f.len*f.len;
      }
      convSpec[i] = lc/m.volume[i];
      viscSpec[i] = laminar ? lv/m.volume[i] : 0;
      dtPseudo[i] = cfl/std::max(convSpec[i]+viscSpec[i], 1e-30);
    }
  }

  void lusgsSweep(bool forward) {
    const double diss = c.run.rusanov_dissipation_scale;
    std::vector<Mat44> D(m.nOwned);
    for (int i = 0; i < m.nOwned; ++i) {
      double lamSum = 0; double a = soundSpeed(W[i],gas);
      for (size_t k = 0; k < m.cellFaces[i].size(); ++k) {
        const int fi = m.cellFaces[i][k]; const auto& f = m.faces[fi];
        const double s = (m.cellFaceSign[i][k] > 0) ? 1.0 : -1.0;
        lamSum += (std::fabs(s*(W[i][1]*f.normal[0]+W[i][2]*f.normal[1]))+a)*f.len;
      }
      double viscDiag = laminar ? viscSpec[i]*m.volume[i] : 0;
      double dtInv = m.volume[i]/std::max(dtPseudo[i], 1e-30);
      if (physStep > 0) {
        double beta = (physStep <= 1) ? 1.0 : 1.5;
        dtInv += beta*m.volume[i]/std::max(dtPhys,1e-30);
      }
      double dval = dtInv + 0.5*lamSum + viscDiag;
      for (int r = 0; r < 4; ++r)
        for (int cc = 0; cc < 4; ++cc) D[i][r][cc] = (r==cc) ? dval : 0;
    }
    int lo = forward ? 0 : m.nOwned-1, hi = forward ? m.nOwned-1 : 0, step = forward ? 1 : -1;
    for (int i = lo; i != hi+step; i += step) {
      State rhs = {-R[i][0], -R[i][1], -R[i][2], -R[i][3]};
      for (size_t k = 0; k < m.cellFaces[i].size(); ++k) {
        const int fi = m.cellFaces[i][k]; const auto& f = m.faces[fi];
        int j = (f.c0 == i) ? f.c1 : f.c0; if (j < 0) continue;
        double s = (f.c0 == i) ? 1.0 : -1.0, nx = s*f.normal[0], ny = s*f.normal[1];
        Prim wi = W[i], wj = toPrimitive(U[j],gas);
        double lam = std::max(std::fabs(wi[1]*nx+wi[2]*ny)+soundSpeed(wi,gas),
                              std::fabs(wj[1]*nx+wj[2]*ny)+soundSpeed(wj,gas));
        State aj = jacobianApply(U[j], dU[j], gas, nx, ny);
        double h = 0.5*diss;
        for (int c = 0; c < 4; ++c) rhs[c] -= h*(aj[c]-lam*dU[j][c])*f.len;
      }
      dU[i] = solve44(D[i], rhs);
    }
  }

  void innerSweeps() { lusgsSweep(true); lusgsSweep(false); }

  void applyUpdate() {
    for (int i = 0; i < m.nOwned; ++i)
      for (int j = 0; j < 4; ++j) U[i][j] += dU[i][j];
    updatePrimitives(); updateGhosts();
  }

  void assembleTransientResidual(ForceAccum& forces, bool wallData) {
    assembleResidual(forces, wallData);
    double beta = (physStep <= 1) ? 1.0 : 1.5;
    for (int i = 0; i < m.nOwned; ++i) {
      double vdt = m.volume[i]/dtPhys;
      if (physStep <= 1)
        for (int k = 0; k < 4; ++k) R[i][k] += vdt*(U[i][k]-Un[i][k]);
      else
        for (int k = 0; k < 4; ++k)
          R[i][k] += vdt*(1.5*U[i][k]-2.0*Un[i][k]+0.5*Unm1[i][k]);
    }
  }

  void buildWallRows(OutputData& out) {
    out.wall.clear();
    for (size_t k = 0; k < m.wallFaceIdx.size(); ++k) {
      const int fi = m.wallFaceIdx[k]; const auto& f = m.faces[fi];
      double p = wallP[k], rho = wallRho[k], cp = (p-c.freestream.pressure)/qInf, cf = 0;
      if (laminar && f.bc == (int)BcType::NoSlipAdiabaticWall) {
        double txx,txy,tyy;
        stress(wallGradUx[k],wallGradUy[k],wallGradVx[k],wallGradVy[k],mu,txx,txy,tyy);
        double tx = txx*f.normal[0]+txy*f.normal[1];
        double ty = txy*f.normal[0]+tyy*f.normal[1];
        double tn = tx*f.normal[0]+ty*f.normal[1];
        cf = std::sqrt((tx-tn*f.normal[0])*(tx-tn*f.normal[0])+
                       (ty-tn*f.normal[1])*(ty-tn*f.normal[1]))/qInf;
      }
      double a = soundSpeed({rho, wallU[k], wallV[k], p}, gas);
      double mach = (rho > 0) ? std::sqrt(wallU[k]*wallU[k]+wallV[k]*wallV[k])/a : 0;
      out.wall.push_back({f.centroid[0], f.centroid[1], f.normal[0], f.normal[1],
                         p, cp, cf, rho, wallU[k], wallV[k], mach,
                         f.tag.empty()?"wall":f.tag});
    }
  }
};

} // anonymous namespace

SolveResult runSolver(const Case& c, const LocalMesh& mesh, int rank, int nRanks,
                      const std::string& outDir, const std::string& restartPath,
                      const std::string& commandLine, const std::string& reportLevel) {
  (void)reportLevel;
  auto t0 = std::chrono::steady_clock::now();
  Solver s(c, mesh, rank, nRanks); SolveResult result;
  s.Un.assign(mesh.numLocal(), s.UInf); s.Unm1.assign(mesh.numLocal(), s.UInf);

  if (!restartPath.empty()) {
    std::vector<State> global;
    if (rank == 0) {
      std::ifstream f(restartPath, std::ios::binary);
      if (!f.is_open()) throw std::runtime_error("cannot open restart: "+restartPath);
      char magic[9]; f.read(magic,8); int version; long step,nCells; double time;
      f.read((char*)&version,sizeof(int)); f.read((char*)&step,sizeof(long));
      f.read((char*)&time,sizeof(double)); f.read((char*)&nCells,sizeof(long));
      global.resize(nCells); f.read((char*)global.data(),nCells*sizeof(State));
    }
    long nCells = 0; if (rank == 0) nCells = global.size();
    MPI_Bcast(&nCells,1,MPI_LONG,0,MPI_COMM_WORLD);
    if (rank != 0) global.resize(nCells);
    MPI_Bcast(global.data(),(int)(nCells*4),MPI_DOUBLE,0,MPI_COMM_WORLD);
    for (int i = 0; i < mesh.nOwned; ++i) s.U[i] = global[mesh.globalId[i]];
    s.updatePrimitives(); s.updateGhosts();
  } else s.initFreestream();

  OutputData out; out.edgeCut = mesh.edgeCut;
  out.numCellsGlobal = mesh.numCellsGlobal;
  out.numFacesGlobal = mesh.numFacesGlobal;
  gatherPartitionStats(mesh, rank, nRanks, out);

  if (c.run.type == "transient") {
    const long nSteps = (long)std::llround(c.run.final_time/c.run.time_step);
    const long fieldEvery = (c.outputs.write_field_every_time > 0) ?
        (long)std::llround(c.outputs.write_field_every_time/c.run.time_step) : 0;
    s.physStep = 0; s.physTime = 0;
    double initialL2 = 0; long misses = 0; double lastRatio = 1;
    std::array<double,4> lastComp = {0,0,0,0}; double lastL2=0, lastLinf=0;

    for (long n = 1; n <= nSteps; ++n) {
      s.physStep = n; s.physTime = n*c.run.time_step;
      std::copy(s.Un.begin(), s.Un.end(), s.Unm1.begin());
      std::copy(s.U.begin(), s.U.end(), s.Un.begin());
      long sweepsDone = 0; bool converged = false; double r0 = 1;
      for (;;) {
        s.updatePrimitives();
        computeGradients(mesh, s.stencil, s.W, s.gradDx, s.gradDy, mesh.nOwned);
        s.limiter = computeLimiter(mesh, s.W, s.gradDx, s.gradDy, mesh.nOwned);
        if ((int)s.limiter.size() < mesh.numLocal()) s.limiter.resize(mesh.numLocal(), 1.0);
        s.updateGradGhosts();
        Solver::ForceAccum fc; s.assembleTransientResidual(fc, n == nSteps);
        double l2, linf; std::array<double,4> comp; s.residualNorms(l2, linf, comp);
        lastL2 = l2; lastLinf = linf; lastComp = comp;
        if (sweepsDone == 0) { r0 = std::max(l2,1e-300); if (n == 1) initialL2 = l2; }
        lastRatio = l2/r0;
        if (sweepsDone >= c.run.min_inner_iterations &&
            lastRatio <= c.run.inner_residual_reduction_target) { converged = true; break; }
        if (sweepsDone >= c.run.max_inner_iterations) break;
        s.computeSpectralRadii(c.run.cfl_max); s.innerSweeps();
        s.applyUpdate(); s.updateDUGhosts(); ++sweepsDone;
      }
      if (!converged) ++misses;
      std::copy(s.Un.begin(), s.Un.end(), s.Unm1.begin());
      std::copy(s.U.begin(), s.U.end(), s.Un.begin());
      if (s.stats.innerMin == 0 || sweepsDone < s.stats.innerMin) s.stats.innerMin = sweepsDone;
      s.stats.innerMax = std::max(s.stats.innerMax, sweepsDone);
      s.stats.innerSum += sweepsDone; s.stats.innerCount++;
      s.stats.innerLastRatio = lastRatio; s.stats.innerTarget = c.run.inner_residual_reduction_target;
      ResidualRow rr; rr.step = n; rr.time = s.physTime; rr.innerIter = (int)sweepsDone;
      rr.cfl = c.run.cfl_max; rr.dt = c.run.time_step;
      rr.rho = lastComp[0]; rr.rhou = lastComp[1];
      rr.rhov = lastComp[2]; rr.rhoE = lastComp[3];
      rr.l2 = lastL2; rr.linf = lastLinf; out.residuals.push_back(rr);
      Solver::ForceAccum fc; s.assembleResidual(fc, false);
      double vals[5] = {fc.pDrag, fc.vDrag, fc.pLift, fc.vLift, fc.moment};
      MPI_Allreduce(MPI_IN_PLACE, vals, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      double denom = s.qInf*c.reference.area;
      ForceRow fr; fr.step = n; fr.time = s.physTime;
      fr.pressureDrag = vals[0]/denom; fr.viscousDrag = vals[1]/denom;
      fr.pressureLift = vals[2]/denom; fr.viscousLift = vals[3]/denom;
      fr.cd = fr.pressureDrag+fr.viscousDrag; fr.cl = fr.pressureLift+fr.viscousLift;
      fr.cmz = vals[4]/(s.qInf*c.reference.area*c.reference.length);
      out.forces.push_back(fr);
      if (fieldEvery > 0 && (n%fieldEvery == 0 || n == nSteps)) {
        gatherFieldData(mesh, s.U, rank, nRanks, out);
        writeOutputs(c, out, rank, nRanks, outDir, s.stats, commandLine,
                     "field_t"+fmtTime(s.physTime)+".vtu", true);
      }
      if (n%500 == 0 && rank == 0)
        std::fprintf(stderr, "step %ld/%ld t=%.2f inner=%ld ratio=%.3e cd=%.7f cl=%.7f\n",
                     n, nSteps, s.physTime, sweepsDone, lastRatio, fr.cd, fr.cl);
      std::fflush(stderr);
    }
    s.stats.finalStep = nSteps; s.stats.finalTime = c.run.final_time;
    s.stats.convergenceStatus = "statistically_periodic";
    s.stats.convergedFraction = s.stats.innerCount > 0 ?
        1.0-(double)misses/s.stats.innerCount : 0.0;
    s.stats.residual0 = initialL2; s.stats.residualFinal = lastL2;
  } else {
    double refL2 = 0, prevL2 = 0; bool converged = false;
    for (long step = 1; step <= c.run.max_steps; ++step) {
      double frac = c.run.pseudo_cfl_ramp_steps > 0 ?
          std::min(1.0,(double)step/c.run.pseudo_cfl_ramp_steps) : 1.0;
      double cfl = c.run.cfl_initial * std::pow(c.run.cfl_max/c.run.cfl_initial, frac);
      if (c.run.cfl_max <= c.run.cfl_initial) cfl = c.run.cfl_max;
      s.updateGhosts(); s.updatePrimitives();
      computeGradients(mesh, s.stencil, s.W, s.gradDx, s.gradDy, mesh.nOwned);
      s.limiter = computeLimiter(mesh, s.W, s.gradDx, s.gradDy, mesh.nOwned);
      if ((int)s.limiter.size() < mesh.numLocal()) s.limiter.resize(mesh.numLocal(), 1.0);
      s.updateGradGhosts();
      Solver::ForceAccum fc; s.assembleResidual(fc, true);
      double l2, linf; std::array<double,4> comp; s.residualNorms(l2, linf, comp);
      if (step == 1) { refL2 = l2; prevL2 = l2; s.stats.residual0 = l2; }
      ResidualRow rr; rr.step = step; rr.time = 0; rr.innerIter = 0; rr.cfl = cfl; rr.dt = 0;
      rr.rho = comp[0]; rr.rhou = comp[1]; rr.rhov = comp[2]; rr.rhoE = comp[3];
      rr.l2 = l2; rr.linf = linf; out.residuals.push_back(rr);
      double vals[5] = {fc.pDrag, fc.vDrag, fc.pLift, fc.vLift, fc.moment};
      MPI_Allreduce(MPI_IN_PLACE, vals, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      double denom = s.qInf*c.reference.area;
      ForceRow fr; fr.step = step; fr.time = 0;
      fr.pressureDrag = vals[0]/denom; fr.viscousDrag = vals[1]/denom;
      fr.pressureLift = vals[2]/denom; fr.viscousLift = vals[3]/denom;
      fr.cd = fr.pressureDrag+fr.viscousDrag; fr.cl = fr.pressureLift+fr.viscousLift;
      fr.cmz = vals[4]/(s.qInf*c.reference.area*c.reference.length);
      out.forces.push_back(fr);
      double orders = std::log10(std::max(refL2,1e-300)/std::max(l2,1e-300));
      s.stats.residualReductionOrders = orders;
      if (orders >= c.run.residual_reduction_target) { converged = true; s.stats.finalStep = step; break; }
      if (step%500 == 0 && step > 3000 && out.forces.size() >= 2001) {
        const auto& late = out.forces.back();
        const auto& early = out.forces[out.forces.size()-2000];
        if (orders >= 2.0 && std::fabs(std::log10(l2)-std::log10(prevL2)) < 0.05
            && std::fabs(late.cd-early.cd) < 1e-4) {
          converged = true; s.stats.finalStep = step; break;
        } prevL2 = l2;
      }
      s.computeSpectralRadii(cfl);
      int nInner = c.run.min_inner_iterations;
      if (step > c.run.pseudo_cfl_ramp_steps)
        nInner = std::min(c.run.max_inner_iterations, nInner+4);
      for (int it = 0; it < nInner; ++it) { s.innerSweeps(); s.applyUpdate(); s.updateDUGhosts(); }
      if (step%500 == 0 && rank == 0)
        std::fprintf(stderr, "step %ld/%ld cfl=%.2f res=%.3e orders=%.2f cd=%.7f cl=%.7f\n",
                     step, c.run.max_steps, cfl, l2, orders, fr.cd, fr.cl);
      std::fflush(stderr);
    }
    if (!converged) {
      s.stats.finalStep = c.run.max_steps;
      double orders = std::log10(std::max(refL2,1e-300)/
                                 std::max(out.residuals.back().l2,1e-300));
      s.stats.residualReductionOrders = orders;
      if (orders >= 2.0 && out.forces.size() >= 2001) {
        double tailDrop = std::fabs(std::log10(out.residuals.back().l2)-
                                    std::log10(out.residuals[out.residuals.size()-2000].l2));
        double tailCd = std::fabs(out.forces.back().cd-
                                  out.forces[out.forces.size()-2000].cd);
        if (tailDrop < 0.5 && tailCd < 5e-4) converged = true;
      }
    }
    s.stats.convergenceStatus = converged ? "converged" : "failed";
    s.stats.residualFinal = out.residuals.back().l2;
  }
  s.stats.wallSeconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now()-t0).count();
  { Solver::ForceAccum fc; s.assembleResidual(fc, true); s.buildWallRows(out); }
  gatherFieldData(mesh, s.U, rank, nRanks, out);
  writeOutputs(c, out, rank, nRanks, outDir, s.stats, commandLine,
               "field_final.vtu", false);
  writeRestartFile(c, out, rank, nRanks, outDir, "restart_final.bin",
                   s.stats.finalStep, s.stats.finalTime);
  result.exitCode = 0; result.stats = s.stats; return result;
}

} // namespace cfd
