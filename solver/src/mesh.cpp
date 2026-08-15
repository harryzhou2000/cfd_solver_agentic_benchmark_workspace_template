#include "mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace fv {

namespace {

uint64_t faceKey(int a, int b, int nNodes) {
  if (a > b) std::swap(a, b);
  return static_cast<uint64_t>(a) * static_cast<uint64_t>(nNodes) + static_cast<uint64_t>(b);
}

// Spatial hash for tolerance-based node merging across zones.
struct PointMerger {
  double tol;
  double invTol;
  std::vector<double> px, py;
  std::unordered_map<uint64_t, std::vector<int>> bins;

  explicit PointMerger(double tolerance)
      : tol(tolerance), invTol(1.0 / tolerance) {}

  static uint64_t binKey(long i, long j) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(i)) << 32) |
           static_cast<uint32_t>(j);
  }

  int insert(double x, double y) {
    const long ci = static_cast<long>(std::floor(x * invTol));
    const long cj = static_cast<long>(std::floor(y * invTol));
    for (long di = -1; di <= 1; ++di) {
      for (long dj = -1; dj <= 1; ++dj) {
        auto it = bins.find(binKey(ci + di, cj + dj));
        if (it == bins.end()) continue;
        for (int idx : it->second) {
          const double dx = px[idx] - x, dy = py[idx] - y;
          if (dx * dx + dy * dy < tol * tol) return idx;
        }
      }
    }
    const int idx = static_cast<int>(px.size());
    px.push_back(x);
    py.push_back(y);
    bins[binKey(ci, cj)].push_back(idx);
    return idx;
  }
};

}  // namespace

