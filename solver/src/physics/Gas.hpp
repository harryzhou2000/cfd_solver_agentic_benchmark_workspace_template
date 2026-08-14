#pragma once

#include "io/Input.hpp"
#include "mesh/GlobalMesh.hpp"
#include "util/common.hpp"

namespace cfds {

// Calorically perfect gas model with optional laminar viscosity.
struct GasModel {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;
  double mu = 0.0;       // constant dynamic viscosity (laminar mode)
  bool viscous = false;

  double cp() const { return gamma * R / (gamma - 1.0); }
  double cv() const { return R / (gamma - 1.0); }

  Primitive to_primitive(const ConsVec& U) const;
  ConsVec to_conservative(const Primitive& p) const;
  double pressure(const ConsVec& U) const;
  double temperature(const Primitive& p) const { return p.p / (p.rho * R); }
  double sound_speed(const Primitive& p) const {
    return std::sqrt(gamma * p.p / p.rho);
  }
  double mach(const Primitive& p) const {
    return std::sqrt(p.u * p.u + p.v * p.v) / sound_speed(p);
  }
};

// Build the gas model from a case config (freestream + Reynolds number).
GasModel make_gas_model(const CaseConfig& cfg);

// Inviscid Euler flux through a face with unit normal n (per unit length).
ConsVec euler_flux(const GasModel& gas, const ConsVec& U, const Vec2& n);

// Jacobian-vector product of the Euler flux (with unit normal n) at the
// primitive state p, applied to the conservative perturbation dU. Used by
// the matrix-free implicit solver (simplified Jacobian).
ConsVec euler_jacobian_times(const GasModel& gas, const Primitive& p,
                             const Vec2& n, const ConsVec& dU);

// Euclidean/operator helper: infinity norm of A(p,n) + sign*lambda*I, where
// A is the Euler flux Jacobian projected on n and lambda is the Rusanov
// dissipation speed. Used to bound the implicit self-block.
double euler_jacobian_norm(const GasModel& gas, const Primitive& p,
                           const Vec2& n, double lambda, double sign);

// Fill the 4x4 Euler flux Jacobian A(p,n) (dF/dU projected on n).
void euler_jacobian_matrix(const GasModel& gas, const Primitive& p,
                           const Vec2& n, double mat[4][4]);

// Rusanov / local-Lax-Friedrichs flux between two states.
ConsVec rusanov_flux(const GasModel& gas, const ConsVec& UL, const ConsVec& UR,
                     const Vec2& n, double dissipation_scale);

// Laminar viscous flux through a face with unit normal n, using the face
// primitive state and the face primitive gradient grad[4][2]
// (order: rho, u, v, p; each [dx, dy]). Per unit length.
ConsVec viscous_flux(const GasModel& gas, const Primitive& pf,
                     const double grad[4][2], const Vec2& n);

// Boundary ghost primitive for a boundary face of type bc, given the cell
// primitive, the face normal (outward from the cell) and the freestream.
Primitive boundary_ghost(BcType bc, const Primitive& pc, const Vec2& n,
                         const Primitive& far);

// Characteristic-based farfield ghost state (subsonic/supersonic inflow and
// outflow from the local Riemann invariants).
Primitive farfield_ghost(const GasModel& gas, const Primitive& pc, const Vec2& n,
                         const Primitive& far);

// Boundary inviscid flux for a boundary face (per unit length). For walls the
// flux collapses to the pressure-only flux; farfield uses the Riemann solver.
ConsVec boundary_inviscid_flux(BcType bc, const GasModel& gas,
                               const Primitive& p_face, const Vec2& n,
                               const Primitive& far, double dissipation_scale);

// Boundary viscous flux at a no-slip adiabatic wall (per unit length), built
// from the mirrored-ghost construction: the ghost cell at distance d across
// the wall has zero velocity, so grad u = -u_cell n^T / d. The effective wall
// distance is floored at half the cell length scale so the O(1/d) stress
// stays bounded at the degenerate leading/trailing-edge sliver cells.
ConsVec wall_viscous_flux(const GasModel& gas, const Primitive& pc,
                          const Vec2& n, double d_eff);

// Tangential shear traction at a wall face (per unit length along the face),
// used for skin-friction reporting. Returns the traction magnitude projected
// onto the wall tangent aligned with the flow direction (signed), plus the
// wall-tangent unit vector.
double wall_shear_coefficient(const GasModel& gas, const Primitive& pc,
                              const Vec2& n_body, double d_eff,
                              const Vec2& flow_dir, Vec2& tangent_out);

}  // namespace cfds
