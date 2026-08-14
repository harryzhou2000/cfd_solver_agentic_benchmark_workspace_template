#include "residual.hpp"

#include <algorithm>
#include <cmath>

#include <mpi.h>

namespace cfd2d {

namespace {

inline void addTo(std::vector<double>& R, int cell, const Vec4& F, double s) {
  for (int q = 0; q < 4; ++q) R[4 * cell + q] += s * F[q];
}

}  // namespace

SolverCore::SolverCore(const LocalMesh& lm, const CaseConfig& cfg, int rank,
                       int nRanks, LimiterType limiterType, bool secondOrder,
                       double updateOmega)
    : lm_(lm), cfg_(cfg), rank_(rank), nRanks_(nRanks),
      limiterType_(limiterType), secondOrder_(secondOrder),
      updateOmega_(updateOmega) {
  ctx_.gas = Gas(cfg.gas);
  ctx_.rusanovScale = cfg.run.rusanovDissipationScale;
  ctx_.viscous = cfg.isViscous();
  ctx_.mu = cfg.isViscous() ? cfg.viscosity() : 0.0;
  ctx_.kCond = cfg.isViscous() ? cfg.thermalConductivity() : 0.0;

  Uinf_ = conservedFromPrimitive(ctx_.gas, cfg.freestream.rho, cfg.freestream.u,
                                 cfg.freestream.v, cfg.freestream.pressure);
  floors_.rhoFloor = 1e-6 * cfg.freestream.rho;
  floors_.pFloor = 1e-6 * cfg.freestream.pressure;

  lsq_ = buildLsqStencil(lm_);
  halo4_.init(lm_, 4);
  halo10_.init(lm_, 10);

  const int nL = lm_.nLocal, nO = lm_.nOwned, nF = lm_.nFaces;
  U_.assign(nL * 4, 0.0);
  W_.assign(nL * 4, 0.0);
  Tcell_.assign(nL, 0.0);
  gradW_.assign(nL * 10, 0.0);
  limiter_.assign(nL * 4, 1.0);
  R_.assign(nO * 4, 0.0);
  R_rhs_.assign(nO * 4, 0.0);
  delta_.assign(nL * 4, 0.0);
  lamL_.assign(nF, 0.0);
  lamR_.assign(nF, 0.0);
  lamF_.assign(nF, 0.0);
  faceMat_.assign(nF * 32, 0.0);
  lamVisc_.assign(nF, 0.0);
  faceDist_.assign(nF, 0.0);
  diag_.assign(nO, 0.0);
  dtau_.assign(nO, 0.0);

  // Geometric per-face factors: cell-center distance (interior) or twice the
  // centroid-to-face distance (boundary), and the viscous spectral-radius
  // factor |A| / d (multiplied by C_v * mu / rho at run time).
  for (int f = 0; f < nF; ++f) {
    const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
    double dx, dy;
    if (R >= 0) {
      dx = lm_.cellCx[R] - lm_.cellCx[L];
      dy = lm_.cellCy[R] - lm_.cellCy[L];
    } else {
      dx = 2.0 * (lm_.faceCx[f] - lm_.cellCx[L]);
      dy = 2.0 * (lm_.faceCy[f] - lm_.cellCy[L]);
    }
    const double d = std::hypot(dx, dy);
    faceDist_[f] = std::max(d, 1e-300);
    lamVisc_[f] = lm_.faceLen[f] / faceDist_[f];
  }
}

void SolverCore::setUniform(const Vec4& U0) {
  for (int c = 0; c < lm_.nLocal; ++c)
    for (int q = 0; q < 4; ++q) U_[4 * c + q] = U0[q];
}

void SolverCore::syncState() { halo4_.exchange(U_); }

void SolverCore::updatePrimitives() {
  // Ghost U is synchronized, so ghost primitives are consistent locally.
  for (int c = 0; c < lm_.nLocal; ++c) {
    Vec4 Uc{U_[4 * c], U_[4 * c + 1], U_[4 * c + 2], U_[4 * c + 3]};
    Vec4 Wc = primitiveFromConserved(ctx_.gas, Uc);
    for (int q = 0; q < 4; ++q) W_[4 * c + q] = Wc[q];
    Tcell_[c] = temperature(ctx_.gas, Wc[0], Wc[3]);
  }
}

void SolverCore::updateReconstruction() {
  updatePrimitives();
  // Gradients of (rho,u,v,p) and T on owned cells.
  std::vector<double> grad4;  // nOwned*8
  computePrimitiveGradients(lsq_, W_, grad4);
  std::vector<double> gradT;  // nOwned*2
  computeScalarGradient(lsq_, Tcell_, gradT);
  for (int i = 0; i < lm_.nOwned; ++i) {
    for (int q = 0; q < 4; ++q) {
      gradW_[10 * i + 2 * q] = grad4[8 * i + 2 * q];
      gradW_[10 * i + 2 * q + 1] = grad4[8 * i + 2 * q + 1];
    }
    gradW_[10 * i + 8] = gradT[2 * i];
    gradW_[10 * i + 9] = gradT[2 * i + 1];
  }
  halo10_.exchange(gradW_);
  computeLimiter(lm_, lsq_, limiterType_, W_, gradW_, 10, limiter_);
  halo4_.exchange(limiter_);
}

void SolverCore::faceStates(int f, Vec4& WL, Vec4& WR) const {
  const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
  const double fx = lm_.faceCx[f], fy = lm_.faceCy[f];
  if (!secondOrder_) {
    for (int q = 0; q < 4; ++q) WL[q] = W_[4 * L + q];
  } else {
    const double dx = fx - lm_.cellCx[L], dy = fy - lm_.cellCy[L];
    for (int q = 0; q < 4; ++q) {
      WL[q] = W_[4 * L + q] +
              limiter_[4 * L + q] *
                  (gradW_[10 * L + 2 * q] * dx + gradW_[10 * L + 2 * q + 1] * dy);
    }
    if (WL[0] < floors_.rhoFloor || WL[3] < floors_.pFloor) {
      for (int q = 0; q < 4; ++q) WL[q] = W_[4 * L + q];  // positivity fallback
    }
  }
  if (R >= 0) {
    if (!secondOrder_) {
      for (int q = 0; q < 4; ++q) WR[q] = W_[4 * R + q];
    } else {
      const double dx = fx - lm_.cellCx[R], dy = fy - lm_.cellCy[R];
      for (int q = 0; q < 4; ++q) {
        WR[q] = W_[4 * R + q] +
                limiter_[4 * R + q] *
                    (gradW_[10 * R + 2 * q] * dx + gradW_[10 * R + 2 * q + 1] * dy);
      }
      if (WR[0] < floors_.rhoFloor || WR[3] < floors_.pFloor) {
        for (int q = 0; q < 4; ++q) WR[q] = W_[4 * R + q];
      }
    }
  } else {
    const int fam = lm_.faceBcFam[f];
    const BcType bc = bcTypeFromString(cfg_.bcMap.at(lm_.famNames[fam]));
    WR = boundaryGhostState(bc, ctx_, cfg_.freestream, WL, lm_.faceNx[f],
                            lm_.faceNy[f]);
  }
}

namespace {

// Face-centered gradients of (u, v, T) for the viscous flux. Interior faces
// use the averaged cell gradients with a normal-direction correction; boundary
// faces use a one-sided correction with the ghost state.
void viscousFaceGrad(const LocalMesh& lm, const std::vector<double>& W,
                     const std::vector<double>& Tcell,
                     const std::vector<double>& gradW, const Gas& gas, int f,
                     const Vec4& WL, const Vec4& WR, double out[3][2]) {
  const int L = lm.faceCellL[f], R = lm.faceCellR[f];
  if (R >= 0) {
    const double dx = lm.cellCx[R] - lm.cellCx[L];
    const double dy = lm.cellCy[R] - lm.cellCy[L];
    const double d2 = std::max(dx * dx + dy * dy, 1e-300);
    const double phiL[3] = {W[4 * L + 1], W[4 * L + 2], Tcell[L]};
    const double phiR[3] = {W[4 * R + 1], W[4 * R + 2], Tcell[R]};
    for (int v = 0; v < 3; ++v) {
      const int off = (v == 0) ? 2 : (v == 1) ? 4 : 8;
      const double gLx = gradW[10 * L + off], gLy = gradW[10 * L + off + 1];
      const double gRx = gradW[10 * R + off], gRy = gradW[10 * R + off + 1];
      const double gax = 0.5 * (gLx + gRx), gay = 0.5 * (gLy + gRy);
      const double corr = ((phiR[v] - phiL[v]) - (gax * dx + gay * dy)) / d2;
      out[v][0] = gax + corr * dx;
      out[v][1] = gay + corr * dy;
    }
  } else {
    const double nx = lm.faceNx[f], ny = lm.faceNy[f];
    const double rx = lm.faceCx[f] - lm.cellCx[L];
    const double ry = lm.faceCy[f] - lm.cellCy[L];
    const double dn = rx * nx + ry * ny;
    const double dx = 2.0 * dn * nx, dy = 2.0 * dn * ny;  // reflection vector
    const double d2 = std::max(dx * dx + dy * dy, 1e-300);
    const double phiL[3] = {W[4 * L + 1], W[4 * L + 2], Tcell[L]};
    const double phiG[3] = {WR[1], WR[2], temperature(gas, WR[0], WR[3])};
    for (int v = 0; v < 3; ++v) {
      const int off = (v == 0) ? 2 : (v == 1) ? 4 : 8;
      const double gLx = gradW[10 * L + off], gLy = gradW[10 * L + off + 1];
      const double corr = ((phiG[v] - phiL[v]) - (gLx * dx + gLy * dy)) / d2;
      out[v][0] = gLx + corr * dx;
      out[v][1] = gLy + corr * dy;
    }
  }
}

}  // namespace

void SolverCore::computeResidual(double cfl) {
  std::fill(R_.begin(), R_.end(), 0.0);
  std::vector<double> lambdaSum(lm_.nOwned, 0.0);
  const double viscC =
      std::max(4.0 / 3.0, ctx_.gas.gamma / ctx_.gas.prandtl);

  for (int f = 0; f < lm_.nFaces; ++f) {
    const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
    const double nx = lm_.faceNx[f], ny = lm_.faceNy[f];
    const double len = lm_.faceLen[f];

    Vec4 WL, WR;
    faceStates(f, WL, WR);
    const Vec4 UL =
        conservedFromPrimitive(ctx_.gas, WL[0], WL[1], WL[2], WL[3]);
    const Vec4 UR =
        conservedFromPrimitive(ctx_.gas, WR[0], WR[1], WR[2], WR[3]);

    double lamFace = 0.0;
    const Vec4 Fi = rusanovFlux(ctx_, WL, WR, UL, UR, nx, ny, len, &lamFace);
    lamL_[f] = convectiveSpectralRadius(ctx_.gas, WL, nx, ny, len);
    lamR_[f] = convectiveSpectralRadius(ctx_.gas, WR, nx, ny, len);
    double lamV = 0.0;

    // LU-SGS matrix off-diagonal blocks (interior faces), built from the
    // current face states and frozen during the inner linear solve.
    if (R >= 0) {
      double AR[16], AL[16];
      eulerFluxJacobian(ctx_.gas, WR, nx, ny, AR);
      eulerFluxJacobian(ctx_.gas, WL, nx, ny, AL);
      const double lf = lamFace;  // without viscous part (added after)
      double* mLR = faceMat_.data() + 32 * f;       // row L, col R
      double* mRL = faceMat_.data() + 32 * f + 16;  // row R, col L
      for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
          // row L, col R:  d Phi/d U_R = 0.5 (A_R - lam I) * len
          mLR[4 * r + c] = 0.5 * (AR[4 * r + c] * len - (r == c ? lf : 0.0));
          // row R, col L:  d(-Phi)/d U_L = -0.5 (A_L + lam I) * len
          mRL[4 * r + c] = -0.5 * (AL[4 * r + c] * len + (r == c ? lf : 0.0));
        }
    }

    Vec4 Fv{0., 0., 0., 0.};
    if (ctx_.viscous) {
      const double rhoF = std::max(0.5 * (WL[0] + WR[0]), floors_.rhoFloor);
      lamV = viscC * ctx_.mu / rhoF * lamVisc_[f];

      bool skipVisc = false;
      if (R < 0) {
        const int fam = lm_.faceBcFam[f];
        const BcType bc = bcTypeFromString(cfg_.bcMap.at(lm_.famNames[fam]));
        skipVisc = (bc == BcType::SlipWall);
      }
      if (!skipVisc) {
        double gr[3][2];
        viscousFaceGrad(lm_, W_, Tcell_, gradW_, ctx_.gas, f, WL, WR, gr);
        double uf = 0.5 * (WL[1] + WR[1]);
        double vf = 0.5 * (WL[2] + WR[2]);
        if (R < 0) {
          const int fam = lm_.faceBcFam[f];
          const BcType bc = bcTypeFromString(cfg_.bcMap.at(lm_.famNames[fam]));
          if (bc == BcType::NoSlipAdiabaticWall) {
            uf = 0.0;
            vf = 0.0;
          }
        }
        Fv = viscousFlux(ctx_, gr[0][0], gr[0][1], gr[1][0], gr[1][1],
                         gr[2][0], gr[2][1], uf, vf, nx, ny, len);
      }
    }
    lamF_[f] = lamFace + lamV;

    Vec4 F;
    for (int q = 0; q < 4; ++q) F[q] = Fi[q] - Fv[q];
    if (L < lm_.nOwned) {
      addTo(R_, L, F, +1.0);
      lambdaSum[L] += lamF_[f];
    }
    if (R >= 0 && R < lm_.nOwned) {
      addTo(R_, R, F, -1.0);
      lambdaSum[R] += lamF_[f];
    }
  }

