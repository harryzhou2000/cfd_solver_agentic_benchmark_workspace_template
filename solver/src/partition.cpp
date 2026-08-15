#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace fv {

namespace {

// Simple byte buffer for packing POD data (single-architecture cluster use).
struct Buffer {
  std::vector<char> data;
  template <typename T>
  void put(const T& v) {
    const char* p = reinterpret_cast<const char*>(&v);
    data.insert(data.end(), p, p + sizeof(T));
  }
  void putBytes(const void* p, size_t n) {
    const char* c = reinterpret_cast<const char*>(p);
    data.insert(data.end(), c, c + n);
  }
  template <typename T>
  void putVec(const std::vector<T>& v) {
    const size_t n = v.size();
    put(n);
    if (n) putBytes(v.data(), n * sizeof(T));
  }
  void putString(const std::string& s) { putVec(std::vector<char>(s.begin(), s.end())); }
};

struct Cursor {
  const char* p = nullptr;
  size_t left = 0;
  template <typename T>
  T get() {
    T v;
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    left -= sizeof(T);
    return v;
  }
  template <typename T>
  std::vector<T> getVec() {
    const size_t n = get<size_t>();
    std::vector<T> v(n);
    if (n) {
      std::memcpy(v.data(), p, n * sizeof(T));
      p += n * sizeof(T);
      left -= n * sizeof(T);
    }
    return v;
  }
  std::string getString() {
    const auto v = getVec<char>();
    return std::string(v.begin(), v.end());
  }
};

}  // namespace

