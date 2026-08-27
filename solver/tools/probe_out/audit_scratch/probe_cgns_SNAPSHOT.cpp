// probe_cgns.cpp -- standalone CGNS mesh inspector / diagnostic tool.
//
// Reports mesh structure facts needed to write a robust 2-D unstructured
// CGNS reader, and runs self-contained correctness experiments:
//   * inter-zone node merging via GridConnectivity point lists (union-find)
//   * edge/watertightness audit (every interior edge shared by exactly 2 cells)
//   * boundary-edge <-> BC-section cross-check
//   * signed-area / winding-order check on stored node ordering
//
// Build (see build_probe.sh):
//   g++ -O2 -std=c++17 -I<ext>/include probe_cgns.cpp -o probe_cgns
//       -L<ext>/lib -lcgns -lhdf5 -lz -Wl,-rpath,<ext>/lib
//
// Usage:
//   ./probe_cgns mesh1.cgns [mesh2.cgns ...]
//
// Everything is written to stdout as Markdown; redirect to capture a report.

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// CGNS node names are capped at 32 chars (CGIO_MAX_NAME_LENGTH in cgns_io.h);
// declare it locally so we only need cgnslib.h.
constexpr int kNameLen = 33;

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

void die(const char *what) {
  std::fprintf(stderr, "FATAL (%s): %s\n", what, cg_get_error());
  std::exit(1);
}

