#include "mesh/GlobalMesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>

#ifndef CGNS_MAX_NAME_LENGTH
#define CGNS_MAX_NAME_LENGTH 32
#endif

namespace cfds {

namespace {

// ---------------------------------------------------------------------------
// Small CGNS RAII wrapper (C API, 64-bit sizes).
// ---------------------------------------------------------------------------
#include <cgnslib.h>

struct CgnsFile {
  int fn = -1;
  explicit CgnsFile(const std::string& path) {
    if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK)
      throw std::runtime_error("cg_open failed for " + path + ": " + cg_get_error());
  }
  ~CgnsFile() {
    if (fn >= 0) cg_close(fn);
  }
};

// Multi-zone reader state: nodes, cells and boundary faces collected while
// walking each zone; interfaces stitched through a node-equivalence map.
struct Builder {
  std::vector<Vec2> nodes;
  std::vector<Cell> cells;
  // Per-zone element sections (name -> [range_start, range_end, element_type]).
  struct Section {
    std::string name;
    int elem_type = 0;
    cgsize_t start = 0;
    cgsize_t end = 0;
    int section_number = 0;
  };
  struct ZoneData {
    std::string name;
    std::vector<Section> sections;
    cgsize_t node_count = 0;
  };
  std::vector<ZoneData> zones;
  // Node equivalence: zone-local node id -> global node id.
  std::vector<std::vector<int>> zone_node_map;
  std::unordered_map<long long, int> node_hash_to_global;  // coord hash -> node

  static long long hash_coord(double x, double y) {
    // Quantize to 1e-12 to make coincident interface nodes match exactly.
    long long hx = static_cast<long long>(std::llround(x * 1e12));
    long long hy = static_cast<long long>(std::llround(y * 1e12));
    return (hx << 20) ^ hy;
  }

  int get_or_add_node(double x, double y) {
    long long h = hash_coord(x, y);
    auto it = node_hash_to_global.find(h);
    if (it != node_hash_to_global.end()) return it->second;
    int id = static_cast<int>(nodes.size());
    nodes.push_back({x, y});
    node_hash_to_global.emplace(h, id);
    return id;
  }
};

// Element types we accept: TRI_3 and QUAD_4.
int element_nnodes(int elem_type) {
  if (elem_type == TRI_3) return 3;
  if (elem_type == QUAD_4) return 4;
  if (elem_type == BAR_2) return 2;   // boundary (edge) elements
  return 0;
}

}  // namespace

std::vector<char> serialize_global_mesh(const GlobalMesh& mesh) {
  std::vector<char> out;
  auto push = [&](const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    out.insert(out.end(), c, c + n);
  };
  auto push_i = [&](int v) { push(&v, sizeof(v)); };
  auto push_d = [&](double v) { push(&v, sizeof(v)); };
  push_i(static_cast<int>(mesh.nodes.size()));
  for (const auto& p : mesh.nodes) { push_d(p[0]); push_d(p[1]); }
  push_i(static_cast<int>(mesh.cells.size()));
  for (const auto& c : mesh.cells) {
    push_i(static_cast<int>(c.nodes.size()));
    for (int n : c.nodes) push_i(n);
    push_d(c.volume);
    push_d(c.centroid[0]);
    push_d(c.centroid[1]);
  }
  push_i(static_cast<int>(mesh.faces.size()));
  for (const auto& f : mesh.faces) {
    push_i(f.n0); push_i(f.n1); push_i(f.cellL); push_i(f.cellR);
    push_d(f.length);
    push_d(f.centroid[0]); push_d(f.centroid[1]);
    push_d(f.normal[0]); push_d(f.normal[1]);
    push_i(static_cast<int>(f.bc));
    push_i(static_cast<int>(f.family.size()));
    out.insert(out.end(), f.family.begin(), f.family.end());
  }
  push_i(static_cast<int>(mesh.cell_face_offsets.size()));
  for (int v : mesh.cell_face_offsets) push_i(v);
  push_i(static_cast<int>(mesh.cell_faces.size()));
  for (int v : mesh.cell_faces) push_i(v);
  push_i(static_cast<int>(mesh.cell_neighbor_offsets.size()));
  for (int v : mesh.cell_neighbor_offsets) push_i(v);
  push_i(static_cast<int>(mesh.cell_neighbors.size()));
  for (int v : mesh.cell_neighbors) push_i(v);
  return out;
}

