#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <cgnslib.h>

namespace cfd {

namespace {

void cg_ok(int ierr, const char* what) {
  if (ierr != CG_OK)
    throw std::runtime_error(std::string("CGNS error in ") + what + ": " +
                             cg_get_error());
}

struct UF {
  std::vector<int> p;
  explicit UF(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int find(int a) { return p[a] == a ? a : p[a] = find(p[a]); }
  void unite(int a, int b) {
    a = find(a); b = find(b);
    if (a != b) p[std::max(a, b)] = std::min(a, b);
  }
};

}  // namespace

GlobalMesh read_cgns_mesh(const std::string& path) {
  int fn = -1;
  cg_ok(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open");
  int nb = 0;
  cg_ok(cg_nbases(fn, &nb), "cg_nbases");
  if (nb != 1) throw std::runtime_error("expected exactly one CGNS base");
  char basename[256];
  int cell_dim = 0, phys_dim = 0;
  cg_ok(cg_base_read(fn, 1, basename, &cell_dim, &phys_dim), "cg_base_read");
  if (cell_dim != 2) throw std::runtime_error("expected 2-D mesh");

  int nz = 0;
  cg_ok(cg_nzones(fn, 1, &nz), "cg_nzones");

  GlobalMesh gm;
  std::vector<int> zone_node_offset(nz + 1, 0);
  int total_nodes = 0;

  struct ZoneData {
    cgsize_t nverts = 0;
    std::vector<double> x, y;
  };
  std::vector<ZoneData> zones(nz);
  std::map<std::string, int> zone_index;

  for (int z = 1; z <= nz; ++z) {
    char zonename[256];
    cgsize_t sizes[3];
    cg_ok(cg_zone_read(fn, 1, z, zonename, sizes), "cg_zone_read");
    zone_index[zonename] = z;
    ZoneType_t zt;
    cg_ok(cg_zone_type(fn, 1, z, &zt), "cg_zone_type");
    if (zt != Unstructured)
      throw std::runtime_error("only unstructured zones are supported");
    ZoneData zd;
    zd.nverts = sizes[0];
    zd.x.resize(zd.nverts);
    zd.y.resize(zd.nverts);
    int nc = 0;
    cg_ok(cg_ncoords(fn, 1, z, &nc), "cg_ncoords");
    bool have_x = false, have_y = false;
    for (int c = 1; c <= nc; ++c) {
      DataType_t dt;
      char cname[256];
      cg_ok(cg_coord_info(fn, 1, z, c, &dt, cname), "cg_coord_info");
      std::string nm(cname);
      if (nm != "CoordinateX" && nm != "CoordinateY") continue;
      std::vector<double> buf(zd.nverts);
      cgsize_t rmin = 1, rmax = zd.nverts;
      if (dt == RealDouble) {
        cg_ok(cg_coord_read(fn, 1, z, cname, RealDouble, &rmin, &rmax,
                            buf.data()),
              "cg_coord_read");
      } else {
        std::vector<float> fbuf(zd.nverts);
        cg_ok(cg_coord_read(fn, 1, z, cname, RealSingle, &rmin, &rmax,
                            fbuf.data()),
              "cg_coord_read");
        for (cgsize_t i = 0; i < zd.nverts; ++i) buf[i] = fbuf[i];
      }
      if (nm == "CoordinateX") { zd.x = std::move(buf); have_x = true; }
      if (nm == "CoordinateY") { zd.y = std::move(buf); have_y = true; }
    }
    if (!have_x || !have_y)
      throw std::runtime_error("zone missing CoordinateX/Y: " +
                               std::string(zonename));
    zone_node_offset[z] = total_nodes;
    total_nodes += static_cast<int>(zd.nverts);
    zones[z - 1] = std::move(zd);
  }

  UF uf(total_nodes);

  for (int z = 1; z <= nz; ++z) {
    int nconns = 0;
    cg_ok(cg_nconns(fn, 1, z, &nconns), "cg_nconns");
    for (int i = 1; i <= nconns; ++i) {
      char cname[256], donor[256];
      GridLocation_t loc;
      GridConnectivityType_t ct;
      PointSetType_t ptset, dptset;
      ZoneType_t dzt;
      DataType_t ddt;
      cgsize_t npts = 0, ndonor = 0;
      cg_ok(cg_conn_info(fn, 1, z, i, cname, &loc, &ct, &ptset, &npts, donor,
                         &dzt, &dptset, &ddt, &ndonor),
            "cg_conn_info");
      if (ct != Abutting1to1) {
        fprintf(stderr, "warning: skipping non-1to1 connectivity %s\n", cname);
        continue;
      }
      auto zit = zone_index.find(std::string(donor));
      if (zit == zone_index.end())
        throw std::runtime_error("unknown donor zone: " + std::string(donor));
      int dz = zit->second;
      if (npts != ndonor)
        throw std::runtime_error("1-to-1 point count mismatch in " +
                                 std::string(cname));
      std::vector<cgsize_t> pnts(npts), dpnts(ndonor);
      cg_ok(cg_conn_read(fn, 1, z, i, pnts.data(), ddt, dpnts.data()),
            "cg_conn_read");
      double maxdist2 = 0.0;
      for (cgsize_t k = 0; k < npts; ++k) {
        cgsize_t a = pnts[k], b = dpnts[k];
        if (a < 1 || a > zones[z - 1].nverts || b < 1 ||
            b > zones[dz - 1].nverts)
          throw std::runtime_error("1-to-1 node index out of range in " +
                                   std::string(cname));
        double dx = zones[z - 1].x[a - 1] - zones[dz - 1].x[b - 1];
        double dy = zones[z - 1].y[a - 1] - zones[dz - 1].y[b - 1];
        maxdist2 = std::max(maxdist2, dx * dx + dy * dy);
        uf.unite(zone_node_offset[z] + static_cast<int>(a) - 1,
                 zone_node_offset[dz] + static_cast<int>(b) - 1);
      }
      if (maxdist2 > 1e-16) {
        fprintf(stderr,
                "warning: 1-to-1 connection %s stitched nodes up to %g apart\n",
                cname, std::sqrt(maxdist2));
      }
    }
  }

  {
    std::map<int, int> root_to_new;
    std::vector<double> nx, ny;
    std::vector<int> old_to_new(total_nodes, -1);
    for (int z = 1; z <= nz; ++z) {
      for (cgsize_t i = 0; i < zones[z - 1].nverts; ++i) {
        int g = zone_node_offset[z] + static_cast<int>(i);
        int root = uf.find(g);
        auto it = root_to_new.find(root);
        if (it == root_to_new.end()) {
          int nid = static_cast<int>(nx.size());
          root_to_new[root] = nid;
          nx.push_back(zones[z - 1].x[i]);
          ny.push_back(zones[z - 1].y[i]);
          it = root_to_new.find(root);
        }
        old_to_new[g] = it->second;
      }
    }
    gm.node_x = std::move(nx);
    gm.node_y = std::move(ny);
    gm.num_nodes = static_cast<int>(gm.node_x.size());

    std::map<std::string, int> fam_id;
    for (int z = 1; z <= nz; ++z) {
      int ns = 0;
      cg_ok(cg_nsections(fn, 1, z, &ns), "cg_nsections");
      for (int s = 1; s <= ns; ++s) {
        char sname[256];
        ElementType_t et;
        cgsize_t istart, iend;
        int nbndry, pflag;
        cg_ok(cg_section_read(fn, 1, z, s, sname, &et, &istart, &iend, &nbndry,
                              &pflag),
              "cg_section_read");
        cgsize_t esize = 0;
        cg_ok(cg_ElementDataSize(fn, 1, z, s, &esize), "cg_ElementDataSize");
        std::vector<cgsize_t> conn(esize);
        cg_ok(cg_elements_read(fn, 1, z, s, conn.data(), nullptr),
              "cg_elements_read");
        cgsize_t nelem = iend - istart + 1;
        if (et == TRI_3 || et == QUAD_4) {
          int nv = et == TRI_3 ? 3 : 4;
          for (cgsize_t e = 0; e < nelem; ++e) {
            std::array<int32_t, 4> cn{0, 0, 0, 0};
            for (int v = 0; v < nv; ++v) {
              int g = zone_node_offset[z] +
                      static_cast<int>(conn[e * nv + v]) - 1;
              cn[v] = old_to_new[g];
            }
            gm.cell_nodes.push_back(cn);
            gm.cell_nverts.push_back(static_cast<int8_t>(nv));
          }
        } else if (et == BAR_2) {
          std::string fam(sname);
          auto it = fam_id.find(fam);
          if (it == fam_id.end()) {
            int id = static_cast<int>(gm.family_names.size());
            fam_id[fam] = id;
            gm.family_names.push_back(fam);
            it = fam_id.find(fam);
          }
          for (cgsize_t e = 0; e < nelem; ++e) {
            GlobalMesh::BndFace bf;
            bf.n0 = old_to_new[zone_node_offset[z] +
                               static_cast<int>(conn[e * 2]) - 1];
            bf.n1 = old_to_new[zone_node_offset[z] +
                               static_cast<int>(conn[e * 2 + 1]) - 1];
            bf.family_id = it->second;
            gm.bnd_faces.push_back(bf);
          }
        }
      }
    }
    gm.num_cells = static_cast<int>(gm.cell_nodes.size());
  }

  cg_ok(cg_close(fn), "cg_close");
  return gm;
}

void compute_cell_geometry(GlobalMesh& m) {
  m.cell_cx.resize(m.num_cells);
  m.cell_cy.resize(m.num_cells);
  m.cell_vol.resize(m.num_cells);
  for (int c = 0; c < m.num_cells; ++c) {
    int nv = m.cell_nverts[c];
    double area = 0.0, cx = 0.0, cy = 0.0;
    for (int v = 0; v < nv; ++v) {
      int a = m.cell_nodes[c][v];
      int b = m.cell_nodes[c][(v + 1) % nv];
      double x0 = m.node_x[a], y0 = m.node_y[a];
      double x1 = m.node_x[b], y1 = m.node_y[b];
      double w = x0 * y1 - x1 * y0;
      area += w;
      cx += (x0 + x1) * w;
      cy += (y0 + y1) * w;
    }
    area *= 0.5;
    if (area < 0.0) {
      area = -area;
      cx = -cx;
      cy = -cy;
    }
    m.cell_vol[c] = area;
    m.cell_cx[c] = cx / (6.0 * area);
    m.cell_cy[c] = cy / (6.0 * area);
  }
}

}  // namespace cfd
