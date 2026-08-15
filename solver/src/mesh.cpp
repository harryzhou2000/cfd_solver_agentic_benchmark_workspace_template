// CGNS mesh reader -> global Cell/Face array. See mesh.hpp for the strategy.
#include "mesh.hpp"
#include <cgnslib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace cfd {

namespace {

int npeOf(ElementType_t t) {
  switch (t) {
    case NODE: return 1;
    case BAR_2: return 2;
    case BAR_3: return 3;
    case TRI_3: return 3;
    case TRI_6: return 6;
    case QUAD_4: return 4;
    case QUAD_8: return 8;
    default: return 0;
  }
}
// number of corner (linear) nodes for higher-order elements
int cornerOf(ElementType_t t) {
  switch (t) {
    case BAR_2: case BAR_3: return 2;
    case TRI_3: case TRI_6: return 3;
    case QUAD_4: case QUAD_8: return 4;
    case NODE: return 1;
    default: return 0;
  }
}
bool isVolumeType(ElementType_t t) {
  return t == TRI_3 || t == TRI_6 || t == QUAD_4 || t == QUAD_8 || t == MIXED;
}

// polygon signed area (shoelace); positive if CCW
double polyArea(const std::vector<Vec2>& v) {
  double a = 0;
  int n = (int)v.size();
  for (int i = 0; i < n; ++i) {
    const Vec2& p = v[i];
    const Vec2& q = v[(i + 1) % n];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5 * a;
}

// Order polygon vertices CCW around their centroid. Some CGNS writers store
// QUAD_4 nodes in a non-perimeter (e.g. structured i,j) order that produces a
// self-intersecting (bowtie) polygon with near-zero signed area and wrong
// edges. Sorting by polar angle around the centroid recovers the true convex
// perimeter, which fixes both the area and the cell-edge topology for any
// input node ordering. Harmless for already-CCW triangles.
void sortCCW(std::vector<Vec2>& v) {
  if (v.size() < 3) return;
  double cx = 0, cy = 0;
  for (const auto& p : v) { cx += p.x; cy += p.y; }
  cx /= v.size(); cy /= v.size();
  std::sort(v.begin(), v.end(), [cx, cy](const Vec2& a, const Vec2& b) {
    double aa = std::atan2(a.y - cy, a.x - cx);
    double ab = std::atan2(b.y - cy, b.x - cx);
    return aa < ab;
  });
}

uint64_t pairKey(int a, int b) {
  if (a > b) std::swap(a, b);
  return (uint64_t(a) << 32) | uint64_t(b);
}

struct PendingEdge {
  int lc;          // global cell id of the owner
  int va, vb;      // zone-local vertex ids (1-based), oriented lc CCW
};

struct InterfaceEdge {
  int zone;
  int lc;          // global cell id
  Vec2 p1, p2, c;
};

}  // namespace

Mesh load_cgns(const std::string& path,
               const std::unordered_map<std::string, BCType>& bc_map) {
  Mesh mesh;
  mesh.mesh_file = path;
  int fn = 0;
  if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK) {
    throw std::runtime_error(std::string("cg_open failed: ") + cg_get_error());
  }
  int nextCellId = 0;
  // family name -> list of interface edges (one per zone side)
  std::unordered_map<std::string, std::vector<InterfaceEdge>> interfaces;

