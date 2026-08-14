#pragma once

#include "cfd/config.hpp"
#include "cfd/compensated_sum.hpp"
#include "cfd/flux.hpp"
#include "cfd/reconstruction.hpp"

#include <mpi.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cfd {

struct ResidualNorms {
  std::array<double, 4> component_l2{};
  double total_l2{};
  double linf{};
};

struct GlobalDiagnostics {
  std::uint64_t venkatakrishnan_limited_face_components{};
  std::uint64_t shock_fallback_cells{};
  std::uint64_t positivity_barth_fallbacks{};
  std::uint64_t positivity_scaled{};
  std::uint64_t first_order_fallbacks{};
};

struct SurfaceBoundaryState {
  GlobalId face_id{invalid_global_id};
  BoundaryCondition condition{};
  Primitive state{};
  Vec2 outward_fluid_normal{};
  Vec2 pressure_body_force{};
  Vec2 viscous_body_force{};
  Vec2 tangential_shear_force{};
  Vec2 center{};
  double length{};
};

struct ResidualResult {
  std::vector<double> value;       // owned-cell major, integrated face residual
  std::vector<double> spectral_radius;
  std::vector<double> local_time_step;
  ResidualNorms norms{};
  GlobalDiagnostics diagnostics{};
  std::vector<SurfaceBoundaryState> surface;
};

struct ResidualEvaluationOptions {
  // Zero selects cell-centred first-order inviscid face states; one selects the
  // complete positivity-limited reconstruction. Intermediate values form a
  // convex primitive-variable continuation between those endpoints.
  double reconstruction_blend{1.0};
  // Internal repeated evaluations may set this only after an equivalent
  // collective preflight and a collective admissibility line search.
  bool collective_preflight{true};
  bool compute_global_norms{true};
  bool compute_global_diagnostics{true};
  bool collect_surface{true};
};

Primitive blend_reconstructed_primitive(const Primitive& cell_center,
                                        const Primitive& reconstructed,
                                        double reconstruction_blend,
                                        const CaloricallyPerfectGas& gas);

struct ForceCoefficients {
  double cl{};
  double cd{};
  double cmz{};
  double cl_pressure{};
  double cd_pressure{};
  double cmz_pressure{};
  double cl_viscous{};
  double cd_viscous{};
  double cmz_viscous{};
};

class ResidualOperator {
 public:
  ResidualOperator(const DistributedMesh& mesh, const CaseConfig& config,
                   MPI_Comm communicator);

  ResidualResult evaluate(std::vector<double>& U, double cfl,
                          const ResidualEvaluationOptions& options = {});
  ResidualResult evaluate(std::vector<double>& U, double cfl, bool second_order);
  void evaluate_into(std::vector<double>& U, double cfl, ResidualResult& result,
                     const ResidualEvaluationOptions& options = {});
  // Completes reductions for an exactly current fast evaluation without
  // repeating reconstruction, halo exchange, or face flux assembly.
  void finalize_cached_result(ResidualResult& result);
  ForceCoefficients forces(const ResidualResult& residual) const;
  const CaloricallyPerfectGas& gas() const noexcept { return gas_; }
  const Conservative& freestream() const noexcept { return freestream_; }
  double viscosity() const noexcept { return viscosity_; }

 private:
  const DistributedMesh& mesh_;
  const CaseConfig& config_;
  MPI_Comm communicator_;
  CaloricallyPerfectGas gas_;
  Conservative freestream_{};
  Primitive freestream_primitive_{};
  double viscosity_{};
  double dissipation_scale_{1.0};
  std::size_t boundary_face_count_{};
  PrimitiveReconstruction reconstruction_;
  std::vector<CompensatedSum> residual_accumulator_;
};

void validate_boundary_mapping(const DistributedMesh& mesh, const CaseConfig& config,
                               MPI_Comm communicator);

}  // namespace cfd
