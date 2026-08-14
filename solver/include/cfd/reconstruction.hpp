#pragma once

#include "cfd/boundary.hpp"
#include "cfd/partition.hpp"

#include <mpi.h>

#include <array>
#include <cstdint>
#include <vector>

namespace cfd {

using PrimitiveGradient = std::array<Vec2, 4>;  // rho, u, v, p
using ComponentLimiter = std::array<double, 4>;

struct ReconstructionDiagnostics {
  std::uint64_t venkatakrishnan_limited_face_components{};
  std::uint64_t shock_fallback_cells{};
  std::uint64_t positivity_barth_fallbacks{};
  std::uint64_t positivity_scaled{};
  std::uint64_t first_order_fallbacks{};
};

struct ReconstructionData {
  std::vector<Primitive> primitive;
  std::vector<PrimitiveGradient> gradient;
  std::vector<ComponentLimiter> limiter;
  std::vector<ComponentLimiter> barth_limiter;
  std::vector<std::array<double, 4>> minimum;
  std::vector<std::array<double, 4>> maximum;
  std::vector<bool> shock_fallback;
  ReconstructionDiagnostics diagnostics{};
};

// Smooth Venkatakrishnan limiter for one face increment. allowed_change is the
// signed-direction distance to the appropriate local extremum, reconstructed_change
// is the unlimited face increment, and epsilon_squared is a component-scaled
// regularization. The returned multiplier is finite and bounded in [0, 1].
double venkatakrishnan_face_limiter(double allowed_change,
                                   double reconstructed_change,
                                   double epsilon_squared) noexcept;

// Smoothly tightens the Venkatakrishnan multiplier toward the no-weaker of
// Venkatakrishnan and Barth--Jespersen as a density/pressure jump approaches
// the strong-shock threshold. This avoids a discontinuous limiter active-set
// change while preserving the strict shock bound.
double shock_limited_face_limiter(double venkatakrishnan,
                                 double barth_jespersen,
                                 double jump_ratio) noexcept;

class PrimitiveReconstruction {
 public:
  PrimitiveReconstruction(const DistributedMesh& mesh, const CaloricallyPerfectGas& gas,
                          const std::map<std::string, BoundaryCondition>& boundary_conditions,
                          const Primitive& freestream, MPI_Comm communicator,
                          double reference_length = 1.0);

  // U is cell-major, includes owned and halo cells, and has width four.
  const ReconstructionData& compute(const std::vector<double>& U);
  Primitive face_value(LocalIndex cell, const Vec2& face_center);
  Primitive face_value(LocalIndex cell, LocalIndex face_index);
  const ReconstructionData& data() const noexcept { return data_; }

 private:
  const DistributedMesh& mesh_;
  const CaloricallyPerfectGas& gas_;
  const std::map<std::string, BoundaryCondition>& boundary_conditions_;
  Primitive freestream_{};
  MPI_Comm communicator_;
  double reference_length_{1.0};
  ReconstructionData data_;
  std::vector<double> exchange_buffer_;

  Primitive face_value_at(LocalIndex cell, const Vec2& face_center);
};

VelocityTemperatureGradients velocity_temperature_gradients(
    const Primitive& state, const PrimitiveGradient& gradient,
    const CaloricallyPerfectGas& gas);

// Symmetric center-line consistency correction, with a bounded correction.
Vec2 corrected_face_gradient(const Vec2& left_gradient, const Vec2& right_gradient,
                             double left_value, double right_value,
                             const Vec2& left_to_right);

}  // namespace cfd
