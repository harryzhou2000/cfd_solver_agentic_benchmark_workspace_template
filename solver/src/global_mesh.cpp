#include "global_mesh.hpp"
#include <cgnslib.h>
#include <map>
#include <unordered_map>
#include <set>

namespace fv {

static void cgOk(int err, const string& what) {
  if (err != CG_OK) die("CGNS error in " + what + ": " + string(cg_get_error()));
}

namespace {
struct UnionFind {
  vector<int> p;
  int add() { p.push_back((int)p.size()); return (int)p.size() - 1; }
  int find(int a) { while (p[a] != a) { p[a] = p[p[a]]; a = p[a]; } return a; }
  void unite(int a, int b) { p[find(a)] = find(b); }
};
}  // namespace

GlobalMesh readCgnsMesh(const string& path) {
  int fn;
  cgOk(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open " + path);

  GlobalMesh mesh;
  UnionFind uf;                    // over concatenated (zone, local node) ids
  vector<long> zoneNodeBase;       // base index into uf per zone
  vector<vector<double>> zx, zy;   // per-zone coordinates
  struct CellRec { array<int,4> n; int nn; };
  struct BarRec { int n0, n1; string family; };
  vector<CellRec> cells;
  vector<BarRec> bars;

  int nbases;
  cgOk(cg_nbases(fn, &nbases), "cg_nbases");
  check(nbases >= 1, "no bases in " + path);
  int b = 1;
  char bname[128];
  int celldim, physdim;
  cgOk(cg_base_read(fn, b, bname, &celldim, &physdim), "cg_base_read");
  check(celldim == 2, "cell dimension must be 2 (got " + std::to_string(celldim) + ")");

  int nzones;
  cgOk(cg_nzones(fn, b, &nzones), "cg_nzones");
  // first pass: node counts per zone so union-find ids exist for all zones
  // (1-to-1 connections may reference zones in either direction)
  {
    long base = 0;
    for (int z = 1; z <= nzones; ++z) {
      char zname[128];
      cgsize_t size[9];
      cgOk(cg_zone_read(fn, b, z, zname, size), "cg_zone_read");
      zoneNodeBase.push_back(base);
      for (long i = 0; i < (long)size[0]; ++i) uf.add();
      base += (long)size[0];
    }
  }
  for (int z = 1; z <= nzones; ++z) {
    ZoneType_t zt;
    cgOk(cg_zone_type(fn, b, z, &zt), "cg_zone_type");
    check(zt == Unstructured, "only unstructured zones supported");
    char zname[128];
    cgsize_t size[9];
    cgOk(cg_zone_read(fn, b, z, zname, size), "cg_zone_read");
    long nnodes = (long)size[0];
    // coordinates
    vector<double> cx(nnodes), cy(nnodes);
    cgsize_t rmin = 1, rmax = nnodes;
    cgOk(cg_coord_read(fn, b, z, "CoordinateX", RealDouble, &rmin, &rmax, cx.data()), "read X");
    cgOk(cg_coord_read(fn, b, z, "CoordinateY", RealDouble, &rmin, &rmax, cy.data()), "read Y");
    zx.push_back(std::move(cx));
    zy.push_back(std::move(cy));

    // element sections
    int nsections;
    cgOk(cg_nsections(fn, b, z, &nsections), "cg_nsections");
    for (int s = 1; s <= nsections; ++s) {
      char sname[128];
      ElementType_t et;
      cgsize_t estart, eend;
      int nbdry, pflag;
      cgOk(cg_section_read(fn, b, z, s, sname, &et, &estart, &eend, &nbdry, &pflag), "cg_section_read");
      long nelem = (long)(eend - estart + 1);
      long base = zoneNodeBase[(size_t)z - 1];
      if (et == TRI_3 || et == QUAD_4) {
        int npe = (et == TRI_3) ? 3 : 4;
        vector<cgsize_t> conn((size_t)nelem * npe);
        cgOk(cg_elements_read(fn, b, z, s, conn.data(), nullptr), "cg_elements_read");
        for (long e = 0; e < nelem; ++e) {
          CellRec cr;
          cr.nn = npe;
          for (int k = 0; k < 4; ++k)
            cr.n[k] = (k < npe) ? (int)(base + conn[e * npe + k] - 1) : -1;
          cells.push_back(cr);
        }
      } else if (et == BAR_2) {
        vector<cgsize_t> conn((size_t)nelem * 2);
        cgOk(cg_elements_read(fn, b, z, s, conn.data(), nullptr), "cg_elements_read");
        for (long e = 0; e < nelem; ++e) {
          BarRec br;
          br.n0 = (int)(base + conn[e * 2 + 0] - 1);
          br.n1 = (int)(base + conn[e * 2 + 1] - 1);
          br.family = sname;
          bars.push_back(br);
        }
      } else {
        die("unsupported element type " + std::to_string((int)et) + " in section " + sname);
      }
    }

    // 1-to-1 grid connectivity: unify interface nodes
    int nconns;
    cgOk(cg_nconns(fn, b, z, &nconns), "cg_nconns");
    for (int c = 1; c <= nconns; ++c) {
      char cname[128], donor[128];
      GridLocation_t gl;
      GridConnectivityType_t gct;
      PointSetType_t pst, dpst;
      cgsize_t npts, ndpts;
      ZoneType_t dzt;
      DataType_t ddt;
      cgOk(cg_conn_info(fn, b, z, c, cname, &gl, &gct, &pst, &npts, donor, &dzt, &dpst, &ddt, &ndpts),
           "cg_conn_info");
      check(gct == Abutting1to1, "only Abutting1to1 zone connectivity supported (got " +
          std::to_string((int)gct) + ")");
      check(npts == ndpts, "1to1 connection point/donor size mismatch");
      // find donor zone index
      int dz = -1;
      for (int zz = 1; zz <= nzones; ++zz) {
        char zn[128];
        cgsize_t zs[9];
        cgOk(cg_zone_read(fn, b, zz, zn, zs), "cg_zone_read");
        if (donor == string(zn)) { dz = zz; break; }
      }
      check(dz > 0, "donor zone not found for connection " + string(cname));
      vector<cgsize_t> pnts(npts), dpnts(npts);
      cgOk(cg_conn_read(fn, b, z, c, pnts.data(), LongInteger, dpnts.data()), "cg_conn_read");
      long baseR = zoneNodeBase[(size_t)z - 1], baseD = zoneNodeBase[(size_t)dz - 1];
      for (long k = 0; k < (long)npts; ++k)
        uf.unite((int)(baseR + pnts[k] - 1), (int)(baseD + dpnts[k] - 1));
    }
  }
  cg_close(fn);

  // compress merged nodes
  long totalNodes = (long)uf.p.size();
  vector<int> remap(totalNodes, -1);
  vector<char> used(totalNodes, 0);
  auto markUsed = [&](int gn) { used[uf.find(gn)] = 1; };
  for (auto& c : cells) for (int k = 0; k < c.nn; ++k) markUsed(c.n[k]);
  for (auto& bf : bars) { markUsed(bf.n0); markUsed(bf.n1); }
  for (long g = 0; g < totalNodes; ++g) {
    int root = uf.find((int)g);
    if (!used[root]) continue;
    if (remap[root] < 0) {
      int id = (int)mesh.x.size();
      remap[root] = id;
      // find a representative (zone, local) pair: use g itself if it is a root member
      mesh.x.push_back(0.0);
      mesh.y.push_back(0.0);
    }
  }
  // second pass: assign coordinates from any member (members share coordinates)
  {
    long offset = 0;
    for (size_t z = 0; z < zx.size(); ++z) {
      for (long i = 0; i < (long)zx[z].size(); ++i) {
        int root = uf.find((int)(offset + i));
        int id = remap[root];
        if (id >= 0) { mesh.x[id] = zx[z][i]; mesh.y[id] = zy[z][i]; }
      }
      offset += (long)zx[z].size();
    }
  }
  for (auto& c : cells)
    for (int k = 0; k < c.nn; ++k) c.n[k] = remap[uf.find(c.n[k])];
  for (auto& bf : bars) {
    bf.n0 = remap[uf.find(bf.n0)];
    bf.n1 = remap[uf.find(bf.n1)];
  }

  // collect cells / bars
  mesh.cell_nodes.reserve(cells.size());
  mesh.cell_nnodes.reserve(cells.size());
  for (auto& c : cells) { mesh.cell_nodes.push_back(c.n); mesh.cell_nnodes.push_back(c.nn); }
  std::map<string, int> famIdx;
  for (auto& bf : bars) {
    auto it = famIdx.find(bf.family);
    if (it == famIdx.end()) {
      int id = (int)mesh.families.size();
      famIdx[bf.family] = id;
      mesh.families.push_back(bf.family);
      it = famIdx.find(bf.family);
    }
    mesh.bface_nodes.push_back({bf.n0, bf.n1});
    mesh.bface_family.push_back(it->second);
  }
  return mesh;
}

void buildFaces(GlobalMesh& m) {
  // edge key -> face index
  std::unordered_map<long long, int> edgeFace;
  auto key = [](int a, int b) {
    if (a > b) std::swap(a, b);
    return ((long long)a << 32) | (unsigned int)b;
  };
  // 2-D cell edges in CGNS node order
  static const int triE[3][2] = {{0, 1}, {1, 2}, {2, 0}};
  static const int quadE[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

  int nc = m.numCells();
  for (int c = 0; c < nc; ++c) {
    int nn = m.cell_nnodes[c];
    for (int e = 0; e < nn; ++e) {
      int i0 = (nn == 3) ? triE[e][0] : quadE[e][0];
      int i1 = (nn == 3) ? triE[e][1] : quadE[e][1];
      int n0 = m.cell_nodes[c][i0], n1 = m.cell_nodes[c][i1];
      auto it = edgeFace.find(key(n0, n1));
      if (it == edgeFace.end()) {
        int f = (int)m.face_nodes.size();
        m.face_nodes.push_back({n0, n1});
        m.face_c0.push_back(c);
        m.face_c1.push_back(-1);
        m.face_bc_family.push_back(-1);
        edgeFace[key(n0, n1)] = f;
      } else {
        int f = it->second;
        check(m.face_c1[f] == -1, "edge shared by more than two cells");
        m.face_c1[f] = c;
      }
    }
  }
  // match boundary bars
  int nInteriorBar = 0;
  for (size_t bi = 0; bi < m.bface_nodes.size(); ++bi) {
    auto it = edgeFace.find(key(m.bface_nodes[bi][0], m.bface_nodes[bi][1]));
    check(it != edgeFace.end(), "boundary bar does not match any cell edge");
    int f = it->second;
    if (m.face_c1[f] >= 0) {
      // bar lies on an interior face: inter-zone interface marker, drop it
      ++nInteriorBar;
      continue;
    }
    check(m.face_bc_family[f] == -1, "duplicate boundary bar on one face");
    m.face_bc_family[f] = m.bface_family[bi];
  }
  // compact: remove bars that were interior (rebuild bface arrays)
  // (bars are only kept for reference; faces already carry the family tags)
  int nBoundaryFaces = 0;
  for (size_t f = 0; f < m.face_c1.size(); ++f) {
    if (m.face_c1[f] < 0) {
      check(m.face_bc_family[f] >= 0, "boundary face without a boundary-family tag");
      ++nBoundaryFaces;
    }
  }
  check(nBoundaryFaces > 0, "no boundary faces identified");
  // drop families that only tagged interior (interface) bars: rebuild the
  // family table from the families actually attached to boundary faces
  {
    std::map<int, int> remap;
    vector<string> newFam;
    for (size_t f = 0; f < m.face_c1.size(); ++f) {
      if (m.face_c1[f] < 0) {
        int old = m.face_bc_family[f];
        auto it = remap.find(old);
        if (it == remap.end()) {
          int id = (int)newFam.size();
          remap[old] = id;
          newFam.push_back(m.families[old]);
          it = remap.find(old);
        }
        m.face_bc_family[f] = it->second;
      }
    }
    m.families = newFam;
    // also rebuild boundary bar arrays to hold only true boundary bars
    vector<array<int, 2>> bn;
    vector<int> bf;
    for (size_t bi = 0; bi < m.bface_nodes.size(); ++bi) {
      auto it = edgeFace.find(key(m.bface_nodes[bi][0], m.bface_nodes[bi][1]));
      if (it == edgeFace.end()) continue;
      int f = it->second;
      if (m.face_c1[f] >= 0) continue;  // interior interface marker
      bn.push_back(m.bface_nodes[bi]);
      bf.push_back(m.face_bc_family[f]);
    }
    m.bface_nodes = bn;
    m.bface_family = bf;
  }
}

}  // namespace fv
