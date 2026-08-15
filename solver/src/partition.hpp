#pragma once

#include <string>
#include <vector>

#include "common.hpp"
#include "case.hpp"
#include "mesh.hpp"

namespace cfd {

struct PartitionResult {
  std::vector<int> partOfCell;  // global cell -> owning rank
  long edgeCut = 0;
};

// Rank-local distributed mesh: owned cells plus one layer of ghost cells.
// Only rank-local data is retained for solver iterations.
struct LocalMesh {
  int nOwned = 0;
  int nGhost = 0;

  std::vector<Vec2> centroid;    // local cell centroid (owned + ghost)
  std::vector<double> volume;    // local cell volume
  std::vector<int> globalId;     // local cell -> global cell id
  std::vector<int> ownerRank;    // local cell -> owning rank

  struct Face {
    int c0, c1;        // local cells; c1 == -1 for boundary faces
    Vec2 normal;       // unit outward normal from c0
    double len;
    Vec2 centroid;
    int bc;            // BcType for boundary faces, -1 otherwise
    std::string tag;   // boundary family/section name for boundary faces
  };
  std::vector<Face> faces;
  std::vector<std::vector<int>> cellFaces;          // face indices per cell
  std::vector<std::vector<signed char>> cellFaceSign;  // +1 if cell==c0
  std::vector<std::vector<int>> neighbors;          // face-neighbor local ids

  std::vector<int> neighborRanks;                   // sorted
  std::vector<std::vector<int>> sendCells;          // per neighbor: local ids
  std::vector<std::vector<int>> recvCells;          // per neighbor: local ids

  // Wall faces (slip or no-slip) with outward normal from the owned cell:
  // parallel arrays for force/surface output.
  std::vector<int> wallFaceIdx;
  std::vector<int> wallCell;
  std::vector<Vec2> wallNormal;   // outward from wallCell
  std::vector<Vec2> wallCentroid;
  std::vector<double> wallLen;
  std::vector<std::string> wallTag;
  long edgeCut = 0;
  long numCellsGlobal = 0, numFacesGlobal = 0;

  // Owned-cell geometry for field output (rank-local only).
  std::vector<std::vector<Vec2>> cellPoints;   // per owned cell: vertex coords
  std::vector<std::vector<int>> cellPointIds;  // per owned cell: global vertex ids

  int numLocal() const { return nOwned + nGhost; }
};

// Partition the cell adjacency graph with METIS k-way (deterministic seed).
// All ranks call this on the full global mesh during preprocessing; the
// solver stage then keeps only the rank-local mesh.
PartitionResult partitionMesh(const GlobalMesh& mesh, int nRanks, int rank);

// Build the rank-local mesh (owned + ghost) from a global partition.
LocalMesh buildLocalMesh(const GlobalMesh& mesh, const PartitionResult& part,
                         int rank, int nRanks);

// Free-form partition summary used for diagnostics.
struct PartitionStats {
  long edgeCut = 0;
  int minOwned = 0, maxOwned = 0;
  double meanOwned = 0.0;
};

}  // namespace cfd
