// CGNS reader implementation. See cgns_reader.hpp.
//
// Multi-zone support: every zone of the base is read; for meshes with more
// than one zone, unassigned boundary edges (e.g. "con-*" inter-zone
// interfaces) are paired geometrically (identical midpoints and endpoints)
// and merged into single interior faces. Single-zone meshes skip the
// stitching pass entirely.
//
// <cgnslib.h> must be included before common/cgns_util.hpp (CHECK_CG).
#include <cgnslib.h>

#include "common/cgns_util.hpp"
#include "mesh/cgns_reader.hpp"
#include "mesh/geometry.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace cfd {
namespace {

// CGNS names are at most 32 chars + NUL (CGIO_MAX_NAME_LENGTH).
constexpr int kNameLen = 33;

using EdgeKey = std::pair<long long, long long>;  // (min, max) vertex ids, 1-based

struct EdgeKeyHash {
  std::size_t operator()(const EdgeKey& k) const {
    return static_cast<std::size_t>(k.first) * 73856093u ^
           static_cast<std::size_t>(k.second) * 19349663u;
  }
};

long long element_nodes(CGNS_ENUMT(ElementType_t) et) {
  switch (et) {
    case CGNS_ENUMV(NODE): return 1;
    case CGNS_ENUMV(BAR_2): return 2;
    case CGNS_ENUMV(TRI_3): return 3;
    case CGNS_ENUMV(QUAD_4): return 4;
    default: return -1;  // unsupported element type
  }
}

bool is_cell_type(CGNS_ENUMT(ElementType_t) et) {
  return et == CGNS_ENUMV(TRI_3) || et == CGNS_ENUMV(QUAD_4);
}

// ---------------------------------------------------------------------------
// Per-zone intermediate data (all indexing is zone-local).
// ---------------------------------------------------------------------------
struct ZoneData {
  std::string zone_name;
  std::vector<Vector3> vertices;  // local vertex coordinates
  std::vector<Cell2D> cells;      // vertex_indices: local; faces: local
  std::vector<Face2D> faces;      // left/right_cell: local; face_id: local
  // tag -> local face indices (boundary faces covered by a BC point set)
  std::map<std::string, std::vector<int>> boundary_faces;
  long long nverts = 0;
  long long ncells = 0;
};

// Reads one zone: coordinates, cell/boundary sections, and BC point sets.
ZoneData read_zone(int fn, int base, int zone, const std::string& filename,
                   const std::map<std::string, std::string>& bc_map) {
  ZoneData zd;

  char zonename[kNameLen] = {0};
  cgsize_t sizes[3] = {0, 0, 0};
  CHECK_CG(cg_zone_read(fn, base, zone, zonename, sizes));
  zd.zone_name = zonename;
  const long long nverts = static_cast<long long>(sizes[0]);
  const long long ncells = static_cast<long long>(sizes[1]);
  zd.nverts = nverts;
  zd.ncells = ncells;
  if (nverts <= 0 || ncells <= 0)
    throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                             zd.zone_name + "' has invalid sizes (vertices=" +
                             std::to_string(nverts) + ", cells=" +
                             std::to_string(ncells) + ")");

  // ---------------------------------------------------------------
  // Coordinates (X and Y; Z ignored for 2D)
  // ---------------------------------------------------------------
  zd.vertices.resize(nverts);
  {
    int ncoords = 0;
    CHECK_CG(cg_ncoords(fn, base, zone, &ncoords));
    if (ncoords < 2)
      throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                               zd.zone_name +
                               "' has fewer than 2 coordinate arrays");

    std::vector<std::string> coord_names;
    std::vector<std::vector<double>> coord_data;
    for (int c = 1; c <= ncoords; ++c) {
      char cname[kNameLen] = {0};
      CGNS_ENUMT(DataType_t) ctype;
      CHECK_CG(cg_coord_info(fn, base, zone, c, &ctype, cname));
      std::vector<double> data(nverts);
      // Request RealDouble; CGNS converts from the stored type. This build
      // rejects NULL ranges, so pass explicit [1, nverts].
      const cgsize_t rmin = 1, rmax = static_cast<cgsize_t>(nverts);
      CHECK_CG(cg_coord_read(fn, base, zone, cname, RealDouble, &rmin, &rmax,
                             data.data()));
      coord_names.emplace_back(cname);
      coord_data.push_back(std::move(data));
    }

