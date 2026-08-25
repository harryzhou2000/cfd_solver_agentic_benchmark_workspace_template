#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include <cgnslib.h>

namespace cfd {

namespace {

[[noreturn]] void cg_fail(const std::string& what, const std::string& path) {
  throw std::runtime_error("CGNS error in " + path + " at " + what + ": " +
                           cg_get_error());
}

struct UF {
  std::vector<int> p;
  explicit UF(int n) : p(n) { for (int i = 0; i < n; ++i) p[i] = i; }
  int find(int i) { return p[i] == i ? i : p[i] = find(p[i]); }
  void unite(int a, int b) { p[find(a)] = find(b); }
};

inline long long face_key(int a, int b) {
  if (a > b) std::swap(a, b);
  return (static_cast<long long>(a) << 32) | static_cast<unsigned int>(b);
}

double polygon_signed_area(const std::vector<double>& x, const std::vector<double>& y,
                           const int* nodes, int nn) {
  double s = 0.0;
  for (int i = 0; i < nn; ++i) {
    int j = (i + 1) % nn;
    s += x[nodes[i]] * y[nodes[j]] - x[nodes[j]] * y[nodes[i]];
  }
  return 0.5 * s;
}

}  // namespace

int GlobalMesh::family_index(const std::string& name) const {
  for (size_t i = 0; i < bc_family_names.size(); ++i)
    if (bc_family_names[i] == name) return static_cast<int>(i);
  return -1;
}

GlobalMesh read_cgns_mesh(const std::string& path) {
  int fn = 0;
  if (cg_open(path.c_str(), CG_MODE_READ, &fn)) cg_fail("cg_open", path);

  int nbases = 0;
  if (cg_nbases(fn, &nbases)) cg_fail("cg_nbases", path);
  if (nbases != 1) throw std::runtime_error(path + ": expected exactly one CGNS base");

  int cell_dim = 0, phys_dim = 0;
  char base_name[256];
  if (cg_base_read(fn, 1, base_name, &cell_dim, &phys_dim)) cg_fail("cg_base_read", path);
  if (cell_dim != 2) throw std::runtime_error(path + ": CellDimension must be 2");

  int nzones = 0;
  if (cg_nzones(fn, 1, &nzones)) cg_fail("cg_nzones", path);

  struct ZoneData {
    std::string name;
    int node_offset = 0;
    int n_nodes = 0;
    std::vector<double> x, y;
    std::vector<std::array<int, 4>> cells;
    std::vector<int> cell_nn;
    struct Bar { int n0, n1; cgsize_t elem_id; };
    std::vector<Bar> bars;
    std::map<cgsize_t, std::string> elem_family;  // boundary element id -> family
  };

  std::vector<ZoneData> zones(nzones);
  int total_nodes = 0;

  for (int Z = 1; Z <= nzones; ++Z) {
    ZoneType_t zt;
    if (cg_zone_type(fn, 1, Z, &zt)) cg_fail("cg_zone_type", path);
    if (zt != CGNS_ENUMV(Unstructured))
      throw std::runtime_error(path + ": only unstructured zones are supported");
    cgsize_t zsize[3];
    char zname[256];
    if (cg_zone_read(fn, 1, Z, zname, zsize)) cg_fail("cg_zone_read", path);
    ZoneData& zd = zones[Z - 1];
    zd.name = zname;
    zd.n_nodes = static_cast<int>(zsize[0]);
    zd.node_offset = total_nodes;
    total_nodes += zd.n_nodes;

    auto read_coord = [&](const char* cname, std::vector<double>& dst) {
      dst.resize(zd.n_nodes);
      cgsize_t rmin = 1, rmax = zd.n_nodes;
      if (cg_coord_read(fn, 1, Z, cname, CGNS_ENUMV(RealDouble), &rmin, &rmax,
                        dst.data()))
        cg_fail(std::string("cg_coord_read ") + cname, path);
    };
    read_coord("CoordinateX", zd.x);
    read_coord("CoordinateY", zd.y);

    int nsections = 0;
    if (cg_nsections(fn, 1, Z, &nsections)) cg_fail("cg_nsections", path);
    for (int S = 1; S <= nsections; ++S) {
      char sname[256];
      ElementType_t etype;
      cgsize_t start = 0, end = 0;
      int nbndry = 0, pflag = 0;
      if (cg_section_read(fn, 1, Z, S, sname, &etype, &start, &end, &nbndry, &pflag))
        cg_fail("cg_section_read", path);
      int npe = 0;
      if (cg_npe(etype, &npe)) cg_fail("cg_npe", path);
      cgsize_t nelem = end - start + 1;
      std::vector<cgsize_t> conn(static_cast<size_t>(nelem) * npe);
      if (cg_elements_read(fn, 1, Z, S, conn.data(), nullptr))
        cg_fail("cg_elements_read", path);

      if (etype == CGNS_ENUMV(TRI_3) || etype == CGNS_ENUMV(QUAD_4)) {
        for (cgsize_t e = 0; e < nelem; ++e) {
          std::array<int, 4> cn{-1, -1, -1, -1};
          for (int k = 0; k < npe; ++k)
            cn[k] = zd.node_offset + static_cast<int>(conn[e * npe + k]) - 1;
          zd.cells.push_back(cn);
          zd.cell_nn.push_back(npe);
        }
      } else if (etype == CGNS_ENUMV(BAR_2)) {
        for (cgsize_t e = 0; e < nelem; ++e) {
          ZoneData::Bar b;
          b.n0 = zd.node_offset + static_cast<int>(conn[e * 2 + 0]) - 1;
          b.n1 = zd.node_offset + static_cast<int>(conn[e * 2 + 1]) - 1;
          b.elem_id = start + e;
          zd.bars.push_back(b);
        }
      }
    }

    int nbocos = 0;
    if (cg_nbocos(fn, 1, Z, &nbocos)) cg_fail("cg_nbocos", path);
    for (int I = 1; I <= nbocos; ++I) {
      char bname[256];
      BCType_t btype;
      PointSetType_t ptype;
      cgsize_t npts = 0;
      int norm_idx[2] = {0, 0};
      cgsize_t norm_list = 0;
      DataType_t norm_dt;
      int ndataset = 0;
      if (cg_boco_info(fn, 1, Z, I, bname, &btype, &ptype, &npts, norm_idx, &norm_list,
                       &norm_dt, &ndataset))
        cg_fail("cg_boco_info", path);
      GridLocation_t gloc = CGNS_ENUMV(Vertex);
      if (cg_boco_gridlocation_read(fn, 1, Z, I, &gloc))
        cg_fail("cg_boco_gridlocation_read", path);
      if (gloc != CGNS_ENUMV(EdgeCenter) && gloc != CGNS_ENUMV(FaceCenter))
        throw std::runtime_error(std::string(path) + ": BC " + bname +
                                 " must use EdgeCenter location");
      if (ptype != CGNS_ENUMV(PointRange) || npts != 2)
        throw std::runtime_error(std::string(path) + ": BC " + bname +
                                 " must use PointRange over boundary elements");
      cgsize_t range[2];
      if (cg_boco_read(fn, 1, Z, I, range, nullptr)) cg_fail("cg_boco_read", path);
      std::string fam = bname;
      for (cgsize_t e = range[0]; e <= range[1]; ++e) zd.elem_family[e] = fam;
    }
  }

  // ---- Merge conformal zone interfaces geometrically ---------------------
  GlobalMesh m;
  {
    std::vector<double> ax(total_nodes), ay(total_nodes);
    for (const auto& zd : zones)
      for (int i = 0; i < zd.n_nodes; ++i) {
        ax[zd.node_offset + i] = zd.x[i];
        ay[zd.node_offset + i] = zd.y[i];
      }
    double xmin = *std::min_element(ax.begin(), ax.end());
    double xmax = *std::max_element(ax.begin(), ax.end());
    double ymin = *std::min_element(ay.begin(), ay.end());
    double ymax = *std::max_element(ay.begin(), ay.end());
    double diag = std::hypot(xmax - xmin, ymax - ymin);
    const double tol = 1e-10 * std::max(diag, 1.0);

    UF uf(total_nodes);
    std::unordered_map<long long, std::vector<int>> grid;
    auto cell_of = [&](double v) { return static_cast<long long>(std::llround(v / tol)); };
    auto key2 = [&](long long i, long long j) {
      return i * 2000003LL + j;
    };
    for (int i = 0; i < total_nodes; ++i) {
      long long ci = cell_of(ax[i]), cj = cell_of(ay[i]);
      bool merged = false;
      for (long long di = -1; di <= 1 && !merged; ++di)
        for (long long dj = -1; dj <= 1 && !merged; ++dj) {
          auto it = grid.find(key2(ci + di, cj + dj));
          if (it == grid.end()) continue;
          for (int j : it->second) {
            if (std::hypot(ax[i] - ax[j], ay[i] - ay[j]) < tol) {
              uf.unite(i, j);
              merged = true;
              break;
            }
          }
        }
      grid[key2(ci, cj)].push_back(i);
    }

    std::map<int, int> canon;
    std::vector<int> remap(total_nodes);
    for (int i = 0; i < total_nodes; ++i) {
      int r = uf.find(i);
      auto it = canon.find(r);
      if (it == canon.end()) {
        int nid = static_cast<int>(canon.size());
        canon[r] = nid;
        m.node_x.push_back(ax[i]);
        m.node_y.push_back(ay[i]);
        remap[i] = nid;
      } else {
        remap[i] = it->second;
      }
    }
    m.n_nodes = static_cast<int>(m.node_x.size());

    for (size_t zi = 0; zi < zones.size(); ++zi) {
      const ZoneData& zd = zones[zi];
      for (size_t ci = 0; ci < zd.cells.size(); ++ci) {
        std::array<int, 4> cn = zd.cells[ci];
        int nn = zd.cell_nn[ci];
        for (int k = 0; k < nn; ++k) cn[k] = remap[cn[k]];
        if (polygon_signed_area(m.node_x, m.node_y, cn.data(), nn) < 0.0) {
          std::reverse(cn.begin(), cn.begin() + nn);
        }
        m.cell_nodes.push_back(cn);
        m.cell_nnodes.push_back(nn);
        m.cell_zone.push_back(static_cast<int>(zi));
      }
    }
    m.n_cells = static_cast<int>(m.cell_nodes.size());

    struct FaceTmp {
      int cell0 = -1, n0 = -1, n1 = -1;
      int cell1 = -1;
      int family = -2;  // -2 unassigned, -1 interface, >=0 family
    };
    std::unordered_map<long long, int> fmap;
    std::vector<FaceTmp> faces;
    faces.reserve(m.n_cells * 4);
    for (int c = 0; c < m.n_cells; ++c) {
      int nn = m.cell_nnodes[c];
      for (int e = 0; e < nn; ++e) {
        int a = m.cell_nodes[c][e];
        int b = m.cell_nodes[c][(e + 1) % nn];
        long long k = face_key(a, b);
        auto it = fmap.find(k);
        if (it == fmap.end()) {
          FaceTmp ft;
          ft.cell0 = c; ft.n0 = a; ft.n1 = b;
          fmap[k] = static_cast<int>(faces.size());
          faces.push_back(ft);
        } else {
          faces[it->second].cell1 = c;
        }
      }
    }

    for (const auto& zd : zones) {
      for (const auto& b : zd.bars) {
        int a = remap[b.n0], c = remap[b.n1];
        auto it = fmap.find(face_key(a, c));
        if (it == fmap.end())
          throw std::runtime_error(path + ": boundary element references unknown edge");
        FaceTmp& ft = faces[it->second];
        auto fam_it = zd.elem_family.find(b.elem_id);
        if (fam_it != zd.elem_family.end()) {
          const std::string& fname = fam_it->second;
          int fi = m.family_index(fname);
          if (fi < 0) {
            m.bc_family_names.push_back(fname);
            fi = static_cast<int>(m.bc_family_names.size()) - 1;
          }
          if (ft.family >= 0 && ft.family != fi)
            throw std::runtime_error(path + ": boundary edge assigned two families");
          ft.family = fi;
        } else {
          if (ft.cell1 < 0)
            throw std::runtime_error(
                path + ": unreferenced interface edge ended up on the domain boundary");
          ft.family = -1;
        }
      }
    }

    for (const auto& ft : faces) {
      if (ft.cell1 >= 0) {
        m.face_cell_l.push_back(ft.cell0);
        m.face_cell_r.push_back(ft.cell1);
        m.face_n0.push_back(ft.n0);
        m.face_n1.push_back(ft.n1);
        m.face_bc_family.push_back(-1);
      }
    }
    m.n_faces_internal = static_cast<int>(m.face_cell_l.size());
    for (const auto& ft : faces) {
      if (ft.cell1 < 0) {
        if (ft.family < 0)
          throw std::runtime_error(path + ": boundary edge has no boundary family");
        m.face_cell_l.push_back(ft.cell0);
        m.face_cell_r.push_back(-1);
        m.face_n0.push_back(ft.n0);
        m.face_n1.push_back(ft.n1);
        m.face_bc_family.push_back(ft.family);
      }
    }
    m.n_faces_boundary = static_cast<int>(m.face_cell_l.size()) - m.n_faces_internal;
  }

  cg_close(fn);
  build_geometry(m);
  return m;
}

void build_geometry(GlobalMesh& m) {
  const int nc = m.n_cells;
  m.cell_cx.resize(nc);
  m.cell_cy.resize(nc);
  m.cell_vol.resize(nc);
  for (int c = 0; c < nc; ++c) {
    int nn = m.cell_nnodes[c];
    const int* nd = m.cell_nodes[c].data();
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < nn; ++i) {
      int j = (i + 1) % nn;
      double x0 = m.node_x[nd[i]], y0 = m.node_y[nd[i]];
      double x1 = m.node_x[nd[j]], y1 = m.node_y[nd[j]];
      double cross = x0 * y1 - x1 * y0;
      a += cross;
      cx += (x0 + x1) * cross;
      cy += (y0 + y1) * cross;
    }
    a *= 0.5;
    if (a <= 0.0)
      throw std::runtime_error("non-positive cell volume encountered (cell " +
                               std::to_string(c) + ")");
    m.cell_vol[c] = a;
    m.cell_cx[c] = cx / (6.0 * a);
    m.cell_cy[c] = cy / (6.0 * a);
  }

