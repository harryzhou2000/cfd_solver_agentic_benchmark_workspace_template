// METIS partitioning and rank-local mesh extraction.
#include "partition.hpp"

#include <metis.h>
#include <mpi.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace cfd {

int countEdgeCut(const Mesh& global, const std::vector<int>& part) {
  int cut = 0;
  for (int f = 0; f < global.nface; ++f) {
    if (global.faceR[f] < 0) continue;
    int a = global.faceL[f], b = global.faceR[f];
    if (part[a] != part[b]) ++cut;
  }
  return cut;
}

// Build the cell adjacency CSR graph (cells sharing a face are neighbors).
static void buildCellGraph(const Mesh& m, std::vector<idx_t>& xadj,
                           std::vector<idx_t>& adjncy) {
  xadj.assign(m.ncell + 1, 0);
  std::vector<int> deg(m.ncell, 0);
  for (int f = 0; f < m.nface; ++f) {
    if (m.faceR[f] < 0) continue;
    deg[m.faceL[f]]++;
    deg[m.faceR[f]]++;
  }
  for (int c = 0; c < m.ncell; ++c) xadj[c + 1] = xadj[c] + deg[c];
  std::vector<int> cursor(m.ncell, 0);
  adjncy.assign(xadj[m.ncell], 0);
  for (int f = 0; f < m.nface; ++f) {
    if (m.faceR[f] < 0) continue;
    int a = m.faceL[f], b = m.faceR[f];
    adjncy[xadj[a] + cursor[a]++] = b;
    adjncy[xadj[b] + cursor[b]++] = a;
  }
}

