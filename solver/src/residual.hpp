#pragma once
// Solver core: state storage, halo pipeline, conservative residual assembly
// (inviscid Rusanov + viscous + boundary fluxes), second-order limited
// reconstruction, local pseudo time steps, symmetric Gauss-Seidel (SGS)
// implicit relaxation, force evaluation, and surface-state output.

#include <array>
#include <string>
#include <vector>

#include "config.hpp"
#include "flux.hpp"
#include "halo.hpp"
#include "partition.hpp"
#include "physics.hpp"
#include "reconstruction.hpp"

namespace cfd2d {

struct ResidualNorms {
  double l2[4] = {0, 0, 0, 0};
  double l2Total = 0.0;
  double linf = 0.0;
};

struct ForceCoefficients {
  double cl = 0, cd = 0, cmz = 0;
  double pressureDrag = 0, viscousDrag = 0;
  double pressureLift = 0, viscousLift = 0;
};

struct SurfaceRow {
  double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
  std::string tag;
};

class SolverCore {
 public:
  SolverCore(const LocalMesh& lm, const CaseConfig& cfg, int rank, int nRanks,
             LimiterType limiterType, bool secondOrder, double updateOmega = 0.5);

  int nOwned() const { return lm_.nOwned; }
  int nLocal() const { return lm_.nLocal; }

  std::vector<double>& U() { return U_; }
  const std::vector<double>& U() const { return U_; }

  void setUniform(const Vec4& U0);
  void syncState();

  // Refresh primitive state W_ and Tcell_ from U_ (no MPI; ghost U must be
  // synchronized). Gradients/limiters are not touched, so this is the cheap
  // per-inner-iteration state refresh.
  void updatePrimitives();

  // Recompute primitive state, gradients (rho,u,v,p,T), and limiters;
  // exchanges ghost gradients/limiters. Frozen during inner iterations.
  void updateReconstruction();
  void setSecondOrder(bool enabled) { secondOrder_ = enabled; }

  // Assemble the spatial residual R_ on owned cells using current U plus the
  // frozen reconstruction. Also refreshes per-face spectral radii, local
  // pseudo time steps for the given CFL, and the SGS diagonal.
  void computeResidual(double cfl);

  // Add the BDF1/BDF2 physical-time term to R_:
  //   R_ += V * (c0 U - c1 Un + c2 Unm1) / dt
  // and add V*c0/dt to the implicit diagonal. Histories frozen during inner
  // iterations.
  void addPhysicalTimeTerm(double c0, double c1, double c2, double dt,
                           const std::vector<double>& Un,
                           const std::vector<double>& Unm1);

  ResidualNorms residualNorms() const;

  // Snapshot the current residual as the fixed right-hand side for the
  // frozen linear system solved by the inner iterations (defect correction).
  void freezeRhs();

  // One symmetric Gauss-Seidel sweep on the FROZEN linear system
  //   (V/dtau + dR1/dU) delta = -R_rhs
  // with the simplified first-order Rusanov scalar Jacobian. Ghost deltas
  // are lagged (block-Jacobi across partition interfaces).
  void sgsSweepLinear();

  // Norms of the linear defect r = R_rhs + A delta with the current delta.
  ResidualNorms linearDefectNorms() const;

  // Reassemble residual/implicit Jacobian from current U with a caller CFL.
  // Useful for robust nonlinear inner iterations and diagnostics.
  void reassemble(double cfl) { updateReconstruction(); computeResidual(cfl); }

  // One symmetric Gauss-Seidel relaxation sweep against the CURRENT residual
  // R_ (nonlinear sub-iteration used by the dual-time transient path).
  void sgsSweep();

  // U += delta on owned cells with per-cell positivity damping of rho and p.
  void applyUpdate();

  // Exchange ghost deltas so SGS sees fresh neighbor updates.
  void syncDelta();

  // Wall force coefficients, globally reduced. Needs current reconstruction.
  ForceCoefficients computeForces() const;

  // Wall surface rows with boundary-condition semantics (no-slip rows report
  // zero velocity; slip-wall rows report tangential velocity). Local rows.
  std::vector<SurfaceRow> computeSurfaceRows() const;

  // Vorticity (z) at owned cells from the current velocity gradients.
  std::vector<double> computeVorticity() const;

  // Global mean of the local pseudo time step (for logging).
  double meanDtau() const;

  // Debug: compare analytic LU-SGS operator A.delta against the finite-
  // difference Jacobian of the residual. Returns relative L2 error.
  double verifyJacobian();

  const LocalMesh& mesh() const { return lm_; }
  const CaseConfig& config() const { return cfg_; }
  const FluxContext& fluxContext() const { return ctx_; }
  const std::vector<double>& primitive() const { return W_; }
  const std::vector<double>& gradients() const { return gradW_; }
  const std::vector<double>& limiter() const { return limiter_; }
  const std::vector<double>& residual() const { return R_; }
  const std::vector<double>& delta() const { return delta_; }
  std::vector<double>& delta() { return delta_; }

 private:
  // Shared SGS sweep implementation against a given right-hand-side field.
  void sgsSweepAgainst(const std::vector<double>& rhsField);

  void faceStates(int f, Vec4& WL, Vec4& WR) const;

  const LocalMesh& lm_;
  CaseConfig cfg_;
  int rank_, nRanks_;
  LimiterType limiterType_;
  bool secondOrder_;
  double updateOmega_ = 0.5;

  FluxContext ctx_;
  Vec4 Uinf_{};
  PositivityFloors floors_;

  LsqStencil lsq_;
  HaloExchange halo4_;   // U, delta, limiter (sequential use)
  HaloExchange halo10_;  // gradients of (rho,u,v,p,T)

  std::vector<double> U_;        // nLocal*4 conservative
  std::vector<double> W_;        // nLocal*4 primitive (rho,u,v,p)
  std::vector<double> Tcell_;    // nLocal temperature
  std::vector<double> gradW_;    // nLocal*10 gradients of (rho,u,v,p,T)
  std::vector<double> limiter_;  // nLocal*4
  std::vector<double> R_;        // nOwned*4 residual
  std::vector<double> R_rhs_;    // nOwned*4 frozen RHS for linear inner solve
  std::vector<double> delta_;    // nLocal*4 SGS update (ghosts lagged)

  std::vector<double> lamL_, lamR_, lamF_;  // nFaces
  // LU-SGS off-diagonal blocks per face: 0.5(A_R^n - lamF I) for the L row,
  // 0.5(lamF I - A_L^n) for the R row; 32 doubles per face, interior only.
  std::vector<double> faceMat_;
  std::vector<double> lamVisc_;             // nFaces (geometric viscous factor)
  std::vector<double> faceDist_;            // nFaces: |cR-cL| or 2|xf-cL|
  std::vector<double> diag_;                // nOwned SGS diagonal
  std::vector<double> dtau_;                // nOwned local pseudo time step
};

}  // namespace cfd2d
