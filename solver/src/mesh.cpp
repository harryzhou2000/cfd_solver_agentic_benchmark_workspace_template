#include "mesh.hpp"

#include <cgnslib.h>
#include <cgns_io.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace cfd {

namespace {

[[noreturn]] void cgnsFail(const std::string& ctx, int ierr) {
  std::ostringstream os;
  os << "CGNS error in " << ctx << " (code " << ierr << ")";
  const char* msg = cg_get_error();
  if (msg && msg[0] != '\0') os << ": " << msg;
  throw std::runtime_error(os.str());
}

void checkCgns(int ierr, const std::string& ctx) {
  if (ierr != CG_OK) cgnsFail(ctx, ierr);
}

// Disjoint-set (union-find) over (zone, vertex) pairs so that explicit
// 1-to-1 connections can identify coincident vertices across zones.
struct VertexUnion {
  std::vector<int> parent;
  std::vector<int> size;

  void resize(int n) {
    parent.resize(n);
    size.assign(n, 1);
    for (int i = 0; i < n; ++i) parent[i] = i;
  }

  int find(int x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  }

  void unite(int a, int b) {
    int ra = find(a), rb = find(b);
    if (ra == rb) return;
    if (size[ra] < size[rb]) std::swap(ra, rb);
    parent[rb] = ra;
    size[ra] += size[rb];
  }
};

struct BocoInfo {
  std::string name;
  std::string family;
  std::string typeName;
};

std::string familyNameOf(int fn, int base, int zone, int iboco) {
  char fam[CGIO_MAX_NAME_LENGTH + 1] = {0};
  int ierr = cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", iboco, "end");
  if (ierr == CG_OK) ierr = cg_famname_read(fam);
  if (ierr != CG_OK || fam[0] == '\0') return "";
  return std::string(fam);
}

std::string bcTypeName(int type) {
  switch (type) {
    case BCFarfield: return "farfield";
    case BCWallInviscid: return "wall_inviscid";
    case BCWallViscous: return "wall_viscous";
    case BCWall: return "wall";
    case BCSymmetryPlane: return "symmetry_plane";
    case BCInflow: return "inflow";
    case BCOutflow: return "outflow";
    case BCInflowSupersonic: return "inflow_supersonic";
    case BCOutflowSupersonic: return "outflow_supersonic";
    default: return "type_" + std::to_string(type);
  }
}

}  // namespace

