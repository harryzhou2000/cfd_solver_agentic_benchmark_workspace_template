// cns2d -- CGNS unstructured mesh import.
//
// Reads every base/zone/section/BC/1-to-1-connectivity node present in the
// file without assuming a particular zone count, element mix, or family naming
// convention.  Boundary families are taken from the file, and the mapping to
// solver boundary conditions is applied later from the case file.
#pragma once

#include <string>

#include "mesh/mesh_types.h"

namespace cns2d {

// Read the first base of a CGNS file into RawMesh.  Throws CnsError for
// unreadable files, structured zones, unsupported element types or missing
// coordinates.
RawMesh readCgnsMesh(const std::string &path);

// Human-readable summary for the run log.
std::string describeRawMesh(const RawMesh &mesh);

}  // namespace cns2d