#define CGCHK(call)                                                            \
  do {                                                                        \
    if ((call) != CG_OK) die(#call);                                          \
  } while (0)

// Non-fatal variant: returns true on success, prints nothing on failure.
#define CGTRY(call) ((call) == CG_OK)

const char *elem_type_name(ElementType_t t) {
  const char *n = cg_ElementTypeName(t);
  return n ? n : "<unknown>";
}

std::string trim(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// number of nodes per element for the fixed-size types we expect in 2-D
int npe_of(ElementType_t t) {
  int n = 0;
  if (cg_npe(t, &n) != CG_OK) return 0;
  return n;
}

// ---------------------------------------------------------------------------
// union-find over global node ids
// ---------------------------------------------------------------------------

struct UnionFind {
  std::vector<int> parent;
  std::vector<int> rank;

  explicit UnionFind(size_t n) : parent(n), rank(n, 0) {
    std::iota(parent.begin(), parent.end(), 0);
  }

  int find(int x) {
    while (parent[x] != x) {
      parent[x] = parent[parent[x]];  // path halving
      x = parent[x];
    }
    return x;
  }

  bool unite(int a, int b) {
    a = find(a);
    b = find(b);
    if (a == b) return false;
    if (rank[a] < rank[b]) std::swap(a, b);
    parent[b] = a;
    if (rank[a] == rank[b]) ++rank[a];
    return true;
  }
};

// ---------------------------------------------------------------------------
// in-memory mesh description
// ---------------------------------------------------------------------------

struct Section {
  int index = 0;
  std::string name;
  ElementType_t type = ElementTypeNull;
  cgsize_t start = 0, end = 0;
  cgsize_t nelem = 0;
  int nbndry = 0;
  int parent_flag = 0;
  cgsize_t data_size = 0;
  int npe = 0;
  // connectivity, 1-based zone-local node ids, nelem * npe (fixed stride only)
  std::vector<cgsize_t> conn;

  bool is_1d() const { return type == BAR_2 || type == BAR_3; }
  bool is_2d_cell() const {
    return type == TRI_3 || type == TRI_6 || type == QUAD_4 || type == QUAD_8 ||
           type == QUAD_9;
  }
  bool contains(cgsize_t e) const { return e >= start && e <= end; }
};

struct Boco {
  int index = 0;
  std::string name;
  BCType_t bctype = BCTypeNull;
  PointSetType_t ptset = PointSetTypeNull;
  GridLocation_t location = GridLocationNull;
  cgsize_t npnts = 0;
  std::vector<cgsize_t> pnts;
  std::string family;
  bool has_location = false;
};

struct Conn1to1 {
  int index = 0;
  std::string name;
  std::string donor;
  std::string this_zone;
  int transform[3] = {0, 0, 0};
  bool has_transform = false;
  GridLocation_t location = GridLocationNull;
  GridConnectivityType_t gctype = GridConnectivityTypeNull;
  PointSetType_t ptset = PointSetTypeNull;
  PointSetType_t donor_ptset = PointSetTypeNull;
  cgsize_t npnts = 0;
  std::vector<cgsize_t> pnts;        // this-zone indices
  std::vector<cgsize_t> donor_pnts;  // donor-zone indices
  // which API produced it
  const char *api = "";
};

struct Zone {
  int index = 0;
  std::string name;
  ZoneType_t type = ZoneTypeNull;
  cgsize_t nnode = 0;
  cgsize_t ncell = 0;
  std::vector<std::string> coord_names;
  std::vector<DataType_t> coord_types;
  // coords[d][i], d over phys_dim, i over nnode
  std::vector<std::vector<double>> coords;
  std::vector<Section> sections;
  std::vector<Boco> bocos;
  std::vector<Conn1to1> conns;
  std::string family;

  // global node offset assigned when concatenating zones
  cgsize_t node_offset = 0;

  double x(cgsize_t i1) const { return coords[0][i1 - 1]; }
  double y(cgsize_t i1) const { return coords.size() > 1 ? coords[1][i1 - 1] : 0.0; }
};

struct Base {
  int index = 0;
  std::string name;
  int cell_dim = 0;
  int phys_dim = 0;
  std::vector<Zone> zones;
  std::vector<std::string> families;
  std::vector<std::string> family_bc;  // parallel: FamilyBC BCType name or ""
};

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

void read_sections(int fn, int B, int Z, Zone &z) {
  int nsec = 0;
  CGCHK(cg_nsections(fn, B, Z, &nsec));
  for (int S = 1; S <= nsec; ++S) {
    Section s;
    char nm[kNameLen] = {0};
    ElementType_t et;
    CGCHK(cg_section_read(fn, B, Z, S, nm, &et, &s.start, &s.end, &s.nbndry,
                          &s.parent_flag));
    s.index = S;
    s.name = trim(nm);
    s.type = et;
    s.nelem = s.end - s.start + 1;
    CGCHK(cg_ElementDataSize(fn, B, Z, S, &s.data_size));
    s.npe = npe_of(et);
    s.conn.assign(static_cast<size_t>(s.data_size), 0);
    if (s.data_size > 0) {
      // fixed-size element types only in these meshes; guard anyway
      if (s.npe > 0 && s.data_size == s.nelem * s.npe) {
        CGCHK(cg_elements_read(fn, B, Z, S, s.conn.data(), nullptr));
      } else {
        std::vector<cgsize_t> offs(static_cast<size_t>(s.nelem) + 1, 0);
        if (!CGTRY(cg_poly_elements_read(fn, B, Z, S, s.conn.data(), offs.data(),
                                         nullptr))) {
          CGCHK(cg_elements_read(fn, B, Z, S, s.conn.data(), nullptr));
        }
      }
    }
    z.sections.push_back(std::move(s));
  }
}

void read_bocos(int fn, int B, int Z, Zone &z) {
  int nboco = 0;
  CGCHK(cg_nbocos(fn, B, Z, &nboco));
  for (int BC = 1; BC <= nboco; ++BC) {
    Boco b;
    char nm[kNameLen] = {0};
    int normal_index[3] = {0, 0, 0};
    cgsize_t normal_list_size = 0;
    DataType_t normal_dt;
    int ndataset = 0;
    BCType_t bt;
    PointSetType_t pst;
    CGCHK(cg_boco_info(fn, B, Z, BC, nm, &bt, &pst, &b.npnts, normal_index,
                       &normal_list_size, &normal_dt, &ndataset));
    b.index = BC;
    b.name = trim(nm);
    b.bctype = bt;
    b.ptset = pst;
    b.pnts.assign(static_cast<size_t>(b.npnts > 0 ? b.npnts : 0), 0);
    if (b.npnts > 0) CGCHK(cg_boco_read(fn, B, Z, BC, b.pnts.data(), nullptr));
    GridLocation_t loc;
    if (CGTRY(cg_boco_gridlocation_read(fn, B, Z, BC, &loc))) {
      b.location = loc;
      b.has_location = true;
    }
    // FamilyName lives under the BC_t node
    char fam[kNameLen] = {0};
    if (CGTRY(cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end")) &&
        CGTRY(cg_famname_read(fam))) {
      b.family = trim(fam);
    }
    z.bocos.push_back(std::move(b));
  }
}

// Read zone connectivity. Tries the 1to1 API first, then falls back to the
// general GridConnectivity_t API (which is what Pointwise actually writes).
void read_conns(int fn, int B, int Z, Zone &z, std::string &api_note) {
  int n11 = 0;
  if (CGTRY(cg_n1to1(fn, B, Z, &n11)) && n11 > 0) {
    api_note = "cg_n1to1/cg_1to1_read";
    for (int I = 1; I <= n11; ++I) {
      Conn1to1 c;
      char cn[kNameLen] = {0};
      char dn[kNameLen] = {0};
      cgsize_t range[6] = {0}, drange[6] = {0};
      CGCHK(cg_1to1_read(fn, B, Z, I, cn, dn, range, drange, c.transform));
      c.index = I;
      c.name = trim(cn);
      c.donor = trim(dn);
      c.this_zone = z.name;
      c.has_transform = true;
      c.api = "cg_1to1_read";
      c.ptset = PointRange;
      c.donor_ptset = PointRange;
      c.npnts = 2;
      c.pnts.assign(range, range + 2);
      c.donor_pnts.assign(drange, drange + 2);
      z.conns.push_back(std::move(c));
    }
    return;
  }

  int nconn = 0;
  if (!CGTRY(cg_nconns(fn, B, Z, &nconn)) || nconn == 0) {
    if (api_note.empty()) api_note = "none (cg_n1to1=0, cg_nconns=0)";
    return;
  }
  api_note = "cg_nconns/cg_conn_info/cg_conn_read (cg_n1to1 returned 0)";
  for (int I = 1; I <= nconn; ++I) {
    Conn1to1 c;
    char cn[kNameLen] = {0};
    char dn[kNameLen] = {0};
    GridLocation_t loc;
    GridConnectivityType_t gct;
    PointSetType_t pst, dpst;
    ZoneType_t dzt;
    DataType_t ddt;
    cgsize_t ndata_donor = 0;
    CGCHK(cg_conn_info(fn, B, Z, I, cn, &loc, &gct, &pst, &c.npnts, dn, &dzt,
                       &dpst, &ddt, &ndata_donor));
    c.index = I;
    c.name = trim(cn);
    c.donor = trim(dn);
    c.this_zone = z.name;
    c.location = loc;
    c.gctype = gct;
    c.ptset = pst;
    c.donor_ptset = dpst;
    c.api = "cg_conn_read";
    c.pnts.assign(static_cast<size_t>(c.npnts > 0 ? c.npnts : 0), 0);
    c.donor_pnts.assign(static_cast<size_t>(ndata_donor > 0 ? ndata_donor : 0), 0);
    CGCHK(cg_conn_read(fn, B, Z, I, c.pnts.data(), ddt,
                       c.donor_pnts.empty() ? nullptr : c.donor_pnts.data()));
    z.conns.push_back(std::move(c));
  }
}

Base read_base(int fn, int B) {
  Base base;
  char nm[kNameLen] = {0};
  CGCHK(cg_base_read(fn, B, nm, &base.cell_dim, &base.phys_dim));
  base.index = B;
  base.name = trim(nm);

  int nfam = 0;
  CGCHK(cg_nfamilies(fn, B, &nfam));
  for (int F = 1; F <= nfam; ++F) {
    char fnm[kNameLen] = {0};
    int nboco = 0, ngeo = 0;
    CGCHK(cg_family_read(fn, B, F, fnm, &nboco, &ngeo));
    base.families.push_back(trim(fnm));
    std::string bcdesc;
    for (int i = 1; i <= nboco; ++i) {
      char bnm[kNameLen] = {0};
      BCType_t bt;
      if (CGTRY(cg_fambc_read(fn, B, F, i, bnm, &bt))) {
        if (!bcdesc.empty()) bcdesc += ", ";
        bcdesc += std::string(trim(bnm)) + "=" + cg_BCTypeName(bt);
      }
    }
    base.family_bc.push_back(bcdesc);
  }

  int nzone = 0;
  CGCHK(cg_nzones(fn, B, &nzone));
  for (int Z = 1; Z <= nzone; ++Z) {
    Zone z;
    cgsize_t size[9] = {0};
    char znm[kNameLen] = {0};
    CGCHK(cg_zone_read(fn, B, Z, znm, size));
    z.index = Z;
    z.name = trim(znm);
    CGCHK(cg_zone_type(fn, B, Z, &z.type));
    z.nnode = size[0];
    z.ncell = size[1];

    int ncoord = 0;
    CGCHK(cg_ncoords(fn, B, Z, &ncoord));
    cgsize_t rmin = 1, rmax = z.nnode;
    for (int C = 1; C <= ncoord; ++C) {
      char cnm[kNameLen] = {0};
      DataType_t dt;
      CGCHK(cg_coord_info(fn, B, Z, C, &dt, cnm));
      z.coord_names.push_back(trim(cnm));
      z.coord_types.push_back(dt);
      std::vector<double> buf(static_cast<size_t>(z.nnode), 0.0);
      CGCHK(cg_coord_read(fn, B, Z, trim(cnm).c_str(), RealDouble, &rmin, &rmax,
                          buf.data()));
      z.coords.push_back(std::move(buf));
    }

    char fam[kNameLen] = {0};
    if (CGTRY(cg_goto(fn, B, "Zone_t", Z, "end")) && CGTRY(cg_famname_read(fam)))
      z.family = trim(fam);

    read_sections(fn, B, Z, z);
    read_bocos(fn, B, Z, z);
    std::string api_note;
    read_conns(fn, B, Z, z, api_note);
    base.zones.push_back(std::move(z));
  }
  return base;
}

// ---------------------------------------------------------------------------
// structural reporting
// ---------------------------------------------------------------------------

// Which BAR_2 section does a boco's element range fall inside?
const Section *section_for_element_range(const Zone &z, cgsize_t lo, cgsize_t hi) {
  for (const auto &s : z.sections)
    if (lo >= s.start && hi <= s.end) return &s;
  return nullptr;
}

// Is a 1-D section referenced by a boco (by element-range containment)?
const Boco *boco_referencing(const Zone &z, const Section &s) {
  for (const auto &b : z.bocos) {
    if (b.npnts < 1) continue;
    if (b.ptset == PointRange && b.npnts >= 2) {
      if (b.pnts[0] >= s.start && b.pnts[1] <= s.end) return &b;
    } else if (b.ptset == ElementRange && b.npnts >= 2) {
      if (b.pnts[0] >= s.start && b.pnts[1] <= s.end) return &b;
    } else {
      bool all_in = true;
      for (cgsize_t v : b.pnts)
        if (!s.contains(v)) { all_in = false; break; }
      if (all_in) return &b;
    }
  }
  return nullptr;
}

// Is a 1-D section named like / referenced by a zone-connectivity node?
const Conn1to1 *conn_referencing(const Zone &z, const Section &s) {
  for (const auto &c : z.conns) {
    // connectivity nodes are named e.g. "1to1Connection:con-2" for section "con-2"
    if (c.name == s.name) return &c;
    if (c.name.size() > s.name.size() &&
        c.name.compare(c.name.size() - s.name.size(), s.name.size(), s.name) == 0)
      return &c;
  }
  return nullptr;
}

void report_structure(const Base &base, const std::string &conn_api_note) {
  std::printf("### Base\n\n");
  std::printf("- name: `%s`\n- CellDimension: %d\n- PhysicalDimension: %d\n",
              base.name.c_str(), base.cell_dim, base.phys_dim);
  std::printf("- nzones: %zu\n\n", base.zones.size());

  std::printf("### Families (base level)\n\n");
  std::printf("| # | FamilyName | FamilyBC |\n|---|---|---|\n");
  for (size_t i = 0; i < base.families.size(); ++i)
    std::printf("| %zu | `%s` | %s |\n", i + 1, base.families[i].c_str(),
                base.family_bc[i].empty() ? "(none)" : base.family_bc[i].c_str());
  std::printf("\n");

  for (const auto &z : base.zones) {
    std::printf("### Zone %d: `%s`\n\n", z.index, z.name.c_str());
    std::printf("- ZoneType: %s\n", cg_ZoneTypeName(z.type));
    std::printf("- nodes (NVertex): %lld\n", (long long)z.nnode);
    std::printf("- cells (NCell): %lld\n", (long long)z.ncell);
    std::printf("- zone FamilyName: %s\n",
                z.family.empty() ? "(none)" : ("`" + z.family + "`").c_str());
    std::printf("- ncoords: %zu\n\n", z.coord_names.size());
    std::printf("| coord | DataType | min | max |\n|---|---|---|---|\n");
    for (size_t d = 0; d < z.coord_names.size(); ++d) {
      double mn = std::numeric_limits<double>::max();
      double mx = -std::numeric_limits<double>::max();
      for (double v : z.coords[d]) { mn = std::min(mn, v); mx = std::max(mx, v); }
      std::printf("| `%s` | %s | %.12g | %.12g |\n", z.coord_names[d].c_str(),
                  cg_DataTypeName(z.coord_types[d]), mn, mx);
    }
    std::printf("\n#### Sections\n\n");
    std::printf("| idx | name | ElementType | start | end | nelem | npe | "
                "ElementDataSize | nbndry | parent_flag | referenced by |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const auto &s : z.sections) {
      std::string ref = "-";
      if (s.is_1d()) {
        const Boco *b = boco_referencing(z, s);
        const Conn1to1 *c = conn_referencing(z, s);
        if (b) ref = "BC `" + b->name + "`" +
                     (b->family.empty() ? "" : " (family `" + b->family + "`)");
        else if (c) ref = "zone connectivity `" + c->name + "` -> `" + c->donor + "`";
        else ref = "**unreferenced**";
      }
      std::printf("| %d | `%s` | %s | %lld | %lld | %lld | %d | %lld | %d | %d | %s |\n",
                  s.index, s.name.c_str(), elem_type_name(s.type),
                  (long long)s.start, (long long)s.end, (long long)s.nelem, s.npe,
                  (long long)s.data_size, s.nbndry, s.parent_flag, ref.c_str());
    }

    std::printf("\n#### Boundary conditions (%zu)\n\n", z.bocos.size());
    if (z.bocos.empty()) std::printf("(none)\n");
    for (const auto &b : z.bocos) {
      std::printf("- boco %d `%s`\n", b.index, b.name.c_str());
      std::printf("  - BCType: %s\n", cg_BCTypeName(b.bctype));
      std::printf("  - GridLocation: %s%s\n",
                  b.has_location ? cg_GridLocationName(b.location) : "(absent)",
                  b.has_location ? "" : " -> default Vertex");
      std::printf("  - PointSetType: %s\n", cg_PointSetTypeName(b.ptset));
      std::printf("  - npnts: %lld\n", (long long)b.npnts);
      std::printf("  - FamilyName: %s\n",
                  b.family.empty() ? "(none)" : ("`" + b.family + "`").c_str());
      std::printf("  - values:");
      for (size_t i = 0; i < b.pnts.size() && i < 4; ++i)
        std::printf(" %lld", (long long)b.pnts[i]);
      if (b.pnts.size() > 8) std::printf(" ...");
      for (size_t i = std::max<size_t>(b.pnts.size() > 4 ? 4 : b.pnts.size(),
                                       b.pnts.size() >= 4 ? b.pnts.size() - 4 : 0);
           i < b.pnts.size(); ++i)
        std::printf(" %lld", (long long)b.pnts[i]);
      std::printf("\n");
      if (b.ptset == PointRange && b.npnts == 2) {
        const Section *s = section_for_element_range(z, b.pnts[0], b.pnts[1]);
        if (s)
          std::printf("  - maps to section `%s` (%s, range %lld..%lld): "
                      "element-range containment, %lld elements\n",
                      s->name.c_str(), elem_type_name(s->type),
                      (long long)s->start, (long long)s->end,
                      (long long)(b.pnts[1] - b.pnts[0] + 1));
        else
          std::printf("  - **no section contains this range**\n");
      }
    }

    std::printf("\n#### Zone connectivity (%zu) [API: %s]\n\n", z.conns.size(),
                conn_api_note.c_str());
    if (z.conns.empty()) std::printf("(none)\n");
    for (const auto &c : z.conns) {
      std::printf("- conn %d `%s`: this zone `%s` -> donor `%s`\n", c.index,
                  c.name.c_str(), c.this_zone.c_str(), c.donor.c_str());
      std::printf("  - read via: %s\n", c.api);
      std::printf("  - GridConnectivityType: %s\n",
                  c.gctype == GridConnectivityTypeNull ? "(n/a)"
                                                       : cg_GridConnectivityTypeName(c.gctype));
      std::printf("  - GridLocation: %s%s\n",
                  c.location == GridLocationNull ? "(absent)"
                                                : cg_GridLocationName(c.location),
                  c.location == GridLocationNull ? " -> default Vertex (NODE indices)" : "");
      std::printf("  - PointSetType: %s / donor %s\n",
                  cg_PointSetTypeName(c.ptset), cg_PointSetTypeName(c.donor_ptset));
      std::printf("  - Transform: %s\n",
                  c.has_transform ? "present" : "(absent -- not a 1to1 node)");
      std::printf("  - npnts: %lld (donor %zu)\n", (long long)c.npnts,
                  c.donor_pnts.size());
      std::printf("  - first 5 (PointList, PointListDonor) pairs:");
      for (size_t i = 0; i < c.pnts.size() && i < 5; ++i)
        std::printf(" (%lld,%lld)", (long long)c.pnts[i],
                    i < c.donor_pnts.size() ? (long long)c.donor_pnts[i] : -1);
      std::printf("\n");
    }
    std::printf("\n");
  }
}

// ---------------------------------------------------------------------------
// experiments: merge, edges, winding, boundary geometry
// ---------------------------------------------------------------------------

struct EdgeKey {
  int a, b;  // sorted merged node ids
  bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
};
struct EdgeHash {
  size_t operator()(const EdgeKey &k) const {
    return std::hash<uint64_t>()((uint64_t)(uint32_t)k.a << 32 |
                                 (uint32_t)k.b);
  }
};

EdgeKey make_edge(int u, int v) {
  return u < v ? EdgeKey{u, v} : EdgeKey{v, u};
}

void run_experiments(Base &base) {
  std::printf("### Experiment: node merging, edge topology, winding\n\n");

  // ---- global node numbering: concatenate zones with an offset ----
  cgsize_t total = 0;
  for (auto &z : base.zones) {
    z.node_offset = total;
    total += z.nnode;
  }
  std::printf("- concatenated raw global nodes: %lld", (long long)total);
  for (auto &z : base.zones)
    std::printf(" (`%s` offset %lld, n %lld)", z.name.c_str(),
                (long long)z.node_offset, (long long)z.nnode);
  std::printf("\n");

  // flat coordinate arrays in global numbering
  std::vector<double> gx(static_cast<size_t>(total), 0.0);
  std::vector<double> gy(static_cast<size_t>(total), 0.0);
  for (const auto &z : base.zones)
    for (cgsize_t i = 0; i < z.nnode; ++i) {
      gx[static_cast<size_t>(z.node_offset + i)] = z.coords[0][i];
      gy[static_cast<size_t>(z.node_offset + i)] =
          z.coords.size() > 1 ? z.coords[1][i] : 0.0;
    }

  // ---- union-find over every connectivity point pair ----
  UnionFind uf(static_cast<size_t>(total));
  std::map<std::string, int> zone_index_by_name;
  for (size_t i = 0; i < base.zones.size(); ++i)
    zone_index_by_name[base.zones[i].name] = static_cast<int>(i);

  long long pair_count = 0, union_count = 0;
  double max_pair_dist = 0.0;
  long long pairs_over_tol = 0;
  const double kTol = 1e-10;

  for (const auto &z : base.zones) {
    for (const auto &c : z.conns) {
      auto it = zone_index_by_name.find(c.donor);
      if (it == zone_index_by_name.end()) {
        std::printf("- WARNING: donor zone `%s` not found\n", c.donor.c_str());
        continue;
      }
      const Zone &dz = base.zones[static_cast<size_t>(it->second)];
      size_t n = std::min(c.pnts.size(), c.donor_pnts.size());
      for (size_t k = 0; k < n; ++k) {
        // PointList entries are 1-based NODE indices in their own zone
        cgsize_t gi = z.node_offset + c.pnts[k] - 1;
        cgsize_t gj = dz.node_offset + c.donor_pnts[k] - 1;
        if (gi < 0 || gi >= total || gj < 0 || gj >= total) {
          std::printf("- WARNING: out-of-range pair (%lld,%lld)\n",
                      (long long)c.pnts[k], (long long)c.donor_pnts[k]);
          continue;
        }
        double d = std::hypot(gx[(size_t)gi] - gx[(size_t)gj],
                              gy[(size_t)gi] - gy[(size_t)gj]);
        max_pair_dist = std::max(max_pair_dist, d);
        if (d > kTol) ++pairs_over_tol;
        ++pair_count;
        if (uf.unite(static_cast<int>(gi), static_cast<int>(gj))) ++union_count;
      }
    }
  }
  std::printf("- connectivity node pairs applied: %lld (effective unions: %lld)\n",
              pair_count, union_count);
  std::printf("- max distance between merged node pairs: %.6e\n", max_pair_dist);
  std::printf("- merged pairs with coordinate mismatch > 1e-10: %lld\n",
              pairs_over_tol);

  // ---- compact merged ids ----
  std::vector<int> merged_id(static_cast<size_t>(total), -1);
  int nmerged = 0;
  for (cgsize_t i = 0; i < total; ++i) {
    int root = uf.find(static_cast<int>(i));
    if (merged_id[static_cast<size_t>(root)] < 0)
      merged_id[static_cast<size_t>(root)] = nmerged++;
  }
  for (cgsize_t i = 0; i < total; ++i)
    merged_id[static_cast<size_t>(i)] =
        merged_id[static_cast<size_t>(uf.find(static_cast<int>(i)))];
  std::printf("- **unique merged nodes: %d** (from %lld raw, %lld collapsed)\n",
              nmerged, (long long)total, (long long)(total - nmerged));

  // ---- build edges from 2-D cells ----
  struct EdgeInfo { int count = 0; };
  std::unordered_map<EdgeKey, EdgeInfo, EdgeHash> edges;
  edges.reserve(static_cast<size_t>(total) * 3);

  long long ncell_total = 0, ntri = 0, nquad = 0;
  long long neg_area = 0, zero_area = 0;
  std::vector<double> first_tri_areas, first_quad_areas;
  double min_abs_area = std::numeric_limits<double>::max();
  double total_area = 0.0;

  for (const auto &z : base.zones) {
    for (const auto &s : z.sections) {
      if (!s.is_2d_cell()) continue;
      int npe = s.npe;
      for (cgsize_t e = 0; e < s.nelem; ++e) {
        const cgsize_t *nodes = &s.conn[static_cast<size_t>(e) * npe];
        // shoelace on stored order, using zone-local coords
        double area2 = 0.0;
        for (int k = 0; k < npe; ++k) {
          cgsize_t n1 = nodes[k];
          cgsize_t n2 = nodes[(k + 1) % npe];
          area2 += z.x(n1) * z.y(n2) - z.x(n2) * z.y(n1);
        }
        double area = 0.5 * area2;
        total_area += area;
        if (area < 0) ++neg_area;
        if (area == 0.0) ++zero_area;
        min_abs_area = std::min(min_abs_area, std::fabs(area));
        if (s.type == TRI_3) {
          if (first_tri_areas.size() < 5) first_tri_areas.push_back(area);
          ++ntri;
        } else if (s.type == QUAD_4) {
          if (first_quad_areas.size() < 5) first_quad_areas.push_back(area);
          ++nquad;
        }
        // edges in merged global ids
        for (int k = 0; k < npe; ++k) {
          int gu = merged_id[static_cast<size_t>(z.node_offset + nodes[k] - 1)];
          int gv = merged_id[static_cast<size_t>(
              z.node_offset + nodes[(k + 1) % npe] - 1)];
          edges[make_edge(gu, gv)].count++;
        }
        ++ncell_total;
      }
    }
  }

  std::printf("- 2-D cells scanned: %lld (TRI_3 %lld, QUAD_4 %lld)\n",
              ncell_total, ntri, nquad);
  std::printf("- **unique edges: %zu**\n", edges.size());

  long long e1 = 0, e2 = 0, e3plus = 0;
  for (const auto &kv : edges) {
    if (kv.second.count == 1) ++e1;
    else if (kv.second.count == 2) ++e2;
    else ++e3plus;
  }
  std::printf("- edges shared by exactly 2 cells (interior): **%lld**\n", e2);
  std::printf("- edges used by exactly 1 cell (boundary): **%lld**\n", e1);
  std::printf("- edges used by 3+ cells (non-manifold): %lld %s\n", e3plus,
              e3plus == 0 ? "(good)" : "(**BAD**)");
  std::printf("- Euler check: unique = 1-cell + 2-cell + 3+ => %lld == %zu %s\n",
              e1 + e2 + e3plus, edges.size(),
              (size_t)(e1 + e2 + e3plus) == edges.size() ? "OK" : "MISMATCH");

  // ---- winding ----
  std::printf("\n#### Winding / signed area (shoelace on stored node order)\n\n");
  std::printf("- first 5 TRI_3 signed areas:");
  for (double a : first_tri_areas) std::printf(" %+.6e", a);
  std::printf("\n- first 5 QUAD_4 signed areas:");
  for (double a : first_quad_areas) std::printf(" %+.6e", a);
  std::printf("\n- cells with negative signed area: **%lld / %lld**\n", neg_area,
              ncell_total);
  std::printf("- cells with exactly zero area: %lld\n", zero_area);
  std::printf("- smallest |area|: %.6e\n", min_abs_area);
  std::printf("- sum of signed areas: %+.6e\n", total_area);
  std::printf("- conclusion: stored ordering is %s\n",
              neg_area == 0 ? "**counter-clockwise (all positive)**"
                            : (neg_area == ncell_total
                                   ? "**clockwise (all negative)**"
                                   : "**MIXED -- must be normalized per cell**"));

  // ---- boundary BC cross-check ----
  std::printf("\n#### Boundary edge <-> BC section cross-check\n\n");
  long long total_bc_elems = 0;
  long long total_found = 0, total_missing = 0;
  std::printf("| zone | boco | family | section | nelem | found among 1-cell edges | "
              "missing |\n|---|---|---|---|---|---|---|\n");
  for (const auto &z : base.zones) {
    for (const auto &b : z.bocos) {
      if (b.ptset != PointRange || b.npnts != 2) continue;
      const Section *s = section_for_element_range(z, b.pnts[0], b.pnts[1]);
      if (!s) continue;
      long long found = 0, missing = 0;
      for (cgsize_t e = b.pnts[0]; e <= b.pnts[1]; ++e) {
        cgsize_t local = e - s->start;
        const cgsize_t *nd = &s->conn[static_cast<size_t>(local) * s->npe];
        int gu = merged_id[static_cast<size_t>(z.node_offset + nd[0] - 1)];
        int gv = merged_id[static_cast<size_t>(z.node_offset + nd[1] - 1)];
        auto it = edges.find(make_edge(gu, gv));
        if (it != edges.end() && it->second.count == 1) ++found;
        else ++missing;
      }
      long long n = b.pnts[1] - b.pnts[0] + 1;
      total_bc_elems += n;
      total_found += found;
      total_missing += missing;
      std::printf("| `%s` | `%s` | `%s` | `%s` | %lld | %lld | %lld |\n",
                  z.name.c_str(), b.name.c_str(), b.family.c_str(), s->name.c_str(),
                  n, found, missing);
    }
  }
  std::printf("\n- total BC boundary elements: **%lld**\n", total_bc_elems);
  std::printf("- 1-cell (boundary) edges from topology: **%lld**\n", e1);
  std::printf("- counts match: **%s**\n",
              total_bc_elems == e1 ? "YES" : "NO");
  std::printf("- all BC elements found among 1-cell edges: **%s** (found %lld, "
              "missing %lld)\n",
              total_missing == 0 ? "YES" : "NO", total_found, total_missing);

  // ---- decisive test: SET EQUALITY between BC edges and 1-cell edges ------
  // Membership alone is weak (shifting a closed boundary loop by one element
  // still lands on another boundary edge). The strong statement is that the
  // set of edges named by the BCs is exactly the set of 1-cell edges, with no
  // duplicates and nothing left over.
  {
    std::set<std::pair<int, int>> bc_set, one_cell_set;
    long long bc_dupes = 0;
    for (const auto &z : base.zones) {
      for (const auto &b : z.bocos) {
        if (b.ptset != PointRange || b.npnts != 2) continue;
        const Section *s = section_for_element_range(z, b.pnts[0], b.pnts[1]);
        if (!s) continue;
        for (cgsize_t e = b.pnts[0]; e <= b.pnts[1]; ++e) {
          cgsize_t local = e - s->start;
          const cgsize_t *nd = &s->conn[static_cast<size_t>(local) * s->npe];
          int gu = merged_id[static_cast<size_t>(z.node_offset + nd[0] - 1)];
          int gv = merged_id[static_cast<size_t>(z.node_offset + nd[1] - 1)];
          EdgeKey k = make_edge(gu, gv);
          if (!bc_set.insert({k.a, k.b}).second) ++bc_dupes;
        }
      }
    }
    for (const auto &kv : edges)
      if (kv.second.count == 1) one_cell_set.insert({kv.first.a, kv.first.b});

    long long in_bc_not_topo = 0, in_topo_not_bc = 0;
    for (const auto &k : bc_set)
      if (!one_cell_set.count(k)) ++in_bc_not_topo;
    for (const auto &k : one_cell_set)
      if (!bc_set.count(k)) ++in_topo_not_bc;

    std::printf("- distinct BC edges: %zu; distinct 1-cell edges: %zu; "
                "duplicate BC edges: %lld\n",
                bc_set.size(), one_cell_set.size(), bc_dupes);
    std::printf("- in BC but not 1-cell: %lld; in 1-cell but not BC: %lld\n",
                in_bc_not_topo, in_topo_not_bc);
    std::printf("- **SET EQUALITY (BC edges == boundary edges): %s**\n",
                (in_bc_not_topo == 0 && in_topo_not_bc == 0 &&
                 bc_set.size() == one_cell_set.size())
                    ? "YES -- every boundary edge is tagged by exactly one BC "
                      "family, and no BC edge is interior"
                    : "NO");

    // negative control that is genuinely falsifiable: interior edges taken
    // from volume cells must NOT appear in the 1-cell set.
    long long ctl_checked = 0, ctl_false_hits = 0;
    for (const auto &z : base.zones) {
      for (const auto &s : z.sections) {
        if (!s.is_2d_cell()) continue;
        // Only QUAD_4 has a true diagonal: for a triangle, (n0,n2) is a real
        // edge, so using it as a "control" would produce spurious hits.
        if (s.type != QUAD_4) continue;
        for (cgsize_t e = 0; e < s.nelem && ctl_checked < 2000; ++e) {
          const cgsize_t *n = &s.conn[static_cast<size_t>(e) * 4];
          int gu = merged_id[static_cast<size_t>(z.node_offset + n[0] - 1)];
          int gv = merged_id[static_cast<size_t>(z.node_offset + n[2] - 1)];
          if (gu == gv) continue;
          ++ctl_checked;
          if (one_cell_set.count({std::min(gu, gv), std::max(gu, gv)}))
            ++ctl_false_hits;
        }
      }
    }
    std::printf("- NEGATIVE CONTROL (%lld cell diagonals probed against the "
                "boundary-edge set): %lld false hits -- lookup is %s\n",
                ctl_checked, ctl_false_hits,
                ctl_false_hits == 0
                    ? "**discriminating (non-boundary edges are correctly "
                      "rejected)**"
                    : "**suspect**");
  }

  // ---- extra validation: connectivity ("con-*") sections must become
  // INTERIOR edges after merging. This is the real test that the node merge
  // worked: before merging these edges are used by 1 cell in each zone
  // separately; after a correct merge they must be used by exactly 2.
  std::printf("\n#### Merge validation: connectivity sections must be interior\n\n");
  long long conn_edges_checked = 0, conn_edges_interior = 0,
            conn_edges_boundary = 0, conn_edges_absent = 0;
  for (const auto &z : base.zones) {
    for (const auto &s : z.sections) {
      if (!s.is_1d()) continue;
      if (boco_referencing(z, s)) continue;  // BC section, not connectivity
      for (cgsize_t e = 0; e < s.nelem; ++e) {
        const cgsize_t *nd = &s.conn[static_cast<size_t>(e) * s.npe];
        int gu = merged_id[static_cast<size_t>(z.node_offset + nd[0] - 1)];
        int gv = merged_id[static_cast<size_t>(z.node_offset + nd[1] - 1)];
        auto it = edges.find(make_edge(gu, gv));
        ++conn_edges_checked;
        if (it == edges.end()) ++conn_edges_absent;
        else if (it->second.count == 2) ++conn_edges_interior;
        else if (it->second.count == 1) ++conn_edges_boundary;
      }
    }
  }
  std::printf("- connectivity (con-*) edges checked: %lld\n", conn_edges_checked);
  std::printf("- ... now interior (2 cells): **%lld**\n", conn_edges_interior);
  std::printf("- ... still boundary (1 cell): **%lld** %s\n", conn_edges_boundary,
              conn_edges_boundary == 0 ? "(good -- merge closed the interface)"
                                       : "(**BAD -- merge failed**)");
  std::printf("- ... not present in edge map at all: %lld\n", conn_edges_absent);

  // ---- extra validation: are there coincident nodes we did NOT merge?
  // Buckets coordinates on a quantized grid; reports duplicate positions that
  // survived merging. A watertight single mesh should have none.
  {
    std::printf("\n#### Merge validation: unmerged coincident nodes\n\n");
    std::map<std::pair<long long, long long>, std::vector<int>> buckets;
    const double q = 1e9;  // 1e-9 quantization
    for (cgsize_t i = 0; i < total; ++i) {
      long long kx = (long long)std::llround(gx[(size_t)i] * q);
      long long ky = (long long)std::llround(gy[(size_t)i] * q);
      buckets[{kx, ky}].push_back(merged_id[(size_t)i]);
    }
    long long dup_positions = 0, worst = 0;
    for (auto &kv : buckets) {
      std::set<int> distinct(kv.second.begin(), kv.second.end());
      if (distinct.size() > 1) {
        ++dup_positions;
        worst = std::max<long long>(worst, (long long)distinct.size());
      }
    }
    std::printf("- distinct quantized positions: %zu (merged nodes: %d)\n",
                buckets.size(), nmerged);
    std::printf("- positions holding >1 distinct merged id (missed merges): "
                "**%lld** (worst multiplicity %lld) %s\n",
                dup_positions, worst,
                dup_positions == 0 ? "(good)" : "(investigate)");
  }

  // ---- extra validation: quad node order traverses the perimeter ----
  // A shoelace over 1-2-3-4 gives a positive area even if the stored order is
  // actually 1-2-4-3 (a bow-tie) in some configurations, so verify directly
  // that consecutive stored nodes are real mesh edges and that the diagonals
  // are not, and cross-check the shoelace area against a triangle split.
  {
    std::printf("\n#### Winding validation: quad node order is perimeter order\n\n");
    long long quads = 0, diag_is_edge = 0, area_mismatch = 0, nonconvex = 0;
    double worst_rel = 0.0;
    for (const auto &z : base.zones) {
      for (const auto &s : z.sections) {
        if (s.type != QUAD_4) continue;
        for (cgsize_t e = 0; e < s.nelem; ++e) {
          const cgsize_t *n = &s.conn[static_cast<size_t>(e) * 4];
          ++quads;
          // diagonals (1,3) and (2,4) must NOT be cell edges
          int g0 = merged_id[static_cast<size_t>(z.node_offset + n[0] - 1)];
          int g1 = merged_id[static_cast<size_t>(z.node_offset + n[1] - 1)];
          int g2 = merged_id[static_cast<size_t>(z.node_offset + n[2] - 1)];
          int g3 = merged_id[static_cast<size_t>(z.node_offset + n[3] - 1)];
          if (edges.count(make_edge(g0, g2))) ++diag_is_edge;
          if (edges.count(make_edge(g1, g3))) ++diag_is_edge;
          // shoelace vs split into triangles (0,1,2) + (0,2,3)
          auto tri = [&](cgsize_t a, cgsize_t b, cgsize_t c) {
            return 0.5 * ((z.x(b) - z.x(a)) * (z.y(c) - z.y(a)) -
                          (z.x(c) - z.x(a)) * (z.y(b) - z.y(a)));
          };
          double t1 = tri(n[0], n[1], n[2]);
          double t2 = tri(n[0], n[2], n[3]);
          double shoe = 0.0;
          for (int k = 0; k < 4; ++k) {
            cgsize_t p = n[k], qn = n[(k + 1) % 4];
            shoe += z.x(p) * z.y(qn) - z.x(qn) * z.y(p);
          }
          shoe *= 0.5;
          double split = t1 + t2;
          double denom = std::max(std::fabs(shoe), 1e-300);
          double rel = std::fabs(shoe - split) / denom;
          worst_rel = std::max(worst_rel, rel);
          if (rel > 1e-9) ++area_mismatch;
          if (t1 <= 0.0 || t2 <= 0.0) ++nonconvex;
        }
      }
    }
    std::printf("- QUAD_4 cells: %lld\n", quads);
    std::printf("- stored diagonals that are also mesh edges: %lld %s\n",
                diag_is_edge,
                diag_is_edge == 0 ? "(good -- stored order is the perimeter)"
                                  : "(**suspicious**)");
    std::printf("- shoelace vs triangle-split area mismatch (>1e-9 rel): %lld "
                "(worst rel %.3e)\n",
                area_mismatch, worst_rel);
    std::printf("- quads with a non-positive sub-triangle (concave/bowtie): "
                "%lld %s\n",
                nonconvex,
                nonconvex == 0 ? "(all strictly convex CCW)" : "(check)");
  }

  // ---- degeneracy checks ----
  {
    long long self_loops = 0, dup_node_cells = 0;
    for (const auto &kv : edges)
      if (kv.first.a == kv.first.b) ++self_loops;
    for (const auto &z : base.zones)
      for (const auto &s : z.sections) {
        if (!s.is_2d_cell()) continue;
        for (cgsize_t e = 0; e < s.nelem; ++e) {
          const cgsize_t *n = &s.conn[static_cast<size_t>(e) * s.npe];
          std::set<cgsize_t> u(n, n + s.npe);
          if ((int)u.size() != s.npe) ++dup_node_cells;
        }
      }
    std::printf("\n#### Degeneracy checks\n\n");
    std::printf("- self-loop edges (a==b): %lld\n", self_loops);
    std::printf("- cells with duplicate node ids: %lld\n", dup_node_cells);
  }

  // ---- boundary geometry per family ----
  std::printf("\n#### Boundary geometry by family\n\n");
  std::printf("| zone | boco | family | nelem | nnodes | x range | y range | "
              "r=|p| min | r max |\n|---|---|---|---|---|---|---|---|---|\n");
  for (const auto &z : base.zones) {
    for (const auto &b : z.bocos) {
      if (b.ptset != PointRange || b.npnts != 2) continue;
      const Section *s = section_for_element_range(z, b.pnts[0], b.pnts[1]);
      if (!s) continue;
      std::set<cgsize_t> nodes;
      for (cgsize_t e = b.pnts[0]; e <= b.pnts[1]; ++e) {
        cgsize_t local = e - s->start;
        const cgsize_t *nd = &s->conn[static_cast<size_t>(local) * s->npe];
        for (int k = 0; k < s->npe; ++k) nodes.insert(nd[k]);
      }
      double xmn = 1e300, xmx = -1e300, ymn = 1e300, ymx = -1e300;
      double rmn = 1e300, rmx = -1e300;
      for (cgsize_t nid : nodes) {
        double X = z.x(nid), Y = z.y(nid);
        xmn = std::min(xmn, X); xmx = std::max(xmx, X);
        ymn = std::min(ymn, Y); ymx = std::max(ymx, Y);
        double r = std::hypot(X, Y);
        rmn = std::min(rmn, r); rmx = std::max(rmx, r);
      }
      std::printf("| `%s` | `%s` | `%s` | %lld | %zu | [%.6g, %.6g] | "
                  "[%.6g, %.6g] | %.6g | %.6g |\n",
                  z.name.c_str(), b.name.c_str(), b.family.c_str(),
                  (long long)(b.pnts[1] - b.pnts[0] + 1), nodes.size(), xmn, xmx,
                  ymn, ymx, rmn, rmx);
    }
  }
  std::printf("\n");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s mesh.cgns [mesh2.cgns ...]\n", argv[0]);
    return 2;
  }

  {
    float ver = 0.0f;
    std::printf("# CGNS mesh probe report\n\n");
    std::printf("- probe built against CGNS %s, sizeof(cgsize_t) = %zu bytes\n",
                CGNS_DOTVERS == 0 ? "?" : "4.50", sizeof(cgsize_t));
    (void)ver;
  }

  for (int i = 1; i < argc; ++i) {
    const char *path = argv[i];
    int fn = 0;
    if (cg_open(path, CG_MODE_READ, &fn) != CG_OK) {
      std::fprintf(stderr, "cannot open %s: %s\n", path, cg_get_error());
      return 1;
    }
    float file_ver = 0.0f;
    cg_version(fn, &file_ver);
    int precision = 0;
    cg_precision(fn, &precision);

    std::printf("\n---\n\n## %s\n\n", path);
    std::printf("- CGNS file version: %.3f, file precision: %d-bit\n\n",
                (double)file_ver, precision);

    int nbases = 0;
    CGCHK(cg_nbases(fn, &nbases));
    std::printf("- nbases: %d\n\n", nbases);

    for (int B = 1; B <= nbases; ++B) {
      // record which connectivity API actually yielded data
      int probe_n11_total = 0, probe_nconn_total = 0;
      {
        int nz = 0;
        CGCHK(cg_nzones(fn, B, &nz));
        for (int Z = 1; Z <= nz; ++Z) {
          int a = 0, b = 0;
          if (CGTRY(cg_n1to1(fn, B, Z, &a))) probe_n11_total += a;
          if (CGTRY(cg_nconns(fn, B, Z, &b))) probe_nconn_total += b;
        }
      }
      char note[256];
      std::snprintf(note, sizeof(note),
                    "cg_n1to1 total=%d, cg_nconns total=%d -> using %s",
                    probe_n11_total, probe_nconn_total,
                    probe_n11_total > 0 ? "cg_1to1_read"
                                        : (probe_nconn_total > 0 ? "cg_conn_read"
                                                                 : "none"));
      Base base = read_base(fn, B);
      report_structure(base, note);
      run_experiments(base);
    }
    CGCHK(cg_close(fn));
  }
  return 0;
}
