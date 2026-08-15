// CGNS mesh reader for 2-D unstructured multi-zone meshes.
//
// Reads all zones, merges coincident vertices across zones (so cross-zone
// abutting interfaces become interior faces), builds the cell/face adjacency,
// and classifies surviving boundary faces through the case BC map.
#include "mesh.hpp"

#include <cgnslib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace cfd {

namespace {

struct CoordKey {
  long long x, y;
  bool operator==(const CoordKey& o) const { return x == o.x && y == o.y; }
};
struct CoordKeyHash {
  size_t operator()(const CoordKey& k) const {
    return (size_t)k.x * 1000003u + (size_t)k.y;
  }
};
constexpr double kMergeScale = 1.0e7;  // ~1e-7 absolute resolution
inline CoordKey keyOf(double x, double y) {
  return CoordKey{static_cast<long long>(std::llround(x * kMergeScale)),
                  static_cast<long long>(std::llround(y * kMergeScale))};
}

struct BarRecord {
  int v0, v1;       // sorted global vertex pair
  int secId;        // index into Mesh::bc_section_names
};

// Polygon signed area + centroid (shoelace).
void polyGeom(const std::vector<double>& vx, const std::vector<double>& vy,
              const std::vector<int>& verts, double& signedArea, double& cx, double& cy) {
  double a = 0.0, ax = 0.0, ay = 0.0;
  int n = (int)verts.size();
  for (int i = 0; i < n; ++i) {
    int j = (i + 1) % n;
    double xi = vx[verts[i]], yi = vy[verts[i]];
    double xj = vx[verts[j]], yj = vy[verts[j]];
    double cr = xi * yj - xj * yi;
    a += cr;
    ax += (xi + xj) * cr;
    ay += (yi + yj) * cr;
  }
  signedArea = 0.5 * a;
  if (std::fabs(signedArea) > 1e-30) {
    cx = ax / (6.0 * signedArea);
    cy = ay / (6.0 * signedArea);
  } else {
    cx = cy = 0.0;
    for (int v : verts) { cx += vx[v]; cy += vy[v]; }
    cx /= n; cy /= n;
  }
}

}  // namespace