  int nbases = 0; cg_nbases(fn, &nbases);
  for (int b = 1; b <= nbases; ++b) {
    char basename[64]; int celldim = 0, physdim = 0;
    cg_base_read(fn, b, basename, &celldim, &physdim);
    if (celldim != 2)
      throw std::runtime_error("mesh celldim != 2 (only 2-D supported)");
    int nzones = 0; cg_nzones(fn, b, &nzones);
    for (int zi = 0; zi < nzones; ++zi) {
      int z = zi + 1;
      char zonename[64]; cgsize_t sizes[9];
      for (int i = 0; i < 9; ++i) sizes[i] = 0;
      cg_zone_read(fn, b, z, zonename, sizes);
      ZoneType_t zt; cg_zone_type(fn, b, z, &zt);
      if (zt != Unstructured)
        throw std::runtime_error("only unstructured zones are supported");
      int nverts = (int)sizes[0];
      // ---- read coordinates (always to double) ----
      std::vector<double> cx(nverts), cy(nverts);
      int ncoords = 0; cg_ncoords(fn, b, z, &ncoords);
      cgsize_t rmin[3] = {1, 1, 1}, rmax[3] = {nverts, 1, 1};
      for (int c = 1; c <= ncoords; ++c) {
        char cname[64]; DataType_t dt = DataTypeNull;
        cg_coord_info(fn, b, z, c, &dt, cname);
        // read CoordinateX/Y/Z by name match
        if (std::string(cname).find("X") != std::string::npos)
          cg_coord_read(fn, b, z, cname, RealDouble, rmin, rmax, cx.data());
        else if (std::string(cname).find("Y") != std::string::npos)
          cg_coord_read(fn, b, z, cname, RealDouble, rmin, rmax, cy.data());
      }

      // ---- read sections ----
      // Build pair -> section name map from boundary (BAR_*) sections and
      // collect volume-element connectivity for edge hashing.
      std::unordered_map<uint64_t, std::string> pairToSection;
      int nsec = 0; cg_nsections(fn, b, z, &nsec);
      struct VolConn { std::vector<int> corners; };
      std::vector<std::vector<int>> volumeCells;  // zone-local corner vertex ids
      for (int s = 1; s <= nsec; ++s) {
        char secname[64]; ElementType_t et; cgsize_t start = 0, end = 0;
        int nbndry = 0, parent = 0;
        cg_section_read(fn, b, z, s, secname, &et, &start, &end, &nbndry, &parent);
        cgsize_t dataSize = 0;
        cg_ElementDataSize(fn, b, z, s, &dataSize);
        std::vector<cgsize_t> conn(dataSize > 0 ? dataSize : 1, 0);
        if (dataSize > 0)
          cg_elements_read(fn, b, z, s, conn.data(), nullptr);
        const std::string sname = secname;

        if (et == MIXED) {
          // walk element by element; each prefixed by its ElementType_t value
          size_t idx = 0;
          for (cgsize_t e = start; e <= end; ++e) {
            ElementType_t et2 = (ElementType_t)conn[idx++];
            int n = npeOf(et2);
            std::vector<int> corners;
            int cn = cornerOf(et2);
            for (int k = 0; k < n; ++k) {
              int v = (int)conn[idx + k];
              if (k < cn) corners.push_back(v);
            }
            idx += n;
            if (isVolumeType(et2)) volumeCells.push_back(corners);
            else if (et2 == BAR_2 || et2 == BAR_3) {
              int a = corners[0], b2 = corners[1];
              pairToSection[pairKey(a, b2)] = sname;
            }
          }
        } else {
          int n = npeOf(et), cn = cornerOf(et);
          if (isVolumeType(et)) {
            for (cgsize_t e = start; e <= end; ++e) {
              size_t base = size_t(e - start) * (size_t)n;
              std::vector<int> corners;
              for (int k = 0; k < cn; ++k) corners.push_back((int)conn[base + k]);
              volumeCells.push_back(corners);
            }
          } else if (et == BAR_2 || et == BAR_3) {
            for (cgsize_t e = start; e <= end; ++e) {
              size_t base = size_t(e - start) * (size_t)n;
              int a = (int)conn[base], b2 = (int)conn[base + 1];
              pairToSection[pairKey(a, b2)] = sname;
            }
          }
        }
      }

      // ---- build cells + per-zone edge hash ----
      std::unordered_map<uint64_t, PendingEdge> pending;  // first-seen edges
      int zoneCellBase = nextCellId;
      for (const auto& corners : volumeCells) {
        std::vector<Vec2> verts;
        std::vector<int> c = corners;  // mutable vertex-id copy (sorted with verts)
        for (int v : corners) {
          int idx = v - 1;
          if (idx < 0 || idx >= nverts)
            throw std::runtime_error("vertex id out of range in connectivity");
          verts.push_back({cx[idx], cy[idx]});
        }
        // Sort vertices CCW around centroid. Some CGNS writers store QUAD_4
        // nodes in a non-perimeter order (bowtie); this recovers the true
        // convex perimeter so both area and edge topology are correct.
        int nvc = (int)verts.size();
        if (nvc >= 3) {
          double ccx = 0, ccy = 0;
          for (const auto& p : verts) { ccx += p.x; ccy += p.y; }
          ccx /= nvc; ccy /= nvc;
          std::vector<int> perm(nvc);
          for (int i = 0; i < nvc; ++i) perm[i] = i;
          std::sort(perm.begin(), perm.end(), [&](int i, int j) {
            double ai = std::atan2(verts[i].y - ccy, verts[i].x - ccx);
            double aj = std::atan2(verts[j].y - ccy, verts[j].x - ccx);
            return ai < aj;
          });
          std::vector<Vec2> nv(nvc); std::vector<int> nc(nvc);
          for (int i = 0; i < nvc; ++i) { nv[i] = verts[perm[i]]; nc[i] = c[perm[i]]; }
          verts = nv; c = nc;
        }
        double a = polyArea(verts);
        if (nextCellId < 3 || nextCellId == 10752 || nextCellId == 10753)
          std::fprintf(stderr, "[mesh] cell %d nvc=%d area=%.6e firstv=(%.4f,%.4f)\n",
                       nextCellId, nvc, std::fabs(a), verts.front().x, verts.front().y);
        if (a < 0) { std::reverse(verts.begin(), verts.end()); std::reverse(c.begin(), c.end()); }
        a = std::fabs(a);
        Cell cell;
        cell.global_id = nextCellId++;
        cell.area = a;
        cell.verts = verts;
        Vec2 cen{0, 0};
        for (const auto& v : verts) { cen.x += v.x; cen.y += v.y; }
        cell.center = cen * (1.0 / verts.size());
        mesh.cells.push_back(cell);

        // enumerate CCW edges (use the sorted vertex-id list c)
        for (int i = 0; i < nvc; ++i) {
          int j = (i + 1) % nvc;
          int va = c[i], vb = c[j];
          uint64_t key = pairKey(va, vb);
          auto it = pending.find(key);
          if (it == pending.end()) {
            pending[key] = PendingEdge{cell.global_id, va, vb};
          } else {
            // finalize internal face: owner = first-seen (lc), this cell = rc
            PendingEdge pe = it->second;
            pending.erase(it);
            Face f;
            f.lc = pe.lc;
            f.rc = cell.global_id;
            int ia = pe.va - 1, ib = pe.vb - 1;
            f.p1 = {cx[ia], cy[ia]};
            f.p2 = {cx[ib], cy[ib]};
            double dx = f.p2.x - f.p1.x, dy = f.p2.y - f.p1.y;
            f.len = std::sqrt(dx * dx + dy * dy);
            f.Sx = dy; f.Sy = -dx;  // outward from lc -> toward rc
            f.center = {(f.p1.x + f.p2.x) * 0.5, (f.p1.y + f.p2.y) * 0.5};
            f.bctype = BCType::Internal;
            mesh.faces.push_back(f);
            mesh.num_internal_faces++;
          }
        }
      }

      // ---- resolve pending (zone-boundary) edges ----
      for (const auto& kv : pending) {
        const PendingEdge& pe = kv.second;
        int ia = pe.va - 1, ib = pe.vb - 1;
        Vec2 p1{cx[ia], cy[ia]}, p2{cx[ib], cy[ib]};
        double dx = p2.x - p1.x, dy = p2.y - p1.y;
        Vec2 center{(p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5};
        std::string sname;
        auto sit = pairToSection.find(pairKey(pe.va, pe.vb));
        if (sit != pairToSection.end()) sname = sit->second;
        auto bit = bc_map.find(sname);
        if (bit != bc_map.end() && !sname.empty()) {
          // real boundary face
          Face f;
          f.lc = pe.lc;
          f.rc = -1;
          f.p1 = p1; f.p2 = p2;
          f.len = std::sqrt(dx * dx + dy * dy);
          f.Sx = dy; f.Sy = -dx;
          f.center = center;
          f.bctype = bit->second;
          f.family = sname;
          mesh.faces.push_back(f);
          mesh.num_boundary_faces++;
          if (f.bctype == BCType::SlipWall || f.bctype == BCType::NoSlipAdiabaticWall)
            mesh.wall_face_ids.push_back((int)mesh.faces.size() - 1);
          else if (f.bctype == BCType::Farfield)
            mesh.farfield_face_ids.push_back((int)mesh.faces.size() - 1);
        } else {
          // inter-zone interface edge; store for cross-zone stitching
          InterfaceEdge ie{zi, pe.lc, p1, p2, center};
          interfaces[sname].push_back(ie);
        }
      }
    }
  }
  cg_close(fn);

  // ---- stitch inter-zone interfaces by matching edge midpoints ----
  // bounding box for tolerance
  mesh.bbox_min = {1e30, 1e30}; mesh.bbox_max = {-1e30, -1e30};
  for (const auto& c : mesh.cells)
    for (const auto& v : c.verts) {
      mesh.bbox_min.x = std::min(mesh.bbox_min.x, v.x);
      mesh.bbox_min.y = std::min(mesh.bbox_min.y, v.y);
      mesh.bbox_max.x = std::max(mesh.bbox_max.x, v.x);
      mesh.bbox_max.y = std::max(mesh.bbox_max.y, v.y);
    }
  mesh.diag = len(mesh.bbox_max - mesh.bbox_min);
  double tol = 1e-5 * mesh.diag + 1e-7;

  for (auto& kv : interfaces) {
    const std::string& fam = kv.first;
    auto& edges = kv.second;
    // grid-hash by quantized center; each midpoint should pair two zones.
    auto quantize = [tol](const Vec2& c) {
      return std::pair<long long, long long>(
        (long long)std::round(c.x / tol), (long long)std::round(c.y / tol));
    };
    std::unordered_map<long long, std::vector<int>> grid;  // hashed bucket
    auto hsh = [](long long x, long long y) -> long long {
      return (long long)((uint64_t)x * 1000003ULL + (uint64_t)y) & 0x3FFFFFFFFFFFFLL;
    };
    std::unordered_map<long long, std::vector<int>> bucket;
    for (int i = 0; i < (int)edges.size(); ++i) {
      auto q = quantize(edges[i].c);
      bucket[hsh(q.first, q.second)].push_back(i);
    }
    std::vector<char> used(edges.size(), 0);
    for (int i = 0; i < (int)edges.size(); ++i) {
      if (used[i]) continue;
      auto q = quantize(edges[i].c);
      int partner = -1;
      for (long long dx = -1; dx <= 1 && partner < 0; ++dx)
        for (long long dy = -1; dy <= 1 && partner < 0; ++dy) {
          auto it = bucket.find(hsh(q.first + dx, q.second + dy));
          if (it == bucket.end()) continue;
          for (int j : it->second) {
            if (j == i || used[j]) continue;
            if (edges[j].zone == edges[i].zone) continue;
            double dd = len(edges[i].c - edges[j].c);
            if (dd < tol) { partner = j; break; }
          }
        }
      if (partner < 0)
        throw std::runtime_error("failed to match interface edge for family '" + fam + "'");
      used[i] = used[partner] = 1;
      // lc = lower zone index, rc = the other
      int li = i, ri = partner;
      if (edges[ri].zone < edges[li].zone) std::swap(li, ri);
      Face f;
      f.lc = edges[li].lc;
      f.rc = edges[ri].lc;
      f.p1 = edges[li].p1; f.p2 = edges[li].p2;
      double ddx = f.p2.x - f.p1.x, ddy = f.p2.y - f.p1.y;
      f.len = std::sqrt(ddx * ddx + ddy * ddy);
      f.Sx = ddy; f.Sy = -ddx;  // outward from lc(zone lo) -> rc
      f.center = {(f.p1.x + f.p2.x) * 0.5, (f.p1.y + f.p2.y) * 0.5};
      f.bctype = BCType::Internal;
      f.family = fam;
      mesh.faces.push_back(f);
      mesh.num_internal_faces++;
    }
  }

  // sanity: every face has a valid lc
  for (const auto& f : mesh.faces)
    if (f.lc < 0)
      throw std::runtime_error("internal error: face with no owner cell");
  return mesh;
}

}  // namespace cfd