GlobalMesh deserialize_global_mesh(const char* data, size_t size) {
  GlobalMesh mesh;
  size_t pos = 0;
  auto get = [&](void* p, size_t n) {
    if (pos + n > size) throw std::runtime_error("corrupt mesh payload");
    std::memcpy(p, data + pos, n);
    pos += n;
  };
  auto get_i = [&]() { int v; get(&v, sizeof(v)); return v; };
  auto get_d = [&]() { double v; get(&v, sizeof(v)); return v; };
  int nn = get_i();
  mesh.nodes.resize(nn);
  for (auto& p : mesh.nodes) { p[0] = get_d(); p[1] = get_d(); }
  int nc = get_i();
  mesh.cells.resize(nc);
  for (auto& c : mesh.cells) {
    int k = get_i();
    c.nodes.resize(k);
    for (auto& n : c.nodes) n = get_i();
    c.volume = get_d();
    c.centroid[0] = get_d();
    c.centroid[1] = get_d();
  }
  int nf = get_i();
  mesh.faces.resize(nf);
  for (auto& f : mesh.faces) {
    f.n0 = get_i(); f.n1 = get_i(); f.cellL = get_i(); f.cellR = get_i();
    f.length = get_d();
    f.centroid[0] = get_d(); f.centroid[1] = get_d();
    f.normal[0] = get_d(); f.normal[1] = get_d();
    f.bc = static_cast<BcType>(get_i());
    int slen = get_i();
    if (pos + slen > size) throw std::runtime_error("corrupt mesh payload (fam)");
    f.family.assign(data + pos, data + pos + slen);
    pos += slen;
  }
  int k = get_i();
  mesh.cell_face_offsets.resize(k);
  for (auto& v : mesh.cell_face_offsets) v = get_i();
  k = get_i();
  mesh.cell_faces.resize(k);
  for (auto& v : mesh.cell_faces) v = get_i();
  k = get_i();
  mesh.cell_neighbor_offsets.resize(k);
  for (auto& v : mesh.cell_neighbor_offsets) v = get_i();
  k = get_i();
  mesh.cell_neighbors.resize(k);
  for (auto& v : mesh.cell_neighbors) v = get_i();
  return mesh;
}

bool bc_from_string(const std::string& name, BcType& out) {
  if (name == "farfield") out = BcType::Farfield;
  else if (name == "slip_wall") out = BcType::SlipWall;
  else if (name == "no_slip_adiabatic_wall") out = BcType::NoSlipAdiabaticWall;
  else return false;
  return true;
}

const char* bc_to_string(BcType bc) {
  switch (bc) {
    case BcType::Farfield: return "farfield";
    case BcType::SlipWall: return "slip_wall";
    case BcType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
    case BcType::Interface: return "interface";
    default: return "unknown";
  }
}

namespace {

}  // namespace

