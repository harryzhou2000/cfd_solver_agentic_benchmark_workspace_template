// cns2d -- conservative finite-volume residual assembly.
//
// The residual of cell i is defined as
//   R_i = - sum_faces ( F_inviscid . n - F_viscous . n ) * |face|
// so that the semi-discrete system reads  V_i dU_i/dt = R_i.
//
// Assembly is face-based and strictly conservative: each face flux is computed
// once and scattered with opposite signs to its two cells.  On a partition
// boundary both adjacent ranks evaluate the same face from identical ghost data
// and each keeps only its own owned-cell contribution, so the global sum of the
// fluxes still telescopes exactly.
#pragma once

#include <string>
#include <vector>

#include "core/case_input.h"
#include "numerics/gradients.h"
#include "numerics/limiter.h"
#include "numerics/riemann_flux.h"
#include "numerics/solution_field.h"
#include "numerics/viscous_flux.h"
#include "parallel/distributed_mesh.h"
#include "parallel/halo_exchange.h"
#include "physics/perfect_gas.h"

namespace cns2d {

// Discretization settings, all resolved from the case file plus task-level
// requirements.  Nothing here is keyed to a specific case id.
struct SchemeOptions {
  RiemannFluxType inviscid_flux{RiemannFluxType::kRoeEntropyFix};
  LimiterType limiter{LimiterType::kVenkatakrishnan};
  bool second_order{true};
  Real venkat_k{5.0};
  Real rusanov_dissipation_scale{1.0};
  bool viscous{false};
};

// Diagnostics accumulated while assembling one residual.
struct ResidualDiagnostics {
  long long positivity_fallbacks{0};   // face reconstructions clipped for positivity
  long long face_evaluations{0};
  Real min_density{0.0};
  Real min_pressure{0.0};
  Real max_mach{0.0};

  // Location of the dominant residual contribution, which is what identifies
  // WHERE a stalled solve is stalling.  'worst_weighted' uses the same
  // volume-weighted measure as the reported global norm, so the cell it names is
  // genuinely the one driving that norm.
  Real worst_weighted{0.0};
  Vec2 worst_location{};
  Real worst_volume{0.0};
  int worst_component{0};
  bool worst_touches_boundary{false};
  int worst_boundary_tag{-1};
};

// Owns the work arrays and evaluates the residual.
class ResidualAssembler {
 public:
  ResidualAssembler(const DistributedMesh &mesh, const FlowContext &flow, const SchemeOptions &scheme);

  // Evaluate R(U) into 'residual' for owned cells.  'U' must already contain
  // valid ghost values; this routine performs the halo exchanges it needs for
  // primitives and gradients itself.
  void evaluate(const StateField &U, StateField &residual, HaloExchange &halo,
                ResidualDiagnostics &diag);

  // Spectral radii of the convective and viscous operators per owned cell,
  // accumulated during the last evaluate() call.  Used by the local time step
  // and by the implicit diagonal.
  const std::vector<Real> &convectiveSpectralRadius() const { return conv_radius_; }
  const std::vector<Real> &viscousSpectralRadius() const { return visc_radius_; }

  // Primitive states and gradients from the last evaluation (owned + ghost).
  const StateField &primitives() const { return W_; }
  const GradientField &gradients() const { return grad_; }
  const LimiterField &limiterFactors() const { return phi_; }

  const SchemeOptions &scheme() const { return scheme_; }
  // Used by the startup verification to probe the assembly with a modified
  // scheme; production code paths never change the scheme mid-run.
  void setScheme(const SchemeOptions &scheme) { scheme_ = scheme; }

 private:
  void computeFaceStates(Index face, const StateField &U, PrimVec &WL, PrimVec &WR,
                         ResidualDiagnostics &diag) const;

  const DistributedMesh &mesh_;
  const FlowContext &flow_;
  SchemeOptions scheme_;

  StateField W_;
  GradientField grad_;
  LimiterField phi_;

  std::vector<Real> conv_radius_;
  std::vector<Real> visc_radius_;
};

}  // namespace cns2d
