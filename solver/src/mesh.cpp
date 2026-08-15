#include "mesh.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <numeric>
#include <set>

#include <cgnslib.h>
#include <metis.h>

namespace cfd {
namespace {

// ---------------------------------------------------------------------------
// Byte buffer used to ship rank-local mesh pieces from rank 0 to all ranks.
// ---------------------------------------------------------------------------
class ByteBuf {
 public:
  void push_i(int v) {
    int be = v;
    for (int k = 0; k < 4; ++k) data_.push_back((char)((be >> (8 * k)) & 0xff));
  }
  void push_d(double v) {
    uint64_t u;
    std::memcpy(&u, &v, 8);
    for (int k = 0; k < 8; ++k) data_.push_back((char)((u >> (8 * k)) & 0xff));
  }
  int get_i(size_t& p) const {
    int v = 0;
    for (int k = 0; k < 4; ++k) v |= ((int)(unsigned char)data_[p + k]) << (8 * k);
    p += 4;
    return v;
  }
  double get_d(size_t& p) const {
    uint64_t u = 0;
    for (int k = 0; k < 8; ++k) u |= ((uint64_t)(unsigned char)data_[p + k]) << (8 * k);
    p += 8;
    double v;
    std::memcpy(&v, &u, 8);
    return v;
  }
  const std::vector<char>& data() const { return data_; }
  std::vector<char>& raw() { return data_; }
  const char* bytes() const { return data_.data(); }
  size_t size() const { return data_.size(); }

 private:
  std::vector<char> data_;
};

class DisjointSet {
 public:
  explicit DisjointSet(int n) : parent_(n) {
    std::iota(parent_.begin(), parent_.end(), 0);
  }
  int find(int a) {
    int r = a;
    while (parent_[r] != r) r = parent_[r];
    while (parent_[a] != a) {
      int nxt = parent_[a];
      parent_[a] = r;
      a = nxt;
    }
    return r;
  }
  void unite(int a, int b) {
    int ra = find(a), rb = find(b);
    if (ra != rb) parent_[std::max(ra, rb)] = std::min(ra, rb);
  }

