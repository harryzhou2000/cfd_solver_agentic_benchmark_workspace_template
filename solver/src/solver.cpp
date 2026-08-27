#include "solver.hpp"
#include "jacobian.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>

namespace fv {

Solver::Solver(CaseConfig cfg, LocalMesh mesh, MPI_Comm comm, bool useRoe)
    : cfg_(std::move(cfg)), mesh_(std::move(mesh)), comm_(comm), useRoe_(useRoe) {
  MPI_Comm_rank(comm_, &rank_);
  MPI_Comm_size(comm_, &nranks_);
  fs_.rho = cfg_.fs_rho;
  fs_.u = cfg_.fs_u();
  fs_.v = cfg_.fs_v();
  fs_.p = cfg_.fs_pressure;
  mu_ = cfg_.viscosity();
  kCond_ = cfg_.conductivity();
  rhoFloor_ = 1e-10 * fs_.rho;
  pFloor_ = 1e-10 * fs_.p;
  int n = mesh_.nAll;
  U_.assign(n, State{});
  Un_.assign(n, State{});
  Unm1_.assign(n, State{});
  R_.assign(n, State{});
  W_.assign(n, Prim{});
  grads_.assign(n, Grads{});
  lim_.assign(n, Limiters{});
  faceLam_.assign(mesh_.faces.size(), 0.0);
  faceLamV_.assign(mesh_.faces.size(), 0.0);
  diag_.assign(mesh_.nOwn, 0.0);
  dtau_.assign(mesh_.nOwn, 0.0);
  sendBuf_.resize(mesh_.neighbors.size());
  recvBuf_.resize(mesh_.neighbors.size());
  lusgsTmp_.assign(mesh_.nAll, State{0, 0, 0, 0});
  const char* fo = std::getenv("FV2D_ORDER");
  if (fo && string(fo) == "1") firstOrder_ = true;
  const char* vk = std::getenv("FV2D_VENKAT");
  if (vk) venkatK_ = std::atof(vk);
  const char* b1 = std::getenv("FV2D_BND1");
  if (b1 && string(b1) == "1") bndFirstOrder_ = true;
  freezeOrders_ = 1e30;  // limiter freeze disabled by default (flapping hurt)
  const char* fz = std::getenv("FV2D_FREEZE_ORDERS");
  if (fz) freezeOrders_ = std::atof(fz);
  const char* fzoff = std::getenv("FV2D_FREEZE");
  if (fzoff && string(fzoff) == "0") freezeOrders_ = 1e30;
  const char* uc = std::getenv("FV2D_UCLIP");
  if (uc) uclip_ = std::atof(uc);
  const char* sw = std::getenv("FV2D_SWITCH_ORDERS");
  if (sw) switchOrders_ = std::atof(sw);
  const char* sst = std::getenv("FV2D_SWITCH_STEP");
  if (sst) switchStep_ = std::atol(sst);
  const char* fst = std::getenv("FV2D_FREEZE_STEP");
  if (fst) freezeStep_ = std::atol(fst);
  if (firstOrder_) switchOrders_ = 1e30;  // debug first-order mode: never switch
}

GhostFn Solver::ghostFn() {
  Prim fs = fs_;
  Gas g = cfg_.gas;
  bool weakFF = std::getenv("FV2D_FFWEAK") != nullptr;
  return [fs, g, weakFF](int bc, const Prim& wc, double nx, double ny) -> Prim {
    switch ((BCType)bc) {
      case BCType::Farfield: return weakFF ? fs : farfieldState(wc, nx, ny, fs, g);
      case BCType::SlipWall: return slipWallGhost(wc, nx, ny);
      case BCType::NoSlipAdiabaticWall: return noSlipGhost(wc);
    }
    return wc;
  };
}

// ---------------- halo exchange ----------------

void Solver::haloExchange(vector<double>& data, int stride) {
  int nn = (int)mesh_.neighbors.size();
  if (nn == 0) return;
  vector<MPI_Request> reqs(2 * nn);
  for (int i = 0; i < nn; ++i) {
    const HaloNeighbor& nb = mesh_.neighbors[i];
    recvBuf_[i].resize(nb.recvIdx.size() * (size_t)stride);
    MPI_Irecv(recvBuf_[i].data(), (int)recvBuf_[i].size(), MPI_DOUBLE, nb.rank, 42, comm_, &reqs[i]);
  }
  for (int i = 0; i < nn; ++i) {
    const HaloNeighbor& nb = mesh_.neighbors[i];
    sendBuf_[i].resize(nb.sendIdx.size() * (size_t)stride);
    for (size_t j = 0; j < nb.sendIdx.size(); ++j)
      std::memcpy(sendBuf_[i].data() + j * stride, data.data() + (size_t)nb.sendIdx[j] * stride,
                  sizeof(double) * stride);
    MPI_Isend(sendBuf_[i].data(), (int)sendBuf_[i].size(), MPI_DOUBLE, nb.rank, 42, comm_,
              &reqs[nn + i]);
  }
  MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
  for (int i = 0; i < nn; ++i) {
    const HaloNeighbor& nb = mesh_.neighbors[i];
    for (size_t j = 0; j < nb.recvIdx.size(); ++j)
      std::memcpy(data.data() + (size_t)nb.recvIdx[j] * stride, recvBuf_[i].data() + j * stride,
                  sizeof(double) * stride);
  }
}

void Solver::syncU() {
  static thread_local vector<double> flat;
  flat.resize((size_t)mesh_.nAll * 4);
  for (int i = 0; i < mesh_.nAll; ++i)
    for (int v = 0; v < 4; ++v) flat[(size_t)i * 4 + v] = U_[i][v];
  haloExchange(flat, 4);
  for (int i = mesh_.nOwn; i < mesh_.nAll; ++i)
    for (int v = 0; v < 4; ++v) U_[i][v] = flat[(size_t)i * 4 + v];
}

void Solver::syncRecon() {
  // exchange gradients (10 doubles) + limiters (4 doubles) per cell
  static thread_local vector<double> flat;
  const int stride = 14;
  flat.resize((size_t)mesh_.nAll * stride);
  for (int i = 0; i < mesh_.nOwn; ++i) {
    const Grads& g = grads_[i];
    const Limiters& L = lim_[i];
    double* d = flat.data() + (size_t)i * stride;
    d[0] = g.rho.x; d[1] = g.rho.y;
    d[2] = g.u.x;   d[3] = g.u.y;
    d[4] = g.v.x;   d[5] = g.v.y;
    d[6] = g.p.x;   d[7] = g.p.y;
    d[8] = g.T.x;   d[9] = g.T.y;
    d[10] = L.rho;  d[11] = L.u;  d[12] = L.v;  d[13] = L.p;
  }
  haloExchange(flat, stride);
  for (int i = mesh_.nOwn; i < mesh_.nAll; ++i) {
    Grads& g = grads_[i];
    Limiters& L = lim_[i];
    const double* d = flat.data() + (size_t)i * stride;
    g.rho = {d[0], d[1]};
    g.u = {d[2], d[3]};
    g.v = {d[4], d[5]};
    g.p = {d[6], d[7]};
    g.T = {d[8], d[9]};
    L.rho = d[10]; L.u = d[11]; L.v = d[12]; L.p = d[13];
  }
}

void Solver::syncDU(vector<State>& dU) {
  static thread_local vector<double> flat;
  flat.resize((size_t)mesh_.nAll * 4);
  for (int i = 0; i < mesh_.nAll; ++i)
    for (int v = 0; v < 4; ++v) flat[(size_t)i * 4 + v] = dU[i][v];
  haloExchange(flat, 4);
  for (int i = mesh_.nOwn; i < mesh_.nAll; ++i)
    for (int v = 0; v < 4; ++v) dU[i][v] = flat[(size_t)i * 4 + v];
}

// ---------------- primitives ----------------

void Solver::updatePrimitives() {
  for (int i = 0; i < mesh_.nAll; ++i) {
    const State& U = U_[i];
    Prim& w = W_[i];
    w.rho = std::max(U[0], rhoFloor_);
    w.u = U[1] / w.rho;
    w.v = U[2] / w.rho;
    w.p = std::max(consPressure(U, cfg_.gas), pFloor_);
  }
}

// ---------------- residual ----------------

void Solver::computeResidual(vector<State>& R, bool withTimeTerm, double bdfC0, double bdfC1,
                             double bdfC2, double dt) {
  const LocalMesh& m = mesh_;
  const Gas& g = cfg_.gas;
  syncU();
  updatePrimitives();
  GhostFn ghost = ghostFn();
  if (!firstOrder_ && secondOrderEnabled_) {
    computeGrads(m, W_, g, ghost, grads_);
    if (limFrozen_) {
      lim_ = limFrozenStore_;
    } else {
      computeLimiters(m, W_, grads_, ghost, rhoFloor_, pFloor_, venkatK_, lim_);
    }
    syncRecon();
    if (freezeOnNextResidual_) {
      limFrozen_ = true;
      limFrozenStore_ = lim_;
      resAtFreeze_ = 0.0;  // caller tracks
      freezeOnNextResidual_ = false;
    }
  } else {
    for (int i = 0; i < m.nAll; ++i) { grads_[i] = Grads{}; lim_[i] = Limiters{0, 0, 0, 0}; }
  }
  for (int i = 0; i < m.nOwn; ++i) R[i] = State{0, 0, 0, 0};
  const bool visc = cfg_.viscous();
  const double viscC = std::max(4.0 / 3.0, g.gamma / g.Pr);

  auto reconstruct = [&](int c, const LocalFace& f) -> Prim {
    const Prim& w = W_[c];
    const Grads& gr = grads_[c];
    const Limiters& L = lim_[c];
    double dx = f.fx - m.xc[c], dy = f.fy - m.yc[c];
    Prim wr;
    wr.rho = w.rho + L.rho * (gr.rho.x * dx + gr.rho.y * dy);
    wr.u = w.u + L.u * (gr.u.x * dx + gr.u.y * dy);
    wr.v = w.v + L.v * (gr.v.x * dx + gr.v.y * dy);
    wr.p = w.p + L.p * (gr.p.x * dx + gr.p.y * dy);
    wr.rho = std::max(wr.rho, rhoFloor_);
    wr.p = std::max(wr.p, pFloor_);
    return wr;
  };

  for (size_t fi = 0; fi < m.faces.size(); ++fi) {
    const LocalFace& f = m.faces[fi];
    bool own0 = f.c0 < m.nOwn;
    bool own1 = (f.c1 >= 0 && f.c1 < m.nOwn);
    if (!own0 && !own1) continue;
    Prim wl = reconstruct(f.c0, f);
    Prim wr;
    bool isBnd = (f.c1 < 0);
    if (!isBnd) {
      wr = reconstruct(f.c1, f);
    } else {
      // boundary: ghost state from BC
      // standard FV boundary treatment: cell-center interior state
      wl = W_[f.c0];
      wr = ghost(f.bc, wl, f.nx, f.ny);
    }
    // inviscid flux
    State F;
    if (isBnd && (f.bc == (int)BCType::SlipWall || f.bc == (int)BCType::NoSlipAdiabaticWall)) {
      F = wallPressureFlux(wl.p, f.nx, f.ny);
    } else if (useRoe_) {
      F = roeFlux(wl, wr, f.nx, f.ny, g);
    } else {
      F = rusanovFlux(wl, wr, f.nx, f.ny, g, cfg_.rc.rusanov_dissipation_scale);
    }
    // spectral radius for implicit weights
    double unL = wl.u * f.nx + wl.v * f.ny;
    double unR = wr.u * f.nx + wr.v * f.ny;
    double lam = std::max(std::fabs(unL) + wl.a(g), std::fabs(unR) + wr.a(g));
    faceLam_[fi] = lam * f.area;
    // viscous flux
    if (visc) {
      const Grads& gL = grads_[f.c0];
      double dxr, dyr, phiRu, phiRv, phiRt;
      Vec2 guR, gvR, gtR;
      double rhoBar;
      if (!isBnd) {
        const Grads& gR = grads_[f.c1];
        guR = gR.u; gvR = gR.v; gtR = gR.T;
        dxr = m.xc[f.c1] - m.xc[f.c0];
        dyr = m.yc[f.c1] - m.yc[f.c0];
        phiRu = wr.u; phiRv = wr.v; phiRt = wr.T(g);
        rhoBar = 0.5 * (W_[f.c0].rho + W_[f.c1].rho);
      } else {
        guR = gL.u; gvR = gL.v; gtR = gL.T;
        dxr = 2.0 * (f.fx - m.xc[f.c0]);
        dyr = 2.0 * (f.fy - m.yc[f.c0]);
        // ghost values for the viscous correction
        Prim wg = ghost(f.bc, W_[f.c0], f.nx, f.ny);
        phiRu = wg.u; phiRv = wg.v; phiRt = wg.T(g);
        rhoBar = W_[f.c0].rho;
      }
      double d2 = dxr * dxr + dyr * dyr;
      d2 = std::max(d2, 1e-30);
      auto corr = [&](Vec2 gLa, Vec2 gRa, double phiL, double phiR) -> Vec2 {
        Vec2 gb{(gLa.x + gRa.x) * 0.5, (gLa.y + gRa.y) * 0.5};
        double resid = (phiR - phiL) - (gb.x * dxr + gb.y * dyr);
        return Vec2(gb.x + resid * dxr / d2, gb.y + resid * dyr / d2);
      };
      Vec2 gu = corr(gL.u, guR, wl.u, phiRu);
      Vec2 gv = corr(gL.v, gvR, wl.v, phiRv);
      Vec2 gt = corr(gL.T, gtR, wl.T(g), phiRt);
      double uf, vf;
      if (!isBnd) {
        uf = 0.5 * (wl.u + wr.u);
        vf = 0.5 * (wl.v + wr.v);
      } else if (f.bc == (int)BCType::NoSlipAdiabaticWall) {
        uf = 0.0; vf = 0.0;
      } else {
        // slip wall or farfield: use boundary-state velocity
        uf = wr.u; vf = wr.v;
      }
      State Fv = viscousFluxN(gu.x, gu.y, gv.x, gv.y, gt.x, gt.y, uf, vf, mu_, kCond_, f.nx, f.ny, g);
      for (int v = 0; v < 4; ++v) F[v] -= Fv[v];
      // viscous spectral radius ~ C * (mu/rho) * A / d  [L^2/T]
      // (diffusion rate (mu/rho)(A/d)/V per unit state, times cell volume)
      faceLamV_[fi] = viscC * (mu_ / std::max(rhoBar, rhoFloor_)) * f.area / std::sqrt(d2);
    } else {
      faceLamV_[fi] = 0.0;
    }
    for (int v = 0; v < 4; ++v) {
      if (own0) R[f.c0][v] += F[v] * f.area;
      if (own1) R[f.c1][v] -= F[v] * f.area;
    }
  }
  if (withTimeTerm) {
    double invdt = 1.0 / dt;
    for (int i = 0; i < m.nOwn; ++i)
      for (int v = 0; v < 4; ++v)
        R[i][v] += m.vol[i] * invdt * (bdfC0 * U_[i][v] + bdfC1 * Un_[i][v] + bdfC2 * Unm1_[i][v]);
  }
}

void Solver::computeLocalDt(double cfl) {
  const LocalMesh& m = mesh_;
  vector<double> spec(m.nOwn, 0.0);
  for (size_t fi = 0; fi < m.faces.size(); ++fi) {
    const LocalFace& f = m.faces[fi];
    double rf = faceLam_[fi] + faceLamV_[fi];
    if (f.c0 < m.nOwn) spec[f.c0] += rf;
    if (f.c1 >= 0 && f.c1 < m.nOwn) spec[f.c1] += rf;
  }
  for (int c = 0; c < m.nOwn; ++c) {
    spec[c] = std::max(spec[c], 1e-30);
    dtau_[c] = cfl * m.vol[c] / spec[c];
  }
}

void Solver::buildDiag(double cfl, double bdfC0, double dt) {
  const LocalMesh& m = mesh_;
  for (int c = 0; c < m.nOwn; ++c) {
    double d = m.vol[c] / dtau_[c];
    if (bdfC0 != 0.0) d += m.vol[c] * bdfC0 / dt;
    diag_[c] = d;
  }
  for (size_t fi = 0; fi < m.faces.size(); ++fi) {
    const LocalFace& f = m.faces[fi];
    // full spectral-radius sum on the diagonal (Yoon-Jameson scalar LU-SGS)
    double full = faceLam_[fi] + faceLamV_[fi];
    if (f.c0 < m.nOwn) diag_[f.c0] += full;
    if (f.c1 >= 0 && f.c1 < m.nOwn) diag_[f.c1] += full;
  }
}

void Solver::sgsSweepPair(vector<State>& dU, const vector<State>& rhs) {
  const LocalMesh& m = mesh_;
  // LU-SGS with split flux Jacobians (Blazek full form):
  // matrix off-diagonal blocks (face f between c0 and c1, normal c0->c1):
  //   row c0, col c1:  O = (A^-_j - 0.5 lamV I) A_f
  //   row c1, col c0:  O = (-A^+_j - 0.5 lamV I) A_f
  // forward: (D+L) dU* = rhs ; backward: (D+U) dU = D dU*
  vector<State>& dUstar = lusgsTmp_;
  for (int i = m.nOwn; i < m.nAll; ++i) dUstar[i] = dU[i];  // lagged ghost values
  const Gas& g = cfg_.gas;
  auto offMatvec = [&](const LocalFace& f, int c, int j, double lamA, double lamV,
                       const State& x, State& y) {
    // off-diagonal block O times x (without face area; applied by caller)
    bool cIsLeft = (f.c0 == c);
    const Prim& wj = W_[j];
    double lam = lamA;  // inviscid spectral radius (|un|+a)
    if (cIsLeft) {
      fluxSplitMatvec(wj, f.nx, f.ny, g, lam, -1, x, y);
    } else {
      fluxSplitMatvec(wj, f.nx, f.ny, g, lam, +1, x, y);
      for (int v = 0; v < 4; ++v) y[v] = -y[v];
    }
    double lv = 0.5 * lamV;
    for (int v = 0; v < 4; ++v) y[v] -= lv * x[v];
  };
  // forward sweep
  for (int c = 0; c < m.nOwn; ++c) {
    State s = rhs[c];
    for (int k = m.cellFaceOff[c]; k < m.cellFaceOff[c + 1]; ++k) {
      int fi = m.cellFaceIdx[k];
      const LocalFace& f = m.faces[fi];
      if (f.c1 < 0) continue;
      int j = (f.c0 == c) ? f.c1 : f.c0;
      if (j < m.nOwn && j >= c) continue;  // not yet updated this sweep
      State y;
      double lamA = faceLam_[fi] / f.area, lamV = faceLamV_[fi] / f.area;
      offMatvec(f, c, j, lamA, lamV, dUstar[j], y);
      for (int v = 0; v < 4; ++v) s[v] -= y[v] * f.area;
    }
    for (int v = 0; v < 4; ++v) dUstar[c][v] = s[v] / diag_[c];
  }
  syncDU(dUstar);
  // backward sweep
  for (int c = m.nOwn - 1; c >= 0; --c) {
    State s;
    for (int v = 0; v < 4; ++v) s[v] = diag_[c] * dUstar[c][v];
    for (int k = m.cellFaceOff[c]; k < m.cellFaceOff[c + 1]; ++k) {
      int fi = m.cellFaceIdx[k];
      const LocalFace& f = m.faces[fi];
      if (f.c1 < 0) continue;
      int j = (f.c0 == c) ? f.c1 : f.c0;
      if (j >= m.nOwn || j <= c) continue;  // upper part: owned j > c (updated this sweep)
      State y;
      double lamA = faceLam_[fi] / f.area, lamV = faceLamV_[fi] / f.area;
      offMatvec(f, c, j, lamA, lamV, dU[j], y);
      for (int v = 0; v < 4; ++v) s[v] -= y[v] * f.area;
    }
    for (int v = 0; v < 4; ++v) dU[c][v] = s[v] / diag_[c];
  }
}

bool Solver::applyUpdate(const vector<State>& dU, vector<State>& U) {
  const Gas& g = cfg_.gas;
  bool okAll = true;
  for (int c = 0; c < mesh_.nOwn; ++c) {
    double fac = 1.0;
    // relative update clipping (robustness against oscillatory overshoot)
    if (uclip_ > 0.0) {
      double rho = U[c][0];
      double p = std::max(consPressure(U[c], g), pFloor_);
      double u0 = U[c][1] / rho, v0 = U[c][2] / rho;
      double sp = std::sqrt(u0 * u0 + v0 * v0) + std::sqrt(g.gamma * p / rho);
      double dr = std::fabs(dU[c][0]);
      if (dr > uclip_ * rho) fac = std::min(fac, uclip_ * rho / dr);
      double dp = std::fabs((g.gamma - 1.0) *
                            (dU[c][3] - u0 * dU[c][1] - v0 * dU[c][2] +
                             0.5 * (u0 * u0 + v0 * v0) * dU[c][0]));
      if (dp > uclip_ * p) fac = std::min(fac, uclip_ * p / dp);
      double dv = std::max(std::fabs(dU[c][1]), std::fabs(dU[c][2])) / rho;
      if (dv > uclip_ * sp * 2.0) fac = std::min(fac, uclip_ * sp * 2.0 / dv);
    }
    for (int iter = 0; iter < 12; ++iter) {
      State trial;
      for (int v = 0; v < 4; ++v) trial[v] = U[c][v] + fac * dU[c][v];
      double rho = trial[0];
      double p = consPressure(trial, g);
      if (rho > rhoFloor_ && p > pFloor_) {
        U[c] = trial;
        break;
      }
      fac *= 0.5;
      if (iter == 11) okAll = false;
    }
  }
  return okAll;
}

ResidualNorms Solver::residualNorms(const vector<State>& R) const {
  // global RMS of R/V per equation + overall + Linf
  double sumsq[4] = {0, 0, 0, 0};
  double linf = 0.0;
  for (int c = 0; c < mesh_.nOwn; ++c) {
    double inv = 1.0 / mesh_.vol[c];
    for (int v = 0; v < 4; ++v) {
      double r = R[c][v] * inv;
      sumsq[v] += r * r;
      linf = std::max(linf, std::fabs(r));
    }
  }
  double gsum[4], glinf;
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, comm_);
  MPI_Allreduce(&linf, &glinf, 1, MPI_DOUBLE, MPI_MAX, comm_);
  long nloc = mesh_.nOwn, nglob;
  MPI_Allreduce(&nloc, &nglob, 1, MPI_LONG, MPI_SUM, comm_);
  ResidualNorms rn;
  double tot = 0.0;
  for (int v = 0; v < 4; ++v) {
    rn.eq[v] = std::sqrt(gsum[v] / nglob);
    tot += gsum[v];
  }
  rn.l2 = std::sqrt(tot / (4.0 * nglob));
  rn.linf = glinf;
  return rn;
}

