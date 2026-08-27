// cns2d -- build a merged face-based unstructured mesh from raw CGNS zones.
//
// Responsibilities:
//   * fuse nodes matched by CGNS 1-to-1 zone connectivity so multi-zone
//     meshes become one conforming mesh;
//   * collect all 2-D elements as cells with a global numbering;
//   * build the unique face list with left/right cell references;
//   * tag boundary faces with the mesh boundary-family name that owns them.
//
// This runs on the preprocessing rank only.  The result is consumed by the
// partitioner and then discarded; solver iterations never see it.
#pragma once

#include <map>
#include <string>

#include "core/case_input.h"
#include "mesh/mesh_types.h"

namespace cns2d {

// Build the merged mesh.  Throws CnsError if the mesh is not watertight, if a
// face is shared by more than two cells, or if a boundary family referenced by
// the case file is absent from the mesh.
GlobalMesh buildGlobalMesh(const RawMesh &raw);

// Verify that every mesh boundary family carrying faces has a case mapping and
// that every mapped name exists in the mesh.  Returns tag -> BCType.
std::vector<BCType> mapBoundaryConditions(const GlobalMesh &mesh,
                                          const std::map<std::string, BCType> &case_map);

std::string describeGlobalMesh(const GlobalMesh &mesh);

}  // namespace cns2d
