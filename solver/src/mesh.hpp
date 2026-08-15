#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "case.hpp"

namespace cfd {

// Global (preprocessing) mesh loaded once from CGNS.  Supports multiple
// unstructured zones, mixed TRI/QUAD cells, boundary element sections and
// conformal 1-to-1 zone interfaces (joined geometrically through coincident
// vertices).
struct GlobalMesh {
  std::vector<Vec2> points;              // merged global vertices
  std::vector<std::vector<int>> cells;   // per-cell vertex lists (CCW)
  std::vector<std::string> cellZoneName; // originating zone per cell

  struct BoundaryFace {
    int v0, v1;       // vertex indices
    int familyId;     // index into familyNames (-1 if unknown)
    std::string section;
  };
  std::vector<BoundaryFace> boundaryFaces;
  std::vector<std::string> familyNames;

  struct Face {
    int v0, v1;   // vertices
    int c0, c1;   // adjacent cells, c1 == -1 for boundary faces
    int familyId; // family index for boundary faces (-1 otherwise)
    double len;   // edge length
    Vec2 normal;  // unit outward normal from c0
    Vec2 centroid;
  };
  std::vector<Face> faces;
  std::vector<int> faceBcType;  // resolved BC type per face (-1 interior)
  std::vector<std::vector<int>> cellFaces;                // face indices per cell
  std::vector<std::vector<signed char>> cellFaceSign;     // +1 if cell==c0

  std::vector<Vec2> cellCentroid;
  std::vector<double> cellVolume;

  std::vector<std::string> zoneNames;
  long n1to1 = 0;

  long numCells() const { return static_cast<long>(cells.size()); }
  long numFaces() const { return static_cast<long>(faces.size()); }
  long numBoundaryFaces() const { return static_cast<long>(boundaryFaces.size()); }
};

// Read a CGNS mesh.  Throws std::runtime_error on unsupported structure.
GlobalMesh loadCgnsMesh(const std::string& path);

// Resolve BC type per boundary face from the case-file family mapping into
// faceBcType.
void assignBoundaryConditions(GlobalMesh& mesh, const Case& case_def);

}  // namespace cfd
