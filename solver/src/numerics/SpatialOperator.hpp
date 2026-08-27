// Cell-centred finite-volume spatial discretisation.
//
// One SpatialOperator owns the rank-local state, the geometric least-squares
// stencils and every quantity derived from them.  evaluateResidual() performs
//   halo exchange -> primitives -> boundary states -> gradients -> limiter
//   -> halo exchange of gradients/limiters -> face fluxes,
// which is the order required for the parallel result to be independent of the
// partitioning: ghost gradients and limiters are always produced by the rank
// that owns the complete stencil of that cell.
#pragma once

#include <mpi.h>

#include <vector>

#include "core/CaseConfig.hpp"
#include "core/Types.hpp"
#include "mesh/LocalMesh.hpp"
#include "numerics/SolverOptions.hpp"
#include "parallel/Comm.hpp"
#include "physics/BoundaryCondition.hpp"
#include "physics/FluxInviscid.hpp"
#include "physics/FluxViscous.hpp"
#include "physics/PerfectGas.hpp"

namespace cfd {

struct ResidualNorms {
  std::array<Real, kNVar> per_equation{};
  Real l2 = 0.0;
  Real linf = 0.0;
};

struct ReconstructionStats {
  long long positivity_fallbacks = 0;   // face states clipped to first order
  long long update_backtracks = 0;      // cells whose update was line-searched
};

class SpatialOperator {
 public:
  SpatialOperator(const LocalMesh& mesh, const CaseConfig& cfg, const SolverOptions& opt,
                  MPI_Comm comm);

  // ---- state access ----
  std::vector<Real>& U() { return u_; }
  const std::vector<Real>& U() const { return u_; }
  const std::vector<Real>& W() const { return w_; }
  const std::vector<Real>& residual() const { return res_; }
  const std::vector<Real>& gradients() const { return grad_; }
  const std::vector<Real>& limiters() const { return phi_; }

  const LocalMesh& mesh() const { return mesh_; }
  const PerfectGas& gas() const { return gas_; }
  const TransportModel& transport() const { return transport_; }
  const CaseConfig& config() const { return cfg_; }
  const SolverOptions& options() const { return opt_; }
  MPI_Comm comm() const { return comm_; }
  const HaloExchanger& halo() const { return halo_; }
  const std::vector<BcType>& patchBc() const { return patch_bc_; }
  const PrimVec& freestreamPrimitive() const { return winf_; }

  void initializeFreestream();
  void setConservative(const std::vector<Real>& u_owned);

  // R(U) = sum_f (F_inviscid - F_viscous) . n S  for every owned cell.
  void evaluateResidual();

  // Norm helper shared by the steady and transient drivers.
  ResidualNorms computeNorms(const std::vector<Real>& r) const;

  // Local pseudo-time step and the LU-SGS diagonal, both from the face
  // spectral radii.  `physical_diag_coeff` adds the BDF/trapezoidal
  // contribution (a0/dt); `spatial_scale` scales the spatial Jacobian, which
  // the trapezoidal rule needs because it linearises 1/2 (R^{n+1} + R^n).
  void computeTimeStep(Real cfl, Real physical_diag_coeff, Real spatial_scale = 1.0);
  Real spatialJacobianScale() const { return spatial_scale_; }
  void setSpatialJacobianScale(Real s) { spatial_scale_ = s; }
  const std::vector<Real>& dtau() const { return dtau_; }
  const std::vector<Real>& diagonal() const { return diag_; }
  const std::vector<Real>& faceLambda() const { return face_lambda_; }

  // Applies U += alpha * dU with a per-cell positivity line search.
  long long applyUpdate(const std::vector<Real>& du, Real alpha);

  // Refresh primitives + boundary states of the halo (used after an update so
  // that the next operation sees a consistent state).
  void syncState();

  // Convert local conservative state to primitives for all local cells.
  void updatePrimitives();

  ReconstructionStats stats() const { return stats_; }
  void resetStats() { stats_ = ReconstructionStats(); }

  // Limiter freezing: once the shock system has settled, the limiter values are
  // held fixed so that the residual can converge past the limit cycle caused by
  // the non-differentiable min/max stencil.  The limiter stays fully active in
  // the reconstruction; only its recomputation stops.
  void setLimiterFrozen(bool f) { limiter_frozen_ = f; }
  bool limiterFrozen() const { return limiter_frozen_; }

  // Per-cell scaled residual magnitude, for convergence diagnostics/plots.
  std::vector<Real> residualMagnitude() const;

  // Boundary-face quantities, valid after evaluateResidual().
  const std::vector<Real>& boundaryPrimitive() const { return bstate_; }
  const std::vector<Real>& boundaryShear() const { return bshear_; }

  Real cellLengthScale(Index c) const { return length_scale_[c]; }

 private:
  void buildLeastSquares();
  void computeBoundaryStates();
  void computeGradients();
  void computeLimiter();
  void reconstructFace(Index cell, const Vec2& xf, PrimVec& w) const;

  const LocalMesh& mesh_;
  CaseConfig cfg_;
  SolverOptions opt_;
  MPI_Comm comm_;
  PerfectGas gas_;
  TransportModel transport_;
  FluxOptions flux_opt_;
  HaloExchanger halo_;

  std::vector<BcType> patch_bc_;
  PrimVec winf_{};
  ConsVec uinf_{};
  Real rho_floor_ = 0.0;
  Real p_floor_ = 0.0;
  std::array<Real, kNVar> res_scale_{};
  std::array<Real, kNVar> var_scale_{};
  GlobalIndex global_cells_ = 0;
  Real global_volume_ = 0.0;
  Real spatial_scale_ = 1.0;

  // state
  std::vector<Real> u_;      // [cell*kNVar + k], owned + ghost
  std::vector<Real> w_;      // primitives, owned + ghost
  std::vector<Real> grad_;   // [cell*kNVar*kDim + k*kDim + d], owned + ghost
  std::vector<Real> phi_;    // limiter, owned + ghost
  std::vector<Real> res_;    // residual, owned cells (sized owned+ghost)
  std::vector<Real> dtau_;
  std::vector<Real> diag_;
  std::vector<Real> face_lambda_;   // per-face spectral radius (per unit area)
  std::vector<Real> bface_lambda_;
  std::vector<Real> bstate_;        // boundary primitive state per boundary face
  std::vector<Real> bshear_;        // wall tangential traction per boundary face (2 comps)
  std::vector<Real> length_scale_;

  // least-squares stencils (owned cells)
  std::vector<Real> lsq_inv_;       // 3 entries per owned cell
  std::vector<Index> lsq_off_;
  std::vector<Index> lsq_nb_;       // >=0 : local cell; <0 : -(bface+1)
  std::vector<Real> lsq_wd_;        // 2 entries per stencil entry
  std::vector<Real> lsq_xf_;        // reconstruction point (face centre) per entry

  ReconstructionStats stats_;
  bool limiter_frozen_ = false;
};

}  // namespace cfd
