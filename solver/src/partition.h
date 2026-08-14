#pragma once

#include "mesh.h"
#include "types.h"
#include <vector>
#include <map>
#include <mpi.h>

namespace cfd2d {

// Rank-local mesh data
struct LocalMesh {
  // Owned + ghost cells (ghost cells come after owned)
  int numOwned = 0;
  int numGhost = 0;
  int numLocal = 0; // owned + ghost

  // Geometry for local cells (owned + ghost)
  std::vector<double> cx, cy, area;
  std::vector<std::vector<int>> cellNodes; // local cell -> global node ids
  // Vertex coordinates (global, shared copy for output)
  std::vector<double> vx, vy;

  // Local faces (interior + boundary), using local cell indices
  std::vector<Face> faces;

  // Ghost cell -> owning rank
  std::vector<int> ghostRank;
  // global cell id -> local index (for owned + ghost)
  std::map<int, int> globalToLocal;
  // reverse map: local index -> global cell id (for fast halo exchange)
  std::vector<int> localToGlobal;

  // Communication info
  std::vector<int> neighborRanks;
  // For each neighbor rank: list of local ghost cell indices to recv
  std::map<int, std::vector<int>> recvFromNeighbor;
  // For each neighbor rank: list of local owned cell indices to send
  std::map<int, std::vector<int>> sendToNeighbor;

  // Partition diagnostics
  int edgeCut = 0;
  int numBoundaryFaces = 0;
  int numNeighborRanks = 0;
};

// Partition the global mesh using METIS and build rank-local mesh.
// Called by all ranks; rank 0 does the partitioning and broadcasts.
bool partitionMesh(const GlobalMesh& gm, int rank, int nranks,
                   LocalMesh& lm, std::string& err);

} // namespace cfd2d