    // Prefer standard names, fall back to array order (first = X, second = Y).
    auto find = [&](const char* name) -> int {
      for (std::size_t i = 0; i < coord_names.size(); ++i)
        if (coord_names[i] == name) return static_cast<int>(i);
      return -1;
    };
    int ix = find("CoordinateX");
    if (ix < 0) ix = find("X");
    if (ix < 0) ix = find("x");
    if (ix < 0) ix = 0;
    int iy = find("CoordinateY");
    if (iy < 0) iy = find("Y");
    if (iy < 0) iy = find("y");
    if (iy < 0) iy = (ix == 0) ? 1 : 0;
    if (ix == iy)
      throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                               zd.zone_name +
                               "': could not identify X/Y coordinate arrays");

    for (long long i = 0; i < nverts; ++i)
      zd.vertices[i] = Vector3(coord_data[ix][i], coord_data[iy][i], 0.0);
  }

  // ---------------------------------------------------------------
  // Sections: cells + boundary edges
  // ---------------------------------------------------------------
  zd.cells.reserve(ncells);

  struct BoundaryEdge {
    long long elem_no;  // global 1-based CGNS element number (zone-global)
    EdgeKey key;
  };
  std::vector<BoundaryEdge> boundary_edges;

  // canonical edge -> local face index
  std::unordered_map<EdgeKey, int, EdgeKeyHash> edge_to_face;
  // face index -> canonical edge key (endpoint recovery, orphan handling)
  std::unordered_map<long long, EdgeKey, std::hash<long long>> fid_to_key;

  auto add_face = [&](const EdgeKey& key, long long cell_idx) -> int {
    auto it = edge_to_face.find(key);
    if (it != edge_to_face.end()) {
      Face2D& f = zd.faces[it->second];
      if (f.right_cell == -1) {
        f.right_cell = static_cast<int>(cell_idx);
      } else {
        fmt::print(stderr,
                   "[warning] zone '{}': non-manifold edge ({}, {}) shared by "
                   "more than two cells\n",
                   zd.zone_name, key.first, key.second);
      }
      return it->second;
    }
    const int fid = static_cast<int>(zd.faces.size());
    Face2D f;
    f.face_id = fid;
    f.left_cell = static_cast<int>(cell_idx);
    f.right_cell = -1;
    zd.faces.push_back(std::move(f));
    edge_to_face.emplace(key, fid);
    fid_to_key.emplace(fid, key);
    return fid;
  };

  int nsections = 0;
  CHECK_CG(cg_nsections(fn, base, zone, &nsections));
  long long ncell_elems = 0;
  for (int s = 1; s <= nsections; ++s) {
    char secname[kNameLen] = {0};
    CGNS_ENUMT(ElementType_t) et;
    cgsize_t start = 0, end = 0;
    int nbndry = 0, parentflag = 0;
    CHECK_CG(cg_section_read(fn, base, zone, s, secname, &et, &start, &end,
                             &nbndry, &parentflag));
    const long long nelem = static_cast<long long>(end - start + 1);
    if (nelem <= 0) continue;

    cgsize_t edatasize = 0;
    CHECK_CG(cg_ElementDataSize(fn, base, zone, s, &edatasize));
    std::vector<cgsize_t> elements(edatasize);
    CHECK_CG(cg_elements_read(fn, base, zone, s, elements.data(), nullptr));

    auto check_node = [&](cgsize_t v) {
      if (v < 1 || v > nverts)
        throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                                 zd.zone_name + "': element vertex index " +
                                 std::to_string(v) + " out of range [1, " +
                                 std::to_string(nverts) + "]");
    };

    if (et == CGNS_ENUMV(MIXED)) {
      // Each entry: element type code followed by its node indices.
      long long pos = 0;
      for (long long e = 0; e < nelem; ++e) {
        if (pos >= edatasize)
          throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                                   zd.zone_name + "': malformed MIXED section '" +
                                   secname + "'");
        const CGNS_ENUMT(ElementType_t) et_elem =
            static_cast<CGNS_ENUMT(ElementType_t)>(elements[pos++]);
        const long long nn = element_nodes(et_elem);
        if (nn < 0)
          throw std::runtime_error(
              "CGNS file '" + filename + "': zone '" + zd.zone_name +
              "': unsupported element type " +
              std::to_string(static_cast<int>(et_elem)) + " in section '" +
              secname + "'");
        if (pos + nn > edatasize)
          throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                                   zd.zone_name +
                                   "': truncated element in MIXED section '" +
                                   secname + "'");

        if (is_cell_type(et_elem)) {
          Cell2D cell;
          cell.cell_id = ncell_elems;
          cell.type = (et_elem == CGNS_ENUMV(QUAD_4)) ? CellType::Quadrilateral
                                                      : CellType::Triangle;
          cell.vertex_indices.reserve(nn);
          for (long long k = 0; k < nn; ++k) {
            check_node(elements[pos + k]);
            cell.vertex_indices.push_back(elements[pos + k] - 1);  // 0-based
          }
          for (long long k = 0; k < nn; ++k) {
            const long long a = elements[pos + k];
            const long long b = elements[pos + (k + 1) % nn];
            const EdgeKey key = (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
            cell.faces.push_back(add_face(key, ncell_elems));
          }
          zd.cells.push_back(std::move(cell));
          ++ncell_elems;
        } else if (et_elem == CGNS_ENUMV(BAR_2)) {
          BoundaryEdge be;
          be.elem_no = static_cast<long long>(start) + e;  // 1-based
          const long long a = elements[pos];
          const long long b = elements[pos + nn - 1];
          be.key = (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
          boundary_edges.push_back(be);
        } else if (et_elem == CGNS_ENUMV(NODE)) {
          fmt::print(stderr,
                     "[warning] zone '{}': skipping NODE element in section "
                     "'{}' (point elements are not supported)\n",
                     zd.zone_name, secname);
        } else {
          fmt::print(stderr, "[warning] zone '{}': skipping unsupported "
                             "element type {} in section '{}'\n",
                     zd.zone_name, static_cast<int>(et_elem), secname);
        }
        pos += nn;
      }
    } else if (is_cell_type(et)) {
      const long long nn = element_nodes(et);
      for (long long e = 0; e < nelem; ++e) {
        const cgsize_t* nd = elements.data() + e * nn;
        Cell2D cell;
        cell.cell_id = ncell_elems;
        cell.type = (et == CGNS_ENUMV(QUAD_4)) ? CellType::Quadrilateral
                                               : CellType::Triangle;
        for (long long k = 0; k < nn; ++k) {
          check_node(nd[k]);
          cell.vertex_indices.push_back(nd[k] - 1);  // 0-based
        }
        for (long long k = 0; k < nn; ++k) {
          const long long a = nd[k];
          const long long b = nd[(k + 1) % nn];
          const EdgeKey key = (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
          cell.faces.push_back(add_face(key, ncell_elems));
        }
        zd.cells.push_back(std::move(cell));
        ++ncell_elems;
      }
    } else if (et == CGNS_ENUMV(BAR_2)) {
      const long long nn = 2;
      for (long long e = 0; e < nelem; ++e) {
        const cgsize_t* nd = elements.data() + e * nn;
        BoundaryEdge be;
        be.elem_no = static_cast<long long>(start) + e;  // 1-based
        const long long a = nd[0];
        const long long b = nd[nn - 1];
        be.key = (a < b) ? EdgeKey(a, b) : EdgeKey(b, a);
        boundary_edges.push_back(be);
      }
    } else if (et == CGNS_ENUMV(NODE)) {
      fmt::print(stderr,
                 "[warning] zone '{}': skipping section '{}' with {} NODE "
                 "element(s) (point elements are not supported)\n",
                 zd.zone_name, secname, nelem);
    } else {
      fmt::print(stderr, "[warning] zone '{}': skipping unsupported section "
                         "'{}' (element type {})\n",
                 zd.zone_name, secname, static_cast<int>(et));
    }
  }

  if (ncell_elems != ncells)
    throw std::runtime_error(
        "CGNS file '" + filename + "': zone '" + zd.zone_name +
        "' declares " + std::to_string(ncells) +
        " cells but sections contain " + std::to_string(ncell_elems) +
        " cell elements");

  // ---------------------------------------------------------------
  // Orphan faces: boundary edges that match no interior edge (non-conforming
  // meshes). Created NOW, before the node-fill pass, so the new faces get
  // their endpoints from the edge key and vertices, and so BC point sets can
  // still reference them.
  // ---------------------------------------------------------------
  // Map 1-based CGNS boundary element number -> face index.
  std::unordered_map<long long, int> belem_to_face;
  for (const BoundaryEdge& be : boundary_edges) {
    auto it = edge_to_face.find(be.key);
    if (it != edge_to_face.end()) {
      belem_to_face[be.elem_no] = it->second;
      continue;
    }
    fmt::print(stderr,
               "[warning] zone '{}': boundary edge ({}, {}) not found in "
               "interior face list; creating orphan face\n",
               zd.zone_name, be.key.first, be.key.second);
    if (be.key.first < 1 || be.key.second > nverts)
      throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                               zd.zone_name +
                               "': boundary edge vertex index out of range");
    const int fid = static_cast<int>(zd.faces.size());
    Face2D f;
    f.face_id = fid;
    f.left_cell = -1;
    f.right_cell = -1;
    f.nodes[0] = zd.vertices[be.key.first - 1];
    f.nodes[1] = zd.vertices[be.key.second - 1];
    zd.faces.push_back(std::move(f));
    edge_to_face.emplace(be.key, fid);
    fid_to_key.emplace(fid, be.key);
    belem_to_face[be.elem_no] = fid;
  }

  // ---------------------------------------------------------------
  // Fill face endpoints for every face (from the canonical edge key).
  // ---------------------------------------------------------------
  for (Face2D& f : zd.faces) {
    auto it = fid_to_key.find(f.face_id);
    if (it == fid_to_key.end())
      throw std::runtime_error("internal error: zone '" + zd.zone_name +
                               "' face " + std::to_string(f.face_id) +
                               " has no associated edge");
    const EdgeKey& key = it->second;
    if (key.first < 1 || key.second > nverts)
      throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                               zd.zone_name +
                               "': face vertex index out of range");
    f.nodes[0] = zd.vertices[key.first - 1];
    f.nodes[1] = zd.vertices[key.second - 1];
  }

  // ---------------------------------------------------------------
  // Boundary conditions: tag faces from BC point sets
  // ---------------------------------------------------------------
  int nbocos = 0;
  CHECK_CG(cg_nbocos(fn, base, zone, &nbocos));
  for (int b = 1; b <= nbocos; ++b) {
    char boconame[kNameLen] = {0};
    CGNS_ENUMT(BCType_t) bct;
    CGNS_ENUMT(PointSetType_t) pst;
    cgsize_t npnts = 0;
    int normalindex = 0;
    cgsize_t normallistflag = 0;
    CGNS_ENUMT(DataType_t) ndt;
    int ndataset = 0;
    CHECK_CG(cg_boco_info(fn, base, zone, b, boconame, &bct, &pst, &npnts,
                          &normalindex, &normallistflag, &ndt, &ndataset));

    std::vector<cgsize_t> pnts(npnts);
    if (npnts > 0)
      CHECK_CG(cg_boco_read(fn, base, zone, b, pnts.data(), nullptr));

    // Family name of this BC (the tag used in the case file's
    // boundary_conditions map); fall back to the BC node name.
    std::string tag = boconame;
    {
      char famname[kNameLen] = {0};
      int err = cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", b,
                        "end");
      if (err == CG_OK && cg_famname_read(famname) == CG_OK &&
          std::string(famname).size() > 0) {
        tag = famname;
      }
    }

    // Determine the BC type from the case-file mapping.
    BCType bctype = BCType::Farfield;
    auto map_it = bc_map.find(tag);
    if (map_it != bc_map.end()) {
      bctype = bc_type_from_string(map_it->second);
    } else {
      fmt::print(stderr,
                 "[warning] zone '{}': boundary tag '{}' (BC '{}') is not "
                 "present in the case boundary_conditions map; treating as "
                 "farfield\n",
                 zd.zone_name, tag, boconame);
    }

    // Collect the referenced boundary element numbers.
    std::vector<long long> elems;
    if (pst == CGNS_ENUMV(PointRange)) {
      if (npnts < 2)
        throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                                 zd.zone_name + "': BC '" +
                                 std::string(boconame) +
                                 "' PointRange has fewer than 2 points");
      const long long lo = pnts[0], hi = pnts[1];
      if (hi < lo)
        throw std::runtime_error("CGNS file '" + filename + "': zone '" +
                                 zd.zone_name + "': BC '" +
                                 std::string(boconame) +
                                 "' PointRange is inverted");
      for (long long e = lo; e <= hi; ++e) elems.push_back(e);
    } else if (pst == CGNS_ENUMV(PointList)) {
      for (cgsize_t i = 0; i < npnts; ++i) elems.push_back(pnts[i]);
    } else {
      fmt::print(stderr,
                 "[warning] zone '{}': BC '{}' uses unsupported point-set "
                 "type {}; skipping\n",
                 zd.zone_name, boconame, static_cast<int>(pst));
      continue;
    }

    for (long long e : elems) {
      auto it = belem_to_face.find(e);
      if (it == belem_to_face.end()) {
        throw std::runtime_error(
            "CGNS file '" + filename + "': zone '" + zd.zone_name + "': BC '" +
            std::string(boconame) + "' references element " +
            std::to_string(e) + " which is not a boundary element");
      }
      Face2D& f = zd.faces[it->second];
      if (f.bc_type != BCType::Interior) {
        fmt::print(stderr,
                   "[warning] zone '{}': face {} tagged by multiple BCs "
                   "('{}' and '{}'); last one wins\n",
                   zd.zone_name, f.face_id, f.bc_tag, tag);
      }
      f.bc_type = bctype;
      f.bc_tag = tag;
      zd.boundary_faces[tag].push_back(static_cast<int>(f.face_id));
    }
  }

  return zd;
}

