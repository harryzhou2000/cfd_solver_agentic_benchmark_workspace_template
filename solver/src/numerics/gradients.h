// cns2d -- least-squares primitive-variable gradients.
//
// Gradients are computed on primitive variables (rho, u, v, p).  Using
// primitives keeps the limiter tied directly to the positivity constraints and
// makes the viscous stress/heat-flux assembly straightforward, since it needs
// exactly grad(u), grad(v) and grad(T).
//
// The stencil weights were precomputed by DistributedMesh; this routine is the
// per-iteration evaluation, which is one fused multiply-add per neighbour and
// variable.
#pragma once

#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "physics/perfect_gas.h"

namespace cns2d {

// Convert conserved to primitive for all local cells (owned + ghost).
void computePrimitives(const DistributedMesh &mesh, const FlowContext &ctx, const StateField &U,
                       StateField &W);

// Least-squares gradients of the primitive variables for owned cells.
// Boundary-face stencil entries use the imposed boundary state, so wall-adjacent
// gradients see the no-slip / slip-wall condition.
void computeGradients(const DistributedMesh &mesh, const FlowContext &ctx, const StateField &W,
                      const StateField &U, GradientField &grad);

}  // namespace cns2d
