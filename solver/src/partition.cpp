#include "partition.h"
#include <metis.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <iostream>
#include <cstring>

namespace cfd2d {

// Build cell adjacency graph (cell-cell via shared faces)
static void buildCellGraph(const GlobalMesh& gm,
                           std::vector<idx_t>& xadj,
                           std::vector<idx_t>& adjncy) {
  int n = gm.numCells; // real cells only
  std::vector<std::vector<int>> adj(n);
  for (auto& f : gm.faces) {
    if (f.l >= 0 && f.r >= 0 && f.l < n && f.r < n) {
      adj[f.l].push_back(f.r);
      adj[f.r].push_back(f.l);
    }
  }
  xadj.resize(n + 1);
  xadj[0] = 0;
  for (int i = 0; i < n; ++i) {
    // dedupe
    std::sort(adj[i].begin(), adj[i].end());
    adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
    xadj[i + 1] = xadj[i] + (idx_t)adj[i].size();
  }
  int total = xadj[n];
  adjncy.resize(total);
  int pos = 0;
  for (int i = 0; i < n; ++i) {
    for (int j : adj[i]) adjncy[pos++] = j;
  }
}

bool partitionMesh(const GlobalMesh& gm, int rank, int nranks,
                  LocalMesh& lm, std::string& err) {
  if (getenv("CFD2D_DEBUG")) {
    std::cerr << "Rank " << rank << ": partitionMesh start, numCells=" << gm.numCells
              << " numBcGhost=" << gm.numBcGhostCells << " nranks=" << nranks << "\n";
  }
  std::vector<idx_t> part;
  idx_t edgeCutVal = 0;

  if (nranks == 1) {
    // Single rank: all cells owned by rank 0, no METIS needed
    part.resize(gm.numCells, 0);
    edgeCutVal = 0;
  } else if (rank == 0) {
    if (getenv("CFD2D_DEBUG")) std::cerr << "Rank 0: building cell graph...\n";
    std::vector<idx_t> xadj, adjncy;
    buildCellGraph(gm, xadj, adjncy);
    if (getenv("CFD2D_DEBUG")) std::cerr << "Rank 0: graph built, xadj size=" << xadj.size()
              << " adjncy size=" << adjncy.size() << "\n";
    idx_t nvtxs = gm.numCells;
    idx_t ncon = 1;
    idx_t nparts = nranks;
    idx_t objval = 0;
    part.resize(nvtxs);

    // METIS options
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);
    options[METIS_OPTION_NUMBERING] = 0; // 0-based

    int ret = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                   nullptr, nullptr, nullptr, &nparts,
                                   nullptr, nullptr, options, &objval, part.data());
    if (ret != METIS_OK) {
      err = "METIS_PartGraphKway failed";
      int fail = 1;
      MPI_Bcast(&fail, 1, MPI_INT, 0, MPI_COMM_WORLD);
      return false;
    }
    edgeCutVal = objval;