// ---------------------------------------------------------------------------
// Interface pairing key: geometric identity of a boundary edge.
// Conforming 1-to-1 interfaces store bit-identical coordinates on both
// sides, so exact comparison of the midpoint and the two canonical-ordered
// endpoints is sufficient.
// ---------------------------------------------------------------------------
struct InterfaceKey {
  Vector3 mid;  // face midpoint
  Vector3 a;    // canonical-ordered endpoints (a <= b)
  Vector3 b;

  bool operator==(const InterfaceKey& o) const {
    return mid.x == o.mid.x && mid.y == o.mid.y && mid.z == o.mid.z &&
           a.x == o.a.x && a.y == o.a.y && a.z == o.a.z &&
           b.x == o.b.x && b.y == o.b.y && b.z == o.b.z;
  }
};

struct InterfaceKeyHash {
  std::size_t operator()(const InterfaceKey& k) const {
    std::size_t h = 0;
    const double vals[9] = {k.mid.x, k.mid.y, k.mid.z, k.a.x, k.a.y,
                            k.a.z,   k.b.x,   k.b.y,   k.b.z};
    for (double v : vals) {
      h ^= std::hash<double>{}(v) + 0x9e3779b9u + (h << 6) + (h >> 2);
    }
    return h;
  }
};

InterfaceKey make_interface_key(const Face2D& f) {
  const Vector3& p0 = f.nodes[0];
  const Vector3& p1 = f.nodes[1];
  InterfaceKey key;
  key.mid = (p0 + p1) * 0.5;
  // canonical endpoint order (lexicographic by coordinates)
  const bool swap = (p1.x < p0.x) || (p1.x == p0.x && p1.y < p0.y) ||
                    (p1.x == p0.x && p1.y == p0.y && p1.z < p0.z);
  key.a = swap ? p1 : p0;
  key.b = swap ? p0 : p1;
  return key;
}

}  // namespace

