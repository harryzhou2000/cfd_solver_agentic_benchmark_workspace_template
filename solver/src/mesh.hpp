#pragma once

#include "common.hpp"
#include "case_file.hpp"
#include <mpi.h>

namespace fv {

// Global mesh, assembled on rank 0 only (serial preprocessing).
struct GlobalMesh {
    long nNodes = 0;
    std::vector<double> x, y;                  // compact node coords
    long nCells = 0;
    std::vector<int> cellNodeOff;              // nCells+1
    std::vector<int> cellNodes;                // compact node ids
    struct BEdge { int a, b; int tag; };
    std::vector<BEdge> bedges;                 // boundary edges
    std::vector<std::string> tagNames;         // tag id -> mesh family name
};

// Rank-local mesh used during solver iterations: owned cells + ghost cells.
struct LocalMesh {
    int nOwned = 0, nGhost = 0;
    int nCells() const { return nOwned + nGhost; }
    int nNodes = 0;
    std::vector<double> nodeX, nodeY;
    std::vector<long> nodeGid;
    std::vector<int> cellNodeOff, cellNodes;   // local node ids, owned first then ghosts
    std::vector<long> cellGid;
    std::vector<int> ghostOwner;               // nGhost

    // cell geometry
    std::vector<double> cellCx, cellCy, cellVol;

    // faces: built for owned cells only
    int nFaces = 0;
    std::vector<int> faceCl;                   // owned cell index
    std::vector<int> faceCr;                   // cell index (owned or ghost) or -1 = boundary
    std::vector<int> faceBc;                   // bc tag id or -1
    std::vector<double> faceNx, faceNy;        // unit normal from cl to cr / outward
    std::vector<double> faceS;                 // face length
    std::vector<double> faceCx, faceCy;
    // vector from cell center to face center, per side
    std::vector<double> faceRxL, faceRyL, faceRxR, faceRyR;
    // LSQ gradient coefficients per face side (multiply var_neighbor - var_cell)
    std::vector<double> lsqLx, lsqLy, lsqRx, lsqRy;

    // per-owned-cell face list (CSR)
    std::vector<int> cellFaceOff, cellFace;

    // boundary face ids
    std::vector<int> bfaces;

    // raw boundary-face records from the partition file (cell, edge-in-cell, tag)
    std::vector<int> bndCell, bndEdge, bndTag;

    // halo communication plans (CSR over neighbor ranks)
    std::vector<int> sendRanks, sendOff, sendCells;
    std::vector<int> recvRanks, recvOff, recvCells;

    std::vector<std::string> tagNames;

    long nCellsGlobal = 0, nFacesGlobal = 0;
    long edgeCut = 0;
};

// Rank 0: read CGNS mesh + merge zones via 1-to-1 connections.
GlobalMesh read_global_mesh(const CaseConfig& cfg);

// Rank 0: partition with METIS and write per-rank partition files into dir.
// Returns false if cache already existed (nothing written).
bool build_partitions(const CaseConfig& cfg, const GlobalMesh& gm, int nparts,
                      const std::string& dir, long& edgeCutOut);

// All ranks: load own partition file.
LocalMesh load_partition(const std::string& dir, int rank);

// Build faces, geometry, LSQ weights after loading.
void finalize_local_mesh(LocalMesh& m);

// Global partition summary (written by rank 0 during build_partitions)
struct PartitionInfo {
    long nCellsGlobal = 0, nFacesGlobal = 0, edgeCut = 0;
    std::vector<int> ownedPerRank, ghostPerRank, bdryPerRank;
    std::vector<long> sendPerRank, recvPerRank;
    std::vector<int> neighborsPerRank;
};
PartitionInfo read_partition_info(const std::string& dir);

} // namespace fv
