// cns2d -- surface force and moment integration.
//
// Pressure and viscous contributions are accumulated separately so the report
// can present the split honestly:
//   * pressure force  = integral of (p - p_inf) n dS
//   * viscous force   = integral of the TANGENTIAL wall traction dS
// The viscous part deliberately excludes the normal viscous traction, which is
// a common source of inflated "skin friction" numbers.
//
// All sums are MPI-reduced so the reported coefficients are global.
#pragma once

#include "numerics/residual.h"
#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "physics/perfect_gas.h"

namespace cns2d {

struct ForceResult {
  Real cl{0.0};
  Real cd{0.0};
  Real cmz{0.0};
  Real pressure_drag{0.0};
  Real viscous_drag{0.0};
  Real pressure_lift{0.0};
  Real viscous_lift{0.0};
  Real wall_length{0.0};
};

// Integrate forces over all wall boundary faces.  Uses the primitive states and
// gradients from the assembler's most recent residual evaluation.
ForceResult computeForces(const DistributedMesh &mesh, const FlowContext &flow,
                          const ResidualAssembler &assembler, const StateField &U, MPI_Comm comm);

}  // namespace cns2d
