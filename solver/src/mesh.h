#pragma once

#include "types.h"
#include "case_io.h"
#include <vector>
#include <string>
#include <map>

namespace cfd2d {

// Global mesh after face/geometry construction (before partitioning)
struct GlobalMesh {
  std::vector<double> x, y;          // vertex coords (1-based: index 0 unused or use 0-based)
  std::vector<std::vector<int>> cellNodes; // cell -> node ids (1-based)
  std::vector<CellGeo> cellGeo;      // cell geometry
  std::vector<Face> faces;           // all faces (interior + boundary)
  std::vector<std::string> bcFamilies; // list of BC family names
  std::map<std::string, BCType> familyBCType;
  int numCells = 0;
  int numVerts = 0;
  int numBcGhostCells = 0; // ghost cells added for boundary faces
};

// Build global mesh geometry and face connectivity from raw Mesh.
bool buildGlobalMesh(Mesh& raw, const CaseInput& ci,
                     GlobalMesh& gm, std::string& err);

} // namespace cfd2d
