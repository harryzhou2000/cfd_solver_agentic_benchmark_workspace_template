// cns2d -- startup verification of the rank-local mesh metrics.
//
// These identities are the foundation every flux computation rests on, and a
// violation produces a solver that runs but cannot converge.  They are checked
// once at startup (the cost is one pass over the faces) and reported in the run
// log so the report can cite concrete numbers instead of asserting correctness.
//
//  1. Face closure:      sum over the faces of a cell of (outward n * area) = 0.
//  2. Volume closure:    sum of the divergence-theorem volume estimate over the
//                        faces reproduces the polygon area of each cell.
//  3. Global area:       the summed cell volumes equal the area enclosed by the
//                        boundary, computed independently from the boundary
//                        faces alone.
//  4. Uniform flow:      a spatially uniform state produces an identically zero
//                        residual on every cell that touches no boundary face
//                        (free-stream preservation of the interior scheme).
#pragma once

#include <string>

#include "numerics/residual.h"
#include "parallel/distributed_mesh.h"
#include "parallel/halo_exchange.h"
#include "physics/perfect_gas.h"

namespace cns2d {

struct GeometryVerification {
  Real max_face_closure_error{0.0};    // max |sum n*area| / (sum area) over cells
  Real max_volume_closure_error{0.0};  // max relative volume mismatch
  Real total_volume{0.0};
  Real boundary_enclosed_area{0.0};
  Real area_mismatch{0.0};             // relative
  Real max_uniform_flow_residual{0.0}; // max |R|/V on interior-only cells
  Index num_interior_only_cells{0};
  // Discrete conservation: the sum of the residual over ALL cells must equal
  // minus the net flux through the outer boundary.  A nonzero difference means
  // interior face fluxes are not telescoping, i.e. the scatter is inconsistent.
  Real conservation_defect{0.0};       // relative
  // Gradient exactness: the least-squares gradient of a linear field must be
  // recovered exactly.  This validates the stencil weights and the
  // boundary-face stencil entries.
  Real max_linear_gradient_error{0.0};
  bool passed{true};
};

// Run the checks.  Throws CnsError if a hard geometric identity is violated
// beyond a tolerance that no valid mesh should exceed.
GeometryVerification verifyMeshGeometry(const DistributedMesh &mesh, const FlowContext &flow,
                                        ResidualAssembler &assembler, HaloExchange &halo);

std::string describeVerification(const GeometryVerification &v);

}  // namespace cns2d