  const int nf = static_cast<int>(m.face_cell_l.size());
  m.face_nx.resize(nf);
  m.face_ny.resize(nf);
  m.face_area.resize(nf);
  m.face_cx.resize(nf);
  m.face_cy.resize(nf);
  for (int f = 0; f < nf; ++f) {
    int cl = m.face_cell_l[f];
    int nn = m.cell_nnodes[cl];
    int a = m.face_n0[f], b = m.face_n1[f];
    bool ab = false;
    for (int e = 0; e < nn; ++e) {
      int p = m.cell_nodes[cl][e], q = m.cell_nodes[cl][(e + 1) % nn];
      if ((p == a && q == b)) { ab = true; break; }
      if ((p == b && q == a)) { ab = false; break; }
    }
    double xa = m.node_x[a], ya = m.node_y[a];
    double xb = m.node_x[b], yb = m.node_y[b];
    double ex = xb - xa, ey = yb - ya;
    double len = std::hypot(ex, ey);
    if (len <= 0.0) throw std::runtime_error("zero-length face encountered");
    double nx = ey / len, ny = -ex / len;
    if (!ab) { nx = -nx; ny = -ny; }
    m.face_nx[f] = nx;
    m.face_ny[f] = ny;
    m.face_area[f] = len;
    m.face_cx[f] = 0.5 * (xa + xb);
    m.face_cy[f] = 0.5 * (ya + yb);
  }

