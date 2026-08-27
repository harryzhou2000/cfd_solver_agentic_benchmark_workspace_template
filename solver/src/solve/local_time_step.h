// cns2d -- CFL-controlled local pseudo time step.
//
// The local step of cell i is
//   dtau_i = CFL * V_i / ( lambda_conv,i + C_visc * lambda_visc,i )
// where lambda_conv is the sum over faces of (|u.n| + a) * |face| and
// lambda_visc is the corresponding viscous spectral-radius estimate.  Including
// the viscous term is what keeps the stretched boundary-layer cells of the
// Re 5000 cases stable at the CFL values the benchmark specifies; a convective
// estimate alone overestimates the admissible step there by orders of magnitude.
#pragma once

#include <vector>

#include "core/types.h"
#include "parallel/distributed_mesh.h"

namespace cns2d {

// Compute the local pseudo time step for each owned cell.
// 'viscous_factor' is the multiplier on the viscous spectral radius (4 is a
// standard choice for a second-order central viscous operator).
void computeLocalTimeSteps(const DistributedMesh &mesh, Real cfl,
                           const std::vector<Real> &conv_radius,
                           const std::vector<Real> &visc_radius, Real viscous_factor,
                           std::vector<Real> &dtau);

// CFL ramp: geometric growth from cfl_initial to cfl_max over ramp_steps.
// A geometric ramp is used rather than linear because the stability limit itself
// grows multiplicatively as the transient decays.
Real rampedCfl(Real cfl_initial, Real cfl_max, int ramp_steps, int step);

}  // namespace cns2d
