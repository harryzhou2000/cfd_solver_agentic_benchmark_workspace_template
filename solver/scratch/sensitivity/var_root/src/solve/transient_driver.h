// cns2d -- second-order transient solve by dual-time BDF2.
//
// TWO-LEVEL LOOP STRUCTURE (this is the property the benchmark checks):
//
//   for each physical step n -> n+1:                     <-- OUTER loop
//       U^{n} and U^{n-1} are FROZEN for the whole step
//       for each inner nonlinear iteration:              <-- INNER loop
//           assemble the spatial residual R(U*)
//           form the total transient residual
//               R* = R(U*) - V * ( a0 U* + a1 U^n + a2 U^{n-1} ) / dt
//           solve ( V/dtau + V a0/dt + dR/dU ) dU = R*   by LU-SGS
//           U* <- U* + dU
//           stop when ||R*|| / ||R*_initial|| <= target, respecting the
//           min/max inner-iteration bounds
//       accept U^{n+1} = U*, THEN shift the histories
//
// The inner convergence test uses the TOTAL transient residual (spatial fluxes
// plus the physical-time term), which is what the benchmark specifies; using the
// spatial residual alone would report convergence that the time-accurate system
// has not actually reached.
//
// BDF2 coefficients for a uniform step: a0 = 3/2, a1 = -2, a2 = 1/2.  The first
// physical step has no U^{n-1}, so it uses BDF1 (a0 = 1, a1 = -1), the standard
// self-starting choice; this costs second-order accuracy only on that single
// step and is documented in the report.
#pragma once

#include "io/output_writer.h"
#include "solve/solver_context.h"

namespace cns2d {

RunOutcome runTransient(SolverContext &context, OutputWriter &writer);

}  // namespace cns2d