    int fail = 0;
    MPI_Bcast(&fail, 1, MPI_INT, 0, MPI_COMM_WORLD);
  } else {
    int fail;
    MPI_Bcast(&fail, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (fail) { err = "partitioning failed on rank 0"; return false; }
  }

  // Broadcast partition vector and edge cut
  int nvtxs = gm.numCells;
  if (rank != 0) part.resize(nvtxs);
  MPI_Bcast(part.data(), nvtxs, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&edgeCutVal, 1, MPI_INT, 0, MPI_COMM_WORLD);
  lm.edgeCut = edgeCutVal;

  // Now each rank builds its local mesh.
  // 1. Collect owned cells (part[c] == rank)
  // 2. Find ghost cells: neighbors of owned cells owned by other ranks
  // 3. Build local face list

  // Owned cells (real cells only, not BC ghost cells)
  std::vector<int> ownedCells;
  int nreal = gm.numCells; // real cells count (BC ghosts are after)
  for (int c = 0; c < nreal; ++c) {
    if (part[c] == rank) ownedCells.push_back(c);
  }

  // Ghost cells: for each face with l or r owned, the other cell is a ghost
  // This includes both inter-partition ghosts and BC ghost cells.
  // BC ghost cells (id >= nreal) are assigned to the rank owning the adjacent real cell.
  std::set<int> ghostSet;
  std::map<int, int> ghostRankMap; // ghost global cell -> owning rank
  for (auto& f : gm.faces) {
    int l = f.l, r = f.r;
    if (l >= 0 && r >= 0) {
      if (part[l] == rank && part[r] != rank) {
        ghostSet.insert(r);
        // BC ghost cells belong to this rank (they're local)
        ghostRankMap[r] = (r >= nreal) ? rank : part[r];
      } else if (part[r] == rank && part[l] != rank) {
        ghostSet.insert(l);
        ghostRankMap[l] = (l >= nreal) ? rank : part[l];
      }
    }
  }

  std::vector<int> ghostCells(ghostSet.begin(), ghostSet.end());

  // Assign local indices: owned first, then ghost
  lm.numOwned = (int)ownedCells.size();
  lm.numGhost = (int)ghostCells.size();
  lm.numLocal = lm.numOwned + lm.numGhost;

  lm.cx.resize(lm.numLocal);
  lm.cy.resize(lm.numLocal);
  lm.area.resize(lm.numLocal);
  lm.cellNodes.resize(lm.numLocal);
  lm.ghostRank.resize(lm.numLocal, -1); // -1 for owned

  // Copy vertex coordinates for output
  lm.vx = gm.x;
  lm.vy = gm.y;

  lm.localToGlobal.resize(lm.numLocal);
  for (int i = 0; i < lm.numOwned; ++i) {
    int g = ownedCells[i];
    lm.globalToLocal[g] = i;
    lm.localToGlobal[i] = g;
    lm.cx[i] = gm.cellGeo[g].cx;
    lm.cy[i] = gm.cellGeo[g].cy;
    lm.area[i] = gm.cellGeo[g].area;
    lm.cellNodes[i] = gm.cellGeo[g].nodeIds;
  }
  for (int i = 0; i < lm.numGhost; ++i) {
    int g = ghostCells[i];
    int li = lm.numOwned + i;
    lm.globalToLocal[g] = li;
    lm.localToGlobal[li] = g;
    lm.cx[li] = gm.cellGeo[g].cx;
    lm.cy[li] = gm.cellGeo[g].cy;
    lm.area[li] = gm.cellGeo[g].area;
    lm.cellNodes[li] = gm.cellGeo[g].nodeIds;
    lm.ghostRank[li] = ghostRankMap[g];
  }

  // BC ghost cells (owning rank == this rank) should not be exchanged.
  // They are already in the ghost list but ghostRank == rank, so they
  // won't appear in recvFromNeighbor (which only includes ghosts from other ranks).

  // Build local faces: faces where at least one cell is owned
  // Map global cell ids to local
  for (auto& f : gm.faces) {
    int l = f.l, r = f.r;
    bool lOwned = (l >= 0 && part[l] == rank);
    bool rOwned = (r >= 0 && part[r] == rank);
    if (!lOwned && !rOwned) continue;

    Face lf = f;
    if (l >= 0) {
      auto it = lm.globalToLocal.find(l);
      if (it != lm.globalToLocal.end()) lf.l = it->second;
      else { err = "cell l not found in local map"; continue; }
    } else {
      lf.l = -1;
    }
    if (r >= 0) {
      auto it = lm.globalToLocal.find(r);
      if (it != lm.globalToLocal.end()) lf.r = it->second;
      else { err = "cell r not found in local map"; continue; }
    } else {
      lf.r = -1;
    }
    lm.faces.push_back(lf);
  }

  // Count boundary faces
  lm.numBoundaryFaces = 0;
  for (auto& f : lm.faces) {
    if (f.bcType != (int)BCType::Interior) lm.numBoundaryFaces++;
  }

  // Build communication maps
  // recvFromNeighbor: ghost cells we need from each neighbor
  // sendToNeighbor: owned cells that are ghosts on other ranks
  std::set<int> neighborSet;
  for (int i = 0; i < lm.numGhost; ++i) {
    int li = lm.numOwned + i;
    int rk = lm.ghostRank[li];
    neighborSet.insert(rk);
    lm.recvFromNeighbor[rk].push_back(li);
  }

  // For sendToNeighbor, we need to know which of our owned cells are ghosts elsewhere.
  // We do an all-to-all exchange of ghost requests.
  // Each rank knows which ghosts it needs and from whom.
  // We send our "recv requests" to the owning rank, which becomes their "send list".

  // Gather all recv requests: for each neighbor rank, send list of global cell ids we need
  // First, figure out how many requests each rank will receive
  int nreq = (int)neighborSet.size();
  std::vector<int> neighborList(neighborSet.begin(), neighborSet.end());

  // Exchange counts
  std::vector<int> recvCounts(nranks, 0);
  std::vector<int> sendCounts(nranks, 0);
  for (int rk : neighborList) {
    sendCounts[rk] = (int)lm.recvFromNeighbor[rk].size();
  }
  MPI_Alltoall(sendCounts.data(), 1, MPI_INT, recvCounts.data(), 1, MPI_INT, MPI_COMM_WORLD);

  if (getenv("CFD2D_DEBUG")) {
    std::cerr << "Rank " << rank << ": nreq=" << nreq << " neighbors=";
    for (int n : neighborList) std::cerr << n << " ";
    std::cerr << "sendCounts=";
    for (int s : sendCounts) std::cerr << s << " ";
    std::cerr << "recvCounts=";
    for (int s : recvCounts) std::cerr << s << " ";
    std::cerr << "\n";
  }

  // Exchange the actual global cell ids
  // Send: the global cell ids we need from each neighbor
  // Recv: the global cell ids other ranks need from us (these are our owned cells to send)
  std::vector<int> sendDispls(nranks + 1, 0), recvDispls(nranks + 1, 0);
  for (int i = 0; i < nranks; ++i) {
    sendDispls[i + 1] = sendDispls[i] + sendCounts[i];
    recvDispls[i + 1] = recvDispls[i] + recvCounts[i];
  }
  int totalSend = sendDispls[nranks];
  int totalRecv = recvDispls[nranks];
  std::vector<int> sendBuf(totalSend), recvBuf(totalRecv);

  // Fill send buffer with global cell ids we need, organized by destination rank
  for (int rk = 0; rk < nranks; ++rk) {
    int pos = sendDispls[rk];
    if (lm.recvFromNeighbor.count(rk)) {
      for (int li : lm.recvFromNeighbor[rk]) {
        sendBuf[pos++] = lm.localToGlobal[li];
      }
    }
  }

  MPI_Alltoallv(sendBuf.data(), sendCounts.data(), sendDispls.data(), MPI_INT,
                recvBuf.data(), recvCounts.data(), recvDispls.data(), MPI_INT,
                MPI_COMM_WORLD);

  // recvBuf contains global cell ids that other ranks need from us
  // These are our owned cells -> map to local indices for sending
  for (int rk = 0; rk < nranks; ++rk) {
    if (recvCounts[rk] == 0) continue;
    neighborSet.insert(rk);
    for (int i = recvDispls[rk]; i < recvDispls[rk + 1]; ++i) {
      int gid = recvBuf[i];
      auto it = lm.globalToLocal.find(gid);
      if (it != lm.globalToLocal.end()) {
        lm.sendToNeighbor[rk].push_back(it->second);
      }
    }
  }

  lm.neighborRanks.assign(neighborSet.begin(), neighborSet.end());
  std::sort(lm.neighborRanks.begin(), lm.neighborRanks.end());
  lm.numNeighborRanks = (int)lm.neighborRanks.size();

  return true;
}

} // namespace cfd2d
