#pragma once
// METIS-based cell graph partitioning and rank-local mesh construction.
//
// Serial preprocessing happens on rank 0: the global mesh is loaded once,
// the cell-face adjacency graph is built, METIS_PartGraphKway partitions it,
// and rank-local packages (owned cells + ghost cells + faces + neighbor
// communication lists) are serialized and sent to worker ranks. During solver
// iterations each rank stores only its own partition plus one ghost layer.

#include <vector>

#include "common.hpp"
#include "mesh.hpp"

namespace cfd2d {

struct LocalMesh {
  int nOwned = 0;
  int nGhost = 0;
  int nLocal = 0;  // nOwned + nGhost

  int nNodes = 0;
  std::vector<double> nodeX, nodeY;

  // Cells: local indices [0, nOwned) are owned; [nOwned, nLocal) are ghosts.
  std::vector<int> cellGlobal;       // local -> global cell id
  std::vector<int> cellOwner;        // owner rank (my rank for owned cells)
  std::vector<int> cellNNodes;
  std::vector<std::array<int, 4>> cellNodes;  // local node ids
  std::vector<double> cellVol, cellCx, cellCy;

  // Faces incident to at least one owned cell.
  int nFaces = 0;
  std::vector<int> faceCellL;   // local cell index (owned)
  std::vector<int> faceCellR;   // local cell index (owned or ghost), -1 = boundary
  std::vector<int> faceBcFam;
  std::vector<double> faceNx, faceNy, faceLen, faceCx, faceCy;

  // Per-cell face lists (faces of owned cells), for sweeps and gradients.
  std::vector<std::vector<int>> cellFaces;

  std::vector<std::string> famNames;

  // Neighbor communication description.
  std::vector<int> neighbors;                 // neighbor ranks, sorted
  std::vector<std::vector<int>> sendCells;    // owned local ids sent to neighbor
  std::vector<std::vector<int>> recvCells;    // ghost local ids received from neighbor
};

// Compute local cell geometry (volumes, centroids) from local nodes.
void computeLocalGeometry(LocalMesh& lm);

// Rank 0: partition the global mesh with METIS and distribute rank-local
// packages. Other ranks: receive their package. edgeCut (rank 0) is reported
// in metadata; on workers it is received from rank 0.
LocalMesh distributeMesh(const GlobalMesh& gm, int rank, int nRanks,
                         int& edgeCut);

}  // namespace cfd2d