 private:
  std::vector<int> parent_;
};

uint64_t edge_key(int a, int b) {
  int lo = std::min(a, b), hi = std::max(a, b);
  return ((uint64_t)(uint32_t)lo << 32) | (uint32_t)hi;
}

}  // namespace

// ---------------------------------------------------------------------------
// CGNS import
// ---------------------------------------------------------------------------
GlobalMesh read_cgns_mesh(const std::string& path, const std::vector<BcSpec>& bcs) {
  GlobalMesh gm;
  int fn = 0;
  if (cg_open(path.c_str(), CG_MODE_READ, &fn)) {
    cg_error_print();
    fatal("failed to open CGNS mesh: " + path);
  }

  int nbases = 0;
  cg_nbases(fn, &nbases);
  if (nbases != 1) fatal("expected exactly one CGNS base, found " + std::to_string(nbases));

  int B = 1;
  char basename[33];
  int celldim = 0, physdim = 0;
  cg_base_read(fn, B, basename, &celldim, &physdim);
  if (celldim != 2) fatal("mesh is not 2-D (celldim=" + std::to_string(celldim) + ")");

  int nz = 0;
  cg_nzones(fn, B, &nz);
  if (nz < 1) fatal("mesh has no zones");

  // ---- Pass 1: collect zone metadata, coordinates, connectivity ----------
  struct ZoneInfo {
    std::string name;
    int node_offset = 0;
    int n_nodes = 0;
    int zone_index = 0;
  };
  std::vector<ZoneInfo> zones;
  std::vector<double> all_x, all_y;  // one entry per global node id
  struct CellRec {
    int nv;
    int v[4];
    int zone;
  };
  std::vector<CellRec> cell_recs;
  struct EdgeRec {
    int a, b;
    int section;  // index into section name list
  };
  std::vector<EdgeRec> edge_recs;
  std::vector<std::string> section_names;
  std::map<std::string, int> section_index;

  int total_nodes = 0;
  for (int z = 1; z <= nz; ++z) {
    char zname[33];
    cgsize_t zsize[9];
    ZoneType_t zt;
    cg_zone_read(fn, B, z, zname, zsize);
    cg_zone_type(fn, B, z, &zt);
    if (zt != Unstructured) fatal("zone '" + std::string(zname) + "' is not unstructured");
    ZoneInfo zi;
    zi.name = zname;
    zi.zone_index = z;
    zi.node_offset = total_nodes;
    zi.n_nodes = (int)zsize[0];
    zones.push_back(zi);
    total_nodes += (int)zsize[0];
  }

  all_x.resize(total_nodes);
  all_y.resize(total_nodes);
  std::vector<bool> has_coord(total_nodes, false);

  for (int z = 1; z <= nz; ++z) {
    ZoneInfo& zi = zones[z - 1];
    cgsize_t rmin = 1, rmax = (cgsize_t)zi.n_nodes;
    int ncoords = 0;
    cg_ncoords(fn, B, z, &ncoords);
    for (int c = 1; c <= ncoords; ++c) {
      DataType_t dt;
      char cname[33];
      cg_coord_info(fn, B, z, c, &dt, cname);
      std::vector<double> buf(zi.n_nodes);
      cg_coord_read(fn, B, z, cname, RealDouble, &rmin, &rmax, buf.data());
      if (std::strcmp(cname, "CoordinateX") == 0) {
        for (int i = 0; i < zi.n_nodes; ++i) all_x[zi.node_offset + i] = buf[i];
        for (int i = 0; i < zi.n_nodes; ++i) has_coord[zi.node_offset + i] = true;
      } else if (std::strcmp(cname, "CoordinateY") == 0) {
        for (int i = 0; i < zi.n_nodes; ++i) all_y[zi.node_offset + i] = buf[i];
      }
    }
    int ns = 0;
    cg_nsections(fn, B, z, &ns);
    for (int s = 1; s <= ns; ++s) {
      char sname[33];
      ElementType_t et;
      cgsize_t estart = 0, eend = 0;
      int nbndry = 0, parentflag = 0;
      cg_section_read(fn, B, z, s, sname, &et, &estart, &eend, &nbndry, &parentflag);
      int n_elems = (int)(eend - estart + 1);
      int nnodes = 0;
      if (et == TRI_3) nnodes = 3;
      else if (et == QUAD_4) nnodes = 4;
      else if (et == BAR_2) nnodes = 2;
      else {
        fatal("unsupported element type in section '" + std::string(sname) + "' of zone '" +
              zi.name + "'");
      }
      std::vector<cgsize_t> conn((size_t)n_elems * nnodes);
      cg_elements_read(fn, B, z, s, conn.data(), nullptr);
      if (!section_index.count(sname)) {
        section_index[sname] = (int)section_names.size();
        section_names.push_back(sname);
      }
      int sec_id = section_index[sname];
      if (nnodes == 2) {
        for (int e = 0; e < n_elems; ++e) {
          EdgeRec er;
          er.a = zi.node_offset + (int)conn[(size_t)e * 2] - 1;
          er.b = zi.node_offset + (int)conn[(size_t)e * 2 + 1] - 1;
          er.section = sec_id;
          edge_recs.push_back(er);
        }
      } else {
        for (int e = 0; e < n_elems; ++e) {
          CellRec cr;
          cr.nv = nnodes;
          cr.zone = z;
          for (int k = 0; k < nnodes; ++k)
            cr.v[k] = zi.node_offset + (int)conn[(size_t)e * nnodes + k] - 1;
          cell_recs.push_back(cr);
        }
      }
    }
  }

  // ---- Pass 2: 1-to-1 vertex connections (multi-block stitching) ---------
  DisjointSet ds(total_nodes);
  for (int z = 1; z <= nz; ++z) {
    ZoneInfo& zi = zones[z - 1];
    int nconns = 0;
    cg_nconns(fn, B, z, &nconns);
    for (int c = 1; c <= nconns; ++c) {
      char cname[33], donorname[33];
      GridLocation_t loc;
      GridConnectivityType_t ctype;
      PointSetType_t pset, donorpset;
      cgsize_t npnts = 0, ndata = 0;
      ZoneType_t donorzt;
      DataType_t donor_dt;
      cg_conn_info(fn, B, z, c, cname, &loc, &ctype, &pset, &npnts, donorname, &donorzt,
                   &donorpset, &donor_dt, &ndata);
      if (ctype != Abutting1to1 || loc != Vertex) {
        fatal("unsupported grid connectivity '" + std::string(cname) + "' in zone '" + zi.name +
              "' (only abutting 1-to-1 vertex connections are supported)");
      }
      if (npnts != ndata) {
        fatal("inconsistent 1-to-1 connection '" + std::string(cname) + "'");
      }
      int donor_zone = 0;
      for (size_t k = 0; k < zones.size(); ++k) {
        if (zones[k].name == donorname) { donor_zone = (int)k; break; }
      }
      if (donor_zone == 0 && zones[0].name != donorname) {
        fatal("1-to-1 connection references unknown donor zone '" + std::string(donorname) + "'");
      }
      std::vector<cgsize_t> pnts(npnts), donor(ndata);
      cg_conn_read(fn, B, z, c, pnts.data(), donor_dt, donor.data());
      int doff = zones[donor_zone].node_offset;
      for (cgsize_t k = 0; k < npnts; ++k) {
        ds.unite(zi.node_offset + (int)pnts[k] - 1, doff + (int)donor[k] - 1);
      }
    }
  }

  // ---- Pass 3: node remap (union-find representatives) --------------------
  std::vector<int> remap(total_nodes);
  std::vector<int> root_to_id(total_nodes, -1);
  int n_unique = 0;
  for (int i = 0; i < total_nodes; ++i) {
    int r = ds.find(i);
    if (root_to_id[r] < 0) root_to_id[r] = n_unique++;
    remap[i] = root_to_id[r];
  }
  gm.n_nodes = n_unique;
  gm.x.assign(n_unique, 0.0);
  gm.y.assign(n_unique, 0.0);
  for (int i = 0; i < total_nodes; ++i) {
    if (!has_coord[i]) fatal("missing CoordinateX data for a mesh node");
    gm.x[remap[i]] = all_x[i];
    gm.y[remap[i]] = all_y[i];
  }

  // ---- Pass 4: cells with CCW orientation ---------------------------------
  gm.cells.resize(cell_recs.size());
  for (size_t i = 0; i < cell_recs.size(); ++i) {
    CellRec& cr = cell_recs[i];
    GlobalMesh::Cell& c = gm.cells[i];
    c.nv = cr.nv;
    for (int k = 0; k < cr.nv; ++k) c.v[k] = remap[cr.v[k]];
    double area2 = 0.0;
    for (int k = 0; k < cr.nv; ++k) {
      int k1 = (k + 1) % cr.nv;
      area2 += gm.x[c.v[k]] * gm.y[c.v[k1]] - gm.x[c.v[k1]] * gm.y[c.v[k]];
    }
    if (area2 < 0.0) {
      std::reverse(c.v, c.v + cr.nv);
      area2 = -area2;
    }
    c.vol = 0.5 * area2;
    if (c.vol <= 0.0) fatal("degenerate cell " + std::to_string(i));
    double sx = 0.0, sy = 0.0;
    for (int k = 0; k < cr.nv; ++k) { sx += gm.x[c.v[k]]; sy += gm.y[c.v[k]]; }
    c.cx = sx / cr.nv;
    c.cy = sy / cr.nv;
  }

  // ---- Pass 5: boundary sections (family -> BcKind) ------------------------
  std::map<std::string, BcKind> family_kind;
  for (const BcSpec& b : bcs) family_kind[b.family] = b.kind;

  std::map<uint64_t, int> section_of_edge;
  std::map<int, int> sec_index;  // section name ordinal -> gm.sections index
  for (size_t e = 0; e < edge_recs.size(); ++e) {
    EdgeRec& er = edge_recs[e];
    uint64_t key = edge_key(remap[er.a], remap[er.b]);
    // con-* sections (block interfaces) are excluded: they must stitch away.
    std::string name = section_names[er.section];
    if (name.rfind("con-", 0) == 0) continue;
    section_of_edge[key] = er.section;
  }
  for (auto& [key, sec] : section_of_edge) {
    std::string name = section_names[sec];
    auto it = family_kind.find(name);
    if (it == family_kind.end()) {
      fatal("mesh boundary section '" + name +
            "' has no mapping in case boundary_conditions");
    }
    if (!sec_index.count(sec)) {
      GlobalMesh::Section s;
      s.family = name;
      s.bc = (int)it->second;
      sec_index[sec] = (int)gm.sections.size();
      gm.sections.push_back(std::move(s));
    }
  }

  // ---- Pass 6: build faces from cell edges --------------------------------
  struct Pending {
    int cell, slot;
  };
  std::unordered_map<uint64_t, Pending> pending;

  for (size_t ci = 0; ci < gm.cells.size(); ++ci) {
    GlobalMesh::Cell& c = gm.cells[ci];
    for (int k = 0; k < c.nv; ++k) {
      int a = c.v[k], b = c.v[(k + 1) % c.nv];
      uint64_t key = edge_key(a, b);
      auto it = pending.find(key);
      if (it == pending.end()) {
        pending[key] = Pending{(int)ci, k};
      } else {
        Pending& first = it->second;
        GlobalMesh::Face f;
        f.v0 = gm.cells[first.cell].v[first.slot];
        f.v1 = gm.cells[first.cell].v[(first.slot + 1) % gm.cells[first.cell].nv];
        f.c0 = first.cell;
        f.c1 = (int)ci;
        double ex = gm.x[f.v1] - gm.x[f.v0];
        double ey = gm.y[f.v1] - gm.y[f.v0];
        f.area = std::sqrt(ex * ex + ey * ey);
        f.fx = 0.5 * (gm.x[f.v0] + gm.x[f.v1]);
        f.fy = 0.5 * (gm.y[f.v0] + gm.y[f.v1]);
        f.nx = ey / f.area;   // outward from c0 (CCW orientation)
        f.ny = -ex / f.area;
        f.bc = -1;
        gm.faces.push_back(f);
        pending.erase(it);
      }
    }
  }

  // Remaining pending edges are boundary faces.
  for (auto& [key, p] : pending) {
    GlobalMesh::Cell& c = gm.cells[p.cell];
    GlobalMesh::Face f;
    f.v0 = c.v[p.slot];
    f.v1 = c.v[(p.slot + 1) % c.nv];
    f.c0 = p.cell;
    f.c1 = -1;
    double ex = gm.x[f.v1] - gm.x[f.v0];
    double ey = gm.y[f.v1] - gm.y[f.v0];
    f.area = std::sqrt(ex * ex + ey * ey);
    f.fx = 0.5 * (gm.x[f.v0] + gm.x[f.v1]);
    f.fy = 0.5 * (gm.y[f.v0] + gm.y[f.v1]);
    f.nx = ey / f.area;
    f.ny = -ex / f.area;
    auto sit = section_of_edge.find(key);
    if (sit == section_of_edge.end()) {
      // Could be a con-* edge that failed to stitch, or an unknown boundary.
      fatal("unmatched mesh boundary edge (mesh stitching error or unmapped section)");
    }
    int sec = sit->second;
    f.bc = (int)family_kind.at(section_names[sec]);
    f.section = sec_index[sec];
    gm.sections[sec_index[sec]].face_ids.push_back((int)gm.faces.size());
    gm.faces.push_back(f);
  }
  int nb = 0;
  for (auto& f : gm.faces)
    if (f.c1 < 0) ++nb;
  gm.num_boundary_faces = nb;
  gm.n_faces_global = (int)gm.faces.size();

  cg_close(fn);
  return gm;
}

// ---------------------------------------------------------------------------
// METIS partition + distribution
// ---------------------------------------------------------------------------
namespace {

struct RankPiece {
  std::vector<int> owned;   // global cell ids, ascending
  std::vector<int> ghosts;  // global cell ids, ascending
  std::vector<int> ghost_owner;
  std::vector<int> ghost_remote;
  std::vector<int> local_id_of;  // global -> local, size = n_cells_global
  ByteBuf buf;
};

void build_rank_piece(const GlobalMesh& gm, const std::vector<int>& part, int np, int rank,
                      const std::vector<std::vector<int>>& owned_all, RankPiece& out) {
  const int ng = (int)gm.cells.size();
  out.local_id_of.assign(ng, -1);
  for (int c = 0; c < ng; ++c)
    if (part[c] == rank) out.owned.push_back(c);
  std::sort(out.owned.begin(), out.owned.end());
  for (size_t i = 0; i < out.owned.size(); ++i) out.local_id_of[out.owned[i]] = (int)i;

  std::set<int> ghost_set;
  std::map<int, int> ghost_owner_map;   // global cell -> owner rank
  for (const auto& f : gm.faces) {
    if (f.c1 < 0) continue;
    if (part[f.c0] == rank && part[f.c1] != rank) {
      ghost_set.insert(f.c1);
      ghost_owner_map[f.c1] = part[f.c1];
    }
    if (part[f.c1] == rank && part[f.c0] != rank) {
      ghost_set.insert(f.c0);
      ghost_owner_map[f.c0] = part[f.c0];
    }
  }
  for (int c : ghost_set) out.ghosts.push_back(c);
  for (int c : out.ghosts) {
    out.ghost_owner.push_back(ghost_owner_map[c]);
    const std::vector<int>& ow = owned_all[ghost_owner_map[c]];
    auto it = std::lower_bound(ow.begin(), ow.end(), c);
    if (it == ow.end() || *it != c) fatal("internal error: ghost not in owner's owned list");
    out.ghost_remote.push_back((int)(it - ow.begin()));
  }

  // ---- pack local mesh -----------------------------------------------------
  ByteBuf& b = out.buf;
  const int n_owned = (int)out.owned.size();
  const int n_ghost = (int)out.ghosts.size();
  const int n_cells = n_owned + n_ghost;
  std::vector<int> local_of_global(ng, -1);
  for (int i = 0; i < n_owned; ++i) local_of_global[out.owned[i]] = i;
  for (int i = 0; i < n_ghost; ++i) local_of_global[out.ghosts[i]] = n_owned + i;

  // local node table
  std::set<int> node_set;
  for (int c : out.owned)
    for (int k = 0; k < gm.cells[c].nv; ++k) node_set.insert(gm.cells[c].v[k]);
  for (int c : out.ghosts)
    for (int k = 0; k < gm.cells[c].nv; ++k) node_set.insert(gm.cells[c].v[k]);
  std::vector<int> nodes(node_set.begin(), node_set.end());
  std::map<int, int> node_local;
  for (size_t i = 0; i < nodes.size(); ++i) node_local[nodes[i]] = (int)i;

  b.push_i(0x43464431);  // 'CFD1'
  b.push_i(n_owned);
  b.push_i(n_ghost);
  b.push_i((int)nodes.size());
  b.push_i((int)gm.faces.size() == 0 ? 0 : 0);  // reserved
  for (int c : out.owned) {
    const auto& cell = gm.cells[c];
    b.push_i(c);
    b.push_i(-1);  // owner
    b.push_i(-1);  // remote
    b.push_i(cell.nv);
    for (int k = 0; k < 4; ++k) b.push_i(k < cell.nv ? node_local[cell.v[k]] : -1);
    b.push_d(cell.cx);
    b.push_d(cell.cy);
    b.push_d(cell.vol);
  }
  for (int i = 0; i < n_ghost; ++i) {
    int c = out.ghosts[i];
    const auto& cell = gm.cells[c];
    b.push_i(c);
    b.push_i(out.ghost_owner[i]);
    b.push_i(out.ghost_remote[i]);
    b.push_i(cell.nv);
    for (int k = 0; k < 4; ++k) b.push_i(k < cell.nv ? node_local[cell.v[k]] : -1);
    b.push_d(cell.cx);
    b.push_d(cell.cy);
    b.push_d(cell.vol);
  }
  for (int nid : nodes) {
    b.push_i(nid);
    b.push_d(gm.x[nid]);
    b.push_d(gm.y[nid]);
  }
  // faces: only those touching owned cells
  int n_faces = 0;
  for (const auto& f : gm.faces) {
    bool inc = (f.c1 < 0 && part[f.c0] == rank) ||
               (f.c0 >= 0 && part[f.c0] == rank) ||
               (f.c1 >= 0 && part[f.c1] == rank);
    if (inc) ++n_faces;
  }
  b.push_i(n_faces);
  for (const auto& f : gm.faces) {
    bool inc = (f.c1 < 0 && part[f.c0] == rank) ||
               (f.c0 >= 0 && part[f.c0] == rank) ||
               (f.c1 >= 0 && part[f.c1] == rank);
    if (!inc) continue;
    b.push_i(local_of_global[f.c0]);
    b.push_i(f.c1 < 0 ? -1 : local_of_global[f.c1]);
    b.push_i(f.bc);
    b.push_i(&f - gm.faces.data());
    b.push_i(f.c1 < 0 ? node_local[f.v0] : -1);
    b.push_i(f.c1 < 0 ? node_local[f.v1] : -1);
    if (f.c1 < 0) {
      const std::string& fam = gm.sections[f.section].family;
      b.push_i((int)fam.size());
      for (char ch : fam) b.push_i((int)(unsigned char)ch);
    }
    b.push_d(f.fx);
    b.push_d(f.fy);
    b.push_d(f.nx);
    b.push_d(f.ny);
    b.push_d(f.area);
  }
}

void unpack_piece(const ByteBuf& b, const CaseConfig& cfg, LocalMesh& lm,
                  std::vector<int>& neighbor_ranks_out) {
  size_t p = 0;
  int magic = b.get_i(p);
  if (magic != 0x43464431) fatal("internal error: bad partition packet");
  int n_owned = b.get_i(p);
  int n_ghost = b.get_i(p);
  int n_nodes = b.get_i(p);
  b.get_i(p);  // reserved
  lm.n_owned = n_owned;
  lm.n_cells = n_owned + n_ghost;
  lm.cells.resize(lm.n_cells);
  lm.node_x.assign(n_nodes, 0.0);
  lm.node_y.assign(n_nodes, 0.0);
  lm.node_global_id.assign(n_nodes, 0);
  for (int i = 0; i < lm.n_cells; ++i) {
    auto& c = lm.cells[i];
    c.global_id = b.get_i(p);
    c.owner = b.get_i(p);
    c.remote_id = b.get_i(p);
    c.nv = b.get_i(p);
    for (int k = 0; k < 4; ++k) c.v[k] = b.get_i(p);
    c.cx = b.get_d(p);
    c.cy = b.get_d(p);
    c.vol = b.get_d(p);
  }
  for (int i = 0; i < n_nodes; ++i) {
    lm.node_global_id[i] = b.get_i(p);
    lm.node_x[i] = b.get_d(p);
    lm.node_y[i] = b.get_d(p);
  }
  int n_faces = b.get_i(p);
  lm.faces.resize(n_faces);
  lm.cell_faces.assign(lm.n_cells, {});
  int nb = 0;
  for (int i = 0; i < n_faces; ++i) {
    auto& f = lm.faces[i];
    f.c0 = b.get_i(p);
    f.c1 = b.get_i(p);
    f.bc = b.get_i(p);
    f.global_id = b.get_i(p);
    f.v0 = b.get_i(p);
    f.v1 = b.get_i(p);
    if (f.c1 < 0) {
      int taglen = b.get_i(p);
      f.tag.resize(taglen);
      for (int k = 0; k < taglen; ++k)
        f.tag[k] = (char)(unsigned char)b.get_i(p);
    }
    f.fx = b.get_d(p);
    f.fy = b.get_d(p);
    f.nx = b.get_d(p);
    f.ny = b.get_d(p);
    f.area = b.get_d(p);
    if (f.c0 < 0 || f.c0 >= lm.n_cells) fatal("internal error: face c0 out of range");
    if (f.c1 < 0) {
      ++nb;
      if (f.bc == (int)BcKind::SlipWall || f.bc == (int)BcKind::NoSlipAdiabaticWall)
        lm.wall_faces.push_back(i);
      if (f.bc == (int)BcKind::Farfield) lm.far_faces.push_back(i);
    }
    lm.cell_faces[f.c0].push_back(i);
    if (f.c1 >= 0) lm.cell_faces[f.c1].push_back(i);
  }
  lm.num_boundary_faces = nb;

  // Sweep order: by default sorted by centroid x (then y) to follow the
  // dominant advection direction; the natural (partition) order is available
  // for experiments via CFD_SWEEP_ORDER=natural.
  static int natural_order = -1;
  if (natural_order < 0) {
    const char* e = std::getenv("CFD_SWEEP_ORDER");
    natural_order = e && std::string(e) == "natural" ? 1 : 0;
  }
  lm.sweep_order.resize(n_owned);
  std::iota(lm.sweep_order.begin(), lm.sweep_order.end(), 0);
  if (!natural_order) {
    std::sort(lm.sweep_order.begin(), lm.sweep_order.end(), [&](int a, int b) {
      if (lm.cells[a].cx != lm.cells[b].cx) return lm.cells[a].cx < lm.cells[b].cx;
      return lm.cells[a].cy < lm.cells[b].cy;
    });
  }
  lm.sweep_pos.assign(n_owned, -1);
  for (int p = 0; p < n_owned; ++p) lm.sweep_pos[lm.sweep_order[p]] = p;

  // freestream-aligned tangent for wall faces (used in surface output / cf)
  double ux = cfg.u_inf;
  double uy = 0.0;
  if (cfg.aoa_degrees != 0.0) {
    double a = cfg.aoa_degrees * 3.14159265358979323846 / 180.0;
    ux = cfg.u_inf * std::cos(a);
    uy = cfg.u_inf * std::sin(a);
  }
  for (int i : lm.wall_faces) {
    auto& f = lm.faces[i];
    double tx = f.ny, ty = -f.nx;
    if (tx * ux + ty * uy < 0.0) { tx = -tx; ty = -ty; }
    f.tx = tx;
    f.ty = ty;
  }

  // ---- halo topology -------------------------------------------------------
  std::set<int> nbr_set;
  for (int i = n_owned; i < lm.n_cells; ++i) nbr_set.insert(lm.cells[i].owner);
  lm.neighbor_ranks.assign(nbr_set.begin(), nbr_set.end());
  lm.send_cells.resize(lm.neighbor_ranks.size());
  lm.recv_cells.resize(lm.neighbor_ranks.size());
  lm.recv_remote.resize(lm.neighbor_ranks.size());
  for (int i = n_owned; i < lm.n_cells; ++i) {
    int ni = lm.neighbor_index(lm.cells[i].owner);
    lm.recv_cells[ni].push_back(i);
    lm.recv_remote[ni].push_back(lm.cells[i].remote_id);
  }
  // send lists: owned cells that are ghosts on neighbors. Because faces carry
  // both sides, scan faces: if c1 is ghost then c0 is sent to c1's owner.
  std::vector<std::set<int>> send_set(lm.neighbor_ranks.size());
  for (const auto& f : lm.faces) {
    if (f.c1 < 0) continue;
    if (lm.is_ghost(f.c1)) {
      int ni = lm.neighbor_index(lm.cells[f.c1].owner);
      send_set[ni].insert(f.c0);
    }
    if (lm.is_ghost(f.c0)) {
      int ni = lm.neighbor_index(lm.cells[f.c0].owner);
      send_set[ni].insert(f.c1);
    }
  }
  for (size_t k = 0; k < lm.neighbor_ranks.size(); ++k) {
    lm.send_cells[k].assign(send_set[k].begin(), send_set[k].end());
    // Sort send_cells by global ID so the order matches the remote recv_cells.
    std::sort(lm.send_cells[k].begin(), lm.send_cells[k].end(),
              [&](int a, int b) { return lm.cells[a].global_id < lm.cells[b].global_id; });
  }
  // Sort recv_cells by the ghost cell's own global ID so the order matches
  // the sender's send_cells (which are sorted by global ID).
  for (size_t k = 0; k < lm.neighbor_ranks.size(); ++k) {
    std::vector<int> idx(lm.recv_cells[k].size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&](int a, int b) {
                return lm.cells[lm.recv_cells[k][a]].global_id <
                       lm.cells[lm.recv_cells[k][b]].global_id;
              });
    std::vector<int> sorted_recv(lm.recv_cells[k].size());
    std::vector<int> sorted_remote(lm.recv_remote[k].size());
    for (size_t i = 0; i < idx.size(); ++i) {
      sorted_recv[i] = lm.recv_cells[k][idx[i]];
      sorted_remote[i] = lm.recv_remote[k][idx[i]];
    }
    lm.recv_cells[k] = sorted_recv;
    lm.recv_remote[k] = sorted_remote;
  }
  neighbor_ranks_out = lm.neighbor_ranks;
}

}  // namespace

