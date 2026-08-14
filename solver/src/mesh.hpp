#ifndef CFD2D_MESH_HPP
#define CFD2D_MESH_HPP

#include "types.hpp"
#include <mpi.h>
#include <map>
#include <unordered_map>

namespace cfd2d {

struct Face {
  int v0, v1;
  int cl, cr;   // left (owner), right (neighbor or -1 boundary)
  BCType bc;
};

// Full mesh read from CGNS (used during preprocessing only, then freed)
struct GlobalMesh {
  std::vector<double> vx, vy;
  std::vector<int> cellOff;     // CSR: cellOff[i]..cellOff[i+1] into cellVerts
  std::vector<int> cellVerts;
  std::vector<int> cellNVert;   // 3 or 4
  int nCells = 0, nVert = 0;

  std::vector<Face> faces;
  std::vector<double> cx, cy, cvol;
  std::vector<double> fcx, fcy, fnx, fny, flen;

  void readCGNS(const std::string& file, const std::vector<std::pair<std::string,BCType>>& bcMap);
  void buildFaces();
  void computeGeometry();
};

// Rank-local mesh with ghost cells and communication info
struct LocalMesh {
  std::vector<double> vx, vy;
  std::vector<int> cellOff, cellVerts, cellNVert;
  int nOwned = 0, nGhost = 0, nCells = 0;
  std::vector<int> globalCellId;

  std::vector<Face> faces;
  std::vector<double> cx, cy, cvol;
  std::vector<double> fcx, fcy, fnx, fny, flen;

  // cell -> faces (CSR)
  std::vector<int> cellFaceOff, cellFaces;

  // Communication
  std::vector<int> neighborRanks;
  std::vector<std::vector<int>> sendLocal, recvLocal;

  int nCellsGlobal = 0, nFacesGlobal = 0, edgeCut = 0;
  double loadBalance = 1.0;

  // Exchange conservative state of ghost cells
  void exchangeGhost(Cons* U, MPI_Comm comm, int rank) const;
};

// Partition the global mesh with METIS and build the local mesh
void partitionAndBuildLocal(const GlobalMesh& gm, int rank, int nprocs,
                            LocalMesh& lm, MPI_Comm comm);

} // namespace cfd2d
#endif