  for (int i = 0; i < lm_.nOwned; ++i) {
    dtau_[i] = cfl * lm_.cellVol[i] / std::max(lambdaSum[i], 1e-300);
    // Add the full spectral-radius contribution to the SGS diagonal.  The
    // extra damping is important for viscous high-Mach cases where the
    // nonlinear flux-difference off-diagonals can otherwise overshoot.
    diag_[i] = lm_.cellVol[i] / dtau_[i] + 1.0 * lambdaSum[i];
  }
}

void SolverCore::addPhysicalTimeTerm(double c0, double c1, double c2,
                                     double dt, const std::vector<double>& Un,
                                     const std::vector<double>& Unm1) {
  for (int i = 0; i < lm_.nOwned; ++i) {
    const double vOverDt = lm_.cellVol[i] / dt;
    for (int q = 0; q < 4; ++q) {
      R_[4 * i + q] += vOverDt * (c0 * U_[4 * i + q] - c1 * Un[4 * i + q] +
                                  c2 * Unm1[4 * i + q]);
    }
    diag_[i] += vOverDt * c0;
  }
}

ResidualNorms SolverCore::residualNorms() const {
  double sumSq[4] = {0, 0, 0, 0};
  double linf = 0.0;
  for (int i = 0; i < lm_.nOwned; ++i) {
    for (int q = 0; q < 4; ++q) {
      const double r = R_[4 * i + q];
      sumSq[q] += r * r;
      linf = std::max(linf, std::abs(r));
    }
  }
  double gSumSq[4] = {0, 0, 0, 0};
  double gLinf = 0.0, gCells = 0.0;
  double localCells = static_cast<double>(lm_.nOwned);
  MPI_Allreduce(sumSq, gSumSq, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf, &gLinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&localCells, &gCells, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  ResidualNorms n;
  double tot = 0.0;
  for (int q = 0; q < 4; ++q) {
    n.l2[q] = std::sqrt(gSumSq[q] / gCells);
    tot += gSumSq[q];
  }
  n.l2Total = std::sqrt(tot / (4.0 * gCells));
  n.linf = gLinf;
  return n;
}

void SolverCore::sgsSweep() {
  // Nonlinear sub-iteration: relax against the current residual R_.
  sgsSweepAgainst(R_);
}

void SolverCore::freezeRhs() { R_rhs_ = R_; }

void SolverCore::sgsSweepLinear() { sgsSweepAgainst(R_rhs_); }

ResidualNorms SolverCore::linearDefectNorms() const {
  // r = R_rhs + A delta with the frozen first-order operator A.
  double sumSq[4] = {0, 0, 0, 0};
  double linf = 0.0;
  for (int i = 0; i < lm_.nOwned; ++i) {
    double r[4];
    for (int q = 0; q < 4; ++q)
      r[q] = R_rhs_[4 * i + q] + diag_[i] * delta_[4 * i + q];
    for (int f : lm_.cellFaces[i]) {
      const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
      const int j = (L == i) ? R : L;
      if (j < 0) continue;
      const double* M = faceMat_.data() + 32 * f + ((L == i) ? 0 : 16);
      for (int q = 0; q < 4; ++q)
        for (int k = 0; k < 4; ++k) r[q] += M[4 * q + k] * delta_[4 * j + k];
    }
    for (int q = 0; q < 4; ++q) {
      sumSq[q] += r[q] * r[q];
      linf = std::max(linf, std::abs(r[q]));
    }
  }
  double gSumSq[4] = {0, 0, 0, 0};
  double gLinf = 0.0, gCells = 0.0;
  double localCells = static_cast<double>(lm_.nOwned);
  MPI_Allreduce(sumSq, gSumSq, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf, &gLinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&localCells, &gCells, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  ResidualNorms n;
  double tot = 0.0;
  for (int q = 0; q < 4; ++q) {
    n.l2[q] = std::sqrt(gSumSq[q] / gCells);
    tot += gSumSq[q];
  }
  n.l2Total = std::sqrt(tot / (4.0 * gCells));
  n.linf = gLinf;
  return n;
}

void SolverCore::sgsSweepAgainst(const std::vector<double>& rhsField) {
  // Nonlinear LU-SGS sweep with a flux-difference off-diagonal treatment.
  // For a face (i,j), the off-diagonal contribution to the residual of cell i
  // from a neighbor update delta_j is evaluated as the numerical-flux
  // difference F_num(U_i, U_j + delta_j) - F_num(U_i, U_j), which captures the
  // true (sign-consistent) flux Jacobian without an analytic matrix. Forward
  // and backward sweeps use lower/upper owned neighbors respectively; ghost
  // neighbor updates are lagged (block-Jacobi across partition interfaces).
  const int nO = lm_.nOwned;
  // Forward sweep.
  for (int i = 0; i < nO; ++i) {
    double rhs[4] = {rhsField[4 * i], rhsField[4 * i + 1], rhsField[4 * i + 2],
                     rhsField[4 * i + 3]};
    for (int f : lm_.cellFaces[i]) {
      const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
      const int j = (L == i) ? R : L;
      if (j < 0 || j >= nO || j >= i) continue;
      // Outward normal of cell i at this face.
      const double sgn = (L == i) ? 1.0 : -1.0;
      const double nx = sgn * lm_.faceNx[f], ny = sgn * lm_.faceNy[f];
      const double len = lm_.faceLen[f];
      // Flux out of cell i: i is the "left" state, j perturbed by delta_j.
      Vec4 Wi{W_[4 * i], W_[4 * i + 1], W_[4 * i + 2], W_[4 * i + 3]};
      Vec4 Wj{W_[4 * j], W_[4 * j + 1], W_[4 * j + 2], W_[4 * j + 3]};
      Vec4 Uj{U_[4 * j], U_[4 * j + 1], U_[4 * j + 2], U_[4 * j + 3]};
      Vec4 Uj2{Uj[0] + delta_[4 * j], Uj[1] + delta_[4 * j + 1],
               Uj[2] + delta_[4 * j + 2], Uj[3] + delta_[4 * j + 3]};
      Vec4 Wj2 = primitiveFromConserved(ctx_.gas, Uj2);
      Vec4 Ui{U_[4 * i], U_[4 * i + 1], U_[4 * i + 2], U_[4 * i + 3]};
      Vec4 Fold = rusanovFlux(ctx_, Wi, Wj, Ui, Uj, nx, ny, len, nullptr);
      Vec4 Fnew = rusanovFlux(ctx_, Wi, Wj2, Ui, Uj2, nx, ny, len, nullptr);
      for (int q = 0; q < 4; ++q) rhs[q] += (Fnew[q] - Fold[q]);
    }
    for (int q = 0; q < 4; ++q) delta_[4 * i + q] = -rhs[q] / diag_[i];
  }
  syncDelta();
  // Backward sweep.
  for (int i = nO - 1; i >= 0; --i) {
    double rhs[4] = {rhsField[4 * i], rhsField[4 * i + 1], rhsField[4 * i + 2],
                     rhsField[4 * i + 3]};
    for (int f : lm_.cellFaces[i]) {
      const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
      const int j = (L == i) ? R : L;
      if (j < 0 || j >= nO || j <= i) continue;
      const double sgn = (L == i) ? 1.0 : -1.0;
      const double nx = sgn * lm_.faceNx[f], ny = sgn * lm_.faceNy[f];
      const double len = lm_.faceLen[f];
      Vec4 Wi{W_[4 * i], W_[4 * i + 1], W_[4 * i + 2], W_[4 * i + 3]};
      Vec4 Wj{W_[4 * j], W_[4 * j + 1], W_[4 * j + 2], W_[4 * j + 3]};
      Vec4 Uj{U_[4 * j], U_[4 * j + 1], U_[4 * j + 2], U_[4 * j + 3]};
      Vec4 Uj2{Uj[0] + delta_[4 * j], Uj[1] + delta_[4 * j + 1],
               Uj[2] + delta_[4 * j + 2], Uj[3] + delta_[4 * j + 3]};
      Vec4 Wj2 = primitiveFromConserved(ctx_.gas, Uj2);
      Vec4 Ui{U_[4 * i], U_[4 * i + 1], U_[4 * i + 2], U_[4 * i + 3]};
      Vec4 Fold = rusanovFlux(ctx_, Wi, Wj, Ui, Uj, nx, ny, len, nullptr);
      Vec4 Fnew = rusanovFlux(ctx_, Wi, Wj2, Ui, Uj2, nx, ny, len, nullptr);
      for (int q = 0; q < 4; ++q) rhs[q] += (Fnew[q] - Fold[q]);
    }
    for (int q = 0; q < 4; ++q) delta_[4 * i + q] = -rhs[q] / diag_[i];
  }
  syncDelta();
}

void SolverCore::applyUpdate() {
  for (int i = 0; i < lm_.nOwned; ++i) {
    Vec4 Uc{U_[4 * i], U_[4 * i + 1], U_[4 * i + 2], U_[4 * i + 3]};
    Vec4 d{delta_[4 * i], delta_[4 * i + 1], delta_[4 * i + 2],
           delta_[4 * i + 3]};
    double alpha = updateOmega_;
    const double rhoMin = std::max(0.05 * Uc[0], floors_.rhoFloor);
    if (d[0] < 0.0 && Uc[0] + alpha * d[0] < rhoMin) {
      alpha = std::min(alpha, (rhoMin - Uc[0]) / d[0]);
    }
    const double pMin = std::max(0.05 * W_[4 * i + 3], floors_.pFloor);
    for (int it = 0; it < 20; ++it) {
      Vec4 Un{Uc[0] + alpha * d[0], Uc[1] + alpha * d[1],
              Uc[2] + alpha * d[2], Uc[3] + alpha * d[3]};
      if (Un[0] >= rhoMin && pressure(ctx_.gas, Un) >= pMin) break;
      alpha *= 0.5;
    }
    for (int q = 0; q < 4; ++q) U_[4 * i + q] += alpha * d[q];
  }
}

void SolverCore::syncDelta() { halo4_.exchange(delta_); }

ForceCoefficients SolverCore::computeForces() const {
  double fp[2] = {0, 0}, fv[2] = {0, 0}, mp = 0.0, mv = 0.0;
  const double xc = cfg_.reference.momentCenter[0];
  const double yc = cfg_.reference.momentCenter[1];
  for (int f = 0; f < lm_.nFaces; ++f) {
    const int R = lm_.faceCellR[f];
    const int L = lm_.faceCellL[f];
    if (R >= 0 || L >= lm_.nOwned) continue;
    const int fam = lm_.faceBcFam[f];
    const BcType bc = bcTypeFromString(cfg_.bcMap.at(lm_.famNames[fam]));
    if (bc == BcType::Farfield) continue;
    Vec4 WL, WR;
    faceStates(f, WL, WR);
    const double nx = lm_.faceNx[f], ny = lm_.faceNy[f];
    const double len = lm_.faceLen[f];
    const double rx = lm_.faceCx[f] - xc, ry = lm_.faceCy[f] - yc;
    const double px = WL[3] * nx * len, py = WL[3] * ny * len;
    fp[0] += px;
    fp[1] += py;
    mp += rx * py - ry * px;
    if (bc == BcType::NoSlipAdiabaticWall && ctx_.viscous) {
      double gr[3][2];
      viscousFaceGrad(lm_, W_, Tcell_, gradW_, ctx_.gas, f, WL, WR, gr);
      const double div = gr[0][0] + gr[1][1];
      const double txx = 2 * ctx_.mu * gr[0][0] - (2.0 / 3.0) * ctx_.mu * div;
      const double tyy = 2 * ctx_.mu * gr[1][1] - (2.0 / 3.0) * ctx_.mu * div;
      const double txy = ctx_.mu * (gr[0][1] + gr[1][0]);
      const double tx = txx * nx + txy * ny;
      const double ty = txy * nx + tyy * ny;
      const double tn = tx * nx + ty * ny;
      // Tangential traction; force on the body is its negative.
      const double ttx = tx - tn * nx, tty = ty - tn * ny;
      const double vx = -ttx * len, vy = -tty * len;
      fv[0] += vx;
      fv[1] += vy;
      mv += rx * vy - ry * vx;
    }
  }
  double local[6] = {fp[0], fp[1], fv[0], fv[1], mp, mv};
  double global[6] = {0, 0, 0, 0, 0, 0};
  MPI_Allreduce(local, global, 6, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  const auto& fs = cfg_.freestream;
  const double qInf = 0.5 * fs.rho * fs.velMag * fs.velMag;
  const double cosA = fs.u / fs.velMag, sinA = fs.v / fs.velMag;
  const double refA = cfg_.reference.area;
  const double refL = cfg_.reference.length;
  const double Fx = global[0] + global[2], Fy = global[1] + global[3];
  ForceCoefficients c;
  c.cd = (Fx * cosA + Fy * sinA) / (qInf * refA);
  c.cl = (-Fx * sinA + Fy * cosA) / (qInf * refA);
  c.cmz = (global[4] + global[5]) / (qInf * refA * refL);
  c.pressureDrag = (global[0] * cosA + global[1] * sinA) / (qInf * refA);
  c.viscousDrag = (global[2] * cosA + global[3] * sinA) / (qInf * refA);
  c.pressureLift = (-global[0] * sinA + global[1] * cosA) / (qInf * refA);
  c.viscousLift = (-global[2] * sinA + global[3] * cosA) / (qInf * refA);
  return c;
}

std::vector<SurfaceRow> SolverCore::computeSurfaceRows() const {
  std::vector<SurfaceRow> rows;
  const auto& fs = cfg_.freestream;
  const double qInf = 0.5 * fs.rho * fs.velMag * fs.velMag;
  const double fx = fs.u / fs.velMag, fy = fs.v / fs.velMag;
  for (int f = 0; f < lm_.nFaces; ++f) {
    const int R = lm_.faceCellR[f];
    const int L = lm_.faceCellL[f];
    if (R >= 0 || L >= lm_.nOwned) continue;
    const int fam = lm_.faceBcFam[f];
    const BcType bc = bcTypeFromString(cfg_.bcMap.at(lm_.famNames[fam]));
    if (bc == BcType::Farfield) continue;
    Vec4 WL, WR;
    faceStates(f, WL, WR);
    const double nx = lm_.faceNx[f], ny = lm_.faceNy[f];
    SurfaceRow row;
    row.x = lm_.faceCx[f];
    row.y = lm_.faceCy[f];
    row.nx = nx;
    row.ny = ny;
    row.pressure = WL[3];
    row.cp = (WL[3] - fs.pressure) / qInf;
    row.rho = WL[0];
    row.cf = 0.0;
    if (bc == BcType::NoSlipAdiabaticWall) {
      // Boundary-condition values: the wall velocity is exactly zero.
      row.u = 0.0;
      row.v = 0.0;
      row.mach = 0.0;
      if (ctx_.viscous) {
        double gr[3][2];
        viscousFaceGrad(lm_, W_, Tcell_, gradW_, ctx_.gas, f, WL, WR, gr);
        const double div = gr[0][0] + gr[1][1];
        const double txx = 2 * ctx_.mu * gr[0][0] - (2.0 / 3.0) * ctx_.mu * div;
        const double tyy = 2 * ctx_.mu * gr[1][1] - (2.0 / 3.0) * ctx_.mu * div;
        const double txy = ctx_.mu * (gr[0][1] + gr[1][0]);
        const double tx = txx * nx + txy * ny;
        const double ty = txy * nx + tyy * ny;
        const double tn = tx * nx + ty * ny;
        const double ttx = tx - tn * nx, tty = ty - tn * ny;
        row.cf = (-ttx * fx + -tty * fy) / qInf;
      }
    } else {
      // Slip wall: near-zero normal velocity, preserved tangential velocity.
      const double un = WL[1] * nx + WL[2] * ny;
      row.u = WL[1] - un * nx;
      row.v = WL[2] - un * ny;
      const double a = soundSpeed(ctx_.gas, WL[0], WL[3]);
      row.mach = std::hypot(row.u, row.v) / a;
    }
    row.tag = lm_.famNames[fam];
    rows.push_back(row);
  }
  return rows;
}

std::vector<double> SolverCore::computeVorticity() const {
  std::vector<double> omega(lm_.nOwned, 0.0);
  for (int i = 0; i < lm_.nOwned; ++i) {
    omega[i] = gradW_[10 * i + 4] - gradW_[10 * i + 3];  // dv/dx - du/dy
  }
  return omega;
}

double SolverCore::meanDtau() const {
  double s = 0.0;
  for (int i = 0; i < lm_.nOwned; ++i) s += dtau_[i];
  double gs = 0.0, gc = 0.0, lc = static_cast<double>(lm_.nOwned);
  MPI_Allreduce(&s, &gs, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&lc, &gc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  return gs / gc;
}

double SolverCore::verifyJacobian() {
  // Random test direction (deterministic seed).
  std::vector<double> d(lm_.nOwned * 4);
  unsigned int seed = 12345;
  double dnorm = 0.0;
  for (auto& x : d) {
    seed = seed * 1103515245u + 12345u;
    x = 1e-4 * ((seed / 65536u % 32768u) / 32768.0 - 0.5);
    dnorm += x * x;
  }
  dnorm = std::sqrt(dnorm);
  // Analytic A d = D d + sum M d_j (linear operator from frozen faceMat_).
  std::vector<double> Ad(lm_.nOwned * 4, 0.0);
  for (int i = 0; i < lm_.nOwned; ++i) {
    for (int q = 0; q < 4; ++q) Ad[4 * i + q] = diag_[i] * d[4 * i + q];
    for (int f : lm_.cellFaces[i]) {
      const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
      const int j = (L == i) ? R : L;
      if (j < 0) continue;
      const double* M = faceMat_.data() + 32 * f + ((L == i) ? 0 : 16);
      for (int q = 0; q < 4; ++q)
        for (int k = 0; k < 4; ++k) Ad[4 * i + q] += M[4 * q + k] * d[4 * j + k];
    }
  }
  // Ad includes V/dtau d via diag_; the pure Jacobian J d = Ad - (V/dtau) d.
  // Numerical J d = [R(U + eps d) - R(U)] / eps.
  const double eps = 1e-6;
  std::vector<double> Usave(U_.begin(), U_.begin() + lm_.nOwned * 4);
  std::vector<double> R0 = R_;
  for (int i = 0; i < lm_.nOwned; ++i)
    for (int q = 0; q < 4; ++q) U_[4 * i + q] = Usave[4 * i + q] + eps * d[4 * i + q];
  syncState();
  updatePrimitives();
  computeResidual(1.0);  // note: recomputes faceMat_/diag_ (unused here)
  std::vector<double> R1 = R_;
  // Restore state and operator.
  for (int i = 0; i < lm_.nOwned; ++i)
    for (int q = 0; q < 4; ++q) U_[4 * i + q] = Usave[4 * i + q];
  syncState();
  updatePrimitives();
  // Compare (J d) vs (Ad - V/dtau d): but faceMat_/diag_ were rebuilt by the
  // computeResidual above at U+eps d; rebuild at U for a clean comparison.
  computeResidual(1.0);
  std::vector<double> Ad2(lm_.nOwned * 4, 0.0);
  for (int i = 0; i < lm_.nOwned; ++i) {
    for (int q = 0; q < 4; ++q) Ad2[4 * i + q] = diag_[i] * d[4 * i + q];
    for (int f : lm_.cellFaces[i]) {
      const int L = lm_.faceCellL[f], R = lm_.faceCellR[f];
      const int j = (L == i) ? R : L;
      if (j < 0) continue;
      const double* M = faceMat_.data() + 32 * f + ((L == i) ? 0 : 16);
      for (int q = 0; q < 4; ++q)
        for (int k = 0; k < 4; ++k) Ad2[4 * i + q] += M[4 * q + k] * d[4 * j + k];
    }
  }
  double num = 0.0, den = 0.0;
  for (int i = 0; i < lm_.nOwned; ++i) {
    const double vdt = lm_.cellVol[i] / dtau_[i];  // pseudo term inside diag_
    for (int q = 0; q < 4; ++q) {
      const double jd = (R1[4 * i + q] - R0[4 * i + q]) / eps;
      const double ad = Ad2[4 * i + q] - vdt * d[4 * i + q];
      num += (jd - ad) * (jd - ad);
      den += jd * jd;
    }
  }
  double g[2];
  double l[2] = {num, den};
  MPI_Allreduce(l, g, 2, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  return std::sqrt(g[0] / std::max(g[1], 1e-300));
}

}  // namespace cfd2d
