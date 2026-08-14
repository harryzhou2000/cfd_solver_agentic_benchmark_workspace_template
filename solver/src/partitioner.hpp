#pragma once
#include "types.hpp"
#include "mesh.hpp"
#include <metis.h>
#include <mpi.h>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>

namespace cfd {

// Distributed (rank-local) mesh
struct DistMesh {
    // Local cell geometry
    std::vector<Real> cellCx, cellCy, cellVol;
    std::vector<std::vector<int>> cellNodes; // local cell -> global node indices
    int nOwned = 0;
    int nGhost = 0;
    int nLocalCells() const { return nOwned + nGhost; }

    // Local face data
    std::vector<int> faceLc, faceRc; // local cell indices (-1 for boundary rc)
    std::vector<Real> faceNx, faceNy, faceArea, faceFx, faceFy;
    std::vector<BCType> faceBc;
    std::vector<std::string> faceFamily;
    std::vector<int> faceGlobalNodes0, faceGlobalNodes1; // for surface output
    int numFaces() const { return (int)faceLc.size(); }

    // Cell-to-face adjacency for LU-SGS
    std::vector<std::vector<int>> cellFaces;

    // Ghost cell info
    std::vector<int> ghostGlobalCell; // global cell index for each ghost
    std::vector<int> ghostOwnerRank;  // owning rank for each ghost

    // Global -> local cell mapping (for this rank)
    std::unordered_map<int,int> globalToLocal;

    // Communication maps
    std::vector<int> neighborRanks;
    std::vector<std::vector<int>> sendCells; // per neighbor: local owned cell indices to send
    std::vector<std::vector<int>> recvCells; // per neighbor: local ghost cell indices to fill

    // Global mesh info
    int numCellsGlobal = 0;
    int numFacesGlobal = 0;
    int numNodesGlobal = 0;
    int edgeCut = 0;

    // Boundary face global node coords for surface output
    std::vector<Real> bfaceNodeX0, bfaceNodeY0, bfaceNodeX1, bfaceNodeY1;
};

class Partitioner {
public:
    // Partition the global mesh and extract rank-local mesh
    // All ranks call this; they all read the full mesh and partition identically
    static DistMesh partition(const GlobalMesh& gmesh, int nParts, int myRank,
                              std::string& err) {
        DistMesh dm;
        int nc = gmesh.numCells();
        dm.numCellsGlobal = nc;
        dm.numFacesGlobal = gmesh.numFaces();
        dm.numNodesGlobal = gmesh.numNodes();

        // Build CSR adjacency graph from face connectivity
        std::vector<std::vector<int>> adj(nc);
        for (int fi = 0; fi < gmesh.numFaces(); fi++) {
            auto& f = gmesh.faces[fi];
            if (f.rc >= 0) {
                adj[f.lc].push_back(f.rc);
                adj[f.rc].push_back(f.lc);
            }
        }

        std::vector<idx_t> xadj(nc + 1), adjncy;
        xadj[0] = 0;
        for (int c = 0; c < nc; c++) {
            for (int n : adj[c]) adjncy.push_back(n);
            xadj[c + 1] = (idx_t)adjncy.size();
        }

        std::vector<idx_t> part(nc);
        idx_t nvtxs = nc, ncon = 1, nparts = nParts, objval = 0;
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_CONTIG] = 1; // contiguous partitions
        options[METIS_OPTION_SEED] = 42;

        int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                       nullptr, nullptr, nullptr,
                                       &nparts, nullptr, nullptr,
                                       options, &objval, part.data());

        if (ret != METIS_OK) {
            err = "METIS partitioning failed";
            return dm;
        }
        dm.edgeCut = (int)objval;

        // Extract local mesh for this rank
        // 1. Collect owned cells
        std::vector<int> ownedCells;
        for (int c = 0; c < nc; c++) {
            if (part[c] == myRank) ownedCells.push_back(c);
        }

        // 2. Find ghost cells (neighbors of owned cells on other ranks)
        std::map<int, int> ghostRank; // global cell -> owning rank
        for (int c : ownedCells) {
            for (int n : adj[c]) {
                if (part[n] != myRank) {
                    ghostRank[n] = part[n];
                }
            }
        }

        // 3. Build local cell list: owned first, then ghost
        std::vector<int> localToGlobal;
        for (int c : ownedCells) localToGlobal.push_back(c);
        for (auto& [gc, rk] : ghostRank) localToGlobal.push_back(gc);

        dm.nOwned = (int)ownedCells.size();
        dm.nGhost = (int)ghostRank.size();

        // Build global -> local mapping
        for (int i = 0; i < (int)localToGlobal.size(); i++) {
            dm.globalToLocal[localToGlobal[i]] = i;
        }

        // 4. Build local cell geometry
        dm.cellCx.resize(dm.nLocalCells());
        dm.cellCy.resize(dm.nLocalCells());
        dm.cellVol.resize(dm.nLocalCells());
        dm.cellNodes.resize(dm.nLocalCells());
        for (int i = 0; i < dm.nLocalCells(); i++) {
            int gc = localToGlobal[i];
            dm.cellCx[i] = gmesh.cellCx[gc];
            dm.cellCy[i] = gmesh.cellCy[gc];
            dm.cellVol[i] = gmesh.cellVol[gc];
            dm.cellNodes[i] = gmesh.cellNodes[gc];
        }

        // 5. Build ghost cell info
        dm.ghostGlobalCell.resize(dm.nGhost);
        dm.ghostOwnerRank.resize(dm.nGhost);
        int gi = 0;
        for (auto& [gc, rk] : ghostRank) {
            dm.ghostGlobalCell[gi] = gc;
            dm.ghostOwnerRank[gi] = rk;
            gi++;
        }