LocalMesh partitionAndScatter(const GlobalMesh* g, MPI_Comm comm, long& edgeCut,
                              std::string& partitionerName) {
  int rank = 0, size = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &size);
  partitionerName = "metis_kway";
  edgeCut = 0;

  std::vector<int> bufSizes(size, 0);
  std::vector<char> myBuf;

  if (rank == 0) {
    if (!g) throw std::runtime_error("rank 0 requires global mesh for partitioning");
    const int nC = g->nCells;

    // ---- cell adjacency graph from interior faces ----
    std::vector<std::vector<int>> adj(nC);
    for (int f = 0; f < g->nFaces; ++f) {
      if (g->faceRight[f] < 0) continue;
      adj[g->faceLeft[f]].push_back(g->faceRight[f]);
      adj[g->faceRight[f]].push_back(g->faceLeft[f]);
    }
    std::vector<int> part(nC, 0);
    if (size > 1) {
      std::vector<idx_t> xadj(nC + 1, 0);
      for (int c = 0; c < nC; ++c) xadj[c + 1] = xadj[c] + static_cast<idx_t>(adj[c].size());
      std::vector<idx_t> adjncy(xadj[nC]);
      size_t pos = 0;
      for (int c = 0; c < nC; ++c)
        for (int nb : adj[c]) adjncy[pos++] = static_cast<idx_t>(nb);
      idx_t nvtxs = nC, ncon = 1, nparts = size, objval = 0;
      std::vector<idx_t> opts(METIS_NOPTIONS);
      METIS_SetDefaultOptions(opts.data());
      opts[METIS_OPTION_CONTIG] = 1;
      std::vector<idx_t> mparts(nC);
      const int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr,
                                         nullptr, nullptr, &nparts, nullptr, nullptr,
                                         opts.data(), &objval, mparts.data());
      if (rc != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
      for (int c = 0; c < nC; ++c) part[c] = static_cast<int>(mparts[c]);
      edgeCut = static_cast<long>(objval);
    }

    // owned cells per rank
    std::vector<std::vector<int>> owned(size);
    for (int c = 0; c < nC; ++c) owned[part[c]].push_back(c);

    // ghost cells per rank: neighbors (through interior faces) owned by others
    std::vector<std::set<int>> ghostSet(size);
    for (int f = 0; f < g->nFaces; ++f) {
      const int L = g->faceLeft[f], R = g->faceRight[f];
      if (R < 0) continue;
      if (part[L] != part[R]) {
        ghostSet[part[L]].insert(R);
        ghostSet[part[R]].insert(L);
      }
    }

    std::vector<Buffer> buffers(size);
    for (int r = 0; r < size; ++r) {
      // local cell numbering: owned (sorted global) then ghosts (sorted global)
      std::vector<int> localOfGlobal(nC, -1);
      std::vector<int> localCells;  // global ids, owned then ghosts
      for (int c : owned[r]) {
        localOfGlobal[c] = static_cast<int>(localCells.size());
        localCells.push_back(c);
      }
      std::vector<int> ghosts(ghostSet[r].begin(), ghostSet[r].end());  // sorted
      for (int c : ghosts) {
        localOfGlobal[c] = static_cast<int>(localCells.size());
        localCells.push_back(c);
      }
      const int nOwned = static_cast<int>(owned[r].size());
      const int nGhost = static_cast<int>(ghosts.size());
      const int nLoc = nOwned + nGhost;

      // local nodes
      std::vector<int> nodeLocal(g->nNodes, -1);
      std::vector<int> locNodes;  // global node ids
      std::vector<int> cellNVerts, cellNodeOff(1, 0), cellNodes;
      for (int lc = 0; lc < nLoc; ++lc) {
        const int gc = localCells[lc];
        const int nv = g->cellNVerts[gc];
        cellNVerts.push_back(nv);
        for (int i = 0; i < nv; ++i) {
          const int gn = g->cellNodes[g->cellNodeOffset[gc] + i];
          if (nodeLocal[gn] < 0) {
            nodeLocal[gn] = static_cast<int>(locNodes.size());
            locNodes.push_back(gn);
          }
          cellNodes.push_back(nodeLocal[gn]);
        }
        cellNodeOff.push_back(static_cast<int>(cellNodes.size()));
      }

      // local faces from global faces: include faces touching >=1 owned cell
      std::vector<int> fL, fR, fBc, fEA, fEB;
      for (int f = 0; f < g->nFaces; ++f) {
        const int gL = g->faceLeft[f], gR = g->faceRight[f];
        const int lL = localOfGlobal[gL];
        const int lR = (gR >= 0) ? localOfGlobal[gR] : -1;
        const bool ownedL = (lL >= 0 && lL < nOwned);
        const bool ownedR = (lR >= 0 && lR < nOwned);
        if (!ownedL && !ownedR) continue;
        if (gR < 0) {
          if (!ownedL) continue;  // boundary face handled by owning rank only
          fL.push_back(lL);
          fR.push_back(-1);
          fBc.push_back(g->faceBc[f]);
        } else if (ownedL) {
          fL.push_back(lL);
          fR.push_back(lR);
          fBc.push_back(-1);
        } else {
          fL.push_back(lR);
          fR.push_back(lL);
          fBc.push_back(-1);
        }
        fEA.push_back(nodeLocal[g->faceNodeA[f]]);
        fEB.push_back(nodeLocal[g->faceNodeB[f]]);
      }

      // halo neighbor lists (global ids, sorted -> consistent order both sides)
      std::map<int, std::vector<int>> sendGlobal, recvGlobal;
      for (int f = 0; f < g->nFaces; ++f) {
        const int gL = g->faceLeft[f], gR = g->faceRight[f];
        if (gR < 0 || part[gL] == part[gR]) continue;
        if (part[gL] == r) {
          sendGlobal[part[gR]].push_back(gL);
          recvGlobal[part[gR]].push_back(gR);
        } else if (part[gR] == r) {
          sendGlobal[part[gL]].push_back(gR);
          recvGlobal[part[gL]].push_back(gL);
        }
      }
      std::vector<int> neighRanks;
      std::vector<std::vector<int>> sendIdx, recvIdx;
      for (auto& kv : recvGlobal) {
        const int q = kv.first;
        auto& rg = kv.second;
        std::sort(rg.begin(), rg.end());
        rg.erase(std::unique(rg.begin(), rg.end()), rg.end());
        auto& sg = sendGlobal[q];
        std::sort(sg.begin(), sg.end());
        sg.erase(std::unique(sg.begin(), sg.end()), sg.end());
        neighRanks.push_back(q);
        std::vector<int> si, ri;
        for (int gc : sg) si.push_back(localOfGlobal[gc]);
        for (int gc : rg) ri.push_back(localOfGlobal[gc]);
        sendIdx.push_back(std::move(si));
        recvIdx.push_back(std::move(ri));
      }

      // ---- pack ----
      Buffer& b = buffers[r];
      b.put(nOwned);
      b.put(nGhost);
      b.put(static_cast<int>(locNodes.size()));
      b.putVec(std::vector<long>(localCells.begin(), localCells.end()));
      std::vector<double> nx(locNodes.size()), ny(locNodes.size());
      for (size_t i = 0; i < locNodes.size(); ++i) {
        nx[i] = g->x[locNodes[i]];
        ny[i] = g->y[locNodes[i]];
      }
      b.putVec(nx);
      b.putVec(ny);
      b.putVec(cellNVerts);
      b.putVec(cellNodeOff);
      b.putVec(cellNodes);
      b.putVec(fL);
      b.putVec(fR);
      b.putVec(fBc);
      b.putVec(fEA);
      b.putVec(fEB);
      b.put(static_cast<int>(g->bcNames.size()));
      for (const auto& nm : g->bcNames) b.putString(nm);
      b.put(static_cast<int>(neighRanks.size()));
      for (size_t i = 0; i < neighRanks.size(); ++i) {
        b.put(neighRanks[i]);
        b.putVec(sendIdx[i]);
        b.putVec(recvIdx[i]);
      }
      bufSizes[r] = static_cast<int>(b.data.size());
    }

    // scatter sizes then buffers
    MPI_Scatter(bufSizes.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, 0, comm);
    // rank 0 keeps its own buffer
    myBuf = buffers[0].data;
    std::vector<MPI_Request> reqs;
    for (int r = 1; r < size; ++r) {
      MPI_Request req;
      MPI_Isend(buffers[r].data.data(), bufSizes[r], MPI_BYTE, r, 77, comm, &req);
      reqs.push_back(req);
    }
    MPI_Waitall(static_cast<int>(reqs.size()), reqs.data(), MPI_STATUSES_IGNORE);
  } else {
    int mySize = 0;
    MPI_Scatter(nullptr, 1, MPI_INT, &mySize, 1, MPI_INT, 0, comm);
    myBuf.resize(mySize);
    MPI_Status st;
    MPI_Recv(myBuf.data(), mySize, MPI_BYTE, 0, 77, comm, &st);
  }

  // edge cut to all ranks
  MPI_Bcast(&edgeCut, 1, MPI_LONG, 0, comm);

  // ---- unpack ----
  Cursor cur{myBuf.data(), myBuf.size()};
  LocalMesh lm;
  lm.nOwned = cur.get<int>();
  lm.nGhost = cur.get<int>();
  lm.nCells = lm.nOwned + lm.nGhost;
  lm.nNodes = cur.get<int>();
  lm.cellGlobal = cur.getVec<long>();
  lm.x = cur.getVec<double>();
  lm.y = cur.getVec<double>();
  lm.cellNVerts = cur.getVec<int>();
  lm.cellNodeOffset = cur.getVec<int>();
  lm.cellNodes = cur.getVec<int>();
  lm.faceL = cur.getVec<int>();
  lm.faceR = cur.getVec<int>();
  lm.faceBc = cur.getVec<int>();
  auto fEA = cur.getVec<int>();
  auto fEB = cur.getVec<int>();
  const int nBcNames = cur.get<int>();
  for (int i = 0; i < nBcNames; ++i) {
    lm.bcNames.push_back(cur.getString());
    lm.bcNameToId[static_cast<int>(i)] = i;
  }
  lm.nFaces = static_cast<int>(lm.faceL.size());
  const int nNeigh = cur.get<int>();
  for (int i = 0; i < nNeigh; ++i) {
    LocalMesh::Neighbor nb;
    nb.rank = cur.get<int>();
    nb.sendIdx = cur.getVec<int>();
    nb.recvIdx = cur.getVec<int>();
    lm.neighbors.push_back(std::move(nb));
  }

  finalizeLocalGeometryFromEdges(lm, fEA, fEB);
  return lm;
}

}  // namespace fv
