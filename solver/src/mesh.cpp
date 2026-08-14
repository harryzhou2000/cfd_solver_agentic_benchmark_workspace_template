#include "mesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <set>
#include <iostream>

namespace cfd2d {

// Sort two node ids to form a canonical edge key
static uint64_t edgeKey(int a, int b) {
  if (a > b) std::swap(a, b);
  return ((uint64_t)a << 32) | (uint64_t)b;
}

static double triArea(double x0, double y0, double x1, double y1, double x2, double y2) {
  return 0.5 * std::abs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
}

static double quadArea(const std::vector<double>& xs, const std::vector<double>& ys) {
  // Shoelace for quad
  double a = 0;
  int n = 4;
  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    a += xs[i] * ys[j] - xs[j] * ys[i];
  }
  return 0.5 * std::abs(a);
}

bool buildGlobalMesh(Mesh& raw, const CaseInput& ci,
                     GlobalMesh& gm, std::string& err) {
  gm.x = raw.x;
  gm.y = raw.y;
  gm.cellNodes = raw.cellNodes;
  gm.numVerts = (int)raw.x.size();
  gm.numCells = (int)raw.cellNodes.size();

  // Build cell geometry
  gm.cellGeo.resize(gm.numCells);
  for (int c = 0; c < gm.numCells; ++c) {
    auto& nodes = gm.cellNodes[c];
    int n = (int)nodes.size();
    std::vector<double> xs(n), ys(n);
    for (int k = 0; k < n; ++k) {
      xs[k] = gm.x[nodes[k] - 1];
      ys[k] = gm.y[nodes[k] - 1];
    }
    double cx = 0, cy = 0;
    for (int k = 0; k < n; ++k) { cx += xs[k]; cy += ys[k]; }
    cx /= n; cy /= n;
    gm.cellGeo[c].cx = cx;
    gm.cellGeo[c].cy = cy;
    gm.cellGeo[c].nodeIds = nodes;
    if (n == 3) {
      gm.cellGeo[c].area = triArea(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
    } else if (n == 4) {
      gm.cellGeo[c].area = quadArea(xs, ys);
    } else {
      gm.cellGeo[c].area = 0;
    }
  }

  // Map family -> BCType
  for (auto& kv : ci.boundaryConditions) {
    const std::string& fam = kv.first;
    const std::string& bct = kv.second;
    BCType bt = BCType::Interior;
    if (bct == "farfield") bt = BCType::Farfield;
    else if (bct == "slip_wall") bt = BCType::SlipWall;
    else if (bct == "no_slip_adiabatic_wall") bt = BCType::NoSlipAdiabaticWall;
    else {
      err = "unknown BC type '" + bct + "' for family '" + fam + "'";
      return false;
    }
    gm.familyBCType[fam] = bt;
    gm.bcFamilies.push_back(fam);
  }

  // Build faces from cell edges.
  // For each cell, each consecutive pair of nodes forms an edge.
  // Interior faces appear twice (once per adjacent cell); boundary faces once.
  std::unordered_map<uint64_t, std::vector<std::pair<int,int>>> edgeMap;
  // edgeKey -> list of (cell, localEdgeIndex)
  edgeMap.reserve(gm.numCells * 4);

  for (int c = 0; c < gm.numCells; ++c) {
    auto& nodes = gm.cellNodes[c];
    int n = (int)nodes.size();
    for (int k = 0; k < n; ++k) {
      int a = nodes[k];
      int b = nodes[(k + 1) % n];
      uint64_t key = edgeKey(a, b);
      edgeMap[key].push_back({c, k});
    }
  }

  // Identify interior (excluded) cells by flood fill from farfield boundary.
  // Build cell-cell adjacency from edgeMap.
  std::vector<std::vector<int>> cellAdj(gm.numCells);
  for (auto& kv : edgeMap) {
    auto& cells = kv.second;
    if (cells.size() == 2) {
      cellAdj[cells[0].first].push_back(cells[1].first);
      cellAdj[cells[1].first].push_back(cells[0].first);
    }
  }
  // Find farfield boundary cells (cells adjacent to farfield boundary faces)
  std::set<int> farfieldCells;
  for (auto& bf : raw.bfaces) {
    auto& fam = bf.family;
    if (gm.familyBCType.count(fam) && gm.familyBCType[fam] == BCType::Farfield) {
      // Find the cell that owns this edge
      uint64_t bk = edgeKey(bf.nodes[0], bf.nodes[1]);
      auto it = edgeMap.find(bk);
      if (it != edgeMap.end() && !it->second.empty()) {
        farfieldCells.insert(it->second[0].first);
      }
    }
  }
  // Flood fill from farfield cells
  std::vector<bool> isExterior(gm.numCells, false);
  std::vector<int> stack(farfieldCells.begin(), farfieldCells.end());
  for (int c : stack) isExterior[c] = true;
  while (!stack.empty()) {
    int c = stack.back(); stack.pop_back();
    for (int n : cellAdj[c]) {
      if (!isExterior[n]) {
        isExterior[n] = true;
        stack.push_back(n);
      }
    }
  }
  // Mark interior cells as excluded
  for (int c = 0; c < gm.numCells; ++c) {
    if (!isExterior[c]) {
      raw.excludedCells.insert(c);
    }
  }
  if (!raw.excludedCells.empty()) {
    // Rebuild cellNodes without excluded cells
    std::vector<std::vector<int>> newCells;
    for (int c = 0; c < gm.numCells; ++c) {
      if (!raw.excludedCells.count(c)) {
        newCells.push_back(gm.cellNodes[c]);
      }
    }
    gm.cellNodes = newCells;
    gm.numCells = (int)newCells.size();
    gm.cellGeo.resize(gm.numCells);
    for (int c = 0; c < gm.numCells; ++c) {
      auto& nodes = gm.cellNodes[c];
      int n = (int)nodes.size();
      std::vector<double> xs(n), ys(n);
      for (int k = 0; k < n; ++k) {
        xs[k] = gm.x[nodes[k] - 1];
        ys[k] = gm.y[nodes[k] - 1];
      }
      double cx = 0, cy = 0;
      for (int k = 0; k < n; ++k) { cx += xs[k]; cy += ys[k]; }
      cx /= n; cy /= n;
      gm.cellGeo[c].cx = cx;
      gm.cellGeo[c].cy = cy;
      gm.cellGeo[c].nodeIds = nodes;
      if (n == 3) {
        gm.cellGeo[c].area = triArea(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
      } else if (n == 4) {
        gm.cellGeo[c].area = quadArea(xs, ys);
      }
    }
    // Rebuild edgeMap without excluded cells
    edgeMap.clear();
    for (int c = 0; c < gm.numCells; ++c) {
      auto& nodes = gm.cellNodes[c];
      int n = (int)nodes.size();
      for (int k = 0; k < n; ++k) {
        int a = nodes[k];
        int b = nodes[(k + 1) % n];
        uint64_t key = edgeKey(a, b);
        edgeMap[key].push_back({c, k});
      }
    }
  }

  // Now build boundary faces from raw.bfaces and match them to edges
  // to get the correct orientation (left cell, right cell).
  // We'll create a map from edgeKey -> boundary face info.
  std::unordered_map<uint64_t, int> bfaceEdgeMap; // edgeKey -> index in gm.faces (boundary)
  // First, collect boundary faces
  struct BFaceInfo {
    int n0, n1; // as given (ordered)
    std::string family;
    BCType bcType;
  };
  std::vector<BFaceInfo> bfList;
  for (auto& bf : raw.bfaces) {
    BFaceInfo bi;
    bi.n0 = bf.nodes[0];
    bi.n1 = bf.nodes[1];
    bi.family = bf.family;
    bi.bcType = gm.familyBCType[bf.family];
    bfList.push_back(bi);
  }

  // Build all faces
  gm.faces.clear();
  // We process edges: if an edge has 2 cells -> interior face
  // if 1 cell -> check if it's a boundary face
  for (auto& kv : edgeMap) {
    uint64_t key = kv.first;
    auto& cells = kv.second;
    if (cells.size() == 2) {
      // Interior face
      int c0 = cells[0].first, e0 = cells[0].second;
      int c1 = cells[1].first, e1 = cells[1].second;
      // Get the two nodes in order for cell c0
      auto& nodes0 = gm.cellNodes[c0];
      int n0 = nodes0[e0], n1 = nodes0[(e0 + 1) % nodes0.size()];
      Face f;
      f.l = c0; f.r = c1;
      f.n0 = n0; f.n1 = n1;
      f.bcType = (int)BCType::Interior;
      f.family = "";
      // Compute geometry
      double xa = gm.x[n0 - 1], ya = gm.y[n0 - 1];
      double xb = gm.x[n1 - 1], yb = gm.y[n1 - 1];
      double dx = xb - xa, dy = yb - ya;
      f.len = std::sqrt(dx * dx + dy * dy);
      // Normal: rotate edge by 90 deg. For CCW cell ordering, normal points outward (left->right).
      // We want normal pointing from c0 to c1.
      // Edge direction (dx,dy). Outward normal for CCW = (dy, -dx) normalized.
      // But we need to verify orientation: c0 centroid vs c1 centroid.
      double nx = dy / f.len, ny = -dx / f.len;
      double c0cx = gm.cellGeo[c0].cx, c0cy = gm.cellGeo[c0].cy;
      double c1cx = gm.cellGeo[c1].cx, c1cy = gm.cellGeo[c1].cy;
      double dot = nx * (c1cx - c0cx) + ny * (c1cy - c0cy);
      if (dot < 0) { nx = -nx; ny = -ny; std::swap(f.l, f.r); std::swap(f.n0, f.n1); }
      f.nx = nx; f.ny = ny;
      f.mx = 0.5 * (xa + xb); f.my = 0.5 * (ya + yb);
      gm.faces.push_back(f);
    } else if (cells.size() == 1) {
      // Boundary face candidate
      int c0 = cells[0].first, e0 = cells[0].second;
      auto& nodes0 = gm.cellNodes[c0];
      int n0 = nodes0[e0], n1 = nodes0[(e0 + 1) % nodes0.size()];
      // Find matching boundary face
      uint64_t bk = edgeKey(n0, n1);
      // Search bfList for this edge
      int foundBf = -1;
      for (int i = 0; i < (int)bfList.size(); ++i) {
        uint64_t bk2 = edgeKey(bfList[i].n0, bfList[i].n1);
        if (bk2 == bk) { foundBf = i; break; }
      }
      if (foundBf < 0) continue; // internal interface edge, skip
      BFaceInfo& bi = bfList[foundBf];
      Face f;
      f.l = c0; f.r = -1;
      f.n0 = n0; f.n1 = n1;
      f.bcType = (int)bi.bcType;
      f.family = bi.family;
      double xa = gm.x[n0 - 1], ya = gm.y[n0 - 1];
      double xb = gm.x[n1 - 1], yb = gm.y[n1 - 1];
      double dx = xb - xa, dy = yb - ya;
      f.len = std::sqrt(dx * dx + dy * dy);
      double nx = dy / f.len, ny = -dx / f.len;
      // Normal should point outward from cell c0
      double c0cx = gm.cellGeo[c0].cx, c0cy = gm.cellGeo[c0].cy;
      double dot = nx * (f.mx - c0cx) + ny * (f.my - c0cy);
      // Use midpoint to check
      f.mx = 0.5 * (xa + xb); f.my = 0.5 * (ya + yb);
      dot = nx * (f.mx - c0cx) + ny * (f.my - c0cy);
      if (dot < 0) { nx = -nx; ny = -ny; }
      f.nx = nx; f.ny = ny;
      gm.faces.push_back(f);
    }
  }

  // Verify we have boundary faces for all families
  for (auto& fam : gm.bcFamilies) {
    bool found = false;
    for (auto& f : gm.faces) {
      if (f.family == fam) { found = true; break; }
    }
    if (!found) {
      err = "no boundary faces found for family '" + fam + "'";
      return false;
    }
  }

  // Create ghost cells for boundary faces.
  // Each boundary face gets a ghost cell (r = ghost cell index).
  // Ghost cells are appended after real cells.
  int nbcGhost = 0;
  for (auto& f : gm.faces) {
    if (f.r < 0) {
      // boundary face: create ghost cell
      int ghostId = gm.numCells + nbcGhost; // 0-based
      f.r = ghostId;
      nbcGhost++;
    }
  }
  // Extend cellGeo for ghost cells (dummy geometry)
  gm.cellGeo.resize(gm.numCells + nbcGhost);
  for (int i = 0; i < nbcGhost; ++i) {
    gm.cellGeo[gm.numCells + i].cx = 0;
    gm.cellGeo[gm.numCells + i].cy = 0;
    gm.cellGeo[gm.numCells + i].area = 1e-20; // tiny area to avoid div by zero
    gm.cellGeo[gm.numCells + i].nodeIds = {};
  }
  gm.numBcGhostCells = nbcGhost;

  return true;
}

} // namespace cfd2d