GlobalMesh load_cgns_mesh(const std::string& path) {
  CgnsFile file(path);
  const int fn = file.fn;

  int nzones = 0;
  if (cg_nzones(fn, 1, &nzones) != CG_OK || nzones < 1)
    throw std::runtime_error("no zones found in " + path);

  Builder b;
  b.zones.resize(nzones);

  // First pass: read zone names, node counts, element sections.
  for (int iz = 1; iz <= nzones; ++iz) {
    auto& zd = b.zones[iz - 1];
    char zname[CGNS_MAX_NAME_LENGTH + 1] = {0};
    cgsize_t sizes[9] = {0};
    if (cg_zone_read(fn, 1, iz, zname, sizes) != CG_OK)
      throw std::runtime_error("cg_zone_read failed for zone " + std::to_string(iz));
    zd.name = zname;
    ZoneType_t zt;
    cg_zone_type(fn, 1, iz, &zt);
    if (zt != CGNS_ENUMV(Unstructured))
      throw std::runtime_error("only unstructured zones supported, zone " +
                               std::to_string(iz) + " is structured");
    zd.node_count = sizes[0];

    int nsections = 0;
    cg_nsections(fn, 1, iz, &nsections);
    for (int is = 1; is <= nsections; ++is) {
      char sname[CGNS_MAX_NAME_LENGTH + 1] = {0};
      CGNS_ENUMT(ElementType_t) etype;
      cgsize_t start = 0, end = 0;
      int nbnd = 0, parent_flag = 0;
      if (cg_section_read(fn, 1, iz, is, sname, &etype, &start, &end,
                          &nbnd, &parent_flag) != CG_OK)
        throw std::runtime_error("cg_section_read failed in zone " + std::to_string(iz));
      Builder::Section sec;
      sec.name = sname;
      sec.elem_type = static_cast<int>(etype);
      sec.start = start;
      sec.end = end;
      sec.section_number = is;
      zd.sections.push_back(sec);
    }
  }

  // Assign node offsets and merge coordinates.
  b.zone_node_map.resize(nzones);
  for (int iz = 1; iz <= nzones; ++iz) {
    const auto& zd = b.zones[iz - 1];
    const cgsize_t nnodes = zd.node_count;
    b.zone_node_map[iz - 1].resize(static_cast<size_t>(nnodes));
    // Read coordinates.
    std::vector<double> x(nnodes), y(nnodes);
    cgsize_t rmin = 1, rmax = nnodes;
    if (cg_coord_read(fn, 1, iz, "CoordinateX", RealDouble, &rmin, &rmax, x.data()) != CG_OK)
      throw std::runtime_error("cg_coord_read X failed in zone " + std::to_string(iz));
    if (cg_coord_read(fn, 1, iz, "CoordinateY", RealDouble, &rmin, &rmax, y.data()) != CG_OK)
      throw std::runtime_error("cg_coord_read Y failed in zone " + std::to_string(iz));
    // Some files also carry CoordinateZ; ignore it for 2-D.
    for (cgsize_t i = 0; i < nnodes; ++i) {
      int gid = b.get_or_add_node(x[i], y[i]);
      b.zone_node_map[iz - 1][i] = gid;
    }
  }

  // Second pass: read cells from element sections whose type is TRI_3/QUAD_4.
  for (int iz = 1; iz <= nzones; ++iz) {
    const auto& zd = b.zones[iz - 1];
    for (const auto& sec : zd.sections) {
      const int nn = element_nnodes(sec.elem_type);
      if (nn != 3 && nn != 4) continue;  // boundary/interface sections or unsupported
      const cgsize_t count = sec.end - sec.start + 1;
      std::vector<cgsize_t> conn(static_cast<size_t>(count) * nn);
      if (cg_elements_read(fn, 1, iz, sec.section_number,
                           conn.data(), nullptr) != CG_OK)
        throw std::runtime_error("cg_elements_read failed for " + sec.name);
      for (cgsize_t e = 0; e < count; ++e) {
        Cell cell;
        cell.nodes.reserve(nn);
        for (int k = 0; k < nn; ++k) {
          const cgsize_t local_node = conn[static_cast<size_t>(e) * nn + k];
          cell.nodes.push_back(b.zone_node_map[iz - 1][local_node - 1]);
        }
        b.cells.push_back(std::move(cell));
      }
    }
  }

  // -------------------------------------------------------------------------
  // Build faces from cell edges. Each undirected node pair (key) appears once
  // for a boundary edge and twice for an interior edge (including across
  // zones, thanks to the merged node numbering).
  // -------------------------------------------------------------------------
  struct EdgeKey {
    int a, b;
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
  };
  struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const {
      return (static_cast<std::size_t>(k.a) << 20) ^ static_cast<std::size_t>(k.b);
    }
  };
  std::unordered_map<EdgeKey, int, EdgeKeyHash> edge_face;

  // Geometry helpers.
  auto polygon_centroid = [](const std::vector<Vec2>& pts) -> Vec2 {
    // Exact centroid for simple polygon via signed-area decomposition.
    const int n = static_cast<int>(pts.size());
    double A = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < n; ++i) {
      const Vec2& p = pts[i];
      const Vec2& q = pts[(i + 1) % n];
      const double cross = p[0] * q[1] - q[0] * p[1];
      A += cross;
      cx += (p[0] + q[0]) * cross;
      cy += (p[1] + q[1]) * cross;
    }
    A *= 0.5;
    if (std::abs(A) < 1e-300) return pts[0];
    return {cx / (6.0 * A), cy / (6.0 * A)};
  };

  GlobalMesh mesh;
  mesh.nodes = b.nodes;
  mesh.cells.resize(b.cells.size());
  for (size_t ci = 0; ci < b.cells.size(); ++ci) {
    auto& cell = mesh.cells[ci];
    cell.nodes = b.cells[ci].nodes;
    // Orient the cell counter-clockwise using the signed area.
    const auto& pts = cell.nodes;
    double area2 = 0.0;
    for (size_t i = 0; i < pts.size(); ++i) {
      const Vec2& p = mesh.nodes[pts[i]];
      const Vec2& q = mesh.nodes[pts[(i + 1) % pts.size()]];
      area2 += p[0] * q[1] - q[0] * p[1];
    }
    if (area2 < 0.0) std::reverse(cell.nodes.begin(), cell.nodes.end());
    std::vector<Vec2> pts_v;
    pts_v.reserve(cell.nodes.size());
    for (int n : cell.nodes) pts_v.push_back(mesh.nodes[n]);
    cell.centroid = polygon_centroid(pts_v);
    cell.volume = std::abs(area2) * 0.5;
    if (!(cell.volume > 0.0))
      throw std::runtime_error("degenerate cell " + std::to_string(ci));
  }

  // Build faces.
  for (size_t ci = 0; ci < mesh.cells.size(); ++ci) {
    const auto& cell = mesh.cells[ci];
    const int n = static_cast<int>(cell.nodes.size());
    for (int e = 0; e < n; ++e) {
      const int a = cell.nodes[e];
      const int b = cell.nodes[(e + 1) % n];
      const EdgeKey key{a < b ? a : b, a < b ? b : a};
      auto it = edge_face.find(key);
      if (it == edge_face.end()) {
        Face face;
        // Keep the nodes in the first cell's traversal order so the normal
        // (dy, -dx) points OUTWARD from that cell (boundary faces must be
        // oriented out of the domain). The sorted key only serves the map.
        face.n0 = a;
        face.n1 = b;
        face.cellL = static_cast<int>(ci);
        face.cellR = -1;
        const Vec2& pa = mesh.nodes[a];
        const Vec2& pb = mesh.nodes[b];
        const double dx = pb[0] - pa[0];
        const double dy = pb[1] - pa[1];
        face.length = std::sqrt(dx * dx + dy * dy);
        face.centroid = {(pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5};
        face.normal = {dy / face.length, -dx / face.length};
        const int fid = static_cast<int>(mesh.faces.size());
        mesh.faces.push_back(face);
        edge_face.emplace(key, fid);
      } else {
        Face& face = mesh.faces[it->second];
        if (face.cellR != -1)
          throw std::runtime_error("edge shared by more than two cells (non-manifold)");
        face.cellR = static_cast<int>(ci);
        // If the second cell's traversal direction makes the stored normal
        // point INTO the second cell, flip the face so normal points L->R.
        // We keep cellL fixed; cellR is stored as-is and the solver uses
        // signed normals per cell.
      }
    }
  }

  // -------------------------------------------------------------------------
  // Boundary faces: attach BC kinds from boundary sections. Boundary sections
  // (e.g. "bc-2", "bc-4", "WALL", "FAR", "con-*") appear as element sets whose
  // element ids map to faces built above. We match them by node pairs.
  // -------------------------------------------------------------------------
  std::unordered_map<EdgeKey, int, EdgeKeyHash> boundary_edge_map;
  for (int iz = 1; iz <= nzones; ++iz) {
    const auto& zd = b.zones[iz - 1];
    int nbocos = 0;
    cg_nbocos(fn, 1, iz, &nbocos);
    for (int ib = 1; ib <= nbocos; ++ib) {
      char bname[CGNS_MAX_NAME_LENGTH + 1] = {0};
      CGNS_ENUMT(BCType_t) btype;
      CGNS_ENUMT(PointSetType_t) ptype;
      cgsize_t npnts = 0;
      int normal_index = 0;
      cgsize_t normal_list_size = 0;
      CGNS_ENUMT(DataType_t) ndtype;
      int ndataset = 0;
      if (cg_boco_info(fn, 1, iz, ib, bname, &btype, &ptype, &npnts,
                       &normal_index, &normal_list_size, &ndtype, &ndataset) != CG_OK)
        continue;
      // Family name of the BC node (via cg_goto + cg_famname_read).
      char famname[CGNS_MAX_NAME_LENGTH + 1] = {0};
      if (cg_goto(fn, 1, "Zone_t", iz, "ZoneBC_t", 1, "BC_t", ib, nullptr) == CG_OK)
        cg_famname_read(famname);
      // Find the element section with this BC name. The BC point range
      // refers to 1-based element ids inside that boundary section.
      const auto sec_it = std::find_if(zd.sections.begin(), zd.sections.end(),
          [&](const Builder::Section& s) { return s.name == bname; });
      if (sec_it == zd.sections.end()) continue;
      const Builder::Section& sec = *sec_it;
      const int nn = element_nnodes(sec.elem_type);
      if (nn != 2) continue;
      const cgsize_t count = sec.end - sec.start + 1;
      std::vector<cgsize_t> conn(static_cast<size_t>(count) * nn);
      if (cg_elements_read(fn, 1, iz, sec.section_number,
                           conn.data(), nullptr) != CG_OK)
        continue;
      std::vector<cgsize_t> pnts(npnts > 0 ? npnts : 1);
      if (npnts == 0) continue;
      if (cg_boco_read(fn, 1, iz, ib, pnts.data(), nullptr) != CG_OK) continue;
      // Points are element indices within the boundary section: a range
      // (two points) or a list. Resolve the 0-based row in the section.
      auto resolve_row = [&](cgsize_t pos, cgsize_t& row) -> bool {
        if (pos >= 1 && pos <= count) { row = pos - 1; return true; }
        if (pos >= sec.start && pos <= sec.end) { row = pos - sec.start; return true; }
        return false;
      };
      std::vector<cgsize_t> rows;
      if (ptype == CGNS_ENUMV(PointRange) && npnts >= 2) {
        const cgsize_t lo = std::min(pnts[0], pnts[1]);
        const cgsize_t hi = std::max(pnts[0], pnts[1]);
        for (cgsize_t pos = lo; pos <= hi; ++pos) {
          cgsize_t row = 0;
          if (resolve_row(pos, row)) rows.push_back(row);
        }
      } else {
        for (cgsize_t pos : pnts) {
          cgsize_t row = 0;
          if (resolve_row(pos, row)) rows.push_back(row);
        }
      }
      for (cgsize_t e : rows) {
        int a = b.zone_node_map[iz - 1][conn[static_cast<size_t>(e) * nn + 0] - 1];
        int bnd = b.zone_node_map[iz - 1][conn[static_cast<size_t>(e) * nn + 1] - 1];
        const EdgeKey key{a < bnd ? a : bnd, a < bnd ? bnd : a};
        auto fit = edge_face.find(key);
        if (fit == edge_face.end()) {
          // The edge is not in any cell: ignore (should not happen).
          continue;
        }
        Face& face = mesh.faces[fit->second];
        face.family = famname[0] ? famname : bname;
        // Boundary type from file (used as fallback; case JSON mapping wins).
        if (btype == CGNS_ENUMV(BCFarfield)) face.bc = BcType::Farfield;
        else if (btype == CGNS_ENUMV(BCWallInviscid)) face.bc = BcType::SlipWall;
        else if (btype == CGNS_ENUMV(BCWallViscous)) face.bc = BcType::NoSlipAdiabaticWall;
        else face.bc = BcType::Count;  // resolved later from case JSON
      }
    }
  }

  // Interfaces (con-*) are boundary-like sections without boco entries; they
  // already formed internal faces during edge pairing, so nothing to do.

  build_adjacency(mesh);

  // Count boundary faces per family.
  for (const auto& face : mesh.faces) {
    if (face.cellR == -1) mesh.family_bc_face_count[face.family]++;
  }

  return mesh;
}

