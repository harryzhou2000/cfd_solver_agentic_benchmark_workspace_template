#include "partition.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include <mpi.h>
#include <metis.h>

namespace cfd2d {

namespace {

struct PackageBuilder {
  const GlobalMesh& gm;
  explicit PackageBuilder(const GlobalMesh& g) : gm(g) {}

  // Build the rank-local package for rank r given the partition vector.
  Buffer build(int r, const std::vector<int>& part) const {
    const int nC = gm.nCells;
    std::vector<int> owned;
    for (int c = 0; c < nC; ++c)
      if (part[c] == r) owned.push_back(c);

    // Ghost cells: neighbors of owned cells owned by other ranks.
    std::set<int> ghostSet;
    for (int f = 0; f < gm.nFaces; ++f) {
      const int L = gm.faceCellL[f], R = gm.faceCellR[f];
      if (R < 0) continue;
      if (part[L] == r && part[R] != r) ghostSet.insert(R);
      if (part[R] == r && part[L] != r) ghostSet.insert(L);
    }

    std::unordered_map<int, int> g2l;  // global cell -> local index
    g2l.reserve((owned.size() + ghostSet.size()) * 2);
    int idx = 0;
    for (int c : owned) g2l[c] = idx++;
    for (int c : ghostSet) g2l[c] = idx++;
    const int nOwned = static_cast<int>(owned.size());
    const int nLocal = idx;

    // Local nodes (deduplicated) for owned+ghost cells.
    std::unordered_map<int, int> gn2ln;
    std::vector<double> lx, ly;
    auto localNode = [&](int gn) {
      auto it = gn2ln.find(gn);
      if (it != gn2ln.end()) return it->second;
      int id = static_cast<int>(lx.size());
      gn2ln[gn] = id;
      lx.push_back(gm.nodeX[gn]);
      ly.push_back(gm.nodeY[gn]);
      return id;
    };

    std::vector<int> cellGlobal(nLocal), cellOwner(nLocal), cellNN(nLocal);
    std::vector<std::array<int, 4>> cellNodes(nLocal);
    for (const auto& kv : g2l) {
      const int g = kv.first, l = kv.second;
      cellGlobal[l] = g;
      cellOwner[l] = part[g];
      cellNN[l] = gm.cellNNodes[g];
      for (int k = 0; k < gm.cellNNodes[g]; ++k)
        cellNodes[l][k] = localNode(gm.cellNodes[g][k]);
      for (int k = gm.cellNNodes[g]; k < 4; ++k) cellNodes[l][k] = -1;
    }

    // Local faces: every global face touching at least one cell owned by r.
    std::vector<int> fL, fR, fFam, fN1, fN2;
    for (int f = 0; f < gm.nFaces; ++f) {
      const int L = gm.faceCellL[f], R = gm.faceCellR[f];
      const bool ownL = (part[L] == r);
      const bool ownR = (R >= 0 && part[R] == r);
      if (!ownL && !ownR) continue;
      fL.push_back(g2l.at(L));
      fR.push_back((R >= 0) ? g2l.at(R) : -1);
      fFam.push_back(gm.faceBcFam[f]);
      fN1.push_back(localNode(gm.faceN1[f]));
      fN2.push_back(localNode(gm.faceN2[f]));
    }

    // Communication lists. For ordered pair (r, s): S(r,s) = cells owned by r
    // adjacent to a cell owned by s, sorted by global id. Rank r sends S(r,s)
    // to s; rank s receives S(r,s) into its ghost slots (same order).
    std::map<int, std::vector<int>> sendG;  // neighbor -> owned global ids
    for (int f = 0; f < gm.nFaces; ++f) {
      const int L = gm.faceCellL[f], R = gm.faceCellR[f];
      if (R < 0) continue;
      if (part[L] == r && part[R] != r) sendG[part[R]].push_back(L);
      if (part[R] == r && part[L] != r) sendG[part[L]].push_back(R);
    }
    std::vector<int> neighbors;
    std::vector<std::vector<int>> sendCells, recvCells;
    for (auto& kv : sendG) {
      auto& g = kv.second;
      std::sort(g.begin(), g.end());
      g.erase(std::unique(g.begin(), g.end()), g.end());
      neighbors.push_back(kv.first);
      std::vector<int> sendL, recvL;
      for (int cg : g) {
        sendL.push_back(g2l.at(cg));   // owned on r
      }
      // recvCells on r for neighbor s: ghost cells owned by s adjacent to r,
      // which are exactly S(s, r) on the neighbor side. Compute S(s, r):
      std::vector<int> fromS;
      for (int f = 0; f < gm.nFaces; ++f) {
        const int L = gm.faceCellL[f], R = gm.faceCellR[f];
        if (R < 0) continue;
        if (part[L] == kv.first && part[R] == r) fromS.push_back(L);
        if (part[R] == kv.first && part[L] == r) fromS.push_back(R);
      }
      std::sort(fromS.begin(), fromS.end());
      fromS.erase(std::unique(fromS.begin(), fromS.end()), fromS.end());
      for (int cg : fromS) recvL.push_back(g2l.at(cg));  // ghost on r
      sendCells.push_back(std::move(sendL));
      recvCells.push_back(std::move(recvL));
    }

    // Serialize.
    Buffer buf;
    buf.put(nOwned);
    buf.put(nLocal - nOwned);
    buf.put(static_cast<int>(lx.size()));
    buf.put(static_cast<int>(fL.size()));
    buf.putVec(lx);
    buf.putVec(ly);
    buf.putVec(cellGlobal);
    buf.putVec(cellOwner);
    buf.putVec(cellNN);
    std::vector<int> flatNodes(nLocal * 4);
    for (int c = 0; c < nLocal; ++c)
      for (int k = 0; k < 4; ++k) flatNodes[c * 4 + k] = cellNodes[c][k];
    buf.putVec(flatNodes);
    buf.putVec(fL);
    buf.putVec(fR);
    buf.putVec(fFam);
    buf.putVec(fN1);
    buf.putVec(fN2);
    buf.put(static_cast<int>(gm.famNames.size()));
    for (const auto& name : gm.famNames) buf.putString(name);
    buf.put(static_cast<int>(neighbors.size()));
    for (size_t i = 0; i < neighbors.size(); ++i) {
      buf.put(neighbors[i]);
      buf.putVec(sendCells[i]);
      buf.putVec(recvCells[i]);
    }
    return buf;
  }
};

LocalMesh unpack(Buffer& buf) {
  LocalMesh lm;
  lm.nOwned = buf.get<int>();
  lm.nGhost = buf.get<int>();
  lm.nLocal = lm.nOwned + lm.nGhost;
  lm.nNodes = buf.get<int>();
  lm.nFaces = buf.get<int>();
  lm.nodeX = buf.getVec<double>();
  lm.nodeY = buf.getVec<double>();
  lm.cellGlobal = buf.getVec<int>();
  lm.cellOwner = buf.getVec<int>();
  lm.cellNNodes = buf.getVec<int>();
  std::vector<int> flatNodes = buf.getVec<int>();
  lm.cellNodes.resize(lm.nLocal);
  for (int c = 0; c < lm.nLocal; ++c)
    for (int k = 0; k < 4; ++k) lm.cellNodes[c][k] = flatNodes[c * 4 + k];
  lm.faceCellL = buf.getVec<int>();
  lm.faceCellR = buf.getVec<int>();
  lm.faceBcFam = buf.getVec<int>();
  std::vector<int> fN1 = buf.getVec<int>();
  std::vector<int> fN2 = buf.getVec<int>();
  const int nFam = buf.get<int>();
  for (int i = 0; i < nFam; ++i) lm.famNames.push_back(buf.getString());
  const int nNeigh = buf.get<int>();
  for (int i = 0; i < nNeigh; ++i) {
    lm.neighbors.push_back(buf.get<int>());
    lm.sendCells.push_back(buf.getVec<int>());
    lm.recvCells.push_back(buf.getVec<int>());
  }

  computeLocalGeometry(lm);

  // Face geometry from local nodes (same formulas as the global builder).
  lm.faceNx.resize(lm.nFaces);
  lm.faceNy.resize(lm.nFaces);
  lm.faceLen.resize(lm.nFaces);
  lm.faceCx.resize(lm.nFaces);
  lm.faceCy.resize(lm.nFaces);
  for (int f = 0; f < lm.nFaces; ++f) {
    const int i1 = fN1[f], i2 = fN2[f];
    const double dx = lm.nodeX[i2] - lm.nodeX[i1];
    const double dy = lm.nodeY[i2] - lm.nodeY[i1];
    const double len = std::hypot(dx, dy);
    double nx = dy / len, ny = -dx / len;
    const double fx = 0.5 * (lm.nodeX[i1] + lm.nodeX[i2]);
    const double fy = 0.5 * (lm.nodeY[i1] + lm.nodeY[i2]);
    const int L = lm.faceCellL[f], R = lm.faceCellR[f];
    double tx, ty;
    if (R >= 0) {
      tx = lm.cellCx[R] - lm.cellCx[L];
      ty = lm.cellCy[R] - lm.cellCy[L];
    } else {
      tx = fx - lm.cellCx[L];
      ty = fy - lm.cellCy[L];
    }
    if (nx * tx + ny * ty < 0.0) {
      nx = -nx;
      ny = -ny;
    }
    lm.faceNx[f] = nx;
    lm.faceNy[f] = ny;
    lm.faceLen[f] = len;
    lm.faceCx[f] = fx;
    lm.faceCy[f] = fy;
  }

  // Per-owned-cell face lists.
  lm.cellFaces.assign(lm.nOwned, {});
  for (int f = 0; f < lm.nFaces; ++f) {
    const int L = lm.faceCellL[f], R = lm.faceCellR[f];
    if (L < lm.nOwned) lm.cellFaces[L].push_back(f);
    if (R >= 0 && R < lm.nOwned) lm.cellFaces[R].push_back(f);
  }
  return lm;
}

}  // namespace

void computeLocalGeometry(LocalMesh& lm) {
  lm.cellVol.assign(lm.nLocal, 0.0);
  lm.cellCx.assign(lm.nLocal, 0.0);
  lm.cellCy.assign(lm.nLocal, 0.0);
  for (int c = 0; c < lm.nLocal; ++c) {
    const int nn = lm.cellNNodes[c];
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int k = 0; k < nn; ++k) {
      const int i1 = lm.cellNodes[c][k];
      const int i2 = lm.cellNodes[c][(k + 1) % nn];
      const double x1 = lm.nodeX[i1], y1 = lm.nodeY[i1];
      const double x2 = lm.nodeX[i2], y2 = lm.nodeY[i2];
      const double cross = x1 * y2 - x2 * y1;
      a += cross;
      cx += (x1 + x2) * cross;
      cy += (y1 + y2) * cross;
    }
    a *= 0.5;
    if (!(a > 0.0)) throw FatalError("non-positive local cell area");
    lm.cellVol[c] = a;
    lm.cellCx[c] = cx / (6.0 * a);
    lm.cellCy[c] = cy / (6.0 * a);
  }
}