        // 6. Build local faces
        // For each global face, check if it involves a local cell
        for (int fi = 0; fi < gmesh.numFaces(); fi++) {
            auto& f = gmesh.faces[fi];
            int gc0 = f.lc;
            int gc1 = f.rc;

            bool lcOwned = (part[gc0] == myRank);
            bool rcOwned = (gc1 >= 0 && part[gc1] == myRank);

            if (!lcOwned && !rcOwned) continue; // face not on this rank

            int localLc, localRc;
            if (lcOwned) {
                localLc = dm.globalToLocal[gc0];
                localRc = (gc1 >= 0) ? dm.globalToLocal[gc1] : -1;
            } else {
                // rc is owned, swap so owned cell is lc
                localLc = dm.globalToLocal[gc1];
                localRc = dm.globalToLocal[gc0];
            }

            dm.faceLc.push_back(localLc);
            dm.faceRc.push_back(localRc);

            // Normal direction: if we swapped, flip the normal
            if (lcOwned) {
                dm.faceNx.push_back(f.nx);
                dm.faceNy.push_back(f.ny);
            } else {
                dm.faceNx.push_back(-f.nx);
                dm.faceNy.push_back(-f.ny);
            }
            dm.faceArea.push_back(f.area);
            dm.faceFx.push_back(f.fx);
            dm.faceFy.push_back(f.fy);
            dm.faceBc.push_back(f.bc);
            dm.faceFamily.push_back(f.family);
            dm.faceGlobalNodes0.push_back(f.n0);
            dm.faceGlobalNodes1.push_back(f.n1);

            // Store boundary face node coords for surface output
            if (f.bc != BCType::None) {
                dm.bfaceNodeX0.push_back(gmesh.x[f.n0]);
                dm.bfaceNodeY0.push_back(gmesh.y[f.n0]);
                dm.bfaceNodeX1.push_back(gmesh.x[f.n1]);
                dm.bfaceNodeY1.push_back(gmesh.y[f.n1]);
            }
        }

        // 7. Build cell-to-face adjacency
        dm.cellFaces.resize(dm.nLocalCells());
        for (int fi = 0; fi < dm.numFaces(); fi++) {
            dm.cellFaces[dm.faceLc[fi]].push_back(fi);
            if (dm.faceRc[fi] >= 0) {
                dm.cellFaces[dm.faceRc[fi]].push_back(fi);
            }
        }

        // 8. Build communication maps
        buildCommMaps(dm, localToGlobal, part, myRank);

        return dm;
    }

private:
    static void buildCommMaps(DistMesh& dm, const std::vector<int>& localToGlobal,
                              const std::vector<idx_t>& part, int myRank) {
        // For each ghost cell, determine which rank owns it and build send/recv lists
        std::map<int, std::vector<int>> sendMap; // neighbor rank -> local owned cell indices to send
        std::map<int, std::vector<int>> recvMap; // neighbor rank -> local ghost cell indices

        // Ghost cells: indices nOwned to nOwned+nGhost-1
        for (int i = 0; i < dm.nGhost; i++) {
            int localIdx = dm.nOwned + i;
            int owner = dm.ghostOwnerRank[i];
            recvMap[owner].push_back(localIdx);
        }

        // For send: for each neighbor rank, find which of our owned cells
        // are ghosts on that rank
        // Build reverse lookup: global cell -> list of ranks that have it as ghost
        // We need to know which cells other ranks need from us
        // Approach: exchange ghost cell global indices with neighbors

        // First, collect neighbor ranks
        std::set<int> neighborSet;
        for (auto& [rk, cells] : recvMap) neighborSet.insert(rk);

        // Exchange ghost cell global indices with each neighbor
        for (int neighbor : neighborSet) {
            // Send our recv (ghost) global indices to neighbor
            // Recv their recv (ghost) global indices from neighbor
            std::vector<int> myGhostGlobals;
            for (int li : recvMap[neighbor]) {
                int gi2 = li - dm.nOwned;
                myGhostGlobals.push_back(dm.ghostGlobalCell[gi2]);
            }

            int sendCount = (int)myGhostGlobals.size();
            int recvCount = 0;
            MPI_Sendrecv(&sendCount, 1, MPI_INT, neighbor, 0,
                         &recvCount, 1, MPI_INT, neighbor, 0,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            std::vector<int> theirGhostGlobals(recvCount);
            MPI_Sendrecv(myGhostGlobals.data(), sendCount, MPI_INT, neighbor, 1,
                         theirGhostGlobals.data(), recvCount, MPI_INT, neighbor, 1,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // Their ghost globals that match our owned cells -> we need to send those
            std::vector<int> sendIdx;
            for (int g : theirGhostGlobals) {
                auto it = dm.globalToLocal.find(g);
                if (it != dm.globalToLocal.end() && it->second < dm.nOwned) {
                    sendIdx.push_back(it->second);
                } else {
                    sendIdx.push_back(-1); // shouldn't happen
                }
            }
            sendMap[neighbor] = sendIdx;
        }

        // Store in DistMesh
        dm.neighborRanks.clear();
        for (auto& [rk, cells] : recvMap) dm.neighborRanks.push_back(rk);
        dm.sendCells.resize(dm.neighborRanks.size());
        dm.recvCells.resize(dm.neighborRanks.size());
        for (size_t i = 0; i < dm.neighborRanks.size(); i++) {
            int rk = dm.neighborRanks[i];
            dm.recvCells[i] = recvMap[rk];
            dm.sendCells[i] = sendMap[rk];
        }
    }
};

} // namespace cfd