ForceRecord Solver::computeForces() const {
  const Gas& g = cfg_.gas;
  double qinf = cfg_.dynamic_pressure();
  double aref = cfg_.ref_area, lref = cfg_.ref_length;
  double fx_p = 0, fy_p = 0, fx_v = 0, fy_v = 0, mz = 0;
  for (int fi : mesh_.wallFaces) {
    const LocalFace& f = mesh_.faces[fi];
    const Prim& w = W_[f.c0];
    double pw = w.p;  // wall pressure from adjacent cell
    double dpx = (pw - cfg_.fs_pressure) * f.nx * f.area;
    double dpy = (pw - cfg_.fs_pressure) * f.ny * f.area;
    fx_p += dpx;
    fy_p += dpy;
    double rx = f.fx - cfg_.moment_center[0];
    double ry = f.fy - cfg_.moment_center[1];
    mz += rx * dpy - ry * dpx;
    if (cfg_.viscous() && f.bc == (int)BCType::NoSlipAdiabaticWall) {
      // wall shear from one-sided velocity gradient (u_wall = 0)
      const Grads& gr = grads_[f.c0];
      double dxr = 2.0 * (f.fx - mesh_.xc[f.c0]);
      double dyr = 2.0 * (f.fy - mesh_.yc[f.c0]);
      double d2 = std::max(dxr * dxr + dyr * dyr, 1e-30);
      auto corr = [&](Vec2 gl, double phiL, double phiR) -> Vec2 {
        double resid = (phiR - phiL) - (gl.x * dxr + gl.y * dyr);
        return Vec2(gl.x + resid * dxr / d2, gl.y + resid * dyr / d2);
      };
      Vec2 gu = corr(gr.u, w.u, -w.u);
      Vec2 gv = corr(gr.v, w.v, -w.v);
      double div = gu.x + gv.y;
      double txx = 2.0 * mu_ * gu.x - (2.0 / 3.0) * mu_ * div;
      double tyy = 2.0 * mu_ * gv.y - (2.0 / 3.0) * mu_ * div;
      double txy = mu_ * (gu.y + gv.x);
      // traction on fluid across the face; tangential part acts on the body
      double sx = txx * f.nx + txy * f.ny;
      double sy = txy * f.nx + tyy * f.ny;
      double sn = sx * f.nx + sy * f.ny;
      double stx = sx - sn * f.nx;  // tangential traction on fluid (from wall)
      double sty = sy - sn * f.ny;
      // force on body is opposite
      fx_v += -stx * f.area;
      fy_v += -sty * f.area;
      mz += rx * (-sty * f.area) - ry * (-stx * f.area);
    }
  }
  double loc[5] = {fx_p, fy_p, fx_v, fy_v, mz};
  double glob[5];
  MPI_Comm c = comm_;
  MPI_Allreduce(loc, glob, 5, MPI_DOUBLE, MPI_SUM, c);
  ForceRecord fr;
  double qA = qinf * aref;
  fr.pressure_drag = glob[0] / qA;
  fr.pressure_lift = glob[1] / qA;
  fr.viscous_drag = glob[2] / qA;
  fr.viscous_lift = glob[3] / qA;
  fr.cd = fr.pressure_drag + fr.viscous_drag;
  fr.cl = fr.pressure_lift + fr.viscous_lift;
  fr.cmz = glob[4] / (qA * lref);
  return fr;
}