GlobalMesh readCgnsMesh(const std::string& path) {
  int fn = -1;
  if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK)
    throw std::runtime_error(std::string("cg_open failed: ") + cg_get_error());
  int nbases = 0;
  cg_nbases(fn, &nbases);
  if (nbases < 1) {
    cg_close(fn);
    throw std::runtime_error("CGNS file has no base: " + path);
  }
  int base = 1;
  char basename[128];
  int cellDim = 0, physDim = 0;
  cg_base_read(fn, base, basename, &cellDim, &physDim);
  if (cellDim != 2) {
    cg_close(fn);
    throw std::runtime_error("only 2-D CGNS meshes supported (cellDim=" +
                             std::to_string(cellDim) + ")");
  }

  int nzones = 0;
  cg_nzones(fn, base, &nzones);

  GlobalMesh m;
  std::vector<std::array<int, 3>> tris;
  std::vector<std::array<int, 4>> quads;
  std::vector<std::array<int, 2>> bars;
  std::vector<std::string> barNames;

  // First pass: read coordinates of all zones with tolerance-based merging.
  // Compute a tolerance from overall bounding box.
  std::vector<std::pair<int, int>> zoneNodeRange;  // [start,count) per zone
  double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
  std::vector<std::vector<double>> zoneX(nzones), zoneY(nzones);
  for (int z = 1; z <= nzones; ++z) {
    char zonename[128];
    cgsize_t size[3] = {0, 0, 0};
    cg_zone_read(fn, base, z, zonename, size);
    const int nn = static_cast<int>(size[0]);
    zoneX[z - 1].resize(nn);
    zoneY[z - 1].resize(nn);
    cgsize_t rmin = 1, rmax = nn;
    cg_coord_read(fn, base, z, "CoordinateX", RealDouble, &rmin, &rmax, zoneX[z - 1].data());
    cg_coord_read(fn, base, z, "CoordinateY", RealDouble, &rmin, &rmax, zoneY[z - 1].data());
    for (int i = 0; i < nn; ++i) {
      xmin = std::min(xmin, zoneX[z - 1][i]);
      xmax = std::max(xmax, zoneX[z - 1][i]);
      ymin = std::min(ymin, zoneY[z - 1][i]);
      ymax = std::max(ymax, zoneY[z - 1][i]);
    }
  }
  const double diag = std::hypot(xmax - xmin, ymax - ymin);
  const double tol = 1e-9 * std::max(1.0, diag);

  PointMerger merger(tol);
  std::vector<std::vector<int>> zoneNodeMap(nzones);
  for (int z = 1; z <= nzones; ++z) {
    const int nn = static_cast<int>(zoneX[z - 1].size());
    zoneNodeMap[z - 1].resize(nn);
    for (int i = 0; i < nn; ++i)
      zoneNodeMap[z - 1][i] = merger.insert(zoneX[z - 1][i], zoneY[z - 1][i]);
  }
  m.nNodes = static_cast<int>(merger.px.size());
  m.x = merger.px;
  m.y = merger.py;

  // Second pass: element sections.
  for (int z = 1; z <= nzones; ++z) {
    int nsections = 0;
    cg_nsections(fn, base, z, &nsections);
    for (int s = 1; s <= nsections; ++s) {
      char secname[128];
      ElementType_t etype;
      cgsize_t start = 0, end = 0;
      int nbound = 0, parentFlag = 0;
      cg_section_read(fn, base, z, s, secname, &etype, &start, &end, &nbound, &parentFlag);
      const cgsize_t nelem = end - start + 1;
      if (nelem <= 0) continue;
      std::vector<cgsize_t> conn;
      // element connectivity size
      int npe = 0;
      cg_npe(etype, &npe);
      conn.resize(static_cast<size_t>(nelem) * npe);
      cg_elements_read(fn, base, z, s, conn.data(), nullptr);
      auto& nmap = zoneNodeMap[z - 1];
      if (etype == TRI_3) {
        for (cgsize_t e = 0; e < nelem; ++e)
          tris.push_back(std::array<int, 3>{nmap[conn[3 * e] - 1], nmap[conn[3 * e + 1] - 1],
                          nmap[conn[3 * e + 2] - 1]});
      } else if (etype == QUAD_4) {
        for (cgsize_t e = 0; e < nelem; ++e)
          quads.push_back(std::array<int, 4>{nmap[conn[4 * e] - 1], nmap[conn[4 * e + 1] - 1],
                           nmap[conn[4 * e + 2] - 1], nmap[conn[4 * e + 3] - 1]});
      } else if (etype == BAR_2) {
        for (cgsize_t e = 0; e < nelem; ++e) {
          bars.push_back(std::array<int, 2>{nmap[conn[2 * e] - 1], nmap[conn[2 * e + 1] - 1]});
          barNames.push_back(secname);
        }
      }
      // other element types ignored (e.g. point elements)
    }
  }
  cg_close(fn);

  // Flatten cells (tris first, then quads).
  m.nCells = static_cast<int>(tris.size() + quads.size());
  m.cellNVerts.reserve(m.nCells);
  m.cellNodeOffset.reserve(m.nCells + 1);
  m.cellNodeOffset.push_back(0);
  for (const auto& t : tris) {
    m.cellNVerts.push_back(3);
    m.cellNodes.insert(m.cellNodes.end(), t.begin(), t.end());
    m.cellNodeOffset.push_back(static_cast<int>(m.cellNodes.size()));
  }
  for (const auto& q : quads) {
    m.cellNVerts.push_back(4);
    m.cellNodes.insert(m.cellNodes.end(), q.begin(), q.end());
    m.cellNodeOffset.push_back(static_cast<int>(m.cellNodes.size()));
  }

  // Boundary family registry (in order of first appearance).
  std::unordered_map<std::string, int> bcId;
  std::unordered_map<uint64_t, int> barBc;
  for (size_t i = 0; i < bars.size(); ++i) {
    int id;
    auto it = bcId.find(barNames[i]);
    if (it == bcId.end()) {
      id = static_cast<int>(m.bcNames.size());
      m.bcNames.push_back(barNames[i]);
      bcId[barNames[i]] = id;
      m.bcNameToId[id] = id;
    } else {
      id = it->second;
    }
    barBc[faceKey(bars[i][0], bars[i][1], m.nNodes)] = id;
  }

  buildFacesAndGeometry(m);

  // Assign boundary tags.
  int nUntagged = 0;
  for (int f = 0; f < m.nFaces; ++f) {
    if (m.faceRight[f] != -1) continue;
    auto it = barBc.find(faceKey(m.faceNodeA[f], m.faceNodeB[f], m.nNodes));
    if (it != barBc.end()) {
      m.faceBc[f] = it->second;
    } else {
      ++nUntagged;
    }
  }
  if (nUntagged > 0)
    throw std::runtime_error("mesh has " + std::to_string(nUntagged) +
                             " boundary faces without a family tag (zone merge or BC "
                             "section mismatch) in " +
                             path);
  return m;
}

