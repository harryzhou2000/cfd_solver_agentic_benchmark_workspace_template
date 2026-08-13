#pragma once

#include "core.hpp"

#include <limits>

namespace aerofv {

// All face fluxes in this header are per unit face length.  Their normal is
// normalized internally and is interpreted as pointing from the left cell to
// the right cell (or outward from the owner at a boundary).  Thus the
// inviscid contribution to an owner residual is +F_i dot n and the viscous
// contribution is -F_v dot n.

bool valid_gas_model(const GasModel &gas);
bool finite(const Primitive &state);
bool physically_valid(const Primitive &state, const GasModel &gas);
bool physically_valid(const Conservative &state, const GasModel &gas);

Primitive sanitize_primitive(Primitive state, const GasModel &gas);
Conservative primitive_to_conservative(const Primitive &state,
                                       const GasModel &gas);
Primitive conservative_to_primitive(const Conservative &state,
                                     const GasModel &gas);
Conservative sanitize_conservative(const Conservative &state,
                                   const GasModel &gas);

double temperature(const Primitive &state, const GasModel &gas);
double sound_speed(const Primitive &state, const GasModel &gas);
double normal_velocity(const Primitive &state, const Vec2 &unit_normal);

Conservative euler_normal_flux(const Conservative &state,
                               const Primitive &primitive,
                               const Vec2 &unit_normal);
Conservative rusanov_flux(const Conservative &left, const Conservative &right,
                          const Vec2 &unit_normal, const GasModel &gas);
Conservative hllc_flux(const Conservative &left, const Conservative &right,
                       const Vec2 &unit_normal, const GasModel &gas);

// Exact inviscid flux through a stationary impermeable wall.  This avoids
// adding a Riemann-solver penalty proportional to the (iteration-level)
// normal-velocity error; impermeability is imposed directly by zero mass and
// energy flux while pressure supplies the normal momentum flux.
Conservative impermeable_wall_flux(const Primitive &interior,
                                   const Vec2 &unit_normal,
                                   const GasModel &gas);

// Ghost state is the state on the other side of a boundary face.  Averaging a
// no-slip ghost with its interior state gives zero wall velocity; copying rho
// and p implements a zero normal temperature gradient for this first
// gradient-level treatment.
Primitive boundary_ghost_primitive(const Primitive &interior,
                                   const Primitive &freestream,
                                   const Vec2 &outward_unit_normal,
                                   BoundaryType type, const GasModel &gas);
Primitive boundary_surface_primitive(const Primitive &interior,
                                     const Primitive &freestream,
                                     const Vec2 &outward_unit_normal,
                                     BoundaryType type, const GasModel &gas);
Conservative boundary_ghost_state(const Conservative &interior,
                                  const Primitive &freestream,
                                  const Vec2 &outward_unit_normal,
                                  BoundaryType type, const GasModel &gas);

// grad[0..3] holds gradients of rho, u, v, and p.  Returned F_v dot n is the
// viscous flux on the equation right hand side: [0, tau.n,
// velocity dot (tau.n) + k grad(T).n].
Vec2 temperature_gradient(const Primitive &state,
                          const PrimitiveGradient &gradient,
                          const GasModel &gas);
Conservative viscous_normal_flux(const Primitive &state,
                                 const PrimitiveGradient &gradient,
                                 const Vec2 &unit_normal,
                                 double dynamic_viscosity,
                                 const GasModel &gas);

double convective_face_spectral_radius(const Primitive &state,
                                       const Vec2 &unit_normal,
                                       double face_length,
                                       const GasModel &gas);
double viscous_face_spectral_radius(const Primitive &state,
                                   double face_length,
                                   double center_distance,
                                   double dynamic_viscosity,
                                   const GasModel &gas);
double local_pseudo_timestep(double cell_volume, double convective_sum,
                             double viscous_sum, double cfl,
                             double physical_timestep =
                                 std::numeric_limits<double>::infinity());

} // namespace aerofv