void build_adjacency(GlobalMesh& mesh) {
  const int nc = static_cast<int>(mesh.cells.size());
  mesh.cell_face_offsets.assign(nc + 1, 0);
  for (const auto& f : mesh.faces) {
    mesh.cell_face_offsets[f.cellL + 1]++;
    if (f.cellR >= 0) mesh.cell_face_offsets[f.cellR + 1]++;
  }
  for (int i = 0; i < nc; ++i) mesh.cell_face_offsets[i + 1] += mesh.cell_face_offsets[i];
  mesh.cell_faces.assign(mesh.cell_face_offsets[nc], -1);
  std::vector<int> fill = mesh.cell_face_offsets;
  for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
    const auto& f = mesh.faces[fi];
    mesh.cell_faces[fill[f.cellL]++] = static_cast<int>(fi);
    if (f.cellR >= 0) mesh.cell_faces[fill[f.cellR]++] = static_cast<int>(fi);
  }

  // Neighbor graph: for each face, the other cell.
  mesh.cell_neighbor_offsets.assign(nc + 1, 0);
  for (const auto& f : mesh.faces) {
    mesh.cell_neighbor_offsets[f.cellL + 1]++;
    if (f.cellR >= 0) mesh.cell_neighbor_offsets[f.cellR + 1]++;
  }
  for (int i = 0; i < nc; ++i) mesh.cell_neighbor_offsets[i + 1] += mesh.cell_neighbor_offsets[i];
  mesh.cell_neighbors.assign(mesh.cell_neighbor_offsets[nc], -1);
  fill = mesh.cell_neighbor_offsets;
  for (const auto& f : mesh.faces) {
    mesh.cell_neighbors[fill[f.cellL]++] = f.cellR;
    if (f.cellR >= 0) mesh.cell_neighbors[fill[f.cellR]++] = f.cellL;
  }
}

}  // namespace cfds
