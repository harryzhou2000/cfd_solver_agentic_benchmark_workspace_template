#pragma once
#include "types.hpp"
#include <mpi.h>
#include <string>

namespace cfd {

// A geometric face in the mesh (2-D: a line segment between two nodes).
struct Face {
    Int node0 = -1, node1 = -1;      // local vertex indices (for output)
    Int cellL = -1, cellR = -1;      // local cell indices; cellR<0 => boundary
    BCType bc = BCType::Interior;
    Vec2 center;
    Vec2 normal;   // unit normal pointing from cellL to cellR
    Real length = 0.0;
    bool ghostR = false; // true if cellR is a ghost (partition interface)
};

// Global mesh read from CGNS (built on every rank during preprocessing,
// then freed after the rank-local mesh is extracted).
struct GlobalMesh {
    std::vector<Vec2> vertices;
    // cells: each cell stores its vertex indices (3 for tri, 4 for quad)
    std::vector<std::vector<Int>> cells;
    std::vector<Int> cellNbr; // 3 or 4
    // boundary faces: family name -> list of (node0,node1) pairs (global vertex ids)
    // built from CGNS BAR_2 boundary sections + family mapping
    std::map<std::string, std::vector<std::array<Int,2>>> bcFaces;
    Int numCells() const { return (Int)cells.size(); }
};

// Global face connectivity (cell-cell adjacency) built from the global mesh.
struct GlobalConnectivity {
    // For each global cell, list of (faceLocalId, neighborCell or -1, bc family idx)
    // We instead build a flat face list:
    std::vector<Int> fNode0, fNode1;     // global vertex ids
    std::vector<Int> fCellL, fCellR;     // global cell ids; fCellR=-1 => boundary
    std::vector<Int> fBCFam;             // index into bcFamilyNames, -1 if interior
    std::vector<std::string> bcFamilyNames;
    Int numFaces() const { return (Int)fNode0.size(); }
};

// Rank-local mesh: owned cells (0..nOwned-1) followed by ghost cells.
struct LocalMesh {
    int rank = 0, nranks = 1;
    Int nOwned = 0, nGhost = 0, nCells = 0;
    std::vector<Vec2> vertices;          // local vertices
    std::vector<std::vector<Int>> cells; // local cell vertex indices
    std::vector<Vec2> cellCenter;
    std::vector<Real> cellArea;
    std::vector<Face> faces;
    // For each cell, list of face indices (for output / reconstruction stencil)
    std::vector<std::vector<Int>> cellFaces;
    // neighbor stencil (cell-cell adjacency, local indices incl. ghosts)
    std::vector<std::vector<Int>> cellNeighbors;

    // partition / halo bookkeeping
    std::vector<int> ghostOwnerRank;     // per ghost cell (local ghost idx -> rank)
    std::vector<Int> ghostGlobalId;      // local ghost idx -> global cell id
    std::vector<Int> ownedGlobalId;      // local owned idx -> global cell id
    std::vector<int> neighborRanks;
    // send: for each neighbor rank, list of owned local cell indices to pack
    std::vector<std::vector<Int>> sendCells;
    // recv: for each neighbor rank, list of local ghost cell indices to fill
    std::vector<std::vector<Int>> recvCells;
    Int edgeCut = 0;
    Int numCellsGlobal = 0, numFacesGlobal = 0;
};

// Read a CGNS mesh (mixed tri/quad, possibly multi-zone) into GlobalMesh.
// bcNamesFound is filled with the boundary family names present in the mesh.
void readCGNS(const std::string& path, GlobalMesh& gm,
              std::vector<std::string>& bcNamesFound);

// Build global face connectivity + cell geometry from the global mesh.
void buildGlobalConnectivity(GlobalMesh& gm, GlobalConnectivity& gc);

// Partition the global cell graph with METIS and build the rank-local mesh.
// Returns the partition array (global cell id -> rank) on all ranks.
void partitionAndBuildLocal(GlobalMesh& gm, GlobalConnectivity& gc,
                            const std::map<std::string, BCType>& bcMap,
                            LocalMesh& lm, MPI_Comm comm);

// Halo exchange of conservative state (nOwned values are source; ghosts filled).
void exchangeHalo(const LocalMesh& lm, std::vector<Cons>& U, MPI_Comm comm);

// Exchange a scalar field (per cell) for diagnostics.
void exchangeHaloScalar(const LocalMesh& lm, std::vector<Real>& f, MPI_Comm comm);

} // namespace cfd
