// partition.cpp - METIS graph partitioning and rank-local mesh construction.
//
// The partitioning is done in serial preprocessing:
//   1. Build the cell adjacency graph (cell -> cell neighbors via faces).
//   2. Call METIS_PartGraphKway to assign each cell to a partition.
//   3. Each rank builds its local mesh: owned cells + ghost cells from
//      neighboring partitions.
//
// During solver iterations, only rank-local owned + ghost cells are stored.
// Halo exchange uses neighbor-scoped MPI_Isend/Irecv.
#include "cfd2d.hpp"
#include <metis.h>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <iostream>
#include <stdexcept>

// METIS uses 32-bit indices in this build (IDXTYPEWIDTH=32).
// We alias the METIS idx_t to avoid name collision with cfd2d::idx_t (int64_t).
using MetisIdx = ::idx_t;

namespace cfd2d {

void partitionMesh(GlobalMesh& mesh, int nProcs) {
    int n = mesh.cells.size();
    if (nProcs <= 1) {
        for (auto& c : mesh.cells) c.partition = 0;  // all cells on rank 0
        return;
    }

    // Build CSR adjacency: xadj, adjncy
    // Each cell's neighbors (via faces, excluding boundary faces with cellR=-1)
    std::vector<MetisIdx> xadj(n+1, 0);
    std::vector<MetisIdx> adjncy;

    for (int c = 0; c < n; c++) {
        std::set<idx_t> nbrs;
        for (idx_t f : mesh.cells[c].faces) {
            const Face& face = mesh.faces[f];
            idx_t nb = (face.cells[0] == c) ? face.cells[1] : face.cells[0];
            if (nb >= 0) nbrs.insert(nb);
        }
        xadj[c+1] = xadj[c] + nbrs.size();
        for (idx_t nb : nbrs) adjncy.push_back((MetisIdx)nb);
    }

    // METIS options
    MetisIdx options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_CONTIG] = 0;    // allow non-contiguous (needed for multi-zone meshes)
    options[METIS_OPTION_SEED] = 42;

    MetisIdx nvtxs = n;
    MetisIdx ncon = 1;
    MetisIdx nparts = nProcs;
    MetisIdx objval = 0;
    std::vector<MetisIdx> part(n);

    int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                  nullptr, nullptr, nullptr, &nparts,
                                  nullptr, nullptr, options, &objval, part.data());

    if (ret != METIS_OK) {
        throw std::runtime_error("METIS partitioning failed with code " + std::to_string(ret));
    }

    for (int c = 0; c < n; c++) {
        mesh.cells[c].partition = part[c];
    }

    // Compute edge cut for reporting
    mesh.numFacesGlobal = mesh.faces.size();
    // We'll report edge cut via partition_diagnostics
}

