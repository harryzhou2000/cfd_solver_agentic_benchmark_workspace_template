// Wall force integration and surface sampling.
//
// The traction exerted by the fluid on the body is  T = p n - tau.n,  where n
// is the outward normal of the *fluid* domain (it points from the fluid into
// the solid).  The pressure part and the tangential part of the viscous part
// are reported separately, as required by OUTPUT_CONTRACT.md; the (very small)
// normal viscous traction is reported as its own diagnostic instead of being
// mislabelled as skin friction.
#pragma once

#include <string>
#include <vector>

#include "core/Types.hpp"
#include "numerics/SpatialOperator.hpp"

namespace cfd {

struct ForceReport {
  Real cl = 0.0, cd = 0.0, cmz = 0.0;
  Real pressure_drag = 0.0, pressure_lift = 0.0;
  Real viscous_drag = 0.0, viscous_lift = 0.0;         // tangential (skin friction)
  Real normal_viscous_drag = 0.0, normal_viscous_lift = 0.0;
  Real wall_area = 0.0;
};

// Global (MPI-reduced) wall forces.
ForceReport computeForces(const SpatialOperator& op);

struct SurfaceRow {
  Real x = 0, y = 0, nx = 0, ny = 0;
  Real pressure = 0, cp = 0, cf = 0;
  Real rho = 0, u = 0, v = 0, mach = 0;
  Real cell_u = 0, cell_v = 0, cell_pressure = 0;  // adjacent cell-centre values
  int patch = 0;
  GlobalIndex cell_gid = 0;
};

// Local wall rows on this rank (boundary-condition values, not cell centres).
std::vector<SurfaceRow> collectSurfaceRows(const SpatialOperator& op);

}  // namespace cfd
