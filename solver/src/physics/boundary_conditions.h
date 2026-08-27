// cns2d -- boundary condition states.
//
// Each boundary condition supplies two things:
//   * a BOUNDARY state, used for the flux at the boundary face, for the
//     gradient stencil, and for surface output.  This is the physical state the
//     boundary condition imposes (e.g. exactly zero velocity on a no-slip
//     wall);
//   * for walls, whether the viscous stress should be evaluated with the
//     no-slip condition.
//
// Distinguishing the imposed boundary state from the adjacent cell-centre state
// is what allows surface.csv to report true wall values (zero velocity on
// no-slip walls, zero normal velocity with retained tangential velocity on slip
// walls) rather than near-wall cell averages.
//
// Adding a boundary condition means adding an enumerator in BCType plus a case
// here; nothing else in the solver needs to change.
#pragma once

#include "core/case_input.h"
#include "core/types.h"
#include "physics/perfect_gas.h"

namespace cns2d {

// Two distinct boundary states are needed, and conflating them is a classic
// source of wrong wall gradients:
//
//   * GHOST state -- the "right" state handed to the Riemann solver.  For walls
//     this is a mirrored state, so that the FACE AVERAGE satisfies the wall
//     condition and the resulting flux carries the correct wall pressure and
//     momentum.  A mirrored state deliberately overshoots (its normal velocity
//     is -u_n, not 0).
//
//   * FACE state -- the physical state that actually exists ON the boundary
//     (normal velocity exactly zero on a slip wall, the whole velocity exactly
//     zero on a no-slip wall).  This is what the least-squares gradient stencil,
//     the limiter envelope and surface.csv must use.  Feeding them the mirrored
//     state would double every wall-normal velocity gradient and create
//     artificial extrema in every wall-adjacent cell.
//
// For the farfield both coincide: the characteristic treatment already returns
// the physical boundary state.
//   interior : conserved state of the adjacent (left) cell, already reconstructed
//              to the face if second-order reconstruction is active
//   normal   : unit outward normal of the boundary face
ConsVec boundaryGhostState(BCType type, const ConsVec &interior, Vec2 normal,
                           const FlowContext &ctx);
ConsVec boundaryFaceState(BCType type, const ConsVec &interior, Vec2 normal,
                          const FlowContext &ctx);

// Farfield state using a characteristic (Riemann-invariant) treatment: the
// outgoing characteristics carry interior information and the incoming ones
// carry freestream information, which avoids over-specifying a subsonic
// boundary and reflects poorly-posed data back into the domain.
ConsVec farfieldState(const ConsVec &interior, Vec2 normal, const FlowContext &ctx);

// Slip (inviscid) wall ghost: mirror the normal velocity component, retain
// tangential.  The face average then has zero normal velocity.
ConsVec slipWallState(const ConsVec &interior, Vec2 normal);

// No-slip wall ghost: reverse the entire velocity vector, so the face average
// velocity is exactly zero.  Density and pressure are preserved, hence so is
// total energy.
ConsVec noSlipWallGhostState(const ConsVec &interior);

// Physical wall state exactly ON a slip wall: normal velocity removed,
// tangential velocity retained.
ConsVec slipWallFaceState(const ConsVec &interior, Vec2 normal);

// No-slip adiabatic wall: zero velocity, zero normal temperature gradient
// (adiabatic => wall temperature equals the adjacent fluid temperature), and
// wall pressure taken from the interior (zero normal pressure gradient).
ConsVec noSlipAdiabaticWallState(const ConsVec &interior, const FlowContext &ctx);

}  // namespace cns2d