  std::vector<std::vector<int>> nbr(nc);
  for (int f = 0; f < m.n_faces_internal; ++f) {
    int l = m.face_cell_l[f], r = m.face_cell_r[f];
    nbr[l].push_back(r);
    nbr[r].push_back(l);
  }
  m.adj_start.assign(nc + 1, 0);
  for (int c = 0; c < nc; ++c) m.adj_start[c + 1] = m.adj_start[c] + static_cast<int>(nbr[c].size());
  m.adj_list.resize(m.adj_start[nc]);
  for (int c = 0; c < nc; ++c) {
    int off = m.adj_start[c];
    for (int v : nbr[c]) m.adj_list[off++] = v;
  }
}

std::string mesh_summary(const GlobalMesh& m) {
  char buf[1024];
  std::string out;
  std::snprintf(buf, sizeof(buf), "nodes=%d cells=%d internal_faces=%d boundary_faces=%d",
                m.n_nodes, m.n_cells, m.n_faces_internal, m.n_faces_boundary);
  out += buf;
  for (size_t i = 0; i < m.bc_family_names.size(); ++i) {
    int cnt = 0;
    for (int f = m.n_faces_internal; f < m.n_faces_internal + m.n_faces_boundary; ++f)
      if (m.face_bc_family[f] == static_cast<int>(i)) ++cnt;
    std::snprintf(buf, sizeof(buf), "\n  family[%zu] %s: %d faces", i,
                  m.bc_family_names[i].c_str(), cnt);
    out += buf;
  }
  return out;
}

}  // namespace cfd