void buildFacesAndGeometry(GlobalMesh& m) {
  // Cell geometry (polygon centroid + area via shoelace).
  m.cellCx.resize(m.nCells);
  m.cellCy.resize(m.nCells);
  m.cellVol.resize(m.nCells);
  for (int c = 0; c < m.nCells; ++c) {
    const int nv = m.cellNVerts[c];
    const int* nodes = &m.cellNodes[m.cellNodeOffset[c]];
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < nv; ++i) {
      const int n0 = nodes[i], n1 = nodes[(i + 1) % nv];
      const double cross = m.x[n0] * m.y[n1] - m.x[n1] * m.y[n0];
      a += cross;
      cx += (m.x[n0] + m.x[n1]) * cross;
      cy += (m.y[n0] + m.y[n1]) * cross;
    }
    a *= 0.5;
    const double area = std::abs(a);
    if (area <= 0.0) throw std::runtime_error("degenerate cell area");
    m.cellVol[c] = area;
    m.cellCx[c] = cx / (6.0 * a);
    m.cellCy[c] = cy / (6.0 * a);
  }

  // Face construction.
  std::unordered_map<uint64_t, int> fmap;
  fmap.reserve(m.nCells * 2);
  std::vector<char> counted(m.nCells, 0);
  for (int c = 0; c < m.nCells; ++c) {
    const int nv = m.cellNVerts[c];
    const int* nodes = &m.cellNodes[m.cellNodeOffset[c]];
    for (int i = 0; i < nv; ++i) {
      int n0 = nodes[i], n1 = nodes[(i + 1) % nv];
      const uint64_t key = faceKey(n0, n1, m.nNodes);
      auto it = fmap.find(key);
      if (it == fmap.end()) {
        const int f = static_cast<int>(m.faceLeft.size());
        fmap[key] = f;
        m.faceNodeA.push_back(std::min(n0, n1));
        m.faceNodeB.push_back(std::max(n0, n1));
        m.faceLeft.push_back(c);
        m.faceRight.push_back(-1);
        m.faceBc.push_back(-1);
      } else {
        m.faceRight[it->second] = c;
      }
    }
  }
  m.nFaces = static_cast<int>(m.faceLeft.size());

  // Face geometry.
  m.faceCx.resize(m.nFaces);
  m.faceCy.resize(m.nFaces);
  m.faceNx.resize(m.nFaces);
  m.faceNy.resize(m.nFaces);
  m.faceArea.resize(m.nFaces);
  m.faceDx.resize(m.nFaces);
  m.faceDy.resize(m.nFaces);
  for (int f = 0; f < m.nFaces; ++f) {
    const int na = m.faceNodeA[f], nb = m.faceNodeB[f];
    const double ax = m.x[na], ay = m.y[na];
    const double bx = m.x[nb], by = m.y[nb];
    const double ex = bx - ax, ey = by - ay;
    const double area = std::hypot(ex, ey);
    const int L = m.faceLeft[f], R = m.faceRight[f];
    // candidate normal: rotate edge by -90 deg: (ey, -ex)
    double nx = ey, ny = -ex;
    const double fcx = 0.5 * (ax + bx), fcy = 0.5 * (ay + by);
    double rx, ry;
    if (R >= 0) {
      rx = m.cellCx[R] - m.cellCx[L];
      ry = m.cellCy[R] - m.cellCy[L];
    } else {
      rx = fcx - m.cellCx[L];
      ry = fcy - m.cellCy[L];
    }
    if (nx * rx + ny * ry < 0.0) {
      nx = -nx;
      ny = -ny;
    }
    m.faceCx[f] = fcx;
    m.faceCy[f] = fcy;
    m.faceNx[f] = nx;  // normal * area (edge length already in nx,ny magnitude)
    m.faceNy[f] = ny;
    m.faceArea[f] = area;
    if (R >= 0) {
      m.faceDx[f] = rx;
      m.faceDy[f] = ry;
    } else {
      // mirrored ghost center: reflect L center across the face line
      const double unx = nx / area, uny = ny / area;
      const double dx = fcx - m.cellCx[L], dy = fcy - m.cellCy[L];
      const double d = dx * unx + dy * uny;
      m.faceDx[f] = 2.0 * d * unx;
      m.faceDy[f] = 2.0 * d * uny;
    }
  }
}

