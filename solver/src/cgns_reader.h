#pragma once

#include "types.h"
#include "case_io.h"
#include <string>

namespace cfd2d {

// Read a CGNS mesh file into a Mesh structure.
// Merges multi-zone meshes into a single global mesh by matching
// coincident vertices (coordinate tolerance). Extracts boundary faces
// from boundary-family element sections.
// Returns false on error.
bool readCGNSMesh(const std::string& filename, const CaseInput& ci,
                  Mesh& mesh, std::string& err);

} // namespace cfd2d