double Solver::cflAt(long step, double cfl0, double cfl1, long ramp) const {
  if (ramp <= 0) return cfl1;
  double t = std::min(1.0, (double)step / ramp);
  return cfl0 + (cfl1 - cfl0) * t;
}

void Solver::logLine(const string& s) {
  if (rank_ != 0) return;
  std::printf("%s\n", s.c_str());
  std::fflush(stdout);
  if (fLog_) {
    std::fprintf(fLog_, "%s\n", s.c_str());
    std::fflush(fLog_);
  }
}

void Solver::logResidualCsv(long step, double time, long inner, double cfl, double dt,
                            const ResidualNorms& rn) {
  if (rank_ != 0 || !fRes_) return;
  std::fprintf(fRes_, "%ld,%.10e,%ld,%.6g,%.6e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n", step, time,
               inner, cfl, dt, rn.eq[0], rn.eq[1], rn.eq[2], rn.eq[3], rn.l2, rn.linf);
}

void Solver::logForcesCsv(long step, double time, const ForceRecord& f) {
  if (rank_ != 0 || !fForce_) return;
  std::fprintf(fForce_, "%ld,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n", step, time, f.cl,
               f.cd, f.cmz, f.pressure_drag, f.viscous_drag, f.pressure_lift, f.viscous_lift);
}

}  // namespace fv
