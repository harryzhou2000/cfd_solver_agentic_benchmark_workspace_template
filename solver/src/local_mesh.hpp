#pragma once
// Rank-local mesh: owned cells + ghost cells + local faces + halo plan.
// This is the only mesh representation used during solver iterations.
#include "common.hpp"
#include "case_config.hpp"

namespace fv {

struct HaloNeighbor {
  int rank = -1;
  vector<int> sendIdx;  // local owned cell indices to send (ascending global id order)
  vector<int> recvIdx;  // local ghost cell indices to receive (same order)
};

struct LocalFace {
  int c0 = -1, c1 = -1;    // local cell indices; c1 == -1 for boundary faces
  int n0 = -1, n1 = -1;    // local node indices
  int bc = 0;              // 0 = interior, else (int)BCType
  int family = -1;         // index into familyNames for boundary faces
  long globalId = -1;      // global face id (for output assembly)
  // geometry
  double fx = 0, fy = 0;   // centroid
  double nx = 0, ny = 0;   // unit normal from c0 towards c1 (outward at boundaries)
  double area = 0;         // face length
};

struct LsqEntry {
  int idx;      // >=0: local cell index; <0: boundary face index encoded as ~faceIdx
  double w;     // LSQ weight
  double dx, dy;  // stencil point relative to cell centroid
};

struct LocalMesh {
  int nOwn = 0, nGhost = 0, nAll = 0;
  int nNodes = 0;
  long nCellsGlobal = 0, nFacesGlobal = 0, nNodesGlobal = 0;
  long partitionEdgeCut = 0;

  vector<long> globalCellId;   // nAll (owned first, then ghosts)
  vector<int> cellOwner;       // nAll: owning rank (my rank for owned cells)

  vector<double> nodeX, nodeY; // nNodes
  vector<long> globalNodeId;   // nNodes

  vector<array<int, 4>> cellNodes;  // nAll, local node ids
  vector<int> cellNNodes;           // nAll

  vector<LocalFace> faces;
  vector<string> familyNames;  // global family table (same on all ranks)

  vector<HaloNeighbor> neighbors;

  // geometry
  vector<double> xc, yc, vol;  // nAll

  // cell -> faces CSR (over faces touching the cell)
  vector<int> cellFaceOff, cellFaceIdx;

  // LSQ reconstruction stencil (owned cells only)
  vector<int> lsqOff;
  vector<LsqEntry> lsqEntries;
  vector<double> lsqInv00, lsqInv01, lsqInv11;  // inverse moment matrix per owned cell

  // boundary face indices (into faces) that are wall-type (for forces/surface)
  vector<int> wallFaces;      // owned boundary faces with wall BC
  vector<int> boundaryFaces;  // all owned boundary faces

  void computeGeometry();
  void buildCellFaces();
  void buildLsq();
  double minCellSize() const;
};

}  // namespace fv
