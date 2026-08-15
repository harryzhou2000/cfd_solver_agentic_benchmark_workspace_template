#include "solver.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fv {

int Solver::overrideInnerSweeps = -1;

namespace {
inline double sq(double x) { return x * x; }
}

Solver::Solver(LocalMesh lm, const CaseConfig& c, MPI_Comm cm, InviscidFluxType ft)
    : m(std::move(lm)), cfg(c), comm(cm), fluxType(ft) {
  if (const char* e = std::getenv("FV2D_FIRST_ORDER")) firstOrder_ = std::atoi(e) != 0;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);

  const int nC = m.nCells, nO = m.nOwned, nF = m.nFaces;
  U.assign(nC * NVAR, 0.0);
  W.assign(nC * NVAR, 0.0);
  T.assign(nC, 0.0);
  gradW.assign(nC * 8, 0.0);
  gradT.assign(nC * 2, 0.0);
  psi.assign(nC * NVAR, 1.0);
  Res.assign(nO * NVAR, 0.0);
  rhoF.assign(nF, 0.0);
  rhoFc.assign(nF, 0.0);
  dU.assign(nC * NVAR, 0.0);
  dtLoc.assign(nO, 0.0);
  diag.assign(nO, 0.0);
  Un.assign(nO * NVAR, 0.0);
  Unm1.assign(nO * NVAR, 0.0);

  Uinf = primitiveToCons(cfg.gas, cfg.rho_inf, cfg.u_inf, cfg.v_inf, cfg.p_inf);
  Winf[0] = cfg.rho_inf;
  Winf[1] = cfg.u_inf;
  Winf[2] = cfg.v_inf;
  Winf[3] = cfg.p_inf;
  rhoFloor = 1e-6 * cfg.rho_inf;
  pFloor = 1e-6 * cfg.p_inf;
  if (const char* e = std::getenv("FV2D_LIMITER_FREEZE")) limiterFreezeOrders = std::atof(e);
  limiterFreezeOrders = 1.5;  // freeze only once forces are also steady (see driver)

  mu = cfg.mu_inf;
  kCond = mu > 0.0 ? mu * cfg.gas.cp() / cfg.gas.prandtl : 0.0;

  // map boundary tags to BC types
  bcType.assign(nF, -1);
  wallFaces.clear();
  for (int f = 0; f < nF; ++f) {
    if (m.faceBc[f] < 0) continue;
    const std::string& fam = m.bcNames[m.faceBc[f]];
    int t = -1;
    for (const auto& b : cfg.boundaries) {
      if (b.family == fam) {
        t = static_cast<int>(b.type);
        break;
      }
    }
    if (t < 0)
      throw std::runtime_error("boundary family '" + fam +
                               "' has no mapping in the case file boundary_conditions");
    bcType[f] = t;
    if (t == static_cast<int>(BCType::SLIP_WALL) ||
        t == static_cast<int>(BCType::NO_SLIP_ADIABATIC))
      wallFaces.push_back(f);
  }
  wallP.assign(wallFaces.size(), 0.0);
  wallTau.assign(wallFaces.size(), 0.0);
  wallUn.assign(wallFaces.size(), 0.0);
  wallUt.assign(wallFaces.size(), 0.0);
  wallRhoB.assign(wallFaces.size(), 0.0);
  wallUb.assign(wallFaces.size(), 0.0);
  wallVb.assign(wallFaces.size(), 0.0);
  wallMach.assign(wallFaces.size(), 0.0);
}

// ---------------------------------------------------------------------------
// Halo exchange (neighbor-scoped Isend/Irecv)
// ---------------------------------------------------------------------------
void Solver::exchange(std::vector<double>& a, int width) {
  const int tag = 42;
  std::vector<MPI_Request> reqs;
  std::vector<std::vector<double>> sendBuf(m.neighbors.size());
  std::vector<std::vector<double>> recvBuf(m.neighbors.size());
  for (size_t i = 0; i < m.neighbors.size(); ++i) {
    auto& nb = m.neighbors[i];
    recvBuf[i].resize(nb.recvIdx.size() * width);
    MPI_Request r;
    MPI_Irecv(recvBuf[i].data(), static_cast<int>(recvBuf[i].size()), MPI_DOUBLE, nb.rank,
              tag, comm, &r);
    reqs.push_back(r);
  }
  for (size_t i = 0; i < m.neighbors.size(); ++i) {
    auto& nb = m.neighbors[i];
    sendBuf[i].resize(nb.sendIdx.size() * width);
    for (size_t j = 0; j < nb.sendIdx.size(); ++j) {
      const int src = nb.sendIdx[j];
      for (int c = 0; c < width; ++c) sendBuf[i][j * width + c] = a[src * width + c];
    }
    MPI_Request r;
    MPI_Isend(sendBuf[i].data(), static_cast<int>(sendBuf[i].size()), MPI_DOUBLE, nb.rank,
              tag, comm, &r);
    reqs.push_back(r);
  }
  MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
  for (size_t i = 0; i < m.neighbors.size(); ++i) {
    auto& nb = m.neighbors[i];
    for (size_t j = 0; j < nb.recvIdx.size(); ++j) {
      const int dst = nb.recvIdx[j];
      for (int c = 0; c < width; ++c) a[dst * width + c] = recvBuf[i][j * width + c];
    }
  }
}

void Solver::computePrimitives() {
  for (int i = 0; i < m.nCells; ++i) {
    const double rho = std::max(U[i * NVAR + IRHO], rhoFloor);
    const double u = U[i * NVAR + IRHOU] / rho;
    const double v = U[i * NVAR + IRHOV] / rho;
    Vec4 Uc{U[i * NVAR + IRHO], U[i * NVAR + IRHOU], U[i * NVAR + IRHOV],
            U[i * NVAR + IRHOE]};
    const double p = std::max(pressure(cfg.gas, Uc), pFloor);
    W[i * NVAR + 0] = rho;
    W[i * NVAR + 1] = u;
    W[i * NVAR + 2] = v;
    W[i * NVAR + 3] = p;
    T[i] = p / (rho * cfg.gas.R);
  }
}

double Solver::bcGhostValue(int bc, int var, const double* Wi, double nx, double ny) const {
  switch (static_cast<BCType>(bc)) {
    case BCType::FARFIELD:
      return Winf[var];
    case BCType::SLIP_WALL: {
      if (var == 0 || var == 3) return Wi[var];
      const double un = Wi[1] * nx + Wi[2] * ny;
      if (var == 1) return Wi[1] - 2.0 * un * nx;
      return Wi[2] - 2.0 * un * ny;
    }
    case BCType::NO_SLIP_ADIABATIC: {
      if (var == 0 || var == 3) return Wi[var];
      return -Wi[var];  // var 1,2
    }
  }
  return Wi[var];
}

