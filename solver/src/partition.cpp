// partition.cpp — METIS partitioning and rank-local mesh construction
#include "partition.hpp"
#include <metis.h>
#include <mpi.h>
#include <iostream>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace cfd2d {

void partitionMesh(const Mesh& mesh, int mpiRank, int mpiSize,
                   std::vector<int>& partition, int& edgeCut) {
    // Build cell adjacency graph (CSR format)
    idx_t nvtxs = mesh.ncell;
    std::vector<idx_t> xadj(nvtxs + 1, 0);
    std::vector<idx_t> adjncy;

    // Count neighbors via faces
    std::vector<std::vector<int>> adjList(nvtxs);
    for (const auto& f : mesh.faces) {
        if (f.rc >= 0) {
            adjList[f.lc].push_back(f.rc);
            adjList[f.rc].push_back(f.lc);
        }
    }
    for (int i = 0; i < nvtxs; i++) {
        xadj[i + 1] = xadj[i] + adjList[i].size();
        for (int j : adjList[i])
            adjncy.push_back(j);
    }

    partition.resize(nvtxs);
    idx_t ncon = 1;
    idx_t nparts = mpiSize;
    idx_t objval = 0;
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_CONTIG] = 1; // try contiguous partitions
    options[METIS_OPTION_SEED] = 42;

    int ret;
    if (mpiSize == 1) {
        std::fill(partition.begin(), partition.end(), 0);
        edgeCut = 0;
    } else {
        ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                  nullptr, nullptr, nullptr, &nparts,
                                  nullptr, nullptr, options, &objval,
                                  partition.data());
        if (ret != METIS_OK) {
            if (mpiRank == 0)
                std::cerr << "METIS partitioning failed (ret=" << ret << "), using naive partition\n";
            // Fallback: contiguous partition
            for (int i = 0; i < nvtxs; i++)
                partition[i] = i * mpiSize / nvtxs;
            edgeCut = 0;
        } else {
            edgeCut = objval;
        }
    }

    // Broadcast partition from rank 0 (METIS was called by all ranks with same seed)
    // Actually METIS is deterministic with same seed, so all ranks get same result.
    // But to be safe, broadcast from rank 0.
    MPI_Bcast(partition.data(), nvtxs, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&edgeCut, 1, MPI_INT, 0, MPI_COMM_WORLD);
}

