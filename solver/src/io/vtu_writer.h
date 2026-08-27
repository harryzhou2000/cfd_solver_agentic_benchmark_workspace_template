// cns2d -- VTK unstructured-grid (.vtu) field output.
//
// The file is written in ASCII XML so it can be read by ParaView, VisIt,
// meshio and the submission's own matplotlib post-processing without a binary
// decoder.  Cell data is written on the ACTUAL mesh cells (triangles and
// quadrilaterals), which is what allows publication-quality filled contours
// rather than point-cloud scatter.
//
// Rank 0 gathers the field for writing.  This happens only at output time, not
// during solver iterations.
#pragma once

#include <string>

#include "numerics/residual.h"
#include "numerics/solution_field.h"
#include "parallel/distributed_mesh.h"
#include "physics/perfect_gas.h"

namespace cns2d {

// Write the final (or an intermediate) field file.
// Contains density, velocity, pressure, Mach number, temperature, total energy,
// vorticity and the owning rank id.
void writeFieldVtu(const std::string &path, const DistributedMesh &mesh, const FlowContext &flow,
                   const ResidualAssembler &assembler, const StateField &U, MPI_Comm comm);

}  // namespace cns2d