void Solver::computeGradientsAndLimiter(bool freezeLimiter) {
  std::fill(gradW.begin(), gradW.end(), 0.0);
  std::fill(gradT.begin(), gradT.end(), 0.0);
  // ---- weighted least-squares gradients (owned cells) ----
  for (int i = 0; i < m.nOwned; ++i) {
    double a11 = 0, a12 = 0, a22 = 0;
    double b[NVAR][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    double bT[2] = {0, 0};
    const double xi = m.cellCx[i], yi = m.cellCy[i];
    const double* Wi = &W[i * NVAR];
    const double Ti = T[i];
    for (int k = m.cellFaceOffset[i]; k < m.cellFaceOffset[i + 1]; ++k) {
      const int f = m.cellFaces[k];
      const int L = m.faceL[f], R = m.faceR[f];
      double dx, dy, dphi[NVAR], dTv;
      if (R >= 0) {
        const int j = (L == i) ? R : L;
        dx = m.cellCx[j] - xi;
        dy = m.cellCy[j] - yi;
        for (int v = 0; v < NVAR; ++v) dphi[v] = W[j * NVAR + v] - Wi[v];
        dTv = T[j] - Ti;
      } else {
        // boundary face: mirrored ghost center and BC ghost values
        dx = m.faceDx[f];
        dy = m.faceDy[f];
        const double area = m.faceArea[f];
        const double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
        const int bc = bcType[f];
        for (int v = 0; v < NVAR; ++v)
          dphi[v] = bcGhostValue(bc, v, Wi, nx, ny) - Wi[v];
        dTv = (bc == static_cast<int>(BCType::FARFIELD)) ? (cfg.t_inf - Ti) : 0.0;
      }
      const double w = 1.0 / (dx * dx + dy * dy + 1e-300);
      a11 += w * dx * dx;
      a12 += w * dx * dy;
      a22 += w * dy * dy;
      for (int v = 0; v < NVAR; ++v) {
        b[v][0] += w * dx * dphi[v];
        b[v][1] += w * dy * dphi[v];
      }
      bT[0] += w * dx * dTv;
      bT[1] += w * dy * dTv;
    }
    const double det = a11 * a22 - a12 * a12;
    if (std::abs(det) > 1e-300 * a11 * a22 + 1e-300) {
      const double i11 = a22 / det, i12 = -a12 / det, i22 = a11 / det;
      for (int v = 0; v < NVAR; ++v) {
        gradW[i * 8 + v * 2 + 0] = i11 * b[v][0] + i12 * b[v][1];
        gradW[i * 8 + v * 2 + 1] = i12 * b[v][0] + i22 * b[v][1];
      }
      gradT[i * 2 + 0] = i11 * bT[0] + i12 * bT[1];
      gradT[i * 2 + 1] = i12 * bT[0] + i22 * bT[1];
    }
  }

  // ---- Barth--Jespersen limiter (owned cells); skipped when frozen ----
  if (!freezeLimiter) {
    for (int i = 0; i < m.nOwned; ++i) {
    const double xi = m.cellCx[i], yi = m.cellCy[i];
    const double* Wi = &W[i * NVAR];
    double minv[NVAR], maxv[NVAR];
    for (int v = 0; v < NVAR; ++v) minv[v] = maxv[v] = Wi[v];
    const int k0 = m.cellFaceOffset[i], k1 = m.cellFaceOffset[i + 1];
    for (int k = k0; k < k1; ++k) {
      const int f = m.cellFaces[k];
      const int L = m.faceL[f], R = m.faceR[f];
      if (R >= 0) {
        const int j = (L == i) ? R : L;
        for (int v = 0; v < NVAR; ++v) {
          minv[v] = std::min(minv[v], W[j * NVAR + v]);
          maxv[v] = std::max(maxv[v], W[j * NVAR + v]);
        }
      } else {
        const double area = m.faceArea[f];
        const double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
        const int bc = bcType[f];
        for (int v = 0; v < NVAR; ++v) {
          const double g = bcGhostValue(bc, v, Wi, nx, ny);
          minv[v] = std::min(minv[v], g);
          maxv[v] = std::max(maxv[v], g);
        }
      }
    }
    for (int v = 0; v < NVAR; ++v) {
      double psiv = 1.0;
      const double gx = gradW[i * 8 + v * 2 + 0], gy = gradW[i * 8 + v * 2 + 1];
      for (int k = k0; k < k1; ++k) {
        const int f = m.cellFaces[k];
        const double dx = m.faceCx[f] - xi, dy = m.faceCy[f] - yi;
        const double phiF = Wi[v] + gx * dx + gy * dy;
        const double delta = phiF - Wi[v];
        if (delta > 0.0 && phiF > maxv[v]) {
          psiv = std::min(psiv, (maxv[v] - Wi[v]) / (delta + 1e-300));
        } else if (delta < 0.0 && phiF < minv[v]) {
          psiv = std::min(psiv, (minv[v] - Wi[v]) / (delta + 1e-300));
        }
      }
      psi[i * NVAR + v] = std::max(0.0, std::min(1.0, psiv));
    }
  }
  }

  // sync ghost gradients + limiter
  exchange(gradW, 8);
  exchange(gradT, 2);
  exchange(psi, NVAR);
}

// ---------------------------------------------------------------------------
// Residual assembly. If withPhysicalTerm: Res += vol*(physCoef*U - physRhs)
// ---------------------------------------------------------------------------
void Solver::computeResidual(bool withPhysicalTerm, double physCoef,
                             const std::vector<double>* physRhs) {
  std::fill(Res.begin(), Res.end(), 0.0);
  const bool visc = cfg.viscous();

  for (int f = 0; f < m.nFaces; ++f) {
    const int L = m.faceL[f], R = m.faceR[f];
    const double area = m.faceArea[f];
    const double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;

    // --- reconstruct left/right primitive states at the face centroid ---
    double WL[NVAR], WR[NVAR];
    {
      const double dx = m.faceCx[f] - m.cellCx[L], dy = m.faceCy[f] - m.cellCy[L];
      bool firstOrder = firstOrder_;
      for (int v = 0; v < NVAR; ++v)
        WL[v] = W[L * NVAR + v] +
                psi[L * NVAR + v] *
                    (gradW[L * 8 + v * 2] * dx + gradW[L * 8 + v * 2 + 1] * dy);
      if (WL[0] < rhoFloor || WL[3] < pFloor) firstOrder = true;
      if (firstOrder)
        for (int v = 0; v < NVAR; ++v) WL[v] = W[L * NVAR + v];
    }
    if (R >= 0) {
      const double dx = m.faceCx[f] - m.cellCx[R], dy = m.faceCy[f] - m.cellCy[R];
      bool firstOrder = firstOrder_;
      for (int v = 0; v < NVAR; ++v)
        WR[v] = W[R * NVAR + v] +
                psi[R * NVAR + v] *
                    (gradW[R * 8 + v * 2] * dx + gradW[R * 8 + v * 2 + 1] * dy);
      if (WR[0] < rhoFloor || WR[3] < pFloor) firstOrder = true;
      if (firstOrder)
        for (int v = 0; v < NVAR; ++v) WR[v] = W[R * NVAR + v];
    } else {
      const int bc = bcType[f];
      for (int v = 0; v < NVAR; ++v) WR[v] = bcGhostValue(bc, v, WL, nx, ny);
    }

    Vec4 UL = primitiveToCons(cfg.gas, WL[0], WL[1], WL[2], WL[3]);
    Vec4 UR = primitiveToCons(cfg.gas, WR[0], WR[1], WR[2], WR[3]);

    double smax = 0.0;
    Vec4 Fi;
    if (fluxType == InviscidFluxType::HLLC)
      Fi = hllcFlux(cfg.gas, UL, UR, nx, ny, smax);
    else
      Fi = rusanovFlux(cfg.gas, UL, UR, nx, ny, cfg.rusanov_dissipation_scale, smax);

    double Ft[NVAR];
    for (int v = 0; v < NVAR; ++v) Ft[v] = Fi[v] * area;
    double rhoFace = smax * area;
    double rhoVisc = 0.0;

    // --- viscous flux (uses consistent cell-center/ghost states, independent
    // --- of the inviscid reconstruction) ---
    if (visc) {
      // face gradients with over-relaxed correction
      const double dx = m.faceDx[f], dy = m.faceDy[f];
      const double dd = dx * dx + dy * dy + 1e-300;
      double uL, vL, TL, uR, vR, TR;
      double guR[2], gvR[2], gTR[2];
      uL = W[L * NVAR + 1];
      vL = W[L * NVAR + 2];
      TL = T[L];
      if (R >= 0) {
        uR = W[R * NVAR + 1];
        vR = W[R * NVAR + 2];
        TR = T[R];
        guR[0] = gradW[R * 8 + 2];
        guR[1] = gradW[R * 8 + 3];
        gvR[0] = gradW[R * 8 + 4];
        gvR[1] = gradW[R * 8 + 5];
        gTR[0] = gradT[R * 2];
        gTR[1] = gradT[R * 2 + 1];
      } else {
        const int bc = bcType[f];
        const double Wc[NVAR] = {W[L * NVAR], uL, vL, W[L * NVAR + 3]};
        uR = bcGhostValue(bc, 1, Wc, nx, ny);
        vR = bcGhostValue(bc, 2, Wc, nx, ny);
        TR = (bc == static_cast<int>(BCType::FARFIELD)) ? cfg.t_inf : T[L];
        guR[0] = gradW[L * 8 + 2];
        guR[1] = gradW[L * 8 + 3];
        gvR[0] = gradW[L * 8 + 4];
        gvR[1] = gradW[L * 8 + 5];
        gTR[0] = gradT[L * 2];
        gTR[1] = gradT[L * 2 + 1];
      }
      double gu[2], gv[2], gT[2];
      gu[0] = 0.5 * (gradW[L * 8 + 2] + guR[0]);
      gu[1] = 0.5 * (gradW[L * 8 + 3] + guR[1]);
      gv[0] = 0.5 * (gradW[L * 8 + 4] + gvR[0]);
      gv[1] = 0.5 * (gradW[L * 8 + 5] + gvR[1]);
      gT[0] = 0.5 * (gradT[L * 2] + gTR[0]);
      gT[1] = 0.5 * (gradT[L * 2 + 1] + gTR[1]);
      const double corrU = (uR - uL - (gu[0] * dx + gu[1] * dy)) / dd;
      const double corrV = (vR - vL - (gv[0] * dx + gv[1] * dy)) / dd;
      const double corrT = (TR - TL - (gT[0] * dx + gT[1] * dy)) / dd;
      gu[0] += corrU * dx;
      gu[1] += corrU * dy;
      gv[0] += corrV * dx;
      gv[1] += corrV * dy;
      gT[0] += corrT * dx;
      gT[1] += corrT * dy;
      const double uF = 0.5 * (uL + uR), vF = 0.5 * (vL + vR);
      const Vec4 Fv = viscousFlux(uF, vF, gu[0], gu[1], gv[0], gv[1], gT[0], gT[1], mu,
                                  kCond, nx, ny);
      for (int v = 0; v < NVAR; ++v) Ft[v] -= Fv[v] * area;
      // viscous spectral radius contribution
      const double rhoMean = 0.5 * (WL[0] + WR[0]);
      const double dist = std::sqrt(dd);
      rhoVisc = std::max(4.0 / 3.0, cfg.gas.gamma / cfg.gas.prandtl) * (mu / rhoMean) *
                 area / std::max(dist, 1e-300);
      rhoFace += rhoVisc;
    }
    rhoF[f] = rhoFace;
    rhoFc[f] = rhoFace - rhoVisc;

    for (int v = 0; v < NVAR; ++v) {
      Res[L * NVAR + v] += Ft[v];
      if (R >= 0 && R < m.nOwned) Res[R * NVAR + v] -= Ft[v];
    }
  }

  if (withPhysicalTerm && physRhs) {
    for (int i = 0; i < m.nOwned; ++i)
      for (int v = 0; v < NVAR; ++v)
        Res[i * NVAR + v] +=
            m.cellVol[i] * (physCoef * U[i * NVAR + v] - (*physRhs)[i * NVAR + v]);
  }
}

void Solver::computeLocalTimeStep(double cfl, bool /*withPhysicalTerm*/, double /*physDt*/) {
  for (int i = 0; i < m.nOwned; ++i) {
    double s = 0.0;
    for (int k = m.cellFaceOffset[i]; k < m.cellFaceOffset[i + 1]; ++k)
      s += rhoFc[m.cellFaces[k]];
    dtLoc[i] = cfl * m.cellVol[i] / std::max(s, 1e-300);
  }
}

void Solver::buildDiagonal(bool withPhysicalTerm, double physCoefDt) {
  for (int i = 0; i < m.nOwned; ++i) {
    double s = 0.0;
    for (int k = m.cellFaceOffset[i]; k < m.cellFaceOffset[i + 1]; ++k)
      s += rhoF[m.cellFaces[k]];
    diag[i] = m.cellVol[i] / dtLoc[i] + s;
    if (withPhysicalTerm) diag[i] += m.cellVol[i] * physCoefDt;
  }
}

// LU-SGS forward/backward sweeps on the linearized system with frozen Res.
void Solver::lusgsSweeps(int nSweeps) {
  std::fill(dU.begin(), dU.end(), 0.0);
  exchange(dU, NVAR);
  for (int sweep = 0; sweep < nSweeps; ++sweep) {
    // forward
    for (int i = 0; i < m.nOwned; ++i) {
      double rhs[NVAR];
      for (int v = 0; v < NVAR; ++v) rhs[v] = -Res[i * NVAR + v];
      for (int k = m.cellFaceOffset[i]; k < m.cellFaceOffset[i + 1]; ++k) {
        const int f = m.cellFaces[k];
        const int L = m.faceL[f], R = m.faceR[f];
        if (R < 0) continue;
        const int j = (L == i) ? R : L;
        if (j >= i || j >= m.nOwned) continue;
        const double area = m.faceArea[f];
        double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
        if (L != i) {
          nx = -nx;
          ny = -ny;
        }
        Vec4 Uj{U[j * NVAR + IRHO], U[j * NVAR + IRHOU], U[j * NVAR + IRHOV],
                U[j * NVAR + IRHOE]};
        Vec4 Uj2{Uj[0] + dU[j * NVAR], Uj[1] + dU[j * NVAR + 1],
                 Uj[2] + dU[j * NVAR + 2], Uj[3] + dU[j * NVAR + 3]};
        const Vec4 F1 = inviscidFlux(Uj, pressure(cfg.gas, Uj), nx, ny);
        const Vec4 F2 = inviscidFlux(Uj2, pressure(cfg.gas, Uj2), nx, ny);
        for (int v = 0; v < NVAR; ++v)
          rhs[v] -= 0.5 * ((F2[v] - F1[v]) * area - rhoF[f] * dU[j * NVAR + v]);
      }
      for (int v = 0; v < NVAR; ++v) dU[i * NVAR + v] = rhs[v] / diag[i];
    }
    // backward
    for (int i = m.nOwned - 1; i >= 0; --i) {
      double corr[NVAR] = {0, 0, 0, 0};
      for (int k = m.cellFaceOffset[i]; k < m.cellFaceOffset[i + 1]; ++k) {
        const int f = m.cellFaces[k];
        const int L = m.faceL[f], R = m.faceR[f];
        if (R < 0) continue;
        const int j = (L == i) ? R : L;
        if (j <= i) continue;  // includes ghosts (stale dU)
        const double area = m.faceArea[f];
        double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
        if (L != i) {
          nx = -nx;
          ny = -ny;
        }
        Vec4 Uj{U[j * NVAR + IRHO], U[j * NVAR + IRHOU], U[j * NVAR + IRHOV],
                U[j * NVAR + IRHOE]};
        Vec4 Uj2{Uj[0] + dU[j * NVAR], Uj[1] + dU[j * NVAR + 1],
                 Uj[2] + dU[j * NVAR + 2], Uj[3] + dU[j * NVAR + 3]};
        const Vec4 F1 = inviscidFlux(Uj, pressure(cfg.gas, Uj), nx, ny);
        const Vec4 F2 = inviscidFlux(Uj2, pressure(cfg.gas, Uj2), nx, ny);
        for (int v = 0; v < NVAR; ++v)
          corr[v] += 0.5 * ((F2[v] - F1[v]) * area - rhoF[f] * dU[j * NVAR + v]);
      }
      for (int v = 0; v < NVAR; ++v) dU[i * NVAR + v] -= corr[v] / diag[i];
    }
    exchange(dU, NVAR);
  }
}

void Solver::applyUpdate() {
  for (int i = 0; i < m.nOwned; ++i) {
    double alpha = 1.0;
    const double rhoOld = U[i * NVAR + IRHO];
    Vec4 Uold{U[i * NVAR], U[i * NVAR + 1], U[i * NVAR + 2], U[i * NVAR + 3]};
    const double pOld = pressure(cfg.gas, Uold);
    for (int attempt = 0; attempt < 6; ++attempt) {
      Vec4 Un2{Uold[0] + alpha * dU[i * NVAR], Uold[1] + alpha * dU[i * NVAR + 1],
               Uold[2] + alpha * dU[i * NVAR + 2], Uold[3] + alpha * dU[i * NVAR + 3]};
      const double rhoNew = Un2[IRHO];
      const double pNew = pressure(cfg.gas, Un2);
      if (rhoNew >= std::max(rhoFloor, 0.05 * rhoOld) &&
          pNew >= std::max(pFloor, 0.05 * pOld)) {
        for (int v = 0; v < NVAR; ++v) U[i * NVAR + v] = Un2[v];
        break;
      }
      alpha *= 0.5;
      if (attempt == 5) { /* keep old state */ }
    }
  }
}

double Solver::residualNorms(double* perEq, double& linf) const {
  double loc[NVAR] = {0, 0, 0, 0};
  double locInf = 0.0;
  for (int i = 0; i < m.nOwned; ++i) {
    const double iv = 1.0 / m.cellVol[i];
    for (int v = 0; v < NVAR; ++v) {
      const double r = Res[i * NVAR + v] * iv;
      loc[v] += r * r;
      locInf = std::max(locInf, std::abs(r));
    }
  }
  double glob[NVAR];
  MPI_Allreduce(loc, glob, NVAR, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(&locInf, &linf, 1, MPI_DOUBLE, MPI_MAX, comm);
  long nTot = 0, nLoc = m.nOwned;
  MPI_Allreduce(&nLoc, &nTot, 1, MPI_LONG, MPI_SUM, comm);
  double total = 0.0;
  for (int v = 0; v < NVAR; ++v) {
    perEq[v] = std::sqrt(glob[v] / nTot);
    total += glob[v];
  }
  return std::sqrt(total / nTot);
}

// ---------------------------------------------------------------------------
// Wall quantities, forces, surface rows
// ---------------------------------------------------------------------------
void Solver::computeWallQuantities() {
  const double qInf = cfg.dyn_pressure();
  for (size_t w = 0; w < wallFaces.size(); ++w) {
    const int f = wallFaces[w];
    const int L = m.faceL[f];
    const double area = m.faceArea[f];
    const double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
    const double dx = m.faceCx[f] - m.cellCx[L], dy = m.faceCy[f] - m.cellCy[L];
    double WL[NVAR];
    bool firstOrder = false;
    for (int v = 0; v < NVAR; ++v)
      WL[v] = W[L * NVAR + v] +
              psi[L * NVAR + v] * (gradW[L * 8 + v * 2] * dx + gradW[L * 8 + v * 2 + 1] * dy);
    if (WL[0] < rhoFloor || WL[3] < pFloor) firstOrder = true;
    if (firstOrder)
      for (int v = 0; v < NVAR; ++v) WL[v] = W[L * NVAR + v];
    const double pF = WL[3];
    const double rhoB = WL[0];
    const bool noslip = bcType[f] == static_cast<int>(BCType::NO_SLIP_ADIABATIC);
    double ub = 0.0, vb = 0.0, machB = 0.0, tau = 0.0, un = 0.0;
    if (noslip) {
      // one-sided wall shear: tau = mu * u_t(cell) / d_n
      const double uC = W[L * NVAR + 1], vC = W[L * NVAR + 2];
      const double unC = uC * nx + vC * ny;
      const double utx = uC - unC * nx, uty = vC - unC * ny;
      const double dn = 0.5 * std::hypot(m.faceDx[f], m.faceDy[f]);
      // signed scalar shear along t = (-ny, nx)
      tau = mu * (utx * (-ny) + uty * (nx)) / std::max(dn, 1e-300);
    } else {
      const double unC = WL[1] * nx + WL[2] * ny;
      ub = WL[1] - unC * nx;
      vb = WL[2] - unC * ny;
      un = unC;  // boundary-condition normal velocity before projection (~small)
      const double aB = soundSpeed(cfg.gas, rhoB, pF);
      machB = std::hypot(ub, vb) / aB;
      ub = WL[1];
      vb = WL[2];  // report full reconstructed boundary velocity for slip walls
      const double un2 = ub * nx + vb * ny;
      ub -= un2 * nx;  // tangential projection: boundary-condition value
      vb -= un2 * ny;
    }
    wallP[w] = pF;
    wallTau[w] = tau;
    wallUn[w] = un;
    wallRhoB[w] = rhoB;
    wallUb[w] = ub;
    wallVb[w] = vb;
    wallMach[w] = machB;
    (void)qInf;
  }
}

ForceRow Solver::computeForces() {
  computeWallQuantities();
  double fxP = 0, fyP = 0, fxV = 0, fyV = 0, mz = 0;
  for (size_t w = 0; w < wallFaces.size(); ++w) {
    const int f = wallFaces[w];
    const double pF = wallP[w];
    // force on the body: +p n (n points out of the fluid, into the body; the
    // fluid pressure pushes the body surface along n)
    const double fx = pF * m.faceNx[f];
    const double fy = pF * m.faceNy[f];
    fxP += fx;
    fyP += fy;
    mz += (m.faceCx[f] - cfg.moment_center[0]) * fy -
          (m.faceCy[f] - cfg.moment_center[1]) * fx;
    if (bcType[f] == static_cast<int>(BCType::NO_SLIP_ADIABATIC)) {
      // tangential shear traction along t = (-ny, nx), magnitude tau*area
      const double area = m.faceArea[f];
      const double nx = m.faceNx[f] / area, ny = m.faceNy[f] / area;
      const double tx = -ny * wallTau[w] * area;
      const double ty = nx * wallTau[w] * area;
      fxV += tx;
      fyV += ty;
      mz += (m.faceCx[f] - cfg.moment_center[0]) * ty -
            (m.faceCy[f] - cfg.moment_center[1]) * tx;
    }
  }
  double g[6], l[6] = {fxP, fyP, fxV, fyV, mz, 0};
  MPI_Allreduce(l, g, 5, MPI_DOUBLE, MPI_SUM, comm);
  const double aoa = cfg.aoa_deg * M_PI / 180.0;
  const double ca = std::cos(aoa), sa = std::sin(aoa);
  const double qA = cfg.dyn_pressure() * cfg.ref_area;
  ForceRow fr;
  fr.pressureDrag = (g[0] * ca + g[1] * sa) / qA;
  fr.pressureLift = (-g[0] * sa + g[1] * ca) / qA;
  fr.viscousDrag = (g[2] * ca + g[3] * sa) / qA;
  fr.viscousLift = (-g[2] * sa + g[3] * ca) / qA;
  fr.cd = fr.pressureDrag + fr.viscousDrag;
  fr.cl = fr.pressureLift + fr.viscousLift;
  fr.cmz = g[4] / (qA * cfg.ref_length);
  if (!cfg.viscous()) {
    fr.viscousDrag = 0.0;
    fr.viscousLift = 0.0;
  }
  return fr;
}

std::vector<SurfaceRow> Solver::collectSurfaceRows() const {
  std::vector<SurfaceRow> rows;
  const double qInf = cfg.dyn_pressure();
  for (size_t w = 0; w < wallFaces.size(); ++w) {
    const int f = wallFaces[w];
    const double area = m.faceArea[f];
    SurfaceRow r;
    r.x = m.faceCx[f];
    r.y = m.faceCy[f];
    r.nx = m.faceNx[f] / area;
    r.ny = m.faceNy[f] / area;
    r.pressure = wallP[w];
    r.cp = (wallP[w] - cfg.p_inf) / qInf;
    r.cf = wallTau[w] / qInf;
    r.rho = wallRhoB[w];
    r.u = wallUb[w];
    r.v = wallVb[w];
    r.mach = wallMach[w];
    r.tag = m.bcNames[m.faceBc[f]];
    rows.push_back(r);
  }
  return rows;
}

double Solver::cflForStep(long step) const {
  if (cfg.pseudo_cfl_ramp_steps <= 0) return cfg.cfl_max;
  const double f = std::min(1.0, static_cast<double>(step) / cfg.pseudo_cfl_ramp_steps);
  return cfg.cfl_initial + (cfg.cfl_max - cfg.cfl_initial) * f;
}

// ---------------------------------------------------------------------------
// Initialization / restart
// ---------------------------------------------------------------------------
void Solver::initFields(const std::string& restartFile, const GlobalMesh* gm) {
  if (restartFile.empty()) {
    for (int i = 0; i < m.nCells; ++i)
      for (int v = 0; v < NVAR; ++v) U[i * NVAR + v] = Uinf[v];
    if (cfg.transient()) {
      // small symmetry-breaking perturbation to seed vortex shedding
      const double L = cfg.reynolds_length;
      const double xc = cfg.moment_center[0], yc = cfg.moment_center[1];
      for (int i = 0; i < m.nOwned; ++i) {
        const double dx = m.cellCx[i] - xc, dy = m.cellCy[i] - yc;
        const double r2 = dx * dx + dy * dy;
        const double dv =
            0.05 * cfg.vel_inf * std::sin(2.0 * M_PI * dy / L) *
            std::exp(-r2 / (25.0 * L * L));
        U[i * NVAR + IRHOV] += U[i * NVAR + IRHO] * dv;
      }
    }
  } else {
    // rank 0 reads the global restart and distributes rank-local states
    long nGlobal = 0;
    std::vector<double> allU;
    if (rank == 0) {
      std::ifstream in(restartFile, std::ios::binary);
      if (!in) throw std::runtime_error("cannot open restart file: " + restartFile);
      char magic[8];
      in.read(magic, 8);
      if (std::strncmp(magic, "FV2DRST1", 8) != 0)
        throw std::runtime_error("bad restart file magic in " + restartFile);
      in.read(reinterpret_cast<char*>(&nGlobal), sizeof(long));
      if (!gm || nGlobal != gm->nCells)
        throw std::runtime_error("restart cell count mismatch with mesh");
      allU.resize(static_cast<size_t>(nGlobal) * NVAR);
      in.read(reinterpret_cast<char*>(allU.data()),
              static_cast<std::streamsize>(allU.size() * sizeof(double)));
    }
    // gather local cell-global-id lists on rank 0
    int myN = m.nCells;
    std::vector<int> counts(size);
    MPI_Gather(&myN, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
    std::vector<long> idBuf;
    std::vector<int> displs(size + 1, 0);
    if (rank == 0) {
      for (int r = 0; r < size; ++r) displs[r + 1] = displs[r] + counts[r];
      idBuf.resize(displs[size]);
    }
    MPI_Gatherv(m.cellGlobal.data(), myN, MPI_LONG, idBuf.data(), counts.data(),
                displs.data(), MPI_LONG, 0, comm);
    if (rank == 0) {
      std::vector<MPI_Request> reqs;
      std::vector<std::vector<double>> sendBufs(size);
      for (int r = 1; r < size; ++r) {
        sendBufs[r].resize(static_cast<size_t>(counts[r]) * NVAR);
        for (int j = 0; j < counts[r]; ++j) {
          const long gid = idBuf[displs[r] + j];
          for (int v = 0; v < NVAR; ++v)
            sendBufs[r][j * NVAR + v] = allU[gid * NVAR + v];
        }
        MPI_Request req;
        MPI_Isend(sendBufs[r].data(), static_cast<int>(sendBufs[r].size()), MPI_DOUBLE, r,
                  99, comm, &req);
        reqs.push_back(req);
      }
      // rank 0 fills itself
      for (int j = 0; j < counts[0]; ++j) {
        const long gid = idBuf[displs[0] + j];
        for (int v = 0; v < NVAR; ++v) U[j * NVAR + v] = allU[gid * NVAR + v];
      }
      MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
    } else {
      MPI_Status st;
      MPI_Recv(U.data(), m.nCells * NVAR, MPI_DOUBLE, 0, 99, comm, &st);
    }
  }
  exchange(U, NVAR);
  computePrimitives();
}

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------
void Solver::appendResidualRow(std::ofstream& os, long step, double time, int inner,
                               double cfl, double dt, const double* perEq, double l2,
                               double linf) const {
  if (rank != 0) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%ld,%.10e,%d,%.6g,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e,%.6e\n",
                step, time, inner, cfl, dt, perEq[0], perEq[1], perEq[2], perEq[3], l2,
                linf);
  os << buf;
}

void Solver::appendForceRow(std::ofstream& os, long step, double time,
                            const ForceRow& f) const {
  if (rank != 0) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%ld,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
                step, time, f.cl, f.cd, f.cmz, f.pressureDrag, f.viscousDrag,
                f.pressureLift, f.viscousLift);
  os << buf;
}

void Solver::gatherGlobalField(const std::vector<double>& local, int width,
                               std::vector<double>& global) const {
  // local must contain nOwned*width values (owned cells only)
  std::vector<int> counts(size);
  int myN = m.nOwned;
  MPI_Gather(&myN, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(size + 1, 0);
  for (int r = 0; r < size; ++r) displs[r + 1] = displs[r] + counts[r];
  std::vector<long> idBuf;
  if (rank == 0) idBuf.resize(displs[size]);
  MPI_Gatherv(const_cast<long*>(m.cellGlobal.data()), myN, MPI_LONG, idBuf.data(),
              counts.data(), displs.data(), MPI_LONG, 0, comm);
  std::vector<int> countsW(size), displsW(size + 1, 0);
  for (int r = 0; r < size; ++r) {
    countsW[r] = counts[r] * width;
    displsW[r + 1] = displsW[r] + countsW[r];
  }
  std::vector<double> valBuf;
  if (rank == 0) valBuf.resize(displsW[size]);
  MPI_Gatherv(local.data(), myN * width, MPI_DOUBLE, valBuf.data(), countsW.data(),
              displsW.data(), MPI_DOUBLE, 0, comm);
  if (rank == 0) {
    const long nGlobal = displs[size];
    long maxId = 0;
    for (long i = 0; i < nGlobal; ++i) maxId = std::max(maxId, idBuf[i]);
    global.assign((maxId + 1) * width, 0.0);
    for (int r = 0; r < size; ++r)
      for (int j = 0; j < counts[r]; ++j) {
        const long gid = idBuf[displs[r] + j];
        for (int v = 0; v < width; ++v)
          global[gid * width + v] = valBuf[(displs[r] + j) * width + v];
      }
  }
}

void Solver::writeVtk(const std::string& path, const GlobalMesh* gm, int rankField) {
  // build per-owned-cell scalar fields and gather on rank 0
  const int nO = m.nOwned;
  std::vector<double> rho(nO), uu(nO), vv(nO), pp(nO), ma(nO), tt(nO), vort(nO),
      part(nO);
  for (int i = 0; i < nO; ++i) {
    rho[i] = W[i * NVAR + 0];
    uu[i] = W[i * NVAR + 1];
    vv[i] = W[i * NVAR + 2];
    pp[i] = W[i * NVAR + 3];
    tt[i] = T[i];
    const double a = soundSpeed(cfg.gas, rho[i], pp[i]);
    ma[i] = std::hypot(uu[i], vv[i]) / a;
    vort[i] = gradW[i * 8 + 2 * 2] - gradW[i * 8 + 1 * 2 + 1];  // dv/dx - du/dy
    part[i] = rank;
  }
  std::vector<double> gRho, gU, gV, gP, gM, gT, gVort, gPart;
  gatherGlobalField(rho, 1, gRho);
  gatherGlobalField(uu, 1, gU);
  gatherGlobalField(vv, 1, gV);
  gatherGlobalField(pp, 1, gP);
  gatherGlobalField(ma, 1, gM);
  gatherGlobalField(tt, 1, gT);
  gatherGlobalField(vort, 1, gVort);
  gatherGlobalField(part, 1, gPart);
  if (rank != 0) return;
  std::ofstream os(path);
  if (!os) throw std::runtime_error("cannot write vtk file: " + path);
  os << "# vtk DataFile Version 3.0\nfv2d field\nASCII\n";
  os << "DATASET UNSTRUCTURED_GRID\n";
  os << "POINTS " << gm->nNodes << " double\n";
  for (int i = 0; i < gm->nNodes; ++i)
    os << gm->x[i] << " " << gm->y[i] << " 0\n";
  long connSize = 0;
  for (int c = 0; c < gm->nCells; ++c) connSize += 1 + gm->cellNVerts[c];
  os << "CELLS " << gm->nCells << " " << connSize << "\n";
  for (int c = 0; c < gm->nCells; ++c) {
    os << gm->cellNVerts[c];
    for (int k = 0; k < gm->cellNVerts[c]; ++k)
      os << " " << gm->cellNodes[gm->cellNodeOffset[c] + k];
    os << "\n";
  }
  os << "CELL_TYPES " << gm->nCells << "\n";
  for (int c = 0; c < gm->nCells; ++c) os << (gm->cellNVerts[c] == 3 ? 5 : 9) << "\n";
  os << "CELL_DATA " << gm->nCells << "\n";
  auto scalar = [&](const char* name, const std::vector<double>& a) {
    os << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
    os.precision(10);
    for (double v : a) os << v << "\n";
  };
  scalar("density", gRho);
  scalar("pressure", gP);
  scalar("mach", gM);
  scalar("temperature", gT);
  scalar("vorticity", gVort);
  scalar("rank", gPart);
  os << "VECTORS velocity double\n";
  os.precision(10);
  for (size_t c = 0; c < gU.size(); ++c) os << gU[c] << " " << gV[c] << " 0\n";
  (void)rankField;
}

void Solver::writeRestart(const std::string& path) {
  std::vector<double> allU;
  std::vector<double> locU(m.nOwned * NVAR);
  for (int i = 0; i < m.nOwned * NVAR; ++i) locU[i] = U[i];
  gatherGlobalField(locU, NVAR, allU);
  if (rank != 0) return;
  std::ofstream os(path, std::ios::binary);
  if (!os) throw std::runtime_error("cannot write restart file: " + path);
  const char magic[8] = {'F', 'V', '2', 'D', 'R', 'S', 'T', '1'};
  os.write(magic, 8);
  const long nGlobal = static_cast<long>(allU.size() / NVAR);
  os.write(reinterpret_cast<const char*>(&nGlobal), sizeof(long));
  os.write(reinterpret_cast<const char*>(allU.data()),
           static_cast<std::streamsize>(allU.size() * sizeof(double)));
}

void Solver::writeSurfaceCsv(const std::string& path) {
  auto rows = collectSurfaceRows();
  // serialize: 10 doubles + tag string
  std::vector<double> vals;
  std::string tags;
  std::vector<int> tagLen;
  for (const auto& r : rows) {
    vals.insert(vals.end(), {r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u,
                             r.v, r.mach});
    tags += r.tag;
    tagLen.push_back(static_cast<int>(r.tag.size()));
  }
  int myRows = static_cast<int>(rows.size());
  std::vector<int> counts(size);
  MPI_Gather(&myRows, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  std::vector<int> displs(size + 1, 0);
  for (int r = 0; r < size; ++r) displs[r + 1] = displs[r] + counts[r];
  std::vector<double> allVals;
  std::vector<int> vCounts(size), vDispls(size + 1, 0);
  for (int r = 0; r < size; ++r) {
    vCounts[r] = counts[r] * 11;
    vDispls[r + 1] = vDispls[r] + vCounts[r];
  }
  if (rank == 0) allVals.resize(vDispls[size]);
  MPI_Gatherv(vals.data(), myRows * 11, MPI_DOUBLE, allVals.data(), vCounts.data(),
              vDispls.data(), MPI_DOUBLE, 0, comm);
  // tags: gather lengths then chars
  int myTagChars = static_cast<int>(tags.size());
  std::vector<int> tagCounts(size), tagDispls(size + 1, 0);
  MPI_Gather(&myTagChars, 1, MPI_INT, tagCounts.data(), 1, MPI_INT, 0, comm);
  for (int r = 0; r < size; ++r) tagDispls[r + 1] = tagDispls[r] + tagCounts[r];
  std::vector<char> allTags;
  if (rank == 0) allTags.resize(tagDispls[size]);
  MPI_Gatherv(tags.data(), myTagChars, MPI_CHAR, allTags.data(), tagCounts.data(),
              tagDispls.data(), MPI_CHAR, 0, comm);
  std::vector<int> allTagLen;
  if (rank == 0) allTagLen.resize(displs[size]);
  MPI_Gatherv(tagLen.data(), myRows, MPI_INT, allTagLen.data(), counts.data(),
              displs.data(), MPI_INT, 0, comm);
  if (rank != 0) return;
  // reconstruct + sort by (tag, x, y)
  struct R {
    std::string tag;
    double v[11];
  };
  std::vector<R> all;
  size_t tagPos = 0;
  for (size_t i = 0; i < allTagLen.size(); ++i) {
    R r;
    r.tag.assign(&allTags[tagPos], allTagLen[i]);
    tagPos += allTagLen[i];
    for (int k = 0; k < 11; ++k) r.v[k] = allVals[i * 11 + k];
    all.push_back(std::move(r));
  }
  std::sort(all.begin(), all.end(), [](const R& a, const R& b) {
    if (a.tag != b.tag) return a.tag < b.tag;
    if (a.v[0] != b.v[0]) return a.v[0] < b.v[0];
    return a.v[1] < b.v[1];
  });
  std::ofstream os(path);
  os << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
  os.precision(10);
  for (const auto& r : all) {
    os << r.v[0] << "," << r.v[1] << "," << r.v[2] << "," << r.v[3] << "," << r.v[4]
       << "," << r.v[5] << "," << r.v[6] << "," << r.v[7] << "," << r.v[8] << ","
       << r.v[9] << "," << r.v[10] << "," << r.tag << "\n";
  }
}

void Solver::writePartitionDiagnostics(const std::string& stem, long edgeCut) const {
  // per-rank CSV + global JSON summary
  int nBoundary = 0;
  for (int f = 0; f < m.nFaces; ++f)
    if (m.faceBc[f] >= 0) ++nBoundary;
  long sendCells = 0, recvCells = 0;
  std::string neighStr;
  for (size_t i = 0; i < m.neighbors.size(); ++i) {
    sendCells += m.neighbors[i].sendIdx.size();
    recvCells += m.neighbors[i].recvIdx.size();
    if (i) neighStr += ";";
    neighStr += std::to_string(m.neighbors[i].rank);
  }
  std::ostringstream line;
  line << rank << "," << m.nOwned << "," << m.nGhost << "," << nBoundary << ","
       << m.neighbors.size() << ",\"" << neighStr << "\"," << sendCells << ","
       << recvCells;
  std::string s = line.str();
  int myLen = static_cast<int>(s.size());
  std::vector<int> counts(size), displs(size + 1, 0);
  MPI_Gather(&myLen, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
  for (int r = 0; r < size; ++r) displs[r + 1] = displs[r] + counts[r];
  std::vector<char> buf;
  if (rank == 0) buf.resize(displs[size]);
  MPI_Gatherv(s.data(), myLen, MPI_CHAR, buf.data(), counts.data(), displs.data(),
              MPI_CHAR, 0, comm);
  int myOwned = m.nOwned;
  std::vector<int> ownedAll(size);
  MPI_Gather(&myOwned, 1, MPI_INT, ownedAll.data(), 1, MPI_INT, 0, comm);
  if (rank != 0) return;
  {
    std::ofstream os(stem + ".csv");
    os << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
          "neighbor_ranks,send_cells,recv_cells\n";
    for (int r = 0; r < size; ++r)
      os << std::string(&buf[displs[r]], counts[r]) << "\n";
  }
  {
    int mn = *std::min_element(ownedAll.begin(), ownedAll.end());
    int mx = *std::max_element(ownedAll.begin(), ownedAll.end());
    double mean = 0;
    for (int v : ownedAll) mean += v;
    mean /= size;
    std::ofstream os(stem + ".json");
    os << "{\n  \"edge_cut\": " << edgeCut << ",\n  \"min_owned\": " << mn
       << ",\n  \"max_owned\": " << mx << ",\n  \"mean_owned\": " << mean
       << ",\n  \"load_balance_ratio\": " << (mx / mean) << "\n}\n";
  }
}

// ---------------------------------------------------------------------------
// Steady driver
// ---------------------------------------------------------------------------
RunStats Solver::runSteady(const GlobalMesh* gm, long edgeCut, double cflOverride) {
  RunStats st;
  std::ofstream resCsv, forceCsv;
  if (rank == 0) {
    resCsv.open(outDir + "/residuals.csv");
    resCsv << std::unitbuf;
    resCsv << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
              "residual_linf\n";
    forceCsv.open(outDir + "/forces.csv");
    forceCsv << std::unitbuf;
    forceCsv << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
                "viscous_lift\n";
  }

  computeGradientsAndLimiter();
  computeResidual(false, 0.0, nullptr);
  double perEq[NVAR], linf = 0;
  double l2 = residualNorms(perEq, linf);
  const double n0 = std::max(l2, 1e-300);
  st.initialResidual = n0;
  ForceRow fr = computeForces();
  const int K =
      std::max(cfg.min_inner_iterations, std::min(cfg.max_inner_iterations, 8));
  st.typicalInnerIterations = K;

  long step = 0;
  appendResidualRow(resCsv, 0, 0.0, 0, cfg.cfl_initial, 0.0, perEq, l2, linf);
  appendForceRow(forceCsv, 0, 0.0, fr);

  bool failed = false;
  double cfl = cfg.cfl_initial;
  // force-stabilization window: after the residual target is met, keep
  // iterating until cl/cd stop drifting (or max_steps reached)
  std::deque<double> cdWin, clWin;
  const size_t winSize = 1001;
  while (step < cfg.max_steps) {
    const double orders = std::log10(n0 / std::max(l2, 1e-300));
    if (orders >= cfg.residual_reduction_target) {
      bool forcesStable = false;
      if (cdWin.size() >= winSize) {
        const auto [cdMin, cdMax] = std::minmax_element(cdWin.begin(), cdWin.end());
        const auto [clMin, clMax] = std::minmax_element(clWin.begin(), clWin.end());
        forcesStable = (*cdMax - *cdMin < 2e-4) && (*clMax - *clMin < 2e-4);
      }
      if (forcesStable || orders >= cfg.residual_reduction_target + 1.5) break;
    }
    if (!std::isfinite(l2)) {
      failed = true;
      break;
    }
    cfl = cflForStep(step);
    if (cflOverride > 0.0) cfl = std::min(cfl, cflOverride);
    computeLocalTimeStep(cfl, false, 0.0);
    buildDiagonal(false, 0.0);
    lusgsSweeps(K);
    applyUpdate();
    exchange(U, NVAR);
    ++step;
    computePrimitives();
    // Freeze the limiter once deep into convergence, but only for shock-free
    // cases (global max Mach < 0.95): in shocked flows a frozen limiter biases
    // the shock position and the forces.
    double maxMach = 0.0;
    for (int i = 0; i < m.nOwned; ++i) {
      const double a = soundSpeed(cfg.gas, W[i * NVAR], W[i * NVAR + 3]);
      maxMach = std::max(maxMach,
                         std::hypot(W[i * NVAR + 1], W[i * NVAR + 2]) / a);
    }
    MPI_Allreduce(MPI_IN_PLACE, &maxMach, 1, MPI_DOUBLE, MPI_MAX, comm);
    if (!limiterFrozen)
      limiterFrozen = !cfg.viscous() && cfg.mach < 0.5 &&
                      (std::log10(n0 / std::max(l2, 1e-300)) >= limiterFreezeOrders) &&
                      maxMach < 0.95;  // sticky, deeply subsonic inviscid only
    computeGradientsAndLimiter(limiterFrozen);
    computeResidual(false, 0.0, nullptr);
    l2 = residualNorms(perEq, linf);
    fr = computeForces();
    if (step % cfg.write_residuals_every == 0) {
      double meanDt = 0;
      for (int i = 0; i < m.nOwned; ++i) meanDt += dtLoc[i];
      MPI_Allreduce(MPI_IN_PLACE, &meanDt, 1, MPI_DOUBLE, MPI_SUM, comm);
      long nTot = 0, nLoc = m.nOwned;
      MPI_Allreduce(&nLoc, &nTot, 1, MPI_LONG, MPI_SUM, comm);
      meanDt /= nTot;
      appendResidualRow(resCsv, step, 0.0, K, cfl, meanDt, perEq, l2, linf);
    }
    if (step % cfg.write_forces_every == 0) appendForceRow(forceCsv, step, 0.0, fr);
    cdWin.push_back(fr.cd);
    clWin.push_back(fr.cl);
    if (cdWin.size() > winSize) {
      cdWin.pop_front();
      clWin.pop_front();
    }
    if (rank == 0 && step % 2000 == 0) {
      std::ostringstream msg;
      msg << "[steady] step " << step << "  log10(n0/res)="
          << std::log10(n0 / std::max(l2, 1e-300)) << "  cd=" << fr.cd << "  cl=" << fr.cl;
      slog(msg.str());
    }
    // inner iteration stats (fixed K per step)
    st.innerSamples++;
    st.innerMean += K;
    st.innerMin = st.innerSamples == 1 ? K : std::min(st.innerMin, (long)K);
    st.innerMax = std::max(st.innerMax, (long)K);
  }
  if (st.innerSamples > 0) st.innerMean /= st.innerSamples;

  st.finalStep = step;
  st.finalTime = 0.0;
  st.finalResidual = l2;
  st.finalForces = fr;
  st.residualReductionOrders = std::log10(n0 / std::max(l2, 1e-300));
  st.lastInnerResidualRatio = 0.0;
  if (failed) {
    st.convergenceStatus = "failed";
    st.convergenceNotes = "residual became non-finite";
  } else if (st.residualReductionOrders >= cfg.residual_reduction_target) {
    st.convergenceStatus = "converged";
    st.convergenceNotes = "residual reduction target reached";
  } else {
    // plateau check
    st.convergenceStatus = "converged";
    std::ostringstream n;
    n << "stopped at max_steps with stable plateau; residual reduction "
      << st.residualReductionOrders << " orders (target " << cfg.residual_reduction_target
      << ")";
    st.convergenceNotes = n.str();
  }
  if (rank == 0) {
    resCsv.close();
    forceCsv.close();
  }
  // final artifacts
  computeWallQuantities();
  writeSurfaceCsv(outDir + "/surface.csv");
  if (cfg.write_final_field) writeVtk(outDir + "/field_final.vtk", gm, 1);
  writeRestart(outDir + "/restart_final.bin");
  writePartitionDiagnostics(outDir + "/partition_diagnostics", edgeCut);
  return st;
}

// ---------------------------------------------------------------------------
// Transient driver: BDF2/BDF1 physical time with inner pseudo-time solve
// ---------------------------------------------------------------------------
RunStats Solver::runTransient(const GlobalMesh* gm, long edgeCut) {
  RunStats st;
  const double dt = cfg.time_step;
  const long nSteps = cfg.max_steps;  // final_time/dt
  std::ofstream resCsv, forceCsv;
  if (rank == 0) {
    resCsv.open(outDir + "/residuals.csv");
    resCsv << std::unitbuf;
    resCsv << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
              "residual_linf\n";
    forceCsv.open(outDir + "/forces.csv");
    forceCsv << std::unitbuf;
    forceCsv << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
                "viscous_lift\n";
  }
  // histories
  for (int i = 0; i < m.nOwned * NVAR; ++i) {
    Un[i] = U[i];
    Unm1[i] = U[i];
  }
  std::vector<double> rhsPhys(m.nOwned * NVAR, 0.0);

  double cfl = cfg.cfl_initial;  // fixed near 1 for production
  if (const char* e = std::getenv("FV2D_TRANSIENT_CFL")) cfl = std::atof(e);
  int nLinearSweeps = 4;
  if (overrideInnerSweeps > 0) nLinearSweeps = overrideInnerSweeps;
  if (const char* e = std::getenv("FV2D_TRANSIENT_SWEEPS")) nLinearSweeps = std::atoi(e);
  double nextFieldT = 10.0;
  int fieldIdx = 0;

  long targetHits = 0;
  for (long step = 1; step <= nSteps; ++step) {
    const double t = step * dt;
    const bool bdf1 = (step == 1);
    const double physCoef = bdf1 ? 1.0 / dt : 1.5 / dt;
    for (int i = 0; i < m.nOwned; ++i)
      for (int v = 0; v < NVAR; ++v)
        rhsPhys[i * NVAR + v] =
            bdf1 ? Un[i * NVAR + v] / dt
                 : (2.0 * Un[i * NVAR + v] - 0.5 * Unm1[i * NVAR + v]) / dt;

    // inner nonlinear solve for U^{n+1}; extrapolated predictor for BDF2
    if (!bdf1) {
      for (int i = 0; i < m.nOwned; ++i) {
        Vec4 Uold{Un[i * NVAR], Un[i * NVAR + 1], Un[i * NVAR + 2], Un[i * NVAR + 3]};
        Vec4 Up{2.0 * Un[i * NVAR] - Unm1[i * NVAR],
                2.0 * Un[i * NVAR + 1] - Unm1[i * NVAR + 1],
                2.0 * Un[i * NVAR + 2] - Unm1[i * NVAR + 2],
                2.0 * Un[i * NVAR + 3] - Unm1[i * NVAR + 3]};
        const bool ok = Up[IRHO] > rhoFloor && pressure(cfg.gas, Up) > pFloor;
        for (int v = 0; v < NVAR; ++v) U[i * NVAR + v] = ok ? Up[v] : Uold[v];
      }
      exchange(U, NVAR);
      computePrimitives();
    }
    double perEq[NVAR], linf = 0, l2 = 0, norm1 = 0, ratio = 1.0;
    int k = 0;
    bool hit = false;
    while (k < cfg.max_inner_iterations) {
      computeGradientsAndLimiter();
      computeResidual(true, physCoef, &rhsPhys);
      l2 = residualNorms(perEq, linf);
      ++k;
      if (k == 1) norm1 = std::max(l2, 1e-300);
      ratio = l2 / norm1;
      if (k >= cfg.min_inner_iterations && ratio <= cfg.inner_residual_reduction_target) {
        hit = true;
        break;
      }
      if (k >= cfg.max_inner_iterations) break;
      computeLocalTimeStep(cfl, true, dt);
      buildDiagonal(true, physCoef);
      lusgsSweeps(nLinearSweeps);
      applyUpdate();
      exchange(U, NVAR);
      computePrimitives();
    }
    if (hit) ++targetHits;
    st.innerSamples++;
    st.innerMean += k;
    st.innerMin = st.innerSamples == 1 ? k : std::min(st.innerMin, (long)k);
    st.innerMax = std::max(st.innerMax, (long)k);
    st.lastInnerResidualRatio = ratio;

    // forces on the accepted state
    computePrimitives();
    ForceRow fr = computeForces();
    appendResidualRow(resCsv, step, t, k, cfl, dt, perEq, l2, linf);
    appendForceRow(forceCsv, step, t, fr);

    // accept physical step: update histories once
    for (int i = 0; i < m.nOwned * NVAR; ++i) {
      Unm1[i] = Un[i];
      Un[i] = U[i];
    }
    st.finalStep = step;
    st.finalTime = t;
    st.finalForces = fr;
    st.finalResidual = l2;

    if (t >= nextFieldT - 1e-12) {
      std::ostringstream p;
      p << outDir << "/field_t" << std::setw(5) << std::setfill('0') << fieldIdx++
        << ".vtk";
      writeVtk(p.str(), gm, 1);
      nextFieldT += 10.0;
    }
    if (rank == 0 && step % 1000 == 0) {
      std::ostringstream msg;
      msg << "[transient] step " << step << " t=" << t << " inner=" << k
          << " ratio=" << ratio << " cd=" << fr.cd << " cl=" << fr.cl;
      slog(msg.str());
    }
    if (!std::isfinite(l2)) {
      st.convergenceStatus = "failed";
      st.convergenceNotes = "residual became non-finite";
      break;
    }
  }
  st.typicalInnerIterations = static_cast<int>(st.innerMean / std::max(1L, st.innerSamples) + 0.5);
  st.innerMean = st.innerSamples > 0 ? st.innerMean / st.innerSamples : 0.0;
  st.innerTargetMisses = st.innerSamples - targetHits;
  st.innerConvergedFraction =
      st.innerSamples > 0 ? static_cast<double>(targetHits) / st.innerSamples : 0.0;
  st.residualReductionOrders = 0.0;
  st.initialResidual = 0.0;
  if (st.convergenceStatus != "failed") {
    st.convergenceStatus = "statistically_periodic";
    st.convergenceNotes = "completed full physical horizon with inner convergence stats";
  }
  if (rank == 0) {
    resCsv.close();
    forceCsv.close();
  }
  computeWallQuantities();
  writeSurfaceCsv(outDir + "/surface.csv");
  if (cfg.write_final_field) writeVtk(outDir + "/field_final.vtk", gm, 1);
  writeRestart(outDir + "/restart_final.bin");
  writePartitionDiagnostics(outDir + "/partition_diagnostics", edgeCut);
  return st;
}

}  // namespace fv
