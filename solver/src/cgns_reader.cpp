#include "cgns_reader.h"
#include <cgnslib.h>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <vector>
#include <map>

namespace cfd2d {

static constexpr double COORD_TOL = 1e-9;

struct CoordKey {
  int64_t ix, iy;
  bool operator<(const CoordKey& o) const {
    if (ix != o.ix) return ix < o.ix;
    return iy < o.iy;
  }
  bool operator==(const CoordKey& o) const {
    return ix == o.ix && iy == o.iy;
  }
};

static CoordKey makeKey(double x, double y) {
  return { (int64_t)std::round(x / COORD_TOL), (int64_t)std::round(y / COORD_TOL) };
}

bool readCGNSMesh(const std::string& filename, const CaseInput& ci,
                  Mesh& mesh, std::string& err) {
  int fn;
  if (cg_open(filename.c_str(), CG_MODE_READ, &fn) != CG_OK) {
    err = "cg_open failed for " + filename;
    return false;
  }

  int nbases;
  cg_nbases(fn, &nbases);
  if (nbases < 1) { err = "no bases in CGNS file"; cg_close(fn); return false; }

  std::map<CoordKey, int> coordMap;
  mesh.x.clear(); mesh.y.clear();

  int B = 1;
  int nzones;
  cg_nzones(fn, B, &nzones);
  if (nzones < 1) { err = "no zones"; cg_close(fn); return false; }

  for (int Z = 1; Z <= nzones; ++Z) {
    char zoneName[33];
    cgsize_t sizes[9];
    cg_zone_read(fn, B, Z, zoneName, sizes);
    int nverts = (int)sizes[0];

    std::vector<double> zx(nverts), zy(nverts);
    cgsize_t rmin = 1, rmax = nverts;
    if (cg_coord_read(fn, B, Z, "CoordinateX", RealDouble, &rmin, &rmax, zx.data()) != CG_OK) {
      err = "failed reading CoordinateX"; cg_close(fn); return false;
    }
    if (cg_coord_read(fn, B, Z, "CoordinateY", RealDouble, &rmin, &rmax, zy.data()) != CG_OK) {
      err = "failed reading CoordinateY"; cg_close(fn); return false;
    }

    std::vector<int> localToGlobal(nverts + 1, 0);
    for (int v = 0; v < nverts; ++v) {
      double x = zx[v], y = zy[v];
      CoordKey key = makeKey(x, y);
      auto it = coordMap.find(key);
      if (it != coordMap.end()) {
        localToGlobal[v + 1] = it->second;
      } else {
        int gid = (int)mesh.x.size() + 1;
        mesh.x.push_back(x);
        mesh.y.push_back(y);
        coordMap[key] = gid;
        localToGlobal[v + 1] = gid;
      }
    }

    // Read element sections
    int nsecs;
    cg_nsections(fn, B, Z, &nsecs);
    for (int S = 1; S <= nsecs; ++S) {
      char secName[33];
      ElementType_t etype;
      cgsize_t start, end;
      int nbndry, parentflag;
      cg_section_read(fn, B, Z, S, secName, &etype, &start, &end, &nbndry, &parentflag);

      int npe;
      if (cg_npe(etype, &npe) != CG_OK) continue;
      if (npe < 0) continue;

      int nconn = (end - start + 1) * npe;
      std::vector<cgsize_t> conn(nconn);
      if (cg_elements_read(fn, B, Z, S, conn.data(), nullptr) != CG_OK) {
        err = "failed reading elements"; cg_close(fn); return false;
      }

      std::string secNameStr(secName);
      bool isBoundary = (etype == BAR_2);

      if (isBoundary) {
        // Determine family name from ZoneBC boco entries
        std::string resolvedFam = secNameStr;
        int nbc;
        cg_nbocos(fn, B, Z, &nbc);
        for (int ibc = 1; ibc <= nbc; ++ibc) {
          char bocoName[33];
          BCType_t bctype;
          PointSetType_t pstype;
          cgsize_t npnts;
          int normalIndex[3];
          cgsize_t normalListSize;
          DataType_t normalDataType;
          int ndataset;
          cg_boco_info(fn, B, Z, ibc, bocoName, &bctype, &pstype, &npnts,
                       normalIndex, &normalListSize, &normalDataType, &ndataset);
          // Read the point range/list
          std::vector<cgsize_t> pnts(npnts * 2);
          cg_boco_read(fn, B, Z, ibc, pnts.data(), nullptr);
          // Check if this boco's range overlaps our section
          if (pstype == PointRange && npnts == 2) {
            cgsize_t r0 = pnts[0], r1 = pnts[1];
            if (r0 > r1) std::swap(r0, r1);
            if (r0 >= start && r1 <= end) {
              resolvedFam = bocoName;
              break;
            }
          } else if (pstype == PointList) {
            // Check if any point is in our section range
            for (cgsize_t p = 0; p < npnts; ++p) {
              if (pnts[p] >= start && pnts[p] <= end) {
                resolvedFam = bocoName;
                break;
              }
            }
          }
        }

        bool hasBC = ci.boundaryConditions.count(resolvedFam) > 0;
        if (hasBC) {
          for (cgsize_t e = 0; e < (end - start + 1); ++e) {
            BoundaryFace bf;
            bf.family = resolvedFam;
            bf.nodes.push_back((int)conn[e * 2]);
            bf.nodes.push_back((int)conn[e * 2 + 1]);
            for (auto& n : bf.nodes) n = localToGlobal[n];
            mesh.bfaces.push_back(bf);
          }
        }
      } else {
        if (etype == TRI_3 || etype == QUAD_4) {
          for (cgsize_t e = 0; e < (end - start + 1); ++e) {
            std::vector<int> nodes(npe);
            for (int k = 0; k < npe; ++k)
              nodes[k] = localToGlobal[(int)conn[e * npe + k]];
            if (getenv("CFD2D_DEBUG") && e < 3) {
              std::cerr << "  cell " << mesh.cellNodes.size() << " raw:";
              for (int k = 0; k < npe; ++k) std::cerr << " " << conn[e*npe+k];
              std::cerr << " mapped:";
              for (int k = 0; k < npe; ++k) std::cerr << " " << nodes[k];
              std::cerr << " zone=" << Z << " sec=" << secNameStr << "\n";
            }
            mesh.cellNodes.push_back(nodes);
          }
        }
      }
    }
  }

  cg_close(fn);

  if (mesh.cellNodes.empty()) {
    err = "no volume cells read from " + filename;
    return false;
  }
  return true;
}

} // namespace cfd2d
