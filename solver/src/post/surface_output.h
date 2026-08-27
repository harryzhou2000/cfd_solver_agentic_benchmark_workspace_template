// cns2d -- wall surface data extraction.
//
// Each wall face contributes one row reporting the BOUNDARY-CONDITION state,
// not the adjacent cell-centre state:
//   * no-slip adiabatic wall -> u = v = 0, mach = 0, cf from tangential shear
//   * inviscid slip wall     -> zero normal velocity, tangential preserved
// The adjacent cell-centre values are also carried in the record so the report
// can compare the two without ambiguity.
#pragma once

#include <string>
#include <vector>

#include "numerics/residual.h"
#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "physics/perfect_gas.h"

namespace cns2d {

struct SurfaceRow {
  Real x{0.0};
  Real y{0.0};
  Real nx{0.0};
  Real ny{0.0};
  Real pressure{0.0};
  Real cp{0.0};
  Real cf{0.0};
  Real rho{0.0};
  Real u{0.0};
  Real v{0.0};
  Real mach{0.0};
  // Adjacent cell-centre values, reported in extra columns for transparency.
  Real cell_u{0.0};
  Real cell_v{0.0};
  Real cell_mach{0.0};
  Real tangential_shear{0.0};
  std::int32_t tag{0};
};

// Collect wall rows from every rank and return the full list on rank 0
// (empty elsewhere).  Rows are sorted for reproducible output.
std::vector<SurfaceRow> collectSurfaceRows(const DistributedMesh &mesh, const FlowContext &flow,
                                          const ResidualAssembler &assembler, const StateField &U,
                                          MPI_Comm comm, std::vector<std::string> &tag_names);

}  // namespace cns2d