LocalMesh partitionMesh(const Mesh& global, const CaseConfig& cfg, int nranks,
                        int rank, std::vector<int>& partGlobal) {
  LocalMesh lm;
  lm.rank = rank;
  lm.nranks = nranks;
  lm.num_cells_global = global.ncell;
  lm.num_faces_global = global.nface;

  partGlobal.assign(global.ncell, 0);
  if (nranks == 1) {
    // Trivial partition: everything on rank 0 (still reported as METIS for the
    // production path; a single partition has zero edge cut).
    if (rank == 0) lm.partition_edge_cut = 0;
  } else {
    std::vector<idx_t> xadj, adjncy;
    if (rank == 0) buildCellGraph(global, xadj, adjncy);
    // Broadcast graph sizes and data from rank 0 so all ranks agree.
    idx_t nvtxs = global.ncell;
    idx_t nxadj = (rank == 0) ? (idx_t)xadj.size() : 0;
    idx_t nadjncy = (rank == 0) ? (idx_t)adjncy.size() : 0;
    MPI_Bcast(&nxadj, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&nadjncy, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank != 0) { xadj.assign(nxadj, 0); adjncy.assign(nadjncy, 0); }
    MPI_Bcast(xadj.data(), nxadj, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(adjncy.data(), nadjncy, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<idx_t> part((size_t)nvtxs, 0);
    if (rank == 0) {
      idx_t ncon = 1;
      idx_t nparts = nranks;
      idx_t objval = 0;
      idx_t options[METIS_NOPTIONS];
      METIS_SetDefaultOptions(options);
      options[METIS_OPTION_CONTIG] = 1;       // contiguous partitions
      options[METIS_OPTION_SEED] = 42;
      int r = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                  nullptr, nullptr, nullptr, &nparts, nullptr,
                                  nullptr, options, &objval, part.data());
      if (r != METIS_OK)
        throw std::runtime_error("METIS_PartGraphKway failed");
      lm.partition_edge_cut = (int)objval;
    }
    MPI_Bcast(part.data(), (int)nvtxs, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&lm.partition_edge_cut, 1, MPI_INT, 0, MPI_COMM_WORLD);
    for (int c = 0; c < global.ncell; ++c) partGlobal[c] = (int)part[c];
  }

  // ---- Extract owned cells and collect ghost cells. ----
  std::vector<int>& g2l = lm.globalToLocal;
  g2l.assign(global.ncell, -1);
  std::vector<int> ownedList;
  ownedList.reserve(global.ncell / nranks + 16);
  for (int c = 0; c < global.ncell; ++c)
    if (partGlobal[c] == rank) { g2l[c] = (int)ownedList.size(); ownedList.push_back(c); }
  lm.nOwned = (int)ownedList.size();

  // Collect ghosts: any neighbor cell (across a face) of an owned cell that is
  // not owned by this rank.  Walk one layer (face neighbors); reconstruction
  // uses only face-neighbor ghosts.
  std::vector<int> ghostList;
  for (int oc : ownedList) {
    int fo = global.cellFaceOffset[oc], fe = global.cellFaceOffset[oc + 1];
    for (int k = fo; k < fe; ++k) {
      int f = global.cellFaces[k];
      int nb = (global.faceL[f] == oc) ? global.faceR[f] : global.faceL[f];
      if (nb < 0) continue;             // boundary face
      if (partGlobal[nb] == rank) continue;
      if (g2l[nb] == -1) {
        g2l[nb] = lm.nOwned + (int)ghostList.size();
        ghostList.push_back(nb);
      }
    }
  }
  lm.nGhost = (int)ghostList.size();

  // Build globalCell and cellOwner.
  lm.globalCell.resize(lm.nLocal());
  lm.cellOwner.resize(lm.nLocal());
  for (int i = 0; i < lm.nOwned; ++i) {
    lm.globalCell[i] = ownedList[i];
    lm.cellOwner[i] = rank;
  }
  for (int i = 0; i < lm.nGhost; ++i) {
    int g = ghostList[i];
    lm.globalCell[lm.nOwned + i] = g;
    lm.cellOwner[lm.nOwned + i] = partGlobal[g];
  }

  // Cell geometry + vertices.
  lm.cellCx.resize(lm.nLocal());
  lm.cellCy.resize(lm.nLocal());
  lm.cellVol.resize(lm.nLocal());
  lm.cellNv.resize(lm.nLocal());
  lm.cellOffset.resize(lm.nLocal() + 1);
  lm.cellOffset[0] = 0;
  for (int i = 0; i < lm.nLocal(); ++i) {
    int g = lm.globalCell[i];
    lm.cellCx[i] = global.cellCx[g];
    lm.cellCy[i] = global.cellCy[g];
    lm.cellVol[i] = global.cellVol[g];
    lm.cellNv[i] = global.cellNv[g];
    lm.cellOffset[i + 1] = lm.cellOffset[i] + global.cellNv[g];
  }
  lm.cellVerts.resize(lm.cellOffset[lm.nLocal()]);
  for (int i = 0; i < lm.nLocal(); ++i) {
    int g = lm.globalCell[i];
    int off = global.cellOffset[g];
    for (int v = 0; v < global.cellNv[g]; ++v)
      lm.cellVerts[lm.cellOffset[i] + v] = global.cellVerts[off + v];
  }

  // ---- Build local face list from the global face list. ----
  // Include a global face if either side is owned by this rank.
  for (int f = 0; f < global.nface; ++f) {
    int a = global.faceL[f], b = global.faceR[f];
    bool ownA = (partGlobal[a] == rank);
    bool ownB = (b >= 0 && partGlobal[b] == rank);
    if (!ownA && !ownB) continue;
    int localL, localR;
    double nx = global.faceNx[f], ny = global.faceNy[f];
    if (ownA) {
      localL = g2l[a];
      localR = (b >= 0) ? g2l[b] : -1;
      // normal already outward of global L (== our L)
    } else {
      // only ownB: we are the R side; flip to make localL = our cell
      localL = g2l[b];
      localR = g2l[a];
      nx = -nx; ny = -ny;
    }
    lm.faceL.push_back(localL);
    lm.faceR.push_back(localR);
    lm.faceV0.push_back(global.faceV0[f]);
    lm.faceV1.push_back(global.faceV1[f]);
    lm.faceNx.push_back(nx);
    lm.faceNy.push_back(ny);
    lm.faceLen.push_back(global.faceLen[f]);
    lm.faceCx.push_back(global.faceCx[f]);
    lm.faceCy.push_back(global.faceCy[f]);
    lm.faceBC.push_back(global.faceBC[f]);
  }
  lm.nFace = (int)lm.faceL.size();
  // ---- Per-owned-cell face adjacency (count both L and owned-R sides). ----
  lm.cellFaceOffset.assign(lm.nOwned + 1, 0);
  std::vector<int> deg(lm.nOwned, 0);
  for (int f = 0; f < lm.nFace; ++f) {
    deg[lm.faceL[f]]++;
    if (lm.faceR[f] >= 0 && lm.faceR[f] < lm.nOwned) deg[lm.faceR[f]]++;
  }
  for (int c = 0; c < lm.nOwned; ++c) lm.cellFaceOffset[c + 1] = lm.cellFaceOffset[c] + deg[c];
  lm.cellFaces.assign(lm.cellFaceOffset[lm.nOwned], -1);
  {
    std::vector<int> cursor(lm.nOwned, 0);
    for (int f = 0; f < lm.nFace; ++f) {
      int lc = lm.faceL[f];
      lm.cellFaces[lm.cellFaceOffset[lc] + cursor[lc]++] = f;
      int rc = lm.faceR[f];
      if (rc >= 0 && rc < lm.nOwned)
        lm.cellFaces[lm.cellFaceOffset[rc] + cursor[rc]++] = f;
    }
  }

  // ---- Neighbor communication maps. ----
  // Group ghost local indices by owner rank, sorted by global cell id for
  // deterministic send/recv ordering.
  std::map<int, std::vector<int>> byOwner;  // owner rank -> local ghost indices
  for (int i = 0; i < lm.nGhost; ++i) byOwner[lm.cellOwner[lm.nOwned + i]].push_back(lm.nOwned + i);
  for (auto& kv : byOwner) {
    std::sort(kv.second.begin(), kv.second.end(),
              [&](int a, int b) { return lm.globalCell[a] < lm.globalCell[b]; });
  }
  // First send each neighbor the list of global cell ids we need; receive the
  // lists our neighbors need from us.  Use non-blocking sends to avoid
  // rendezvous deadlock when two ranks exchange simultaneously.
  std::vector<int> neighborOrder;
  for (auto& kv : byOwner) neighborOrder.push_back(kv.first);
  std::vector<std::vector<int>> needLists(neighborOrder.size());
  std::vector<int> needSizes(neighborOrder.size(), 0);
  std::vector<MPI_Request> sendReqs(neighborOrder.size() * 2);
  int sreq = 0;
  for (size_t i = 0; i < neighborOrder.size(); ++i) {
    int nbr = neighborOrder[i];
    auto& ghosts = byOwner[nbr];
    std::vector<int> need;
    need.reserve(ghosts.size());
    for (int li : ghosts) need.push_back(lm.globalCell[li]);
    needLists[i] = need;
    needSizes[i] = (int)need.size();
    MPI_Isend(&needSizes[i], 1, MPI_INT, nbr, 100, MPI_COMM_WORLD, &sendReqs[sreq++]);
    if (needSizes[i] > 0)
      MPI_Isend(need.data(), needSizes[i], MPI_INT, nbr, 101, MPI_COMM_WORLD, &sendReqs[sreq++]);
    else
      sendReqs[sreq++] = MPI_REQUEST_NULL;
  }
  // Receive neighbor need-lists.
  std::vector<std::vector<int>> nbrNeeds(neighborOrder.size());
  for (size_t i = 0; i < neighborOrder.size(); ++i) {
    int nbr = neighborOrder[i];
    int m;
    MPI_Recv(&m, 1, MPI_INT, nbr, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::vector<int> nbrneed(m);
    if (m > 0) MPI_Recv(nbrneed.data(), m, MPI_INT, nbr, 101, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    std::sort(nbrneed.begin(), nbrneed.end());
    nbrNeeds[i] = std::move(nbrneed);
  }
  MPI_Waitall((int)sendReqs.size(), sendReqs.data(), MPI_STATUSES_IGNORE);
  // Build send/recv maps.  The ghost receive order is the sorted-global-id
  // order of needLists[i]; the owned send order on the neighbor is the same
  // sorted global ids, so both ranks pack/unpack in matching sequence.
  for (size_t i = 0; i < neighborOrder.size(); ++i) {
    int nbr = neighborOrder[i];
    std::vector<int> sendLocal;
    sendLocal.reserve(nbrNeeds[i].size());
    for (int g : nbrNeeds[i]) sendLocal.push_back(g2l[g]);
    lm.neighborRanks.push_back(nbr);
    lm.recvGhosts.push_back(byOwner[nbr]);
    lm.sendCells.push_back(sendLocal);
  }
  return lm;
}

}  // namespace cfd
