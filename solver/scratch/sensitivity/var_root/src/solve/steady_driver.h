// cns2d -- steady implicit pseudo-time march.
//
// Structure of one nonlinear step:
//   1. exchange ghost states;
//   2. assemble R(U) (second-order, limited, viscous if requested);
//   3. form local pseudo time steps from the CFL schedule and the convective +
//      viscous spectral radii;
//   4. solve ( V/dtau I + dR/dU ) dU = R with LU-SGS sweeps;
//   5. update U, enforce positivity, log residuals and forces.
//
// The inner LU-SGS iteration count adapts between the case file's
// min_inner_iterations and max_inner_iterations, using extra sweeps while the
// linear residual is still dropping.  Both the requested and the achieved inner
// statistics are recorded for the report.
//
// CFL control.  The case files supply a CFL ramp towards a large cfl_max, which
// the pseudo-transient continuation follows.  On top of the ramp the driver
// applies a residual-based safeguard: if a step increases the nonlinear residual
// sharply, the step is REJECTED, the state is restored, and the CFL is cut
// before retrying.  Successful steps let the CFL recover geometrically towards
// the ramp value.  Without this safeguard the simplified-Jacobian LU-SGS solve
// becomes the accuracy bottleneck once the ramp passes CFL ~10: the linear
// system is then solved only to O(10%), the nonlinear update overshoots, and the
// residual plateaus while the force history oscillates.  The effective CFL
// actually used is what residuals.csv records.
#pragma once

#include "io/output_writer.h"
#include "solve/solver_context.h"

namespace cns2d {

RunOutcome runSteady(SolverContext &context, OutputWriter &writer);

}  // namespace cns2d
