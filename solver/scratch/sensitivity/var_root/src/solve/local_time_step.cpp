#include "solve/local_time_step.h"

#include <algorithm>
#include <cmath>

namespace cns2d {

void computeLocalTimeSteps(const DistributedMesh &mesh, Real cfl,
                           const std::vector<Real> &conv_radius,
                           const std::vector<Real> &visc_radius, Real viscous_factor,
                           std::vector<Real> &dtau) {
  const Index num_owned = mesh.numOwned();
  dtau.assign(static_cast<std::size_t>(num_owned), 0.0);
  const auto &cells = mesh.cells();
  for (Index c = 0; c < num_owned; ++c) {
    const Real volume = cells[static_cast<std::size_t>(c)].volume;
    Real denom = conv_radius[static_cast<std::size_t>(c)];
    if (!visc_radius.empty()) {
      denom += viscous_factor * visc_radius[static_cast<std::size_t>(c)];
    }
    dtau[static_cast<std::size_t>(c)] = (denom > 0.0) ? cfl * volume / denom : 0.0;
  }
}

Real rampedCfl(Real cfl_initial, Real cfl_max, int ramp_steps, int step) {
  if (ramp_steps <= 0 || cfl_max <= cfl_initial) return cfl_max;
  if (step >= ramp_steps) return cfl_max;
  const Real fraction = static_cast<Real>(step) / static_cast<Real>(ramp_steps);
  // Geometric interpolation in log space.
  return cfl_initial * std::pow(cfl_max / cfl_initial, fraction);
}

}  // namespace cns2d
