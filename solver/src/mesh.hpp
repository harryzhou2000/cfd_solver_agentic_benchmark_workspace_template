#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "case_file.hpp"
#include "common.hpp"

namespace fv {

// Global (rank-0) unstructured 2-D mixed mesh.
struct GlobalMesh {
  int nNodes = 0;
  std::vector<double> x, y;  // node coords

  int nCells = 0;
  std::vector<int> cellNVerts;      // 3 or 4 per cell
  std::vector<int> cellNodeOffset;  // nCells+1
  std::vector<int> cellNodes;       // global node ids

  // faces: interior faces have right >= 0; boundary faces have right == -1
  int nFaces = 0;
  std::vector<int> faceNodeA, faceNodeB;  // sorted global node ids
  std::vector<int> faceLeft, faceRight;
  std::vector<int> faceBc;  // -1 interior, else index into bcNames

  std::vector<std::string> bcNames;  // boundary family names

  // geometry
  std::vector<double> cellCx, cellCy, cellVol;
  std::vector<double> faceCx, faceCy;    // face centroid
  std::vector<double> faceNx, faceNy;    // normal * area, points L->R (out of L)
  std::vector<double> faceArea;
  std::vector<double> faceDx, faceDy;    // vector from L-center to R-center (or ghost center)

  std::map<int, int> bcNameToId;  // family name -> index in bcNames
};

// Rank-local mesh: owned cells [0, nOwned) then ghost cells [nOwned, nCells).
struct LocalMesh {
  int nOwned = 0;
  int nGhost = 0;
  int nCells = 0;  // owned + ghost
  int nNodes = 0;

  std::vector<long> cellGlobal;  // global cell id per local cell
  std::vector<double> x, y;      // local node coords
  std::vector<int> cellNVerts;
  std::vector<int> cellNodeOffset;
  std::vector<int> cellNodes;  // local node ids

  // faces touching at least one owned cell; left cell is always owned
  int nFaces = 0;
  std::vector<int> faceL;              // owned local cell
  std::vector<int> faceR;              // local cell index or -1 for boundary
  std::vector<int> faceBc;             // -1 interior, else bc id
  std::vector<double> faceCx, faceCy;
  std::vector<double> faceNx, faceNy;  // normal*area out of L
  std::vector<double> faceArea;
  std::vector<double> faceDx, faceDy;  // L-center -> R-center (or mirrored ghost center)

  std::vector<std::string> bcNames;
  std::map<int, int> bcNameToId;

  // per-cell list of face indices (CSR)
  std::vector<int> cellFaceOffset;
  std::vector<int> cellFaces;
  std::vector<int> cellFaceSign;  // +1 if cell is L, -1 if cell is R (boundary: +1)

  std::vector<double> cellCx, cellCy, cellVol;

  // halo communication
  struct Neighbor {
    int rank = -1;
    std::vector<int> sendIdx;  // owned local indices to send (ascending global order)
    std::vector<int> recvIdx;  // ghost local indices to recv (same order as sender's sendIdx)
  };
  std::vector<Neighbor> neighbors;
};

// Read CGNS file and build the global mesh (rank 0 only).
GlobalMesh readCgnsMesh(const std::string& path);

// Build face topology + geometry from cell connectivity.
void buildFacesAndGeometry(GlobalMesh& m);

// Finalize a rank-local mesh: cell geometry, face geometry, cell-face CSR.
// fEdgeA/fEdgeB give local node ids of each face edge (size nFaces).
void finalizeLocalGeometryFromEdges(LocalMesh& lm, const std::vector<int>& fEdgeA,
                                    const std::vector<int>& fEdgeB);

}  // namespace fv
