#include "mesh.h"

#include <cgnslib.h>
#include <unordered_map>

namespace cfd {
namespace {

struct EdgeRec {
  int a, b;        // directed endpoints (a->b) as traversed by the cell
  int cell;        // owning cell
  int section;     // boundary section index (or -1 for interior candidates)
  std::string family;
  BCType bc = BCType::Interior;
};

double polygon_centroid_x(const std::vector<Vec2>& p) {
  double a = 0.0, cx = 0.0;
  int n = (int)p.size();
  for (int i = 0; i < n; ++i) {
    const Vec2& p0 = p[i];
    const Vec2& p1 = p[(i + 1) % n];
    double d = p0.x * p1.y - p1.x * p0.y;
    a += d;
    cx += (p0.x + p1.x) * d;
  }
  return cx / (3.0 * a);
}

double polygon_centroid_y(const std::vector<Vec2>& p) {
  double a = 0.0, cy = 0.0;
  int n = (int)p.size();
  for (int i = 0; i < n; ++i) {
    const Vec2& p0 = p[i];
    const Vec2& p1 = p[(i + 1) % n];
    double d = p0.x * p1.y - p1.x * p0.y;
    a += d;
    cy += (p0.y + p1.y) * d;
  }
  return cy / (3.0 * a);
}

double polygon_area(const std::vector<Vec2>& p) {
  double a = 0.0;
  int n = (int)p.size();
  for (int i = 0; i < n; ++i) {
    const Vec2& p0 = p[i];
    const Vec2& p1 = p[(i + 1) % n];
    a += p0.x * p1.y - p1.x * p0.y;
  }
  return 0.5 * a;
}

int64_t qcoord(double x) { return llround(x * 1.0e9); }

}  // namespace

const char* bc_type_name(BCType t) {
  switch (t) {
    case BCType::Farfield: return "farfield";
    case BCType::SlipWall: return "slip_wall";
    case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
    case BCType::Interface: return "interface";
    default: return "interior";
  }
}

bool read_mesh(const std::string& cgns_path,
               const std::map<std::string, std::string>& bc_map,
               Mesh& mesh, std::string& err) {
  int fn = -1;
  if (cg_open(cgns_path.c_str(), CG_MODE_READ, &fn) != CG_OK) {
    err = "cannot open CGNS mesh: " + cgns_path + " (" + cg_get_error() + ")";
    return false;
  }

  int nbases = 0;
  cg_nbases(fn, &nbases);
  if (nbases < 1) {
    cg_close(fn);
    err = "CGNS file has no base node: " + cgns_path;
    return false;
  }
  const int base = 1;
  char basename[64];
  int celldim = 0, physdim = 0;
  cg_base_read(fn, base, basename, &celldim, &physdim);

  int nzones = 0;
  cg_nzones(fn, base, &nzones);
  if (nzones < 1) {
    cg_close(fn);
    err = "CGNS base has no zones: " + cgns_path;
    return false;
  }

  int node_offset = 0;
  // per-zone cell ranges so cells keep a stable global ordering.
  struct BcEdge {
    int a, b;
    std::string family;
    BCType bc;
  };
  std::vector<std::vector<EdgeRec>> zone_edges(nzones);
  std::vector<std::vector<BcEdge>> zone_bc_edges(nzones);

  for (int iz = 1; iz <= nzones; ++iz) {
    char zname[64];
    cgsize_t sizes[3] = {0, 0, 0};
    if (cg_zone_read(fn, base, iz, zname, sizes) != CG_OK) {
      cg_close(fn);
      err = std::string("cg_zone_read failed for zone ") + std::to_string(iz);
      return false;
    }
    cgsize_t nNodes = sizes[0];
    if (nNodes <= 0) {
      cg_close(fn);
      err = std::string("zone ") + zname + " has no nodes";
      return false;
    }
    std::vector<double> x(nNodes), y(nNodes);
    cgsize_t rmin[1] = {1}, rmax[1] = {nNodes};
    if (cg_coord_read(fn, base, iz, "CoordinateX", RealDouble, rmin, rmax, x.data()) != CG_OK ||
        cg_coord_read(fn, base, iz, "CoordinateY", RealDouble, rmin, rmax, y.data()) != CG_OK) {
      cg_close(fn);
      err = std::string("cannot read coordinates for zone ") + zname;
      return false;
    }
    for (cgsize_t i = 0; i < nNodes; ++i) {
      mesh.nodes.push_back(Vec2{x[i], y[i]});
    }

    int nsections = 0;
    cg_nsections(fn, base, iz, &nsections);
    for (int is = 1; is <= nsections; ++is) {
      char sname[64];
      ElementType_t etype;
      cgsize_t estart = 0, eend = 0;
      int nbndry = 0, parent_flag = 0;
      if (cg_section_read(fn, base, iz, is, sname, &etype, &estart, &eend, &nbndry, &parent_flag) != CG_OK) {
        cg_close(fn);
        err = std::string("cg_section_read failed in zone ") + zname;
        return false;
      }
      cgsize_t count = eend - estart + 1;
      cgsize_t data_size = 0;
      cg_ElementDataSize(fn, base, iz, is, &data_size);
      if (data_size <= 0) continue;
      std::vector<cgsize_t> conn(data_size);
      if (cg_elements_read(fn, base, iz, is, conn.data(), nullptr) != CG_OK) {
        cg_close(fn);
        err = std::string("cg_elements_read failed for section ") + sname;
        return false;
      }

      if (etype == TRI_3 || etype == QUAD_4) {
        int npe = (etype == TRI_3) ? 3 : 4;
        for (cgsize_t e = 0; e < count; ++e) {
          Cell c;
          c.nverts = npe;
          for (int k = 0; k < npe; ++k) {
            cgsize_t n = conn[e * npe + k];
            if (n < 1 || n > nNodes) {
              cg_close(fn);
              err = "cell connectivity out of range in " + std::string(sname);
              return false;
            }
            c.nodes[k] = node_offset + (int)(n - 1);
          }
          mesh.cells.push_back(c);
        }
      } else if (etype == BAR_2) {
        // Boundary section: edges.
        char family[128] = "";
        if (cg_goto(fn, base, "Zone_t", iz, "Elements_t", is, "end") == CG_OK) {
          cg_famname_read(family);
        }
        std::string fam = family[0] ? std::string(family) : std::string(sname);
        auto it = bc_map.find(fam);
        BCType bc = BCType::Interface;
        if (it != bc_map.end()) {
          const std::string& t = it->second;
          if (t == "farfield") bc = BCType::Farfield;
          else if (t == "slip_wall") bc = BCType::SlipWall;
          else if (t == "no_slip_adiabatic_wall") bc = BCType::NoSlipAdiabaticWall;
          else {
            cg_close(fn);
            err = "unsupported boundary condition type '" + t + "' for family '" + fam + "'";
            return false;
          }
        }
        for (cgsize_t e = 0; e < count; ++e) {
          int a = node_offset + (int)(conn[2 * e] - 1);
          int b = node_offset + (int)(conn[2 * e + 1] - 1);
          zone_bc_edges[iz - 1].push_back(BcEdge{a, b, fam, bc});
        }
      } else {
        cg_close(fn);
        err = "unsupported element type " + std::string(ElementTypeName[etype]) +
              " in section " + std::string(sname) +
              " (expected TRI_3/QUAD_4 cells and BAR_2 boundary edges)";
        return false;
      }
    }
    node_offset += (int)nNodes;
  }
  cg_close(fn);

  if (mesh.cells.empty()) {
    err = "mesh contains no cells";
    return false;
  }
  mesh.num_cells_global = (int)mesh.cells.size();
  mesh.num_nodes_global = (int)mesh.nodes.size();

  // --- Cell geometry; normalize node order to counter-clockwise ------------
  for (Cell& c : mesh.cells) {
    std::vector<Vec2> pts(c.nverts);
    for (int k = 0; k < c.nverts; ++k) {
      const Vec2& n = mesh.nodes[c.nodes[k]];
      pts[k] = Vec2{n.x, n.y};
    }
    double area = polygon_area(pts);
    if (area < 0.0) {
      std::reverse(c.nodes.begin(), c.nodes.begin() + c.nverts);
      std::reverse(pts.begin(), pts.end());
      area = -area;
    }
    if (area <= 0.0) {
      err = "degenerate cell (zero area) in mesh";
      return false;
    }
    c.vol = area;
    c.cx = polygon_centroid_x(pts);
    c.cy = polygon_centroid_y(pts);
  }

  // --- Build edges ----------------------------------------------------------
  struct EdgeKey {
    int lo, hi;
    bool operator==(const EdgeKey& o) const { return lo == o.lo && hi == o.hi; }
    bool operator<(const EdgeKey& o) const {
      return lo != o.lo ? lo < o.lo : hi < o.hi;
    }
  };
  struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
      return (size_t)k.lo * 2654435761u ^ (size_t)k.hi;
    }
  };
  std::unordered_map<EdgeKey, std::vector<EdgeRec>, EdgeKeyHash> edges;
  for (int ci = 0; ci < (int)mesh.cells.size(); ++ci) {
    const Cell& c = mesh.cells[ci];
    for (int k = 0; k < c.nverts; ++k) {
      int a = c.nodes[k];
      int b = c.nodes[(k + 1) % c.nverts];
      EdgeKey key{std::min(a, b), std::max(a, b)};
      edges[key].push_back(EdgeRec{a, b, ci, -1, "", BCType::Interior});
    }
  }

  // --- Assemble faces --------------------------------------------------------
  auto add_face = [&](int ca, int cb, double nx, double ny, double len,
                      BCType bc, int bface) {
    int fid = (int)mesh.faces.size();
    mesh.faces.push_back(Face{ca, cb, nx, ny, len, bc, bface});
    mesh.cell_faces[ca].push_back(fid);
    mesh.cell_face_sign[ca].push_back(+1);
    if (cb >= 0) {
      mesh.cell_faces[cb].push_back(fid);
      mesh.cell_face_sign[cb].push_back(-1);
    }
    return fid;
  };

  mesh.cell_faces.resize(mesh.cells.size());
  mesh.cell_face_sign.resize(mesh.cells.size());

  for (auto& kv : edges) {
    auto& recs = kv.second;
    if (recs.size() == 2) {
      const EdgeRec& r0 = recs[0];
      const EdgeRec& r1 = recs[1];
      int ca = r0.cell, cb = r1.cell;
      // Directed edge from r0's traversal; n0 = (dy,-dx) is outward from r0's cell.
      const Vec2& pa = mesh.nodes[r0.a];
      const Vec2& pb = mesh.nodes[r0.b];
      double dx = pb.x - pa.x, dy = pb.y - pa.y;
      double len = std::sqrt(dx * dx + dy * dy);
      double nx = dy / len, ny = -dx / len;
      // Orient from ca to cb.
      double ddot = nx * (mesh.cells[cb].cx - mesh.cells[ca].cx) +
                    ny * (mesh.cells[cb].cy - mesh.cells[ca].cy);
      if (ddot < 0.0) { nx = -nx; ny = -ny; }
      add_face(ca, cb, nx * len, ny * len, len, BCType::Interior, -1);
    } else if (recs.size() == 1) {
      // Boundary edge (physical BC or candidate interface); assigned below.
    } else {
      err = "non-manifold edge (shared by more than two cells) in mesh";
      return false;
    }
  }

  // Boundary sections carry the BC assignment (section -> family -> type).
  // Attach each section edge's BC to the matching single-cell edge record.
  {
    std::unordered_map<EdgeKey, EdgeRec*, EdgeKeyHash> edge_rec;
    for (auto& kv : edges) {
      if (kv.second.size() == 1) edge_rec[kv.first] = &kv.second[0];
    }
    for (int z = 0; z < nzones; ++z) {
      for (auto& be : zone_bc_edges[z]) {
        EdgeKey key{std::min(be.a, be.b), std::max(be.a, be.b)};
        auto fit = edge_rec.find(key);
        if (fit == edge_rec.end()) {
          err = "boundary section edge not found in cell adjacency (family " + be.family + ")";
          return false;
        }
        fit->second->bc = be.bc;
        fit->second->family = be.family;
      }
    }
  }

  // Now create boundary faces from edges with a single cell, and match
  // interface (unmapped) edges across zones by coordinates.
  std::map<std::array<int64_t, 4>, int> iface_map;  // quantized endpoint coords -> edge idx
  int niface = 0;

  for (auto& kv : edges) {
    auto& recs = kv.second;
    if (recs.size() != 1) continue;
    const EdgeRec& r = recs[0];
    int cell = r.cell;
    const Vec2& pa = mesh.nodes[r.a];
    const Vec2& pb = mesh.nodes[r.b];
    double dx = pb.x - pa.x, dy = pb.y - pa.y;
    double len = std::sqrt(dx * dx + dy * dy);
    double nx = dy / len, ny = -dx / len;  // outward from cell (CCW traversal)
    double xm = 0.5 * (pa.x + pb.x), ym = 0.5 * (pa.y + pb.y);
    // Ensure outward from the owning cell (defensive).
    double ddot = nx * (xm - mesh.cells[cell].cx) + ny * (ym - mesh.cells[cell].cy);
    if (ddot < 0.0) { nx = -nx; ny = -ny; }

    if (r.bc != BCType::Interface) {
      int fid = add_face(cell, -1, nx * len, ny * len, len, r.bc, -1);
      int bfid = (int)mesh.boundary_faces.size();
      mesh.faces[fid].bface = bfid;
      mesh.boundary_faces.push_back(BoundaryFace{fid, cell, r.bc, r.family, xm, ym});
    } else {
      std::array<int64_t, 4> key{
          qcoord(std::min(pa.x, pb.x)), qcoord(std::min(pa.y, pb.y)),
          qcoord(std::max(pa.x, pb.x)), qcoord(std::max(pa.y, pb.y))};
      iface_map[key] = niface++;
    }
  }

  // Match interface edges pairwise.
  {
    // Re-iterate edges; find partner via coordinate key.
    std::vector<int> partner(niface, -1);
    int ii = 0;
    for (auto& kv : edges) {
      auto& recs = kv.second;
      if (recs.size() == 1 && recs[0].bc == BCType::Interface) {
        const EdgeRec& r = recs[0];
        const Vec2& pa = mesh.nodes[r.a];
        const Vec2& pb = mesh.nodes[r.b];
        std::array<int64_t, 4> key{
            qcoord(std::min(pa.x, pb.x)), qcoord(std::min(pa.y, pb.y)),
            qcoord(std::max(pa.x, pb.x)), qcoord(std::max(pa.y, pb.y))};
        auto it = iface_map.find(key);
        if (it != iface_map.end() && it->second != ii) {
          partner[ii] = it->second;
        }
        ++ii;
      }
    }

    // Create internal faces for matched pairs.
    std::vector<int> used(niface, 0);
    int matched = 0;
    for (int i = 0; i < niface; ++i) {
      if (used[i] || partner[i] < 0) continue;
      int j = partner[i];
      if (used[j]) continue;
      used[i] = used[j] = 1;
      // Recover the two edge records.
      const EdgeRec *ri = nullptr, *rj = nullptr;
      int ii2 = 0;
      for (auto& kv : edges) {
        if (kv.second.size() == 1 && kv.second[0].bc == BCType::Interface) {
          if (ii2 == i) ri = &kv.second[0];
          if (ii2 == j) rj = &kv.second[0];
          ++ii2;
        }
      }
      if (!ri || !rj) { err = "internal error matching interface edges"; return false; }
      int ca = ri->cell, cb = rj->cell;
      const Vec2& pa = mesh.nodes[ri->a];
      const Vec2& pb = mesh.nodes[ri->b];
      double dx = pb.x - pa.x, dy = pb.y - pa.y;
      double len = std::sqrt(dx * dx + dy * dy);
      double nx = dy / len, ny = -dx / len;
      double ddot = nx * (mesh.cells[cb].cx - mesh.cells[ca].cx) +
                    ny * (mesh.cells[cb].cy - mesh.cells[ca].cy);
      if (ddot < 0.0) { nx = -nx; ny = -ny; }
      add_face(ca, cb, nx * len, ny * len, len, BCType::Interior, -1);
      ++matched;
    }
    for (int i = 0; i < niface; ++i) {
      if (!used[i]) {
        err = "unmatched 1-to-1 interface edge in mesh; cannot determine boundary condition";
        return false;
      }
    }
    g_log.logf("mesh: matched %d interface edge pairs across zones\n", matched);
  }

  mesh.num_faces_global = (int)mesh.faces.size();
  g_log.logf("mesh: %d nodes, %d cells, %d faces, %d boundary faces\n",
             mesh.num_nodes_global, mesh.num_cells_global, mesh.num_faces_global,
             (int)mesh.boundary_faces.size());
  // Sanity: report degenerate (zero-length) faces.
  int nzero = 0;
  for (const Face& f : mesh.faces) {
    if (f.len < 1.0e-14) {
      if (nzero < 5) {
        g_log.logf("mesh: zero-length face between cells %d and %d\n", f.ca, f.cb);
      }
      ++nzero;
    }
  }
  if (nzero) g_log.logf("mesh: WARNING %d zero-length faces\n", nzero);
  return true;
}

}  // namespace cfd
