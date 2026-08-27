#include "solve/solver_context.h"

#include <algorithm>
#include <cmath>

#include "core/exceptions.h"
#include "core/logging.h"
#include "io/restart_io.h"

namespace cns2d {

SolverContext::SolverContext(const CaseInput &input, const CommandLineOptions &options, MPI_Comm comm)
    : input_(input), options_(options), comm_(comm) {
  MPI_Comm_rank(comm_, &rank_);
  MPI_Comm_size(comm_, &size_);

  mesh_ = DistributedMesh::build(input_, comm_);
  flow_ = makeFlowContext(input_);

  // --- resolve the discretization from the case file ---------------------
  scheme_.viscous = flow_.viscous;
  scheme_.second_order = input_.numerics_required.spatial_order >= 2;
  scheme_.rusanov_dissipation_scale = input_.run_control.rusanov_dissipation_scale;

  // Flux choice: Roe with the Harten-Hyman entropy fix is used for every case.
  // It is the least dissipative of the implemented solvers, which the Re 5000
  // boundary layers need, and the entropy fix makes it admissible at the sonic
  // points of the Mach 2 cases.  HLLC is engaged automatically wherever the Roe
  // average would be non-physical.
  scheme_.inviscid_flux = RiemannFluxType::kRoeEntropyFix;

  // Limiter: Venkatakrishnan (a smooth Barth-Jespersen variant).  The smooth
  // form avoids the limiter chattering that stalls steady residual convergence,
  // and it degenerates to Barth-Jespersen behaviour at genuine extrema.
  scheme_.limiter = LimiterType::kVenkatakrishnan;
  scheme_.venkat_k = 5.0;

  // Verification overrides from the command line, if any.  Each one is logged so
  // a run that deviates from the production scheme is self-documenting.
  if (options_.override_spatial_order > 0) {
    scheme_.second_order = options_.override_spatial_order >= 2;
    logWarn(formatString("verification override: spatial order forced to %d",
                         options_.override_spatial_order));
  }
  if (!options_.override_flux.empty()) {
    scheme_.inviscid_flux = parseRiemannFluxType(options_.override_flux);
    logWarn("verification override: inviscid flux forced to " + options_.override_flux);
  }
  if (!options_.override_limiter.empty()) {
    scheme_.limiter = parseLimiterType(options_.override_limiter);
    logWarn("verification override: limiter forced to " + options_.override_limiter);
  }
  if (options_.override_venkat_k >= 0.0) {
    scheme_.venkat_k = options_.override_venkat_k;
    logWarn(formatString("verification override: Venkatakrishnan K = %.4g", scheme_.venkat_k));
  }

  halo_ = std::make_unique<HaloExchange>(*mesh_, std::max(GradientField::kComponents, kNumVars));
  assembler_ = std::make_unique<ResidualAssembler>(*mesh_, flow_, scheme_);
  implicit_ = std::make_unique<ImplicitSolver>(*mesh_, flow_, ImplicitSolverType::kLuSgs);
  implicit_label_ = implicitSolverName(implicit_->type());

  U_.resize(mesh_->numLocal());
}

void SolverContext::initializeState() {
  if (!options_.restart_path.empty()) {
    readRestart(options_.restart_path, *mesh_, U_, comm_);
    logInfo("initial state read from restart file " + options_.restart_path);
  } else {
    const Index n = mesh_->numLocal();
    for (Index c = 0; c < n; ++c) U_.set(c, flow_.freestream_cons);
    logInfo(formatString("initial state set to freestream: rho=%.6g u=%.6g v=%.6g p=%.6g (M=%.4g)",
                         flow_.freestream_prim[kPrimRho], flow_.freestream_prim[kPrimU],
                         flow_.freestream_prim[kPrimV], flow_.freestream_prim[kPrimP],
                         flow_.freestream_mach));
  }
  syncState();
}

void SolverContext::syncState() { halo_->exchange(U_.data(), kNumVars, kNumVars); }

ResidualNorms SolverContext::computeNorms(const StateField &residual) const {
  // The residual has units of (conserved quantity)/(time), integrated over the
  // cell.  Dividing by the cell volume converts it to a rate of change of the
  // cell average, which makes the norm independent of the local cell size and
  // therefore comparable across meshes and rank counts.
  const Index num_owned = mesh_->numOwned();
  const auto &cells = mesh_->cells();

  std::array<Real, kNumVars> local_sq{};
  Real local_linf = 0.0;
  Real local_volume = 0.0;

  for (Index c = 0; c < num_owned; ++c) {
    const Real inv_v = cells[static_cast<std::size_t>(c)].inv_volume;
    const Real v = cells[static_cast<std::size_t>(c)].volume;
    local_volume += v;
    const Real *r = residual.cell(c);
    for (int k = 0; k < kNumVars; ++k) {
      const Real rate = r[k] * inv_v;
      local_sq[static_cast<std::size_t>(k)] += rate * rate * v;
      local_linf = std::max(local_linf, std::abs(rate));
    }
  }

  // Global reductions: every reported residual is an MPI-reduced global value.
  std::array<Real, kNumVars + 1> send{};
  for (int k = 0; k < kNumVars; ++k) send[static_cast<std::size_t>(k)] = local_sq[static_cast<std::size_t>(k)];
  send[kNumVars] = local_volume;
  std::array<Real, kNumVars + 1> recv{};
  MPI_Allreduce(send.data(), recv.data(), kNumVars + 1, MPI_DOUBLE, MPI_SUM, comm_);
  Real global_linf = 0.0;
  MPI_Allreduce(&local_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm_);

  ResidualNorms norms;
  const Real total_volume = std::max(recv[kNumVars], kTiny);
  Real combined = 0.0;
  for (int k = 0; k < kNumVars; ++k) {
    norms.component_l2[static_cast<std::size_t>(k)] =
        std::sqrt(recv[static_cast<std::size_t>(k)] / total_volume);
    combined += recv[static_cast<std::size_t>(k)];
  }
  norms.l2 = std::sqrt(combined / total_volume);
  norms.linf = global_linf;
  return norms;
}

long long SolverContext::enforcePositivity(StateField &U) const {
  // Final safety net after an implicit update.  A cell whose update would drive
  // density or pressure non-positive is reset to a floor derived from the
  // freestream, and the event is counted so the run log and metadata can report
  // it honestly rather than silently producing a plausible-looking field.
  const Index num_owned = mesh_->numOwned();
  const Real rho_floor = 1.0e-8 * flow_.freestream_prim[kPrimRho];
  const Real p_floor = 1.0e-8 * flow_.freestream_prim[kPrimP];
  long long limited = 0;

  for (Index c = 0; c < num_owned; ++c) {
    ConsVec u = U.get(c);
    bool touched = false;
    if (!(u[kRho] > rho_floor) || !std::isfinite(u[kRho])) {
      u[kRho] = rho_floor;
      touched = true;
    }
    Real p = flow_.gas.pressureFromCons(u);
    if (!(p > p_floor) || !std::isfinite(p)) {
      // Keep velocity, reset internal energy to the floor pressure.
      const Real inv_rho = 1.0 / u[kRho];
      const Real ke = 0.5 * (u[kRhoU] * u[kRhoU] + u[kRhoV] * u[kRhoV]) * inv_rho;
      u[kRhoE] = p_floor / (flow_.gas.gamma() - 1.0) + ke;
      touched = true;
    }
    for (int k = 0; k < kNumVars; ++k) {
      if (!std::isfinite(u[k])) {
        u = flow_.freestream_cons;
        touched = true;
        break;
      }
    }
    if (touched) {
      U.set(c, u);
      ++limited;
    }
  }
  return limited;
}

}  // namespace cns2d
