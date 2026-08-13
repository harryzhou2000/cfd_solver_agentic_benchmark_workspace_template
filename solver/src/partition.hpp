// partition.hpp — METIS partitioning and rank-local mesh construction
#pragma once
#include "mesh.hpp"
#include <vector>
#include <map>
#include <set>

namespace cfd2d {

// Local mesh for a single MPI rank
struct LocalMesh {
    // Global -> local cell index mapping
    std::vector<int> globalToLocal; // size = global ncell, -1 if not local
    std::vector<int> localToGlobal; // local cell -> global cell

    int nOwned = 0;       // number of owned cells
    int nGhost = 0;       // number of ghost cells
    int nLocalCells = 0;  // nOwned + nGhost

    // Ghost cell info
    std::vector<int> ghostOwnerRank; // size = nGhost, rank that owns this ghost
    std::vector<int> ghostGlobalId;  // size = nGhost

    // Local faces (all faces of owned cells)
    struct LocalFace {
        int lc, rc;       // local cell indices (-1 for boundary on rc)
        int bcTag;        // -1 if interior
        double nx, ny, area, cx, cy;
        int gn0, gn1;     // global node IDs
    };
    std::vector<LocalFace> faces;
    int nInteriorFace = 0;
    int nBoundaryFace = 0;
    int nPartitionFace = 0;

    // Cell geometry (local)
    std::vector<double> cellCx, cellCy, cellVol;

    // Original mesh pointer (for vertex access during output)
    const Mesh* globalMesh = nullptr;

    // Communication pattern
    struct NeighborComm {
        int rank;
        std::vector<int> sendCells;  // local indices of owned cells to send
        std::vector<int> recvCells;  // local indices of ghost cells to receive
    };
    std::vector<NeighborComm> neighbors;
    std::vector<int> neighborRanks;

    // Partition info
    int edgeCut = 0;
    int mpiRank = 0;
    int mpiSize = 1;
    std::string partitionerName = "metis_kway";
};

// Partition mesh with METIS and construct local mesh for each rank
void partitionMesh(const Mesh& mesh, int mpiRank, int mpiSize,
                   std::vector<int>& partition, int& edgeCut);

// Build rank-local mesh from global mesh and partition
void buildLocalMesh(LocalMesh& lm, const Mesh& mesh, const std::vector<int>& partition,
                    int mpiRank, int mpiSize, int edgeCut);

} // namespace cfd2d
