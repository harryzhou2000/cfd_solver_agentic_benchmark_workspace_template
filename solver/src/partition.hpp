#pragma once
// METIS cell-graph partitioning and rank-local mesh construction.
//
// The full mesh is loaded during preprocessing (allowed).  Rank 0 builds the
// cell adjacency graph, calls METIS_PartGraphKway, and broadcasts the partition
// to all ranks.  Each rank then extracts its owned cells plus the ghost cells
// required by its stencil, builds a rank-local face list, and computes the
// neighbor-scoped send/recv maps used for halo exchange.  During solver
// iterations only the rank-local (owned + ghost) state is stored; the full
// mesh and full state are never replicated during iterations.
#include <map>
#include <vector>
#include "mesh.hpp"
#include "types.hpp"

namespace cfd {

struct LocalMesh {
  int rank = 0;
  int nranks = 1;

  // Local cells: first nOwned are owned, next nGhost are ghosts.
  int nOwned = 0;
  int nGhost = 0;
  int nLocal() const { return nOwned + nGhost; }

  // Cell geometry (size nLocal).
  std::vector<double> cellCx, cellCy, cellVol;
  // Cell vertices (for output): per-cell offset + vertex global indices.
  std::vector<int> cellNv, cellOffset, cellVerts;

  // Global index of each local cell (owned then ghosts).
  std::vector<int> globalCell;
  // Owner rank of each local cell (owned => rank, ghost => owner rank).
  std::vector<int> cellOwner;
  // Map global cell id -> local index (only for cells touching this rank).
  std::vector<int> globalToLocal;  // size = global ncell, -1 if absent

  // Faces touching at least one owned cell.  faceL is always an owned local
  // cell; faceR is a local index (owned or ghost) or -1 for a boundary face.
  // The face normal is the outward normal of faceL.
  int nFace = 0;
  std::vector<int> faceL, faceR;
  std::vector<int> faceV0, faceV1;          // global vertex indices
  std::vector<double> faceNx, faceNy, faceLen, faceCx, faceCy;
  std::vector<BoundaryFaceInfo> faceBC;

  // Per-owned-cell face adjacency (for reconstruction / residual).
  std::vector<int> cellFaceOffset;          // size nOwned+1
  std::vector<int> cellFaces;               // face indices

  // Neighbor communication: for each neighbor rank, the local owned cell
  // indices to send and the local ghost indices to receive (matched order).
  std::vector<int> neighborRanks;
  std::vector<std::vector<int>> sendCells;   // local owned idx to pack & send
  std::vector<std::vector<int>> recvGhosts;  // local ghost idx to fill

  // Global partition diagnostics (collected on rank 0).
  int num_cells_global = 0;
  int num_faces_global = 0;
  int partition_edge_cut = 0;

  int numBoundaryFaces() const {
    int n = 0;
    for (int f = 0; f < nFace; ++f) if (faceR[f] < 0) ++n;
    return n;
  }
};

// Partition the global mesh into nranks parts and return this rank's local
// mesh.  Must be called collectively by all MPI ranks.
LocalMesh partitionMesh(const Mesh& global, const CaseConfig& cfg, int nranks,
                        int rank, /*out*/ std::vector<int>& partGlobal);

// Count the METIS edge cut of a partition (sum of cut interior faces).
int countEdgeCut(const Mesh& global, const std::vector<int>& part);

}  // namespace cfd