LocalMesh distributeMesh(const GlobalMesh& gm, int rank, int nRanks,
                         int& edgeCut) {
  std::vector<int> part(gm.nCells, 0);
  int cut = 0;

  if (rank == 0) {
    if (nRanks > 1) {
      // Cell adjacency graph from interior faces.
      std::vector<idx_t> xadj(gm.nCells + 1, 0);
      for (int f = 0; f < gm.nFaces; ++f)
        if (gm.faceCellR[f] >= 0) {
          ++xadj[gm.faceCellL[f] + 1];
          ++xadj[gm.faceCellR[f] + 1];
        }
      for (int c = 0; c < gm.nCells; ++c) xadj[c + 1] += xadj[c];
      std::vector<idx_t> adjncy(xadj[gm.nCells]);
      {
        std::vector<idx_t> pos(xadj.begin(), xadj.end() - 1);
        for (int f = 0; f < gm.nFaces; ++f)
          if (gm.faceCellR[f] >= 0) {
            adjncy[pos[gm.faceCellL[f]]++] = gm.faceCellR[f];
            adjncy[pos[gm.faceCellR[f]]++] = gm.faceCellL[f];
          }
      }
      idx_t nvtxs = gm.nCells, ncon = 1, nparts = nRanks, objval = 0;
      std::vector<idx_t> metisPart(gm.nCells);
      int ierr = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(),
                                     nullptr, nullptr, nullptr, &nparts,
                                     nullptr, nullptr, nullptr, &objval,
                                     metisPart.data());
      if (ierr != METIS_OK) throw FatalError("METIS_PartGraphKway failed");
      for (int c = 0; c < gm.nCells; ++c) part[c] = metisPart[c];
      cut = objval;
    }
  }
  MPI_Bcast(&cut, 1, MPI_INT, 0, MPI_COMM_WORLD);
  edgeCut = cut;

  LocalMesh lm;
  if (rank == 0) {
    PackageBuilder builder(gm);
    // Send packages to workers (largest rank first is unnecessary; plain loop).
    for (int r = 1; r < nRanks; ++r) {
      Buffer buf = builder.build(r, part);
      uint64_t sz = buf.size();
      MPI_Send(&sz, 1, MPI_UINT64_T, r, 700, MPI_COMM_WORLD);
      MPI_Send(buf.data().data(), static_cast<int>(buf.size()), MPI_UINT8_T, r,
               701, MPI_COMM_WORLD);
    }
    Buffer own = builder.build(0, part);
    lm = unpack(own);
  } else {
    uint64_t sz = 0;
    MPI_Recv(&sz, 1, MPI_UINT64_T, 0, 700, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    Buffer buf;
    buf.data().resize(sz);
    MPI_Recv(buf.data().data(), static_cast<int>(sz), MPI_UINT8_T, 0, 701,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    lm = unpack(buf);
  }
  return lm;
}

}  // namespace cfd2d
