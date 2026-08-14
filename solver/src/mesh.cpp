#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

#include <cgnslib.h>

namespace cfd2d {

namespace {

void cgOk(int ierr, const std::string& what) {
  if (ierr != CG_OK) {
    throw FatalError("CGNS error in " + what + ": " + cg_get_error());
  }
}

// Hash key for geometric duplicate-node merging at conformal zone interfaces.
struct NodeKey {
  long long ix, iy;
  bool operator==(const NodeKey& o) const { return ix == o.ix && iy == o.iy; }
};
struct NodeKeyHash {
  size_t operator()(const NodeKey& k) const {
    uint64_t a = static_cast<uint64_t>(k.ix) * 0x9E3779B97F4A7C15ULL;
    uint64_t b = static_cast<uint64_t>(k.iy) * 0xC2B2AE3D27D4EB4FULL;
    return static_cast<size_t>((a >> 3) ^ (b << 1));
  }
};

struct EdgeKey {
  int a, b;  // sorted node ids
  bool operator<(const EdgeKey& o) const {
    return (a != o.a) ? (a < o.a) : (b < o.b);
  }
};

}  // namespace

GlobalMesh readCgnsMesh(const std::string& path,
                        const std::vector<std::string>& bcFamilyNames) {
  int fn = -1;
  cgOk(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open(" + path + ")");

  int nBases = 0;
  cgOk(cg_nbases(fn, &nBases), "cg_nbases");
  if (nBases != 1) {
    cg_close(fn);
    throw FatalError("expected exactly one CGNS base, got " +
                     std::to_string(nBases));
  }
  int base = 1;
  char baseName[256];
  int cellDim = 0, physDim = 0;
  cgOk(cg_base_read(fn, base, baseName, &cellDim, &physDim), "cg_base_read");
  if (cellDim != 2) {
    cg_close(fn);
    throw FatalError("only 2-D (cellDim=2) meshes are supported");
  }

  int nZones = 0;
  cgOk(cg_nzones(fn, base, &nZones), "cg_nzones");
  if (nZones < 1) {
    cg_close(fn);
    throw FatalError("mesh has no zones");
  }

  const std::set<std::string> bcSet(bcFamilyNames.begin(), bcFamilyNames.end());

  // Raw (pre-merge) data: zone nodes are concatenated; element connectivity
  // references raw node ids.
  std::vector<double> rawX, rawY;
  struct RawCell { int n; std::array<int, 4> nodes; };
  std::vector<RawCell> rawCells;
  // Boundary edges per family name.
  std::map<std::string, std::vector<std::pair<int, int>>> famEdges;

  for (int zone = 1; zone <= nZones; ++zone) {
    char zoneName[256];
    cgsize_t sizes[3] = {0, 0, 0};
    cgOk(cg_zone_read(fn, base, zone, zoneName, sizes), "cg_zone_read");
    ZoneType_t zoneType;
    cgOk(cg_zone_type(fn, base, zone, &zoneType), "cg_zone_type");
    if (zoneType != Unstructured) {
      cg_close(fn);
      throw FatalError(std::string("zone ") + zoneName + " is not unstructured");
    }
    const cgsize_t znNodes = sizes[0];
    const int nodeOffset = static_cast<int>(rawX.size());

    std::vector<double> x(znNodes), y(znNodes);
    cgsize_t rmin = 1, rmax = znNodes;
    cgOk(cg_coord_read(fn, base, zone, "CoordinateX", RealDouble, &rmin, &rmax,
                       x.data()),
         "cg_coord_read X");
    cgOk(cg_coord_read(fn, base, zone, "CoordinateY", RealDouble, &rmin, &rmax,
                       y.data()),
         "cg_coord_read Y");
    rawX.insert(rawX.end(), x.begin(), x.end());
    rawY.insert(rawY.end(), y.begin(), y.end());

    int nSections = 0;
    cgOk(cg_nsections(fn, base, zone, &nSections), "cg_nsections");
    for (int sec = 1; sec <= nSections; ++sec) {
      char secName[256];
      ElementType_t type;
      cgsize_t start = 0, end = 0;
      int nBnd = 0, parentFlag = 0;
      cgOk(cg_section_read(fn, base, zone, sec, secName, &type, &start, &end,
                           &nBnd, &parentFlag),
           "cg_section_read");
      const cgsize_t nElem = end - start + 1;
      if (nElem <= 0) continue;

      int npe = 0;
      cgOk(cg_npe(type, &npe), "cg_npe");

      if (type == TRI_3 || type == QUAD_4) {
        std::vector<cgsize_t> conn(nElem * npe);
        cgOk(cg_elements_read(fn, base, zone, sec, conn.data(), nullptr),
             "cg_elements_read");
        for (cgsize_t e = 0; e < nElem; ++e) {
          RawCell c;
          c.n = npe;
          for (int k = 0; k < npe; ++k)
            c.nodes[k] = nodeOffset + static_cast<int>(conn[e * npe + k]) - 1;
          for (int k = npe; k < 4; ++k) c.nodes[k] = -1;
          rawCells.push_back(c);
        }
      } else if (type == BAR_2) {
        std::vector<cgsize_t> conn(nElem * 2);
        cgOk(cg_elements_read(fn, base, zone, sec, conn.data(), nullptr),
             "cg_elements_read bar");
        if (bcSet.count(secName)) {
          auto& edges = famEdges[secName];
          for (cgsize_t e = 0; e < nElem; ++e) {
            edges.emplace_back(nodeOffset + static_cast<int>(conn[2 * e]) - 1,
                               nodeOffset + static_cast<int>(conn[2 * e + 1]) - 1);
          }
        }
        // BAR sections not listed in the case boundary map (e.g. 1-to-1 zone
        // interface markers) are ignored as BCs; after duplicate-node merging
        // those edges coincide with interior cell faces.
      } else {
        cg_close(fn);
        throw FatalError(std::string("unsupported CGNS element type in section '") +
                         secName + "' (only TRI_3, QUAD_4, BAR_2 supported)");
      }
    }
  }
  cgOk(cg_close(fn), "cg_close");

  if (rawCells.empty()) throw FatalError("mesh contains no volume cells");

  // ---- Merge geometrically duplicate nodes (conformal zone interfaces) ----
  double xmin = *std::min_element(rawX.begin(), rawX.end());
  double xmax = *std::max_element(rawX.begin(), rawX.end());
  double ymin = *std::min_element(rawY.begin(), rawY.end());
  double ymax = *std::max_element(rawY.begin(), rawY.end());
  const double scale = std::max({xmax - xmin, ymax - ymin, 1.0});
  const double eps = 1e-9 * scale;

  std::unordered_map<NodeKey, int, NodeKeyHash> keyToNode;
  keyToNode.reserve(rawX.size() * 2);
  std::vector<int> rawToMerged(rawX.size());
  GlobalMesh m;
  for (size_t i = 0; i < rawX.size(); ++i) {
    NodeKey key{static_cast<long long>(std::llround(rawX[i] / eps)),
                static_cast<long long>(std::llround(rawY[i] / eps))};
    auto it = keyToNode.find(key);
    if (it == keyToNode.end()) {
      int id = static_cast<int>(m.nodeX.size());
      keyToNode.emplace(key, id);
      m.nodeX.push_back(rawX[i]);
      m.nodeY.push_back(rawY[i]);
      rawToMerged[i] = id;
    } else {
      rawToMerged[i] = it->second;
    }
  }
  m.nNodes = static_cast<int>(m.nodeX.size());

  m.nCells = static_cast<int>(rawCells.size());
  m.cellNNodes.resize(m.nCells);
  m.cellNodes.resize(m.nCells);
  for (int c = 0; c < m.nCells; ++c) {
    m.cellNNodes[c] = rawCells[c].n;
    for (int k = 0; k < rawCells[c].n; ++k)
      m.cellNodes[c][k] = rawToMerged[rawCells[c].nodes[k]];
    for (int k = rawCells[c].n; k < 4; ++k) m.cellNodes[c][k] = -1;
  }

  // Family names in deterministic order (sorted) so family ids are stable.
  std::set<std::string> famNameSet;
  for (const auto& kv : famEdges) famNameSet.insert(kv.first);
  for (const auto& name : famNameSet) m.famNames.push_back(name);

  // Boundary edge key -> family id.
  std::map<EdgeKey, int> edgeFam;
  for (const auto& kv : famEdges) {
    int fam = m.famId(kv.first);
    for (const auto& e : kv.second) {
      EdgeKey key{std::min(rawToMerged[e.first], rawToMerged[e.second]),
                  std::max(rawToMerged[e.first], rawToMerged[e.second])};
      edgeFam[key] = fam;
    }
  }

  // ---- Build faces from cell edges ----
  struct FaceBuild {
    int cell = -1;
    int n1 = -1, n2 = -1;  // oriented as seen by first cell
    int fam = -2;          // -2: unknown, -1: interior, >=0: boundary family
  };
  std::map<EdgeKey, FaceBuild> faceMap;
  for (int c = 0; c < m.nCells; ++c) {
    int nn = m.cellNNodes[c];
    for (int k = 0; k < nn; ++k) {
      int n1 = m.cellNodes[c][k];
      int n2 = m.cellNodes[c][(k + 1) % nn];
      EdgeKey key{std::min(n1, n2), std::max(n1, n2)};
      auto it = faceMap.find(key);
      if (it == faceMap.end()) {
        FaceBuild fb;
        fb.cell = c;
        fb.n1 = n1;
        fb.n2 = n2;
        auto fe = edgeFam.find(key);
        if (fe != edgeFam.end()) fb.fam = fe->second;
        faceMap.emplace(key, fb);
      } else {
        it->second.fam = -1;  // seen twice -> interior face
      }
    }
  }

  m.nFaces = static_cast<int>(faceMap.size());
  m.faceCellL.resize(m.nFaces);
  m.faceCellR.resize(m.nFaces);
  m.faceBcFam.resize(m.nFaces);
  m.faceN1.resize(m.nFaces);
  m.faceN2.resize(m.nFaces);
  {
    int f = 0;
    for (const auto& kv : faceMap) {
      const FaceBuild& fb = kv.second;
      m.faceCellL[f] = fb.cell;
      m.faceCellR[f] = -1;
      m.faceN1[f] = fb.n1;
      m.faceN2[f] = fb.n2;
      m.faceBcFam[f] = (fb.fam >= 0) ? fb.fam : -1;
      ++f;
    }
    // Second pass to fill right cells.
    std::map<EdgeKey, int> faceIndex;
    {
      int g = 0;
      for (const auto& kv : faceMap) faceIndex.emplace(kv.first, g++);
    }
    for (int c = 0; c < m.nCells; ++c) {
      int nn = m.cellNNodes[c];
      for (int k = 0; k < nn; ++k) {
        EdgeKey key{std::min(m.cellNodes[c][k], m.cellNodes[c][(k + 1) % nn]),
                    std::max(m.cellNodes[c][k], m.cellNodes[c][(k + 1) % nn])};
        int fi = faceIndex.at(key);
        if (m.faceCellL[fi] != c) m.faceCellR[fi] = c;
      }
    }
  }

  buildGeometry(m, bcFamilyNames);
  return m;
}

void buildGeometry(GlobalMesh& m, const std::vector<std::string>& bcFamilyNames) {
  const int nC = m.nCells, nF = m.nFaces;
  m.cellVol.assign(nC, 0.0);
  m.cellCx.assign(nC, 0.0);
  m.cellCy.assign(nC, 0.0);
  m.faceNx.assign(nF, 0.0);
  m.faceNy.assign(nF, 0.0);
  m.faceLen.assign(nF, 0.0);
  m.faceCx.assign(nF, 0.0);
  m.faceCy.assign(nF, 0.0);

  // Cell area/centroid with the polygon (shoelace) formula.
  for (int c = 0; c < nC; ++c) {
    const int nn = m.cellNNodes[c];
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int k = 0; k < nn; ++k) {
      const int i1 = m.cellNodes[c][k];
      const int i2 = m.cellNodes[c][(k + 1) % nn];
      const double x1 = m.nodeX[i1], y1 = m.nodeY[i1];
      const double x2 = m.nodeX[i2], y2 = m.nodeY[i2];
      const double cross = x1 * y2 - x2 * y1;
      a += cross;
      cx += (x1 + x2) * cross;
      cy += (y1 + y2) * cross;
    }
    a *= 0.5;
    if (a <= 0.0)
      throw FatalError("non-positive cell area in cell " + std::to_string(c) +
                       " (check element orientation)");
    m.cellVol[c] = a;
    m.cellCx[c] = cx / (6.0 * a);
    m.cellCy[c] = cy / (6.0 * a);
  }

  int nBoundary = 0, nTagged = 0;
  for (int f = 0; f < nF; ++f) {
    const int i1 = m.faceN1[f], i2 = m.faceN2[f];
    const double dx = m.nodeX[i2] - m.nodeX[i1];
    const double dy = m.nodeY[i2] - m.nodeY[i1];
    const double len = std::hypot(dx, dy);
    if (!(len > 0.0)) throw FatalError("zero-length face detected");
    // Candidate normal (perpendicular to edge).
    double nx = dy / len, ny = -dx / len;
    const double fx = 0.5 * (m.nodeX[i1] + m.nodeX[i2]);
    const double fy = 0.5 * (m.nodeY[i1] + m.nodeY[i2]);
    const int L = m.faceCellL[f];
    const int R = m.faceCellR[f];
    // Orient normal from L toward R (interior) or away from L (boundary).
    double tx, ty;
    if (R >= 0) {
      tx = m.cellCx[R] - m.cellCx[L];
      ty = m.cellCy[R] - m.cellCy[L];
    } else {
      tx = fx - m.cellCx[L];
      ty = fy - m.cellCy[L];
    }
    if (nx * tx + ny * ty < 0.0) {
      nx = -nx;
      ny = -ny;
    }
    m.faceNx[f] = nx;
    m.faceNy[f] = ny;
    m.faceLen[f] = len;
    m.faceCx[f] = fx;
    m.faceCy[f] = fy;

    if (R < 0) {
      ++nBoundary;
      if (m.faceBcFam[f] >= 0) ++nTagged;
    }
  }

  // Every boundary face must carry a family tag from the case BC map, and
  // every family in the case map must tag at least one face.
  if (nTagged != nBoundary) {
    throw FatalError("found " + std::to_string(nBoundary - nTagged) +
                     " untagged boundary faces (mesh BC sections do not match "
                     "the case boundary_conditions map)");
  }
  for (const auto& name : bcFamilyNames) {
    if (m.famId(name) < 0) {
      throw FatalError("boundary family '" + name +
                       "' from case file not present in mesh");
    }
  }
}

}  // namespace cfd2d
