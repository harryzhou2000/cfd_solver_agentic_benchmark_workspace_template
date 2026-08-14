#pragma once
// Global unstructured mesh model and CGNS reader (rank-0 preprocessing).
//
// The reader supports multi-zone CGNS files with TRI_3/QUAD_4 volume elements
// and BAR_2 boundary/interface edge sections. Duplicate nodes at conformal
// zone interfaces are merged geometrically so the whole domain becomes one
// global unstructured mesh. Boundary faces are tagged by the boundary-family
// names that appear in the case file's boundary_conditions map; other BAR
// sections (e.g. 1-to-1 zone interface markers) are ignored as BCs.

#include <map>
#include <string>
#include <vector>

#include "common.hpp"

namespace cfd2d {

struct GlobalMesh {
  int nNodes = 0;
  std::vector<double> nodeX, nodeY;

  int nCells = 0;
  std::vector<int> cellNNodes;                 // 3 or 4
  std::vector<std::array<int, 4>> cellNodes;   // global node ids

  int nFaces = 0;
  std::vector<int> faceCellL;                  // owner cell (always >= 0)
  std::vector<int> faceCellR;                  // neighbor cell, -1 = boundary
  std::vector<int> faceBcFam;                  // family id for boundary, else -1
  std::vector<int> faceN1, faceN2;             // global node ids of the edge

  // Geometry
  std::vector<double> cellVol, cellCx, cellCy;
  std::vector<double> faceNx, faceNy;          // unit normal, L -> R / outward
  std::vector<double> faceLen;
  std::vector<double> faceCx, faceCy;

  std::vector<std::string> famNames;           // family id -> name

  int famId(const std::string& name) const {
    for (size_t i = 0; i < famNames.size(); ++i)
      if (famNames[i] == name) return static_cast<int>(i);
    return -1;
  }
};

// Read a CGNS file and build the global mesh. bcFamilyNames lists the
// boundary-family names (from the case JSON) that should be treated as
// boundary conditions; it is used only for validation up front (the tag
// itself is stored by name match later). Throws FatalError on problems.
GlobalMesh readCgnsMesh(const std::string& path,
                        const std::vector<std::string>& bcFamilyNames);

// Build cell volumes/centroids, faces, face geometry, and BC tagging.
void buildGeometry(GlobalMesh& m, const std::vector<std::string>& bcFamilyNames);

}  // namespace cfd2d