// Build faces over owned+ghost cells of a LocalMesh from the given face edge
// endpoints (local node ids), face left/right cells and boundary tags, then
// compute all face geometry and the cell-face CSR tables. Faces between two
// ghost cells must already have been removed by the caller; faceL must be owned.
void finalizeLocalGeometryFromEdges(LocalMesh& lm, const std::vector<int>& fEdgeA,
                                    const std::vector<int>& fEdgeB) {
  // Cell geometry.
  lm.cellCx.resize(lm.nCells);
  lm.cellCy.resize(lm.nCells);
  lm.cellVol.resize(lm.nCells);
  for (int c = 0; c < lm.nCells; ++c) {
    const int nv = lm.cellNVerts[c];
    const int* nodes = &lm.cellNodes[lm.cellNodeOffset[c]];
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < nv; ++i) {
      const int n0 = nodes[i], n1 = nodes[(i + 1) % nv];
      const double cross = lm.x[n0] * lm.y[n1] - lm.x[n1] * lm.y[n0];
      a += cross;
      cx += (lm.x[n0] + lm.x[n1]) * cross;
      cy += (lm.y[n0] + lm.y[n1]) * cross;
    }
    a *= 0.5;
    lm.cellVol[c] = std::abs(a);
    lm.cellCx[c] = cx / (6.0 * a);
    lm.cellCy[c] = cy / (6.0 * a);
  }

  const int nKF = lm.nFaces;
  lm.faceCx.resize(nKF);
  lm.faceCy.resize(nKF);
  lm.faceNx.resize(nKF);
  lm.faceNy.resize(nKF);
  lm.faceArea.resize(nKF);
  lm.faceDx.resize(nKF);
  lm.faceDy.resize(nKF);
  for (int f = 0; f < nKF; ++f) {
    const int na = fEdgeA[f], nb = fEdgeB[f];
    const double ax = lm.x[na], ay = lm.y[na];
    const double bx = lm.x[nb], by = lm.y[nb];
    const double ex = bx - ax, ey = by - ay;
    const double area = std::hypot(ex, ey);
    const int L = lm.faceL[f], R = lm.faceR[f];
    double nx = ey, ny = -ex;
    const double fcx = 0.5 * (ax + bx), fcy = 0.5 * (ay + by);
    double rx, ry;
    if (R >= 0) {
      rx = lm.cellCx[R] - lm.cellCx[L];
      ry = lm.cellCy[R] - lm.cellCy[L];
    } else {
      rx = fcx - lm.cellCx[L];
      ry = fcy - lm.cellCy[L];
    }
    if (nx * rx + ny * ry < 0.0) {
      nx = -nx;
      ny = -ny;
    }
    lm.faceCx[f] = fcx;
    lm.faceCy[f] = fcy;
    lm.faceNx[f] = nx;
    lm.faceNy[f] = ny;
    lm.faceArea[f] = area;
    if (R >= 0) {
      lm.faceDx[f] = rx;
      lm.faceDy[f] = ry;
    } else {
      const double unx = nx / area, uny = ny / area;
      const double dx = fcx - lm.cellCx[L], dy = fcy - lm.cellCy[L];
      const double d = dx * unx + dy * uny;
      lm.faceDx[f] = 2.0 * d * unx;
      lm.faceDy[f] = 2.0 * d * uny;
    }
  }

  // cell-face CSR (owned cells only need it; include all for simplicity)
  std::vector<int> count(lm.nCells, 0);
  for (int f = 0; f < nKF; ++f) {
    count[lm.faceL[f]]++;
    if (lm.faceR[f] >= 0) count[lm.faceR[f]]++;
  }
  lm.cellFaceOffset.assign(lm.nCells + 1, 0);
  for (int c = 0; c < lm.nCells; ++c) lm.cellFaceOffset[c + 1] = lm.cellFaceOffset[c] + count[c];
  lm.cellFaces.resize(lm.cellFaceOffset[lm.nCells]);
  lm.cellFaceSign.resize(lm.cellFaceOffset[lm.nCells]);
  std::vector<int> pos(lm.cellFaceOffset.begin(), lm.cellFaceOffset.end() - 1);
  for (int f = 0; f < nKF; ++f) {
    const int L = lm.faceL[f], R = lm.faceR[f];
    lm.cellFaces[pos[L]] = f;
    lm.cellFaceSign[pos[L]] = 1;
    pos[L]++;
    if (R >= 0) {
      lm.cellFaces[pos[R]] = f;
      lm.cellFaceSign[pos[R]] = -1;
      pos[R]++;
    }
  }
}

}  // namespace fv