void buildLocalMesh(LocalMesh& lm, const Mesh& mesh, const std::vector<int>& partition,
                    int mpiRank, int mpiSize, int edgeCut) {
    lm.globalMesh = &mesh;
    lm.mpiRank = mpiRank;
    lm.mpiSize = mpiSize;
    lm.edgeCut = edgeCut;
    lm.globalToLocal.assign(mesh.ncell, -1);

    // 1. Identify owned cells
    std::vector<int> ownedCells;
    for (int i = 0; i < mesh.ncell; i++) {
        if (partition[i] == mpiRank) {
            int localIdx = lm.localToGlobal.size();
            lm.globalToLocal[i] = localIdx;
            lm.localToGlobal.push_back(i);
            ownedCells.push_back(i);
        }
    }
    lm.nOwned = ownedCells.size();

    // 2. Identify ghost cells (neighbors of owned cells on other ranks)
    std::map<int, int> ghostLocalMap; // global cell -> local ghost index
    for (int ci : ownedCells) {
        // Find faces of this cell
        // We need to iterate faces to find neighbors
        // (Build a cell->face lookup)
    }

    // Build cell->faces adjacency for quick lookup
    std::vector<std::vector<int>> cellFaces(mesh.ncell);
    for (int fi = 0; fi < mesh.nface; fi++) {
        const Face& f = mesh.faces[fi];
        cellFaces[f.lc].push_back(fi);
        if (f.rc >= 0) cellFaces[f.rc].push_back(fi);
    }

    // Find ghost cells
    for (int ci : ownedCells) {
        for (int fi : cellFaces[ci]) {
            const Face& f = mesh.faces[fi];
            int neighbor = (f.lc == ci) ? f.rc : f.lc;
            if (neighbor >= 0 && partition[neighbor] != mpiRank) {
                if (ghostLocalMap.find(neighbor) == ghostLocalMap.end()) {
                    int localIdx = lm.localToGlobal.size();
                    ghostLocalMap[neighbor] = localIdx;
                    lm.globalToLocal[neighbor] = localIdx;
                    lm.localToGlobal.push_back(neighbor);
                    lm.ghostGlobalId.push_back(neighbor);
                    lm.ghostOwnerRank.push_back(partition[neighbor]);
                }
            }
        }
    }
    lm.nGhost = ghostLocalMap.size();
    lm.nLocalCells = lm.nOwned + lm.nGhost;

    // 3. Build local faces (all faces of owned cells)
    std::set<int> processedFaces;
    for (int ci : ownedCells) {
        for (int fi : cellFaces[ci]) {
            if (processedFaces.count(fi)) continue;
            processedFaces.insert(fi);

            const Face& f = mesh.faces[fi];
            LocalMesh::LocalFace lf;
            lf.nx = f.nx; lf.ny = f.ny;
            lf.area = f.area; lf.cx = f.cx; lf.cy = f.cy;
            lf.gn0 = f.n0; lf.gn1 = f.n1;
            lf.bcTag = f.bcTag;

            // Ensure lc is the owned cell
            if (f.lc == ci) {
                lf.lc = lm.globalToLocal[f.lc];
                lf.rc = (f.rc >= 0) ? lm.globalToLocal[f.rc] : -1;
            } else {
                // f.rc == ci, so we need to flip the normal
                lf.lc = lm.globalToLocal[f.rc];
                lf.rc = lm.globalToLocal[f.lc];
                lf.nx = -f.nx; lf.ny = -f.ny;
                std::swap(lf.gn0, lf.gn1);
            }

            lm.faces.push_back(lf);
        }
    }

    // Count face types
    for (const auto& f : lm.faces) {
        if (f.bcTag >= 0)
            lm.nBoundaryFace++;
        else if (f.rc >= lm.nOwned) // ghost cell
            lm.nPartitionFace++;
        else
            lm.nInteriorFace++;
    }

    // 4. Copy cell geometry
    lm.cellCx.resize(lm.nLocalCells);
    lm.cellCy.resize(lm.nLocalCells);
    lm.cellVol.resize(lm.nLocalCells);
    for (int i = 0; i < lm.nLocalCells; i++) {
        int gi = lm.localToGlobal[i];
        lm.cellCx[i] = mesh.cellCx[gi];
        lm.cellCy[i] = mesh.cellCy[gi];
        lm.cellVol[i] = mesh.cellVol[gi];
    }

    // 5. Build communication pattern
    std::map<int, std::vector<int>> sendToRank;  // rank -> list of local owned cell indices
    std::map<int, std::vector<int>> recvFromRank; // rank -> list of local ghost cell indices
    for (int g = 0; g < lm.nGhost; g++) {
        int ownerRank = lm.ghostOwnerRank[g];
        int localIdx = lm.nOwned + g;
        recvFromRank[ownerRank].push_back(localIdx);
    }
    // To find what to send, we need to know which of our owned cells are ghosts on other ranks
    // Each rank exchanges its ghost request lists, then the sender knows what to send
    // Simple approach: all-to-all communication of ghost global IDs
    // Collect global IDs of ghosts we need from each neighbor
    std::map<int, std::vector<int>> recvGlobalIds; // rank -> global cell IDs we need
    for (int g = 0; g < lm.nGhost; g++) {
        int ownerRank = lm.ghostOwnerRank[g];
        recvGlobalIds[ownerRank].push_back(lm.ghostGlobalId[g]);
    }

    // Exchange with all ranks using non-blocking sends to avoid deadlock
    std::vector<MPI_Request> sendReqs;
    std::vector<std::vector<int>> sendBuffers(mpiSize);
    
    // Post all sends first (non-blocking)
    for (int r = 0; r < mpiSize; r++) {
        if (r == mpiRank) continue;
        int count = recvGlobalIds[r].size();
        sendBuffers[r] = recvGlobalIds[r];  // copy
        MPI_Request req;
        MPI_Isend(&count, 1, MPI_INT, r, 0, MPI_COMM_WORLD, &req);
        sendReqs.push_back(req);
        if (count > 0) {
            MPI_Request req2;
            MPI_Isend(sendBuffers[r].data(), count, MPI_INT, r, 1, MPI_COMM_WORLD, &req2);
            sendReqs.push_back(req2);
        }
    }
    
    // Receive from all ranks
    for (int r = 0; r < mpiSize; r++) {
        if (r == mpiRank) continue;
        int count;
        MPI_Recv(&count, 1, MPI_INT, r, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (count > 0) {
            std::vector<int> gids(count);
            MPI_Recv(gids.data(), count, MPI_INT, r, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (int gid : gids) {
                sendToRank[r].push_back(lm.globalToLocal[gid]);
            }
        }
    }
    
    // Wait for all sends to complete
    MPI_Waitall(sendReqs.size(), sendReqs.data(), MPI_STATUSES_IGNORE);

    // Build neighbor list
    for (int r = 0; r < mpiSize; r++) {
        if (r == mpiRank) continue;
        if (!sendToRank[r].empty() || !recvFromRank[r].empty()) {
            LocalMesh::NeighborComm nc;
            nc.rank = r;
            nc.sendCells = sendToRank[r];
            nc.recvCells = recvFromRank[r];
            lm.neighbors.push_back(nc);
            lm.neighborRanks.push_back(r);
        }
    }

    if (mpiRank == 0) {
        // Print global partition stats
        std::vector<int> counts(mpiSize, 0);
        for (int i = 0; i < mesh.ncell; i++) counts[partition[i]]++;
        double mean = (double)mesh.ncell / mpiSize;
        double maxDev = 0;
        for (int r = 0; r < mpiSize; r++)
            maxDev = std::max(maxDev, std::abs(counts[r] - mean) / mean);
        std::cerr << "Partition: edgeCut=" << edgeCut << " loadImbalance=" << maxDev << "\n";
    }
}

} // namespace cfd2d