GlobalMesh loadCgnsMesh(const std::string& path) {
  int fn = -1;
  checkCgns(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open(" + path + ")");

  int nBases = 0;
  checkCgns(cg_nbases(fn, &nBases), "cg_nbases");
  if (nBases < 1) throw std::runtime_error("CGNS file has no base: " + path);

  GlobalMesh mesh;
  std::unordered_map<std::string, int> zoneIndex;

  // Per-zone raw data (zone-local vertex numbering).
  struct RawZone {
    std::string name;
    std::vector<double> x, y;
    std::vector<std::vector<int>> cells;
    std::vector<GlobalMesh::BoundaryFace> bfaces;
    std::vector<std::pair<int, int>> connPairs;  // local pairs (v_zone, v_donor)
    std::vector<int> donorZone;                  // donor zone per pair
  };
  std::vector<RawZone> zones;
  int totalVertices = 0;

  // Pre-register all zone names so 1-to-1 connections can reference donor
  // zones regardless of file order.
  for (int base = 1; base <= nBases; ++base) {
    int nZones = 0;
    checkCgns(cg_nzones(fn, base, &nZones), "cg_nzones");
    for (int iz = 1; iz <= nZones; ++iz) {
      char zoneName[CGIO_MAX_NAME_LENGTH + 1];
      cgsize_t sizes[3];
      checkCgns(cg_zone_read(fn, base, iz, zoneName, sizes), "cg_zone_read");
      zoneIndex[zoneName] = static_cast<int>(zoneIndex.size());
    }
  }

  for (int base = 1; base <= nBases; ++base) {
    char baseName[CGIO_MAX_NAME_LENGTH + 1];
    int cellDim = 0, physDim = 0;
    checkCgns(cg_base_read(fn, base, baseName, &cellDim, &physDim), "cg_base_read");
    if (cellDim != 2)
      throw std::runtime_error("unsupported cell dimension " + std::to_string(cellDim) +
                               " (2-D unstructured expected)");
    int nZones = 0;
    checkCgns(cg_nzones(fn, base, &nZones), "cg_nzones");

    for (int iz = 1; iz <= nZones; ++iz) {
      char zoneName[CGIO_MAX_NAME_LENGTH + 1];
      cgsize_t sizes[3];
      checkCgns(cg_zone_read(fn, base, iz, zoneName, sizes), "cg_zone_read");
      ZoneType_t zt;
      checkCgns(cg_zone_type(fn, base, iz, &zt), "cg_zone_type");
      if (zt != Unstructured)
        throw std::runtime_error("zone " + std::string(zoneName) +
                                 " is not unstructured (only unstructured zones are supported)");

      const cgsize_t nNodes = sizes[0];
      mesh.zoneNames.push_back(zoneName);
      RawZone z;
      z.name = zoneName;
      z.x.resize(nNodes);
      z.y.resize(nNodes);
      zones.push_back(std::move(z));
      RawZone& rz = zones.back();
      const int zv0 = totalVertices;
      totalVertices += static_cast<int>(nNodes);

      // ---- coordinates ----
      const cgsize_t rmin = 1;
      const cgsize_t rmax = nNodes;
      checkCgns(cg_coord_read(fn, base, iz, "CoordinateX", RealDouble, &rmin, &rmax,
                              rz.x.data()),
                "cg_coord_read X");
      checkCgns(cg_coord_read(fn, base, iz, "CoordinateY", RealDouble, &rmin, &rmax,
                              rz.y.data()),
                "cg_coord_read Y");

      // ---- boco list: name -> family ----
      std::vector<BocoInfo> bocos;
      int nBocos = 0;
      checkCgns(cg_nbocos(fn, base, iz, &nBocos), "cg_nbocos");
      for (int ib = 1; ib <= nBocos; ++ib) {
        char bocoName[CGIO_MAX_NAME_LENGTH + 1];
        BCType_t btype;
        PointSetType_t pstype;
        cgsize_t plen = 0;
        int normalIndex = 0;
        cgsize_t normalListSize = 0;
        DataType_t normalDataType = DataTypeNull;
        int ndataset = 0;
        checkCgns(cg_boco_info(fn, base, iz, ib, bocoName, &btype, &pstype, &plen,
                               &normalIndex, &normalListSize, &normalDataType, &ndataset),
                  "cg_boco_info");
        BocoInfo b;
        b.name = bocoName;
        b.family = familyNameOf(fn, base, iz, ib);
        b.typeName = bcTypeName(static_cast<int>(btype));
        bocos.push_back(std::move(b));
      }
      std::unordered_map<std::string, std::string> bocoFamily;
      for (const auto& b : bocos) bocoFamily[b.name] = b.family;

      // ---- 1-to-1 connectivity ----
      int nconn = 0;
      if (cg_nconns(fn, base, iz, &nconn) == CG_OK) {
        for (int ic = 1; ic <= nconn; ++ic) {
          char connName[CGIO_MAX_NAME_LENGTH + 1], donorName[CGIO_MAX_NAME_LENGTH + 1];
          GridLocation_t loc;
          GridConnectivityType_t ctype;
          PointSetType_t ptype, dptype;
          cgsize_t npnts = 0, ndata = 0;
          ZoneType_t dztype;
          DataType_t ddtype;
          checkCgns(cg_conn_info(fn, base, iz, ic, connName, &loc, &ctype, &ptype, &npnts,
                                 donorName, &dztype, &dptype, &ddtype, &ndata),
                    "cg_conn_info");
          if ((ctype != Abutting && ctype != Abutting1to1) || ptype != PointList) {
            throw std::runtime_error("unsupported grid connectivity in zone " +
                                     std::string(zoneName) + ": only abutting 1-to-1 "
                                     "point-list connections are supported");
          }
          std::vector<cgsize_t> plist(npnts), dlist(ndata);
          checkCgns(cg_conn_read(fn, base, iz, ic, plist.data(), ddtype, dlist.data()),
                    "cg_conn_read");
          auto dit = zoneIndex.find(donorName);
          if (dit == zoneIndex.end())
            throw std::runtime_error("1-to-1 connection references unknown donor zone '" +
                                     std::string(donorName) + "'");
          const int donorZ = dit->second;
          if (ndata != npnts)
            throw std::runtime_error("1-to-1 connection point/donor counts differ");
          for (cgsize_t k = 0; k < npnts; ++k) {
            rz.connPairs.emplace_back(static_cast<int>(plist[k] - 1),
                                      static_cast<int>(dlist[k] - 1));
            rz.donorZone.push_back(donorZ);
          }
          mesh.n1to1 += 1;
        }
      }

      // ---- sections ----
      int nSections = 0;
      checkCgns(cg_nsections(fn, base, iz, &nSections), "cg_nsections");
      for (int is = 1; is <= nSections; ++is) {
        char secName[CGIO_MAX_NAME_LENGTH + 1];
        ElementType_t etype;
        cgsize_t start = 0, end = 0;
        int nbnd = 0, parentFlag = 0;
        checkCgns(cg_section_read(fn, base, iz, is, secName, &etype, &start, &end, &nbnd,
                                  &parentFlag),
                  "cg_section_read");
        const cgsize_t nElem = end - start + 1;
        if (nElem <= 0) continue;
        std::fprintf(stderr, "section %s type %d start %ld end %ld nElem %ld\n", secName,
                     static_cast<int>(etype), static_cast<long>(start), static_cast<long>(end),
                     static_cast<long>(nElem));
        std::fflush(stderr);

        if (etype == TRI_3 || etype == QUAD_4 || etype == MIXED) {
          // ---- volume cells ----
          if (etype == MIXED) {
            std::vector<cgsize_t> raw(nElem * 5);
            checkCgns(cg_elements_read(fn, base, iz, is, raw.data(), nullptr),
                      "cg_elements_read mixed");
            cgsize_t pos = 0;
            for (cgsize_t e = 0; e < nElem; ++e) {
              int et = static_cast<int>(raw[pos++]);
              if (et == TRI_3) {
                rz.cells.push_back({static_cast<int>(raw[pos] - 1),
                                    static_cast<int>(raw[pos + 1] - 1),
                                    static_cast<int>(raw[pos + 2] - 1)});
                pos += 3;
              } else if (et == QUAD_4) {
                rz.cells.push_back({static_cast<int>(raw[pos] - 1),
                                    static_cast<int>(raw[pos + 1] - 1),
                                    static_cast<int>(raw[pos + 2] - 1),
                                    static_cast<int>(raw[pos + 3] - 1)});
                pos += 4;
              } else {
                throw std::runtime_error("zone " + std::string(zoneName) +
                                         " mixed section contains unsupported element type " +
                                         std::to_string(et));
              }
            }
          } else {
            const int npe = (etype == TRI_3) ? 3 : 4;
            std::vector<cgsize_t> raw(nElem * npe);
            checkCgns(cg_elements_read(fn, base, iz, is, raw.data(), nullptr),
                      "cg_elements_read");
            for (cgsize_t e = 0; e < nElem; ++e) {
              std::vector<int> cell(npe);
              for (int k = 0; k < npe; ++k) {
                cgsize_t v = raw[e * npe + k];
                if (v < 1 || v > nNodes) {
                  std::ostringstream os;
                  os << "section '" << secName << "' connectivity out of range: " << v
                     << " (nNodes=" << nNodes << ")";
                  throw std::runtime_error(os.str());
                }
                cell[k] = static_cast<int>(v - 1);
              }
              rz.cells.push_back(std::move(cell));
            }
          }
        } else if (etype == BAR_2) {
          // ---- boundary faces ----
          std::vector<cgsize_t> raw(nElem * 2);
          checkCgns(cg_elements_read(fn, base, iz, is, raw.data(), nullptr),
                    "cg_elements_read bar");
          std::string fam =
              (bocoFamily.count(secName) && !bocoFamily[secName].empty())
                  ? bocoFamily[secName]
                  : std::string(secName);
          int familyId = -1;
          for (size_t k = 0; k < mesh.familyNames.size(); ++k) {
            if (mesh.familyNames[k] == fam) {
              familyId = static_cast<int>(k);
              break;
            }
          }
          if (familyId < 0) {
            familyId = static_cast<int>(mesh.familyNames.size());
            mesh.familyNames.push_back(fam);
          }
          for (cgsize_t e = 0; e < nElem; ++e) {
            rz.bfaces.push_back({static_cast<int>(raw[e * 2] - 1),
                                 static_cast<int>(raw[e * 2 + 1] - 1), familyId,
                                 std::string(secName)});
          }
        }
        // other section types (NGON/NFACE, etc.) are ignored
      }
    }
  }

  checkCgns(cg_close(fn), "cg_close");

  // ---- merge vertices via 1-to-1 connections ----
  VertexUnion uf;
  uf.resize(totalVertices);
  auto zvOffset = [&](int zi) {
    int base = 0;
    for (int p = 0; p < zi; ++p) base += static_cast<int>(zones[p].x.size());
    return base;
  };
  for (size_t zi = 0; zi < zones.size(); ++zi) {
    const auto& z = zones[zi];
    for (size_t k = 0; k < z.connPairs.size(); ++k) {
      int donorZ = z.donorZone[k];
      int localV = z.connPairs[k].first;
      int donorV = z.connPairs[k].second;
      // Zone-local index -> global flat index
      int a = zvOffset(static_cast<int>(zi)) + localV;
      int b = zvOffset(donorZ) + donorV;
      uf.unite(a, b);
    }
  }
  // build global vertex list: one point per equivalence class
  std::vector<int> rootToGlobal(totalVertices, -1);
  std::vector<double> gx, gy, gcount;
  for (int zi = 0; zi < static_cast<int>(zones.size()); ++zi) {
    const auto& z = zones[zi];
    int base = zvOffset(zi);
    for (size_t v = 0; v < z.x.size(); ++v) {
      int r = uf.find(base + static_cast<int>(v));
      if (rootToGlobal[r] < 0) {
        rootToGlobal[r] = static_cast<int>(gx.size());
        gx.push_back(z.x[v]);
        gy.push_back(z.y[v]);
        gcount.push_back(1.0);
      } else {
        int g = rootToGlobal[r];
        gx[g] += z.x[v];
        gy[g] += z.y[v];
        gcount[g] += 1.0;
      }
    }
  }
  mesh.points.resize(gx.size());
  for (size_t g = 0; g < gx.size(); ++g) {
    mesh.points[g] = {gx[g] / gcount[g], gy[g] / gcount[g]};
  }

  // remap cells and boundary faces to global vertex ids
  for (size_t zi = 0; zi < zones.size(); ++zi) {
    const auto& z = zones[zi];
    int base = zvOffset(static_cast<int>(zi));
    for (const auto& cell : z.cells) {
      std::vector<int> gc(cell.size());
      for (size_t k = 0; k < cell.size(); ++k) {
        gc[k] = rootToGlobal[uf.find(base + cell[k])];
      }
      mesh.cells.push_back(gc);
      mesh.cellZoneName.push_back(z.name);
    }
    for (const auto& bf : z.bfaces) {
      GlobalMesh::BoundaryFace gbf;
      gbf.v0 = rootToGlobal[uf.find(base + bf.v0)];
      gbf.v1 = rootToGlobal[uf.find(base + bf.v1)];
      gbf.familyId = bf.familyId;
      gbf.section = bf.section;
      mesh.boundaryFaces.push_back(gbf);
    }
  }

  // ---- cell geometry ----
  const long nCells = static_cast<long>(mesh.cells.size());
  if (nCells == 0) throw std::runtime_error("mesh contains no volume cells: " + path);

  mesh.cellCentroid.resize(nCells);
  mesh.cellVolume.resize(nCells);
  for (long c = 0; c < nCells; ++c) {
    auto& cell = mesh.cells[c];
    const int nv = static_cast<int>(cell.size());
    if (nv < 3) throw std::runtime_error("degenerate cell with fewer than 3 vertices");
    for (int k = 0; k < nv; ++k) {
      if (cell[k] < 0 || cell[k] >= static_cast<int>(mesh.points.size())) {
        std::ostringstream os;
        os << "cell " << c << " vertex " << k << " out of range: " << cell[k]
           << " (npoints=" << mesh.points.size() << ")";
        throw std::runtime_error(os.str());
      }
    }
    double area2 = 0.0;
    for (int k = 0; k < nv; ++k) {
      const Vec2& a = mesh.points[cell[k]];
      const Vec2& b = mesh.points[cell[(k + 1) % nv]];
      area2 += a[0] * b[1] - b[0] * a[1];
    }
    if (area2 == 0.0) throw std::runtime_error("zero-area cell in mesh");
    if (area2 < 0.0) {
      std::reverse(cell.begin(), cell.end());
      area2 = -area2;
    }
    mesh.cellVolume[c] = 0.5 * area2;
    double cx = 0.0, cy = 0.0;
    for (int k = 0; k < nv; ++k) {
      const Vec2& a = mesh.points[cell[k]];
      const Vec2& b = mesh.points[cell[(k + 1) % nv]];
      double cr = a[0] * b[1] - b[0] * a[1];
      cx += (a[0] + b[0]) * cr;
      cy += (a[1] + b[1]) * cr;
    }
    mesh.cellCentroid[c] = {cx / (3.0 * area2), cy / (3.0 * area2)};
  }

  // ---- edge map ----
  const long nPoints = static_cast<long>(mesh.points.size());
  struct EdgeRec {
    int v0 = -1, v1 = -1;
    int c0 = -1, c1 = -1;
  };
  std::unordered_map<long long, EdgeRec> edges;
  edges.reserve(static_cast<size_t>(nCells) * 4);
  auto edgeKey = [nPoints](int a, int b) {
    return static_cast<long long>(std::min(a, b)) * nPoints + std::max(a, b);
  };
  for (long c = 0; c < nCells; ++c) {
    const auto& cell = mesh.cells[c];
    const int nv = static_cast<int>(cell.size());
    for (int k = 0; k < nv; ++k) {
      int a = cell[k], b = cell[(k + 1) % nv];
      if (a == b) throw std::runtime_error("cell has a repeated vertex");
      long long key = edgeKey(a, b);
      auto& rec = edges[key];
      rec.v0 = a;
      rec.v1 = b;
      if (rec.c0 < 0) {
        rec.c0 = static_cast<int>(c);
      } else if (rec.c1 < 0) {
        rec.c1 = static_cast<int>(c);
      } else {
        std::ostringstream os;
        os << "non-manifold edge (" << a << "," << b << ") shared by more than two cells";
        throw std::runtime_error(os.str());
      }
    }
  }

  std::unordered_set<long long> boundaryEdges;
  for (const auto& bf : mesh.boundaryFaces) boundaryEdges.insert(edgeKey(bf.v0, bf.v1));

  // ---- faces ----
  mesh.faces.reserve(edges.size());
  mesh.faceBcType.assign(static_cast<size_t>(edges.size()), -1);
  mesh.cellFaces.assign(nCells, {});
  mesh.cellFaceSign.assign(nCells, {});
  for (const auto& kv : edges) {
    const EdgeRec& rec = kv.second;
    if (rec.c0 < 0) continue;
    GlobalMesh::Face f;
    f.v0 = rec.v0;
    f.v1 = rec.v1;
    f.c0 = rec.c0;
    f.c1 = rec.c1;
    f.familyId = -1;
    const Vec2& p0 = mesh.points[f.v0];
    const Vec2& p1 = mesh.points[f.v1];
    const double dx = p1[0] - p0[0], dy = p1[1] - p0[1];
    f.len = std::sqrt(dx * dx + dy * dy);
    f.centroid = {0.5 * (p0[0] + p1[0]), 0.5 * (p0[1] + p1[1])};
    if (f.c1 < 0) {
      // Boundary face: outward normal from the single adjacent cell (CCW).
      f.normal = {dy / f.len, -dx / f.len};
      const long long key = edgeKey(f.v0, f.v1);
      if (boundaryEdges.find(key) == boundaryEdges.end()) {
        std::ostringstream os;
        os << "boundary edge (" << f.v0 << "," << f.v1
           << ") has no boundary section; mesh may be non-conformal or missing "
              "a boundary definition";
        throw std::runtime_error(os.str());
      }
      for (const auto& bf : mesh.boundaryFaces) {
        if (edgeKey(bf.v0, bf.v1) == key) {
          f.familyId = bf.familyId;
          break;
        }
      }
    } else {
      // Interior face: normal outward from c0 (CCW winding).
      const auto& cell = mesh.cells[rec.c0];
      const int nv = static_cast<int>(cell.size());
      int dir = 0;
      for (int k = 0; k < nv; ++k) {
        if (cell[k] == rec.v0 && cell[(k + 1) % nv] == rec.v1) {
          dir = 1;
          break;
        }
        if (cell[k] == rec.v1 && cell[(k + 1) % nv] == rec.v0) {
          dir = -1;
          break;
        }
      }
      if (dir == 0) throw std::runtime_error("internal face not found in cell edge loop");
      if (dir == 1) {
        f.normal = {dy / f.len, -dx / f.len};
      } else {
        f.normal = {-dy / f.len, dx / f.len};
      }
    }
    const int fid = static_cast<int>(mesh.faces.size());
    mesh.faces.push_back(f);
    mesh.faceBcType[fid] = (f.familyId >= 0) ? f.familyId : -1;
    mesh.cellFaces[rec.c0].push_back(fid);
    mesh.cellFaceSign[rec.c0].push_back(1);
    if (rec.c1 >= 0) {
      mesh.cellFaces[rec.c1].push_back(fid);
      mesh.cellFaceSign[rec.c1].push_back(-1);
    }
  }

  return mesh;
}

void assignBoundaryConditions(GlobalMesh& mesh, const Case& case_def) {
  // Only faces that are actually on the domain boundary (c1 < 0) get a BC.
  // Interface sections (e.g. con-* in multi-zone meshes) match interior edges
  // after vertex merging and are skipped.
  mesh.faceBcType.assign(mesh.faces.size(), -1);
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    auto& f = mesh.faces[fi];
    if (f.familyId < 0) continue;
    const std::string& family = mesh.familyNames[f.familyId];
    BcType type;
    auto it = case_def.bc.families.find(family);
    if (it != case_def.bc.families.end()) {
      type = it->second;
    } else {
      throw std::runtime_error("boundary family '" + family +
                               "' is not mapped in the case file boundary_conditions");
    }
    mesh.faceBcType[fi] = static_cast<int>(type);
  }
}

}  // namespace cfd
