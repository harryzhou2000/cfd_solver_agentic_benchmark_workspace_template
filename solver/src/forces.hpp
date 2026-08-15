// Force and moment coefficient computation (lift, drag, moment).
// Phase 3a: pressure + viscous surface integration over wall faces.

#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "config.hpp"
#include "gas.hpp"
#include "partition.hpp"
#include "types.hpp"

namespace cfd {

// Integrated force/moment coefficients for one output step.
struct ForceResult {
    double cl = 0.0;
    double cd = 0.0;
    double cmz = 0.0;
    double pressure_drag = 0.0;
    double viscous_drag = 0.0;
    double pressure_lift = 0.0;
    double viscous_lift = 0.0;
    double fx = 0.0;  // raw force components on the body (x/y)
    double fy = 0.0;
    double mz = 0.0;  // raw moment about the reference center
};

// Integrates pressure and viscous shear over the wall boundary faces of the
// mesh (slip and no-slip walls; farfield faces are excluded) and reduces
// the totals across `comm`.
//
//   mesh       : full mesh with boundary-face geometry (bface_* arrays)
//   local_mesh : rank-local owned/ghost lists (only the rank owning the
//                cell adjacent to a wall face integrates that face)
//   U_local    : conservative states (owned + ghost)
//   viscosity  : 0 for inviscid runs -> viscous forces are identically zero
//
// Sign conventions (n = outward unit normal of the fluid domain): the force
// of the fluid on the body is dF = (p * n - tau * n) * dA; lift/drag use
// the freestream-aligned axes with CL = -Fy, CD = Fx at zero incidence.
ForceResult compute_forces(const Mesh& mesh, const LocalMesh& local_mesh,
                           const std::vector<Vector4>& U_local,
                           const GasParams& gas,
                           const FreestreamParams& freestream,
                           const ReferenceParams& reference,
                           double viscosity, MPI_Comm comm);

}  // namespace cfd
