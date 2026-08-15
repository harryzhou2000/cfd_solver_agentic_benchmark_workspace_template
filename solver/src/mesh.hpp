#pragma once
// Unstructured 2-D mesh: CGNS reader, geometry, face/cell adjacency.
//
// The mesh is read into a single global structure (all zones merged).  Cross-
// zone abutting interfaces are resolved into interior faces by merging
// coincident vertices across zones (the mesh generator writes identical
// coordinates on both sides of every interface).  Boundary faces that survive
// as single-sided are classified by their CGNS BAR_2 section name through the
// case boundary-condition map.
#include <map>
#include <string>
#include <vector>
#include "case.hpp"
#include "types.hpp"

namespace cfd {

struct BoundaryFaceInfo {
  BCType type = BCType::None;
  int section_id = -1;          // index into Mesh::bc_section_names
  std::string family;           // CGNS family / section name
};

struct Mesh {
  // Vertices (global)
  int nvert = 0;
  std::vector<double> vx, vy;

  // Cells (mixed TRI_3 / QUAD_4)
  int ncell = 0;
  std::vector<int> cellOffset;   // size ncell+1, into cellVerts
  std::vector<int> cellVerts;    // concatenated vertex indices
  std::vector<int> cellNv;       // 3 or 4 per cell
  std::vector<double> cellCx, cellCy, cellVol;

  // Faces (edges).  Interior faces have faceR>=0; boundary faces have faceR<0
  // and a BC entry in faceBC.
  int nface = 0;
  std::vector<int> faceL, faceR;          // owning cells; faceR=-1 if boundary
  std::vector<int> faceV0, faceV1;        // face vertices (v0 < v1)
  std::vector<double> faceNx, faceNy;     // outward normal of L cell
  std::vector<double> faceLen;            // edge length (2-D "area")
  std::vector<double> faceCx, faceCy;     // face midpoint
  std::vector<BoundaryFaceInfo> faceBC;   // only meaningful when faceR<0

  // CGNS boundary section names (for surface output grouping / debugging).
  std::vector<std::string> bc_section_names;

  // Per-cell face list (for reconstruction / viscous gradients).
  std::vector<int> cellFaceOffset;        // size ncell+1
  std::vector<int> cellFaces;             // face indices (signed: negative => use as boundary, see below)

  int numBoundaryFaces() const {
    int n = 0;
    for (int i = 0; i < nface; ++i) if (faceR[i] < 0) ++n;
    return n;
  }
};

// Read a CGNS mesh (possibly multi-zone) and resolve interfaces.  The bc map
// comes from the case file (family name -> BCType).  Returns a filled Mesh.
Mesh readCGNSMesh(const std::string& file, const CaseConfig& cfg);

}  // namespace cfd