LocalMesh buildLocalMesh(const GlobalMesh& global, int rank, int nProcs) {
    LocalMesh local;
    local.rank = rank;
    local.nProcs = nProcs;
    local.numCellsGlobal = global.numCellsGlobal;

    int n = global.cells.size();

    // Determine which cells this rank owns
    std::vector<int> cellPartition(n);
    for (int c = 0; c < n; c++) cellPartition[c] = global.cells[c].partition;

    std::unordered_set<idx_t> ownedSet;
    for (int c = 0; c < n; c++) {
        if (cellPartition[c] == rank) ownedSet.insert(c);
    }

    // Find ghost cells: neighbors of owned cells that belong to other ranks
    // Also find which neighbor ranks we communicate with
    std::map<int, std::set<idx_t>> ghostByRank; // rank -> set of global cell IDs
    std::unordered_set<idx_t> ghostSet;

    for (idx_t c : ownedSet) {
        for (idx_t f : global.cells[c].faces) {
            const Face& face = global.faces[f];
            idx_t nb = (face.cells[0] == c) ? face.cells[1] : face.cells[0];
            if (nb >= 0 && cellPartition[nb] != rank) {
                ghostSet.insert(nb);
                ghostByRank[cellPartition[nb]].insert(nb);
            }
        }
    }

    // Also add ghosts needed for reconstruction (2nd ring neighbors)
    // For Barth-Jespersen limiter and least-squares gradient, we need the
    // face-neighbors. But for second-order reconstruction across partition
    // boundaries, we may need neighbors-of-neighbors. Let's add one more layer.
    std::unordered_set<idx_t> extraGhosts;
    for (idx_t g : ghostSet) {
        for (idx_t f : global.cells[g].faces) {
            const Face& face = global.faces[f];
            idx_t nb = (face.cells[0] == g) ? face.cells[1] : face.cells[0];
            if (nb >= 0 && cellPartition[nb] != rank && ownedSet.find(nb) == ownedSet.end() && ghostSet.find(nb) == ghostSet.end()) {
                extraGhosts.insert(nb);
                ghostByRank[cellPartition[nb]].insert(nb);
            }
        }
    }
    for (idx_t g : extraGhosts) ghostSet.insert(g);

    // Build local cell list: [owned..., ghost...]
    // Global -> local ID mapping
    std::unordered_map<idx_t, int> globalToLocal;
    local.cells.clear();
    local.nOwned = ownedSet.size();
    local.nGhost = ghostSet.size();

    local.cells.resize(local.nOwned + local.nGhost);

    int localId = 0;
    // Owned cells
    for (int c = 0; c < n; c++) {
        if (cellPartition[c] == rank) {
            local.cells[localId] = global.cells[c];
            local.cells[localId].isGhost = false;
            local.cells[localId].ghostOwnerRank = -1;
            local.cells[localId].globalId = c;
            globalToLocal[c] = localId;
            localId++;
        }
    }
    // Ghost cells
    for (idx_t g : ghostSet) {
        local.cells[localId] = global.cells[g];
        local.cells[localId].isGhost = true;
        local.cells[localId].ghostOwnerRank = cellPartition[g];
        local.cells[localId].globalId = g;
        local.cells[localId].ghostRemoteId = -1; // will be filled during comm setup
        globalToLocal[g] = localId;
        localId++;
    }

    // Build local faces
    local.faces.clear();
    local.boundaryFaceIds.clear();
    local.wallFaceIds.clear();

    // Build a set of local cell IDs for quick lookup
    // For each face in the global mesh, include it if at least one cell is local (owned or ghost)
    // But we only compute residual contributions for owned cells.
    // Faces between two owned cells, or between owned and ghost, or between owned and boundary
    // are needed.  Faces between two ghosts can be skipped.

    std::set<int> localFaceIds;
    for (int li = 0; li < (int)local.cells.size(); li++) {
        Cell& lc = local.cells[li];
        idx_t g = lc.globalId;
        for (idx_t f : global.cells[g].faces) {
            localFaceIds.insert(f);
        }
    }

    // Reindex faces to local numbering
    // We need to map global face IDs to local face IDs
    std::unordered_map<idx_t, int> globalFaceToLocal;
    int localFaceIdx = 0;
    for (idx_t gf : localFaceIds) {
        const Face& gf_ = global.faces[gf];
        Face lf;
        lf.nodes = gf_.nodes;
        lf.area = gf_.area;
        lf.fcx = gf_.fcx;
        lf.fcy = gf_.fcy;
        lf.nx = gf_.nx;
        lf.ny = gf_.ny;
        lf.isBoundary = gf_.isBoundary;
        lf.bcType = gf_.bcType;
        lf.bcFamily = gf_.bcFamily;
        lf.isInterface = false;
        lf.neighborRank = -1;

        // Map cells to local IDs
        idx_t c0 = gf_.cells[0];
        idx_t c1 = gf_.cells[1];
        int lc0 = (globalToLocal.find(c0) != globalToLocal.end()) ? globalToLocal[c0] : -1;
        int lc1 = (c1 >= 0 && globalToLocal.find(c1) != globalToLocal.end()) ? globalToLocal[c1] : -1;

        lf.cells = {lc0, lc1};

        // Determine if this is a partition interface face
        if (c1 >= 0) {
            int p0 = cellPartition[c0];
            int p1 = cellPartition[c1];
            if (p0 != p1) {
                lf.isInterface = true;
                if (p0 == rank) lf.neighborRank = p1;
                else lf.neighborRank = p0;
            }
        }

        globalFaceToLocal[gf] = localFaceIdx;
        local.faces.push_back(lf);
        localFaceIdx++;
    }

    // Update cell face lists with local face IDs
    for (int li = 0; li < (int)local.cells.size(); li++) {
        Cell& lc = local.cells[li];
        idx_t g = lc.globalId;
        std::vector<idx_t> oldFaces = lc.faces;
        lc.faces.clear();
        lc.neighbors.clear();
        for (idx_t gf : oldFaces) {
            int lf = globalFaceToLocal[gf];
            lc.faces.push_back(lf);
            // Determine neighbor local id
            Face& f = local.faces[lf];
            int nb = (f.cells[0] == li) ? f.cells[1] : f.cells[0];
            lc.neighbors.push_back(nb);
        }
    }

    // Collect boundary and wall faces
    for (int f = 0; f < (int)local.faces.size(); f++) {
        Face& face = local.faces[f];
        if (face.isBoundary) {
            local.boundaryFaceIds.push_back(f);
            if (face.bcType == (int)BCType::SlipWall || face.bcType == (int)BCType::NoSlipAdiabatic) {
                local.wallFaceIds.push_back(f);
            }
        }
    }

    // Build halo exchange maps
    local.neighborRanks.clear();
    for (auto& [nbr, cells] : ghostByRank) {
        local.neighborRanks.push_back(nbr);
    }
    std::sort(local.neighborRanks.begin(), local.neighborRanks.end());

    int numNbrs = local.neighborRanks.size();
    local.ghostRecvCells.resize(numNbrs);
    local.ghostRecvLocalIds.resize(numNbrs);
    local.ghostSendLocalIds.resize(numNbrs);

    // For each neighbor rank, list the ghost cells we need to receive (by global ID)
    // and the local IDs where they'll be stored.
    std::map<int, int> rankToIdx;
    for (int i = 0; i < numNbrs; i++) rankToIdx[local.neighborRanks[i]] = i;

    for (int li = local.nOwned; li < (int)local.cells.size(); li++) {
        Cell& gc = local.cells[li];
        int nbrRank = gc.ghostOwnerRank;
        int idx = rankToIdx[nbrRank];
        local.ghostRecvCells[idx].push_back(gc.globalId);
        local.ghostRecvLocalIds[idx].push_back(li);
    }

    // Build send lists: for each neighbor, which of our OWNED cells do they need?
    // We need to communicate this.  For now, we'll build the send lists during
    // the first halo exchange by having each rank tell its neighbors what it needs.
    // A simpler approach: each rank broadcasts its ghostRecvCells to all neighbors,
    // and each neighbor checks which of those global IDs it owns.
    // We'll implement this in the halo exchange function in the solver.

    // Compute local node coordinates (for output)
    std::set<idx_t> usedNodes;
    for (auto& c : local.cells) {
        for (idx_t n : c.nodes) usedNodes.insert(n);
    }
    std::map<idx_t, int> nodeMap;
    local.coordsX.clear();
    local.coordsY.clear();
    int nodeIdx = 0;
    for (idx_t n : usedNodes) {
        nodeMap[n] = nodeIdx;
        local.coordsX.push_back(global.coordsX[n]);
        local.coordsY.push_back(global.coordsY[n]);
        nodeIdx++;
    }
    local.nNodes = nodeIdx;
    // Update cell node IDs to local
    for (auto& c : local.cells) {
        for (auto& n : c.nodes) n = nodeMap[n];
    }
    // Update face node IDs to local
    for (auto& f : local.faces) {
        f.nodes[0] = nodeMap[f.nodes[0]];
        f.nodes[1] = nodeMap[f.nodes[1]];
    }

    // Compute edge cut (faces crossing partitions)
    local.edgeCut = 0;
    for (auto& f : local.faces) {
        if (f.isInterface) local.edgeCut++;
    }
    // Each interface face is counted twice (once per rank), so divide by 2
    // But for local reporting we keep the local count

    return local;
}

} // namespace cfd2d