MeshReadResult read_cgns_mesh(const std::string& filename,
                              const std::map<std::string, std::string>& bc_map,
                              bool verbose) {
  MeshReadResult result;
  Mesh2D& mesh = result.mesh;
  std::vector<Vector3>& vertices = result.vertices;

  int fn = 0;
  CHECK_CG(cg_open(filename.c_str(), CG_MODE_READ, &fn));

  try {
    // ---------------------------------------------------------------
    // Base
    // ---------------------------------------------------------------
    int nbases = 0;
    CHECK_CG(cg_nbases(fn, &nbases));
    if (nbases < 1)
      throw std::runtime_error("CGNS file '" + filename + "': no bases found");
    const int base = 1;

    int nzones = 0;
    CHECK_CG(cg_nzones(fn, base, &nzones));
    if (nzones < 1)
      throw std::runtime_error("CGNS file '" + filename +
                               "': no zones found in base " +
                               std::to_string(base));
    const bool multi = nzones > 1;

    // ---------------------------------------------------------------
    // Read every zone
    // ---------------------------------------------------------------
    std::vector<ZoneData> zones;
    zones.reserve(nzones);
    for (int z = 1; z <= nzones; ++z) {
      zones.push_back(read_zone(fn, base, z, filename, bc_map));
      if (verbose) {
        const ZoneData& zd = zones.back();
        fmt::print("[cgns] zone '{}': {} vertices, {} cells, {} faces\n",
                   zd.zone_name, zd.nverts, zd.ncells, zd.faces.size());
        for (const auto& kv : zd.boundary_faces)
          fmt::print("[cgns]   boundary tag '{}': {} faces ({})\n", kv.first,
                     kv.second.size(),
                     bc_type_name(zd.faces[kv.second[0]].bc_type));
      }
    }

    // Family names (base level), for diagnostics.
    {
      int nfam = 0;
      CHECK_CG(cg_nfamilies(fn, base, &nfam));
      std::vector<std::string> fams;
      for (int f = 1; f <= nfam; ++f) {
        char famname[kNameLen] = {0};
        int nboco = 0, ngeos = 0;
        if (cg_family_read(fn, base, f, famname, &nboco, &ngeos) == CG_OK)
          fams.emplace_back(famname);
      }
      if (verbose) {
        fmt::print("[cgns] families: [");
        for (std::size_t i = 0; i < fams.size(); ++i)
          fmt::print("{}{}", i ? ", " : "", fams[i]);
        fmt::print("]\n");
      }
    }

    // ---------------------------------------------------------------
    // Concatenate zones (global indexing: vertex/cell/face offsets)
    // ---------------------------------------------------------------
    long long vtx_offset = 0, cell_offset = 0, face_offset = 0;
    for (ZoneData& zd : zones) {
      for (Vector3& v : zd.vertices) vertices.push_back(v);
      for (Cell2D& c : zd.cells) {
        c.cell_id += cell_offset;
        for (int& vi : c.vertex_indices) vi += static_cast<int>(vtx_offset);
        for (int& fid : c.faces) fid += static_cast<int>(face_offset);
        mesh.cells.push_back(std::move(c));
      }
      for (Face2D& f : zd.faces) {
        f.face_id += face_offset;
        if (f.left_cell >= 0) f.left_cell += static_cast<int>(cell_offset);
        if (f.right_cell >= 0) f.right_cell += static_cast<int>(cell_offset);
        mesh.faces.push_back(std::move(f));
      }
      for (auto& kv : zd.boundary_faces) {
        for (int& fid : kv.second) fid += static_cast<int>(face_offset);
        auto& dst = mesh.boundary_faces[kv.first];
        dst.insert(dst.end(), kv.second.begin(), kv.second.end());
      }
      vtx_offset += zd.nverts;
      cell_offset += zd.ncells;
      face_offset += static_cast<long long>(zd.faces.size());
    }
    mesh.num_vertices = vtx_offset;
    mesh.zone_name = zones.front().zone_name;
    if (multi)
      mesh.zone_name += " (+" + std::to_string(nzones - 1) + " zones)";

    // ---------------------------------------------------------------
    // Stitch inter-zone interfaces (multi-zone meshes only; single-zone
    // meshes take the fast path and skip this pass entirely).
    // Candidates: boundary faces (one adjacent cell) that are not covered by
    // any BC point set (e.g. "con-*" sections). Pairs are found by exact
    // geometric identity of midpoint + canonical endpoints and merged into a
    // single interior face (left cell from the first zone, right from the
    // second); the duplicate face is removed.
    // ---------------------------------------------------------------
    long long n_stitched = 0;
    if (multi) {
      std::unordered_map<InterfaceKey, int, InterfaceKeyHash> key_to_fid;
      std::vector<char> merged(mesh.faces.size(), 0);
      std::vector<int> survivor(mesh.faces.size(), -1);
      for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const Face2D& f = mesh.faces[i];
        if (f.left_cell < 0 || f.right_cell >= 0 ||
            f.bc_type != BCType::Interior)
          continue;  // not an interface candidate
        const InterfaceKey key = make_interface_key(f);
        auto it = key_to_fid.find(key);
        if (it == key_to_fid.end()) {
          key_to_fid.emplace(key, static_cast<int>(i));
          continue;
        }
        const int other = it->second;
        if (merged[other] || other == static_cast<int>(i)) continue;
        // merge: the surviving face keeps its left cell, gains the partner's
        // owner cell on the right (geometry orients normals afterwards)
        mesh.faces[other].right_cell = f.left_cell;
        merged[i] = 1;
        survivor[i] = other;
        ++n_stitched;
      }

      if (n_stitched > 0) {
        // Compact the face list, dropping merged duplicates. Faces on the
        // partner side must keep referencing the surviving face, so merged
        // ids are remapped to the survivor's new id.
        std::vector<int> remap(mesh.faces.size(), -1);
        std::vector<Face2D> compact;
        compact.reserve(mesh.faces.size() - n_stitched);
        for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
          if (merged[i]) continue;
          remap[i] = static_cast<int>(compact.size());
          Face2D f = std::move(mesh.faces[i]);
          f.face_id = static_cast<long long>(compact.size());
          compact.push_back(std::move(f));
        }
        for (std::size_t i = 0; i < mesh.faces.size(); ++i)
          if (merged[i]) remap[i] = remap[survivor[i]];
        mesh.faces = std::move(compact);
        for (Cell2D& c : mesh.cells)
          for (int& fid : c.faces) fid = remap[fid];
        for (auto& kv : mesh.boundary_faces)
          for (int& fid : kv.second) fid = remap[fid];
      }
      if (verbose)
        fmt::print("[cgns] stitched {} interface face pair(s)\n", n_stitched);
    }

    // ---------------------------------------------------------------
    // Finalize counters and diagnostics
    // ---------------------------------------------------------------
    mesh.finalize();

    long long n_orphan = 0, n_unassigned = 0;
    for (const Face2D& f : mesh.faces) {
      const bool has_l = f.left_cell >= 0, has_r = f.right_cell >= 0;
      if (!has_l && !has_r) ++n_orphan;
      if (has_l != has_r && f.bc_type == BCType::Interior) ++n_unassigned;
    }
    if (n_orphan > 0)
      fmt::print(stderr, "[warning] {} face(s) have no adjacent cell\n",
                 n_orphan);
    if (n_unassigned > 0)
      fmt::print(stderr,
                 "[warning] {} boundary face(s) are not covered by any BC "
                 "point set and did not match any interface partner\n",
                 n_unassigned);

    if (verbose) {
      fmt::print("[cgns] mesh '{}': {} vertices, {} cells, {} faces ({} "
                 "shared, {} boundary)\n",
                 mesh.zone_name, mesh.num_vertices, mesh.num_cells,
                 mesh.num_faces, mesh.num_faces - mesh.num_boundary_faces,
                 mesh.num_boundary_faces);
      for (const auto& kv : mesh.boundary_faces)
        fmt::print("[cgns]   boundary tag '{}': {} faces ({})\n", kv.first,
                   kv.second.size(),
                   bc_type_name(mesh.faces[kv.second[0]].bc_type));
    }

  } catch (...) {
    cg_close(fn);
    throw;
  }

  CHECK_CG(cg_close(fn));

  // Complete the geometry so the returned mesh is always ready for
  // computation (cell centers/volumes, face centers/normals/areas), and
  // keep the vertex coordinates on the mesh for the field writers.
  compute_geometry(mesh, vertices);
  mesh.vertices = vertices;

  return result;
}

}  // namespace cfd