int LocalMesh::neighbor_index(int rank) const {
  auto it = std::lower_bound(neighbor_ranks.begin(), neighbor_ranks.end(), rank);
  if (it == neighbor_ranks.end() || *it != rank) return -1;
  return (int)(it - neighbor_ranks.begin());
}

void partition_and_scatter(GlobalMesh& gm, const CaseConfig& cfg, LocalMesh& lm,
                           int& edge_cut, int& n_faces_global) {
  const int np = g_nranks;
  edge_cut = 0;
  n_faces_global = gm.n_faces_global;
  std::vector<int> part;

  if (g_rank == 0) {
    // ---- build cell adjacency CSR ------------------------------------------
    const int ng = (int)gm.cells.size();
    std::vector<std::vector<int>> adj(ng);
    for (const auto& f : gm.faces) {
      if (f.c1 < 0) continue;
      adj[f.c0].push_back(f.c1);
      adj[f.c1].push_back(f.c0);
    }
    std::vector<idx_t> xadj(ng + 1), adjncy;
    xadj[0] = 0;
    for (int i = 0; i < ng; ++i) {
      std::sort(adj[i].begin(), adj[i].end());
      adj[i].erase(std::unique(adj[i].begin(), adj[i].end()), adj[i].end());
      adjncy.insert(adjncy.end(), adj[i].begin(), adj[i].end());
      xadj[i + 1] = (idx_t)adjncy.size();
    }
    part.assign(ng, 0);
    if (ng > 0 && np > 1) {
      idx_t nvtxs = ng, ncon = 1, nparts = np;
      idx_t objval = 0;
      idx_t options[METIS_NOPTIONS];
      METIS_SetDefaultOptions(options);
      options[METIS_OPTION_NUMBERING] = 0;
      options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
      options[METIS_OPTION_CTYPE] = METIS_CTYPE_SHEM;
      options[METIS_OPTION_IPTYPE] = METIS_IPTYPE_EDGE;
      options[METIS_OPTION_RTYPE] = METIS_RTYPE_FM;
      options[METIS_OPTION_NITER] = 20;
      options[METIS_OPTION_DBGLVL] = 0;
      options[METIS_OPTION_SEED] = 20260808;
      int rc = METIS_PartGraphKway(&nvtxs, &ncon, xadj.data(), adjncy.data(), nullptr, nullptr,
                                   nullptr, &nparts, nullptr, nullptr, options, &objval,
                                   part.data());
      if (rc != METIS_OK) fatal("METIS_PartGraphKway failed");
      edge_cut = (int)objval;
    }
  }

  MPI_Bcast(&edge_cut, 1, MPI_INT, 0, MPI_COMM_WORLD);

  // ---- build pieces on rank 0 and distribute --------------------------------
  std::vector<RankPiece> pieces;
  if (g_rank == 0) {
    // owned lists for every rank (deterministic, ascending)
    std::vector<std::vector<int>> owned_all(np);
    for (int c = 0; c < (int)gm.cells.size(); ++c) owned_all[part[c]].push_back(c);
    for (auto& v : owned_all) std::sort(v.begin(), v.end());
    pieces.resize(np);
    for (int r = 0; r < np; ++r)
      build_rank_piece(gm, part, np, r, owned_all, pieces[r]);
  }

  if (g_rank == 0) {
    for (int r = 1; r < np; ++r) {
      int n = (int)pieces[r].buf.size();
      MPI_Send(&n, 1, MPI_INT, r, 11, MPI_COMM_WORLD);
      if (n > 0)
        MPI_Send(pieces[r].buf.bytes(), n, MPI_BYTE, r, 12, MPI_COMM_WORLD);
    }
    std::vector<int> nbrs;
    unpack_piece(ByteBuf(pieces[0].buf), cfg, lm, nbrs);
  } else {
    int n = 0;
    MPI_Recv(&n, 1, MPI_INT, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    ByteBuf b;
    if (n > 0) {
      b.raw().resize(n);
      MPI_Recv(b.raw().data(), n, MPI_BYTE, 0, 12, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    std::vector<int> nbrs;
    unpack_piece(b, cfg, lm, nbrs);
  }

  // Free the full global mesh on every rank; solver iterations only ever touch
  // the rank-local mesh.
  gm.clear();
  MPI_Barrier(MPI_COMM_WORLD);
}

void print_mesh_summary(const GlobalMesh& gm, const CaseConfig& cfg) {
  log0("mesh: " + cfg.mesh_file);
  log0("  nodes            : " + std::to_string(gm.n_nodes));
  log0("  cells            : " + std::to_string(gm.cells.size()));
  log0("  faces (global)   : " + std::to_string(gm.n_faces_global));
  log0("  boundary faces   : " + std::to_string(gm.num_boundary_faces));
  for (const auto& s : gm.sections) {
    std::string kind;
    switch (s.bc) {
      case (int)BcKind::Farfield: kind = "farfield"; break;
      case (int)BcKind::SlipWall: kind = "slip_wall"; break;
      case (int)BcKind::NoSlipAdiabaticWall: kind = "no_slip_adiabatic_wall"; break;
    }
    log0("  section '" + s.family + "' (" + kind + "): " +
         std::to_string(s.face_ids.size()) + " faces");
  }
  double vol = 0.0;
  for (const auto& c : gm.cells) vol += c.vol;
  log0("  total area       : " + fmt1("%.6f", vol));
}

}  // namespace cfd