Mesh readCGNSMesh(const std::string& file, const CaseConfig& cfg) {
  int fn;
  if (cg_open(file.c_str(), CG_MODE_READ, &fn))
    throw std::runtime_error("cg_open failed for " + file);

  Mesh m;
  std::unordered_map<CoordKey, int, CoordKeyHash> vmap;
  std::vector<BarRecord> barRecords;

  auto bcForFamily = [&](const std::string& fam) -> BCType {
    for (const auto& bc : cfg.bcs)
      if (bc.first == fam) return bc.second;
    return BCType::None;
  };

  int nbases;
  cg_nbases(fn, &nbases);
  for (int b = 1; b <= nbases; ++b) {
    char bname[64]; int celldim, physdim;
    cg_base_read(fn, b, bname, &celldim, &physdim);
    if (celldim != 2)
      throw std::runtime_error("only 2-D meshes supported (celldim=2)");
    int nzones;
    cg_nzones(fn, b, &nzones);
    for (int z = 1; z <= nzones; ++z) {
      char zname[64]; cgsize_t zsize[9];
      cg_zone_read(fn, b, z, zname, zsize);
      int nvertsZone = (int)zsize[0];
      cgsize_t rmin[3] = {1, 1, 1}, rmax[3] = {1, 1, 1};
      rmax[0] = nvertsZone;
      std::vector<double> X(nvertsZone), Y(nvertsZone);
      int ncoord;
      cg_ncoords(fn, b, z, &ncoord);
      if (ncoord < 2) throw std::runtime_error("mesh needs X and Y coordinates");
      char cname[64]; DataType_t dt;
      cg_coord_info(fn, b, z, 1, &dt, cname);
      if (cg_coord_read(fn, b, z, cname, RealDouble, rmin, rmax, X.data()))
        throw std::runtime_error("failed reading CoordinateX");
      cg_coord_info(fn, b, z, 2, &dt, cname);
      if (cg_coord_read(fn, b, z, cname, RealDouble, rmin, rmax, Y.data()))
        throw std::runtime_error("failed reading CoordinateY");
      std::vector<int> localGlobal(nvertsZone, -1);
      for (int i = 0; i < nvertsZone; ++i) {
        CoordKey k = keyOf(X[i], Y[i]);
        auto it = vmap.find(k);
        if (it != vmap.end()) {
          localGlobal[i] = it->second;
        } else {
          int g = (int)m.vx.size();
          m.vx.push_back(X[i]);
          m.vy.push_back(Y[i]);
          vmap[k] = g;
          localGlobal[i] = g;
        }
      }

      int nsec;
      cg_nsections(fn, b, z, &nsec);
      for (int s = 1; s <= nsec; ++s) {
        char sname[64]; ElementType_t et; cgsize_t start, end; int nbnd, parentflag;
        cg_section_read(fn, b, z, s, sname, &et, &start, &end, &nbnd, &parentflag);
        int count = (int)(end - start + 1);
        if (et == TRI_3 || et == QUAD_4) {
          int nvpe = (et == TRI_3) ? 3 : 4;
          std::vector<cgsize_t> conn((size_t)nvpe * count);
          if (cg_elements_read(fn, b, z, s, conn.data(), nullptr))
            throw std::runtime_error(std::string("failed reading section ") + sname);
          for (int e = 0; e < count; ++e) {
            m.cellOffset.push_back((int)m.cellVerts.size());
            m.cellNv.push_back(nvpe);
            for (int v = 0; v < nvpe; ++v) {
              int lv = (int)conn[(size_t)e * nvpe + v] - 1;
              if (lv < 0 || lv >= nvertsZone)
                throw std::runtime_error("cell vertex out of range");
              m.cellVerts.push_back(localGlobal[lv]);
            }
          }
        } else if (et == BAR_2) {
          std::vector<cgsize_t> conn((size_t)2 * count);
          if (cg_elements_read(fn, b, z, s, conn.data(), nullptr))
            throw std::runtime_error(std::string("failed reading section ") + sname);
          int secId = (int)m.bc_section_names.size();
          m.bc_section_names.push_back(sname);
          for (int e = 0; e < count; ++e) {
            int a = (int)conn[(size_t)e * 2] - 1;
            int c = (int)conn[(size_t)e * 2 + 1] - 1;
            int ga = localGlobal[a], gc = localGlobal[c];
            barRecords.push_back({std::min(ga, gc), std::max(ga, gc), secId});
          }
        }
        // other element types (NODE, MIXED, ...) are ignored.
      }
    }
  }
  cg_close(fn);

  m.cellOffset.push_back((int)m.cellVerts.size());
  m.ncell = (int)m.cellOffset.size() - 1;
  m.nvert = (int)m.vx.size();

  // ---- Build faces from cell edges, deduping by sorted vertex pair. ----
  struct FaceHalf { int cell, v0, v1; };
  std::unordered_map<long long, int> faceByPair;
  auto pairKey = [](int a, int b) -> long long {
    // a < b guaranteed by caller
    return ((long long)a << 32) | (long long)(unsigned int)b;
  };
  // Resize face arrays lazily via vectors; we push as we discover.
  auto addNewFace = [&](int cell, int a, int b) -> int {
    int id = (int)m.faceL.size();
    m.faceL.push_back(cell);
    m.faceR.push_back(-1);
    m.faceV0.push_back(a);
    m.faceV1.push_back(b);
    m.faceNx.push_back(0); m.faceNy.push_back(0);
    m.faceLen.push_back(0);
    m.faceCx.push_back(0); m.faceCy.push_back(0);
    m.faceBC.push_back(BoundaryFaceInfo{});
    faceByPair[pairKey(a, b)] = id;
    return id;
  };

  for (int c = 0; c < m.ncell; ++c) {
    int off = m.cellOffset[c];
    int nv = m.cellNv[c];
    for (int i = 0; i < nv; ++i) {
      int j = (i + 1) % nv;
      int va = m.cellVerts[off + i];
      int vb = m.cellVerts[off + j];
      int a = std::min(va, vb), b = std::max(va, vb);
      long long key = pairKey(a, b);
      auto it = faceByPair.find(key);
      if (it == faceByPair.end()) {
        addNewFace(c, a, b);
      } else {
        // Second cell sharing this face.
        int fid = it->second;
        if (m.faceR[fid] >= 0) {
          // Already shared by two cells -- should not happen for a valid 2-D
          // mesh.  Treat as a non-manifold edge and skip silently.
          continue;
        }
        m.faceR[fid] = c;
      }
    }
  }

  m.nface = (int)m.faceL.size();

  // ---- Classify boundary faces via the BAR records. ----
  // Build map sorted-pair -> section id (last writer wins; pairs unique).
  std::unordered_map<long long, int> barByPair;
  for (const auto& br : barRecords)
    barByPair[pairKey(br.v0, br.v1)] = br.secId;

  int unresolvedBoundary = 0, interfaceBoundary = 0;
  for (int f = 0; f < m.nface; ++f) {
    if (m.faceR[f] >= 0) continue;  // interior
    long long key = pairKey(m.faceV0[f], m.faceV1[f]);
    auto it = barByPair.find(key);
    if (it == barByPair.end()) {
      ++unresolvedBoundary;
      m.faceBC[f].type = BCType::None;
      m.faceBC[f].section_id = -1;
      continue;
    }
    int secId = it->second;
    const std::string& name = m.bc_section_names[secId];
    m.faceBC[f].section_id = secId;
    m.faceBC[f].family = name;
    BCType t = bcForFamily(name);
    if (t == BCType::None) {
      // con-* (grid connectivity) section remaining as boundary means the
      // cross-zone vertex merge failed for this interface edge.
      ++interfaceBoundary;
      m.faceBC[f].type = BCType::InteriorInterface;  // will be reported as error
    } else {
      m.faceBC[f].type = t;
    }
  }
  if (unresolvedBoundary > 0 || interfaceBoundary > 0) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "mesh topology error: %d unresolved boundary faces, %d unmerged interface faces",
                  unresolvedBoundary, interfaceBoundary);
    throw std::runtime_error(buf);
  }

  // ---- Cell geometry. ----
  m.cellCx.assign(m.ncell, 0.0);
  m.cellCy.assign(m.ncell, 0.0);
  m.cellVol.assign(m.ncell, 0.0);
  for (int c = 0; c < m.ncell; ++c) {
    int off = m.cellOffset[c];
    int nv = m.cellNv[c];
    std::vector<int> verts(m.cellVerts.begin() + off,
                           m.cellVerts.begin() + off + nv);
    double sa, cx, cy;
    polyGeom(m.vx, m.vy, verts, sa, cx, cy);
    m.cellCx[c] = cx;
    m.cellCy[c] = cy;
    m.cellVol[c] = std::fabs(sa);
  }

  // ---- Face geometry: outward normal of L cell, length, midpoint. ----
  for (int f = 0; f < m.nface; ++f) {
    int v0 = m.faceV0[f], v1 = m.faceV1[f];
    double x0 = m.vx[v0], y0 = m.vy[v0];
    double x1 = m.vx[v1], y1 = m.vy[v1];
    double ex = x1 - x0, ey = y1 - y0;
    double len = std::sqrt(ex * ex + ey * ey);
    m.faceLen[f] = len;
    m.faceCx[f] = 0.5 * (x0 + x0 + ex);
    m.faceCy[f] = 0.5 * (y0 + y0 + ey);
    // Two candidate unit normals: (ey,-ex)/len and (-ey,ex)/len.
    double n1x = ey / len, n1y = -ex / len;
    // Pick the one pointing from L centroid toward the face midpoint (outward
    // from L).
    int lc = m.faceL[f];
    double dx = m.faceCx[f] - m.cellCx[lc];
    double dy = m.faceCy[f] - m.cellCy[lc];
    double dot = n1x * dx + n1y * dy;
    if (dot >= 0.0) {
      m.faceNx[f] = n1x;
      m.faceNy[f] = n1y;
    } else {
      m.faceNx[f] = -n1x;
      m.faceNy[f] = -n1y;
    }
  }

  // ---- Cell -> face adjacency. ----
  m.cellFaceOffset.assign(m.ncell + 1, 0);
  // Count faces per cell first.
  std::vector<int> cellFaceCount(m.ncell, 0);
  for (int f = 0; f < m.nface; ++f) {
    cellFaceCount[m.faceL[f]]++;
    if (m.faceR[f] >= 0) cellFaceCount[m.faceR[f]]++;
  }
  for (int c = 0; c < m.ncell; ++c)
    m.cellFaceOffset[c + 1] = m.cellFaceOffset[c] + cellFaceCount[c];
  m.cellFaces.assign(m.cellFaceOffset[m.ncell], -1);
  std::vector<int> cursor(m.ncell, 0);
  for (int f = 0; f < m.nface; ++f) {
    int lc = m.faceL[f];
    int pos = m.cellFaceOffset[lc] + cursor[lc]++;
    m.cellFaces[pos] = f;
    if (m.faceR[f] >= 0) {
      int rc = m.faceR[f];
      int posr = m.cellFaceOffset[rc] + cursor[rc]++;
      m.cellFaces[posr] = f;
    }
  }

  return m;
}

}  // namespace cfd
