#include "partition.hpp"

#include <metis.h>

#include <algorithm>
#include <set>
#include <stdexcept>

namespace cfd {

PartitionResult partitionMesh(const GlobalMesh& mesh, int nRanks, int /*rank*/) {
  idx_t n = static_cast<idx_t>(mesh.numCells());
  if (nRanks < 1) throw std::runtime_error("partitionMesh: nRanks < 1");
  idx_t nparts = static_cast<idx_t>(nRanks);

  if (nRanks == 1) {
    PartitionResult res;
    res.partOfCell.assign(n, 0);
    res.edgeCut = 0;
    return res;
  }

  // Cell adjacency graph.
  std::vector<idx_t> xadj(n + 1);
  std::vector<idx_t> adjncy;
  adjncy.reserve(static_cast<size_t>(mesh.cellFaces.size()) * 2);
  xadj[0] = 0;
  for (idx_t c = 0; c < n; ++c) {
    std::set<idx_t> nbrs;
    for (int fi : mesh.cellFaces[c]) {
      const auto& f = mesh.faces[fi];
      idx_t other = (f.c0 == c) ? f.c1 : f.c0;
      if (other >= 0) nbrs.insert(other);
    }
    for (idx_t nb : nbrs) adjncy.push_back(nb);
    xadj[c + 1] = static_cast<idx_t>(adjncy.size());
  }

  idx_t options[METIS_NOPTIONS];
  METIS_SetDefaultOptions(options);
  options[METIS_OPTION_NUMBERING] = 0;   // C-style
  options[METIS_OPTION_SEED] = 20240807;
  options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
  options[METIS_OPTION_DBGLVL] = 0;

  idx_t objval = 0;
  std::vector<idx_t> part(n);
  idx_t ncon = 1;
  int rc = METIS_PartGraphKway(&n, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr,
                               nullptr, &nparts, nullptr, nullptr, options, &objval,
                               part.data());
  if (rc != METIS_OK)
    throw std::runtime_error("METIS_PartGraphKway failed (rc=" + std::to_string(rc) + ")");

  PartitionResult res;
  res.partOfCell.assign(n, 0);
  for (idx_t c = 0; c < n; ++c) res.partOfCell[c] = static_cast<int>(part[c]);
  res.edgeCut = static_cast<long>(objval);
  return res;
}

LocalMesh buildLocalMesh(const GlobalMesh& mesh, const PartitionResult& part,
                         int rank, int nRanks) {
  const long nGlobal = mesh.numCells();
  LocalMesh lm;
  lm.edgeCut = part.edgeCut;
  lm.numCellsGlobal = mesh.numCells();
  lm.numFacesGlobal = mesh.numFaces();

  // Owned cells (global order) then ghosts (global order) for determinism.
  std::vector<int> owned, ghosts;
  for (long c = 0; c < nGlobal; ++c)
    if (part.partOfCell[c] == rank) owned.push_back(static_cast<int>(c));
  std::set<int> ghostSet;
  for (int c : owned) {
    for (int fi : mesh.cellFaces[c]) {
      const auto& f = mesh.faces[fi];
      int other = (f.c0 == c) ? f.c1 : f.c0;
      if (other >= 0 && part.partOfCell[other] != rank) ghostSet.insert(other);
    }
  }
  ghosts.assign(ghostSet.begin(), ghostSet.end());

  lm.nOwned = static_cast<int>(owned.size());
  lm.nGhost = static_cast<int>(ghosts.size());
  const int nLocal = lm.numLocal();
  lm.centroid.resize(nLocal);
  lm.volume.resize(nLocal);
  lm.globalId.resize(nLocal);
  lm.ownerRank.resize(nLocal);
  lm.cellFaces.resize(nLocal);
  lm.cellFaceSign.resize(nLocal);
  lm.neighbors.resize(nLocal);
  lm.cellPoints.resize(lm.nOwned);
  lm.cellPointIds.resize(lm.nOwned);

  std::vector<int> globalToLocal(nGlobal, -1);
  auto fillCell = [&](int localIdx, int globalIdx) {
    lm.centroid[localIdx] = mesh.cellCentroid[globalIdx];
    lm.volume[localIdx] = mesh.cellVolume[globalIdx];
    lm.globalId[localIdx] = globalIdx;
    lm.ownerRank[localIdx] = part.partOfCell[globalIdx];
    globalToLocal[globalIdx] = localIdx;
  };
  for (size_t i = 0; i < owned.size(); ++i) fillCell(static_cast<int>(i), owned[i]);
  for (size_t i = 0; i < ghosts.size(); ++i)
    fillCell(lm.nOwned + static_cast<int>(i), ghosts[i]);
  for (size_t i = 0; i < owned.size(); ++i) {
    const int c = owned[i];
    for (int v : mesh.cells[c]) {
      lm.cellPoints[i].push_back(mesh.points[v]);
      lm.cellPointIds[i].push_back(v);
    }
  }

  // Local faces.
  std::set<int> neighborRankSet;
  for (size_t gi = 0; gi < mesh.faces.size(); ++gi) {
    const auto& gf = mesh.faces[gi];
    const bool c0local = (gf.c0 >= 0 && globalToLocal[gf.c0] >= 0);
    const bool c1local = (gf.c1 >= 0 && globalToLocal[gf.c1] >= 0);
    if (!c0local && !c1local) continue;   // both cells on other ranks
    // Boundary faces are only stored by the rank that owns the adjacent cell.
    if (gf.c1 < 0 && (gf.c0 < 0 || part.partOfCell[gf.c0] != rank)) continue;

    LocalMesh::Face f;
    if (c0local) {
      f.c0 = globalToLocal[gf.c0];
      f.normal = gf.normal;
    } else {
      f.c0 = globalToLocal[gf.c1];
      f.normal = {-gf.normal[0], -gf.normal[1]};
    }
    f.c1 = (gf.c1 >= 0) ? globalToLocal[gf.c1] : -1;
    f.len = gf.len;
    f.centroid = gf.centroid;
    f.bc = (gf.c1 < 0) ? mesh.faceBcType[gi] : -1;
    f.tag = (gf.c1 < 0 && gf.familyId >= 0) ? mesh.familyNames[gf.familyId] : "";
    const int fid = static_cast<int>(lm.faces.size());
    lm.faces.push_back(f);
    lm.cellFaces[f.c0].push_back(fid);
    lm.cellFaceSign[f.c0].push_back(1);
    if (f.c1 >= 0) {
      lm.cellFaces[f.c1].push_back(fid);
      lm.cellFaceSign[f.c1].push_back(-1);
    }
    if (f.c1 >= 0) {
      lm.neighbors[f.c0].push_back(f.c1);
      lm.neighbors[f.c1].push_back(f.c0);
    }
    if (f.bc >= 0) {
      lm.wallFaceIdx.push_back(fid);
      lm.wallCell.push_back(f.c0);
      lm.wallNormal.push_back(f.normal);
      lm.wallCentroid.push_back(f.centroid);
      lm.wallLen.push_back(f.len);
      lm.wallTag.push_back(f.tag);
    }
  }

  // Halo exchange tables.
  std::map<int, std::vector<int>> recvMap;  // rank -> ghost local ids
  for (int g = lm.nOwned; g < nLocal; ++g) {
    neighborRankSet.insert(lm.ownerRank[g]);
    recvMap[lm.ownerRank[g]].push_back(g);
  }
  std::map<int, std::vector<int>> sendMap;  // rank -> owned local ids
  for (int c : owned) {
    std::set<int> sranks;
    for (int fi : mesh.cellFaces[c]) {
      const auto& gf = mesh.faces[fi];
      int other = (gf.c0 == c) ? gf.c1 : gf.c0;
      if (other >= 0 && part.partOfCell[other] != rank) sranks.insert(part.partOfCell[other]);
    }
    for (int r : sranks) sendMap[r].push_back(globalToLocal[c]);
  }

  lm.neighborRanks.assign(neighborRankSet.begin(), neighborRankSet.end());
  lm.sendCells.resize(lm.neighborRanks.size());
  lm.recvCells.resize(lm.neighborRanks.size());
  for (size_t k = 0; k < lm.neighborRanks.size(); ++k) {
    int r = lm.neighborRanks[k];
    lm.sendCells[k] = sendMap[r];
    lm.recvCells[k] = recvMap[r];
    std::sort(lm.sendCells[k].begin(), lm.sendCells[k].end());
    std::sort(lm.recvCells[k].begin(), lm.recvCells[k].end());
  }
  return lm;
}

}  // namespace cfd
