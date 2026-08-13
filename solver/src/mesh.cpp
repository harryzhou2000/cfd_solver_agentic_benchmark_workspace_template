#include "mesh.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cgnslib.h>

namespace cfd {

namespace {

// ---------------------------------------------------------------------------
// CGNS error handling
// ---------------------------------------------------------------------------
[[noreturn]] void throw_cgns_error(const std::string& what) {
  throw std::runtime_error(what + " (CGNS: " + cg_get_error() + ")");
}

void check_cgns(int err, const std::string& what) {
  if (err != CG_OK) {
    throw_cgns_error(what);
  }
}

// RAII wrapper around a CGNS file handle: opens on construction and always
// closes on destruction, even when an exception unwinds through this scope.
struct CgnsFile {
  int fn = -1;

  explicit CgnsFile(const std::string& path) {
    check_cgns(cg_open(path.c_str(), CG_MODE_READ, &fn), "cg_open: " + path);
  }

  ~CgnsFile() {
    if (fn >= 0) {
      cg_close(fn);
    }
  }

  CgnsFile(const CgnsFile&) = delete;
  CgnsFile& operator=(const CgnsFile&) = delete;
};

// ---------------------------------------------------------------------------
// Edge hashing: an edge is an unordered pair of node indices (0-based).
// Packed into a uint64 (min << 32 | max) so a single unordered_map works.
// ---------------------------------------------------------------------------
inline uint64_t make_edge_key(int n0, int n1) {
  const uint64_t a = static_cast<uint64_t>(std::min(n0, n1));
  const uint64_t b = static_cast<uint64_t>(std::max(n0, n1));
  return (a << 32) | b;
}

inline std::pair<int, int> edge_key_nodes(uint64_t key) {
  return {static_cast<int>(key >> 32), static_cast<int>(key & 0xFFFFFFFFu)};
}

// Tag carried by boundary (BAR_2) elements: which section they came from and
// which BC type applies.
struct BoundaryTag {
  std::string section;  // BAR_2 section name in the CGNS file
  std::string family;   // family name (== section name if no ZoneBC entry)
  BCType bc_type = BCType::Invalid;
};

// One boundary condition declared under ZoneBC.
struct BocoInfo {
  std::string name;     // boco name
  std::string family;   // family name referenced by the boco
  GridLocation_t gridloc = GridLocationNull;
  BCType bc_type = BCType::Invalid;
};

const char* point_set_type_name(PointSetType_t t) {
  switch (t) {
    case PointList: return "PointList";
    case PointListDonor: return "PointListDonor";
    case PointRange: return "PointRange";
    case PointRangeDonor: return "PointRangeDonor";
    case ElementRange: return "ElementRange";
    case ElementList: return "ElementList";
    case CellListDonor: return "CellListDonor";
    default: return "unknown";
  }
}

// True for explicit-list point sets, which this reader does not support:
// the BC point list would have to be matched against boundary elements
// individually, and silently discarding it leaves edges untagged.
inline bool is_point_list_set(PointSetType_t t) {
  return t == PointList || t == PointListDonor || t == ElementList ||
         t == CellListDonor;
}

// ---------------------------------------------------------------------------
// Union-find used to merge coincident 1-to-1 interface nodes across zones.
// ---------------------------------------------------------------------------
class UnionFind {
 public:
  explicit UnionFind(int n) : parent_(n) {
    for (int i = 0; i < n; ++i) {
      parent_[i] = i;
    }
  }

  int find(int x) {
    int root = x;
    while (parent_[root] != root) {
      root = parent_[root];
    }
    while (parent_[x] != x) {  // path compression
      const int next = parent_[x];
      parent_[x] = root;
      x = next;
    }
    return root;
  }

  void unite(int a, int b) {
    const int ra = find(a);
    const int rb = find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

 private:
  std::vector<int> parent_;
};

// ---------------------------------------------------------------------------
// Raw per-zone data, collected before the shared edge hash is built.
// ---------------------------------------------------------------------------
struct RawZone {
  std::string name;
  int n_nodes = 0;
  int n_cells = 0;
  int node_offset = 0;  // global index of this zone's first node
  int cell_offset = 0;  // global index of this zone's first cell

  std::vector<Vec2> coords;  // zone-local nodes (0-based, zone-local ids)

  // Cell connectivity: raw zone-local (0-based) node ids, padded to 4.
  std::vector<std::array<int, 4>> cell_conn;
  std::vector<uint8_t> cell_npe;

  // Boundary BAR_2 sections: tag plus raw zone-local (0-based) edge pairs.
  struct BarSection {
    BoundaryTag tag;
    std::vector<std::pair<int, int>> edges;
  };
  std::vector<BarSection> bar_sections;

  // 1-to-1 vertex connections: (this zone local id, donor zone, donor local
  // id), all 0-based.
  struct ConnPair {
    int local;
    int donor_zone;
    int donor_local;
  };
  std::vector<ConnPair> conn_pairs;
};

}  // namespace

// ---------------------------------------------------------------------------
// Mesh reading
// ---------------------------------------------------------------------------
Mesh read_mesh(const std::string& cgns_path,
               const std::map<std::string, BCType>& bc_map) {
  Mesh mesh;
  mesh.file_path = cgns_path;

  CgnsFile file(cgns_path);
  const int fn = file.fn;

  // ---- 1. Base ----
  int nbases = 0;
  check_cgns(cg_nbases(fn, &nbases), "cg_nbases");
  if (nbases < 1) {
    throw std::runtime_error("CGNS file contains no bases: " + cgns_path);
  }
  const int base = 1;
  if (nbases > 1) {
    std::printf("Note: CGNS file has %d bases; only base 1 is loaded\n",
                nbases);
  }

  char base_name[33] = {0};
  int cell_dim = 0, phys_dim = 0;
  check_cgns(cg_base_read(fn, base, base_name, &cell_dim, &phys_dim),
             "cg_base_read");

  // ---- 2. Zones: names and sizes ----
  int nzones = 0;
  check_cgns(cg_nzones(fn, base, &nzones), "cg_nzones");
  if (nzones < 1) {
    throw std::runtime_error("CGNS base '" + std::string(base_name) +
                             "' contains no zones: " + cgns_path);
  }

  std::vector<RawZone> zones(nzones);
  std::map<std::string, int> zone_index;  // zone name -> index into zones
  for (int z = 1; z <= nzones; ++z) {
    RawZone& rz = zones[z - 1];
    char zname[33] = {0};
    cgsize_t zsize[3] = {0, 0, 0};
    check_cgns(cg_zone_read(fn, base, z, zname, zsize), "cg_zone_read");
    rz.name = zname;
    rz.n_nodes = static_cast<int>(zsize[0]);
    rz.n_cells = static_cast<int>(zsize[1]);
    zone_index[zname] = z - 1;

    ZoneType_t ztype = ZoneTypeNull;
    check_cgns(cg_zone_type(fn, base, z, &ztype), "cg_zone_type");
    if (ztype != Unstructured) {
      throw std::runtime_error("zone '" + rz.name +
                               "' is not Unstructured (unsupported for "
                               "Phase 1)");
    }
  }
  int node_offset = 0, cell_offset = 0;
  for (RawZone& rz : zones) {
    rz.node_offset = node_offset;
    node_offset += rz.n_nodes;
    rz.cell_offset = cell_offset;
    cell_offset += rz.n_cells;
  }
  mesh.zone_name = zones[0].name;
  mesh.zone_names.reserve(nzones);
  for (const RawZone& rz : zones) {
    mesh.zone_names.push_back(rz.name);
  }
  mesh.n_nodes = node_offset;

  // Base-level family names (used for diagnostics only).
  std::set<std::string> family_names;
  {
    int nfamilies = 0;
    check_cgns(cg_nfamilies(fn, base, &nfamilies), "cg_nfamilies");
    for (int f = 1; f <= nfamilies; ++f) {
      char fam_name[33] = {0};
      int nboco = 0, ngeos = 0;
      check_cgns(cg_family_read(fn, base, f, fam_name, &nboco, &ngeos),
                 "cg_family_read");
      family_names.insert(fam_name);
    }
  }

  // ---- 3. Per-zone data: coordinates, elements, BCs, connections ----
  for (int zi = 0; zi < nzones; ++zi) {
    const int zone = zi + 1;  // CGNS is 1-based
    RawZone& rz = zones[zi];

    // 3a. Coordinates.
    rz.coords.resize(rz.n_nodes);
    int ncoords = 0;
    check_cgns(cg_ncoords(fn, base, zone, &ncoords), "cg_ncoords");
    const cgsize_t rmin[3] = {1, 1, 1};
    const cgsize_t rmax[3] = {static_cast<cgsize_t>(rz.n_nodes), 1, 1};
    for (int c = 1; c <= ncoords; ++c) {
      DataType_t ctype = DataTypeNull;
      char cname[33] = {0};
      check_cgns(cg_coord_info(fn, base, zone, c, &ctype, cname),
                 "cg_coord_info");
      const std::string name(cname);
      if (name != "CoordinateX" && name != "CoordinateY") {
        continue;  // ignore e.g. CoordinateZ in 2D meshes
      }
      std::vector<double> data(rz.n_nodes);
      check_cgns(cg_coord_read(fn, base, zone, cname, RealDouble, rmin, rmax,
                               data.data()),
                 "cg_coord_read: " + name);
      for (int i = 0; i < rz.n_nodes; ++i) {
        if (name == "CoordinateX") {
          rz.coords[i].x = data[i];
        } else {
          rz.coords[i].y = data[i];
        }
      }
    }

    // 3b. Boundary conditions under ZoneBC.
    std::vector<BocoInfo> bocos;
    int nbocos = 0;
    check_cgns(cg_nbocos(fn, base, zone, &nbocos), "cg_nbocos");
    for (int b = 1; b <= nbocos; ++b) {
      char bname[33] = {0};
      BCType_t bctype = BCTypeNull;
      PointSetType_t ptset = PointSetTypeNull;
      cgsize_t npnts = 0;
      int normal_index = 0;
      cgsize_t normal_list_size = 0;
      DataType_t normal_dt = DataTypeNull;
      int ndataset = 0;
      check_cgns(cg_boco_info(fn, base, zone, b, bname, &bctype, &ptset,
                              &npnts, &normal_index, &normal_list_size,
                              &normal_dt, &ndataset),
                 "cg_boco_info");

      // Explicit-list point sets (e.g. PointList) are not supported:
      // matching each listed entity against the boundary elements would be
      // required to tag edges, and reading-and-discarding the list would
      // silently leave boundary edges untagged, so fail loudly instead.
      // Range-style sets (PointRange / ElementRange) are fine: the boco is
      // matched to its BAR_2 section by name below.
      if (is_point_list_set(ptset)) {
        throw std::runtime_error(
            "boco '" + std::string(bname) + "' in zone '" + rz.name +
            "' uses point-set type '" + point_set_type_name(ptset) +
            "' (only range-style point sets, e.g. 'PointRange' or "
            "'ElementRange', are supported)");
      }

      std::vector<cgsize_t> pnts(std::max<cgsize_t>(npnts, 1));
      check_cgns(cg_boco_read(fn, base, zone, b, pnts.data(), nullptr),
                 "cg_boco_read");

      // Family name + grid location of this BC (cg_goto to the BC_t node).
      char fam_name[33] = {0};
      GridLocation_t gridloc = GridLocationNull;
      if (cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", b, "end") ==
          CG_OK) {
        cg_famname_read(fam_name);
        cg_gridlocation_read(&gridloc);
      }

      BocoInfo info;
      info.name = bname;
      info.family = fam_name;
      info.gridloc = gridloc;
      // Map the mesh family name to a BC type from the case JSON.
      const auto it = bc_map.find(info.family);
      if (it != bc_map.end()) {
        info.bc_type = it->second;
      }
      bocos.push_back(std::move(info));
    }

    // 3c. Element sections.
    // BAR_2 sections hold boundary face elements; TRI_3/QUAD_4 hold cells.
    int nsections = 0;
    check_cgns(cg_nsections(fn, base, zone, &nsections), "cg_nsections");
    for (int s = 1; s <= nsections; ++s) {
      char sname[33] = {0};
      ElementType_t etype = ElementTypeNull;
      cgsize_t start = 0, end = 0;
      int nbndry = 0, parent_flag = 0;
      check_cgns(cg_section_read(fn, base, zone, s, sname, &etype, &start,
                                 &end, &nbndry, &parent_flag),
                 "cg_section_read");

      const cgsize_t n_elems = end - start + 1;
      if (n_elems <= 0) {
        continue;
      }

      cgsize_t data_size = 0;
      check_cgns(cg_ElementDataSize(fn, base, zone, s, &data_size),
                 "cg_ElementDataSize: " + std::string(sname));
      std::vector<cgsize_t> conn(data_size);
      check_cgns(cg_elements_read(fn, base, zone, s, conn.data(), nullptr),
                 "cg_elements_read: " + std::string(sname));

      if (etype == TRI_3 || etype == QUAD_4) {
        // Volume elements -> cells (connectivity is 1-based in CGNS).
        const int npe = (etype == TRI_3) ? 3 : 4;
        for (cgsize_t e = 0; e < n_elems; ++e) {
          std::array<int, 4> nodes{};
          for (int k = 0; k < npe; ++k) {
            const int local = static_cast<int>(conn[e * npe + k] - 1);
            if (local < 0 || local >= rz.n_nodes) {
              throw std::runtime_error(
                  "cell connectivity out of range in zone '" + rz.name +
                  "': node index " + std::to_string(local) + " (0-based) is "
                  "outside [0, " + std::to_string(rz.n_nodes - 1) + "]");
            }
            nodes[k] = local;
          }
          rz.cell_conn.push_back(nodes);
          rz.cell_npe.push_back(static_cast<uint8_t>(npe));
        }
      } else if (etype == BAR_2) {
        // Boundary elements. Find the matching ZoneBC entry (by section
        // name) to resolve the family and BC type; otherwise tag with the
        // section name.
        BoundaryTag tag;
        tag.section = sname;
        tag.family = sname;
        for (const BocoInfo& bi : bocos) {
          if (bi.name == sname) {
            tag.family = bi.family.empty() ? sname : bi.family;
            tag.bc_type = bi.bc_type;
            break;
          }
        }
        RawZone::BarSection bar;
        bar.tag = std::move(tag);
        bar.edges.reserve(n_elems);
        for (cgsize_t e = 0; e < n_elems; ++e) {
          const int n0 = static_cast<int>(conn[2 * e] - 1);
          const int n1 = static_cast<int>(conn[2 * e + 1] - 1);
          if (n0 < 0 || n0 >= rz.n_nodes || n1 < 0 || n1 >= rz.n_nodes) {
            throw std::runtime_error(
                "boundary element connectivity out of range in zone '" +
                rz.name + "': node index outside [0, " +
                std::to_string(rz.n_nodes - 1) + "]");
          }
          bar.edges.emplace_back(n0, n1);
        }
        rz.bar_sections.push_back(std::move(bar));
      }
      // Other element types (e.g. BAR_3, TRI_6) are unsupported: skipped.
    }

    // 3d. 1-to-1 vertex connections (ZoneGridConnectivity). These pair
    // coincident interface nodes between zones; the union-find built from
    // them lets the shared edge hash see interface edges from both sides.
    int nconns = 0;
    check_cgns(cg_nconns(fn, base, zone, &nconns), "cg_nconns");
    for (int c = 1; c <= nconns; ++c) {
      char cname[33] = {0};
      GridLocation_t loc = GridLocationNull;
      GridConnectivityType_t ctype = GridConnectivityTypeNull;
      PointSetType_t ptype = PointSetTypeNull;
      cgsize_t npe = 0;
      char donor_name[33] = {0};
      ZoneType_t donor_zonetype = ZoneTypeNull;
      PointSetType_t donor_ptype = PointSetTypeNull;
      DataType_t donor_dt = DataTypeNull;
      cgsize_t ndata = 0;
      check_cgns(cg_conn_info(fn, base, zone, c, cname, &loc, &ctype, &ptype,
                              &npe, donor_name, &donor_zonetype,
                              &donor_ptype, &donor_dt, &ndata),
                 "cg_conn_info: " + std::string(cname));

      if (loc != Vertex || ctype != Abutting1to1) {
        continue;  // only vertex-to-vertex 1-to-1 connections matter here
      }
      const auto donor_it = zone_index.find(donor_name);
      if (donor_it == zone_index.end()) {
        throw std::runtime_error("1-to-1 connection '" + std::string(cname) +
                                 "' in zone '" + rz.name +
                                 "' references unknown donor zone '" +
                                 std::string(donor_name) + "'");
      }
      const int donor_zone = donor_it->second;
      std::vector<cgsize_t> pnts(npe > 0 ? npe : 1);
      std::vector<cgsize_t> dpnts(npe > 0 ? npe : 1);
      check_cgns(
          cg_conn_read(fn, base, zone, c, pnts.data(), donor_dt, dpnts.data()),
          "cg_conn_read: " + std::string(cname));
      for (cgsize_t i = 0; i < npe; ++i) {
        const int local = static_cast<int>(pnts[i] - 1);
        const int donor_local = static_cast<int>(dpnts[i] - 1);
        if (local < 0 || local >= rz.n_nodes) {
          throw std::runtime_error(
              "1-to-1 connection '" + std::string(cname) + "' in zone '" +
              rz.name + "' has out-of-range point index " +
              std::to_string(local));
        }
        if (donor_local < 0 ||
            donor_local >= zones[donor_zone].n_nodes) {
          throw std::runtime_error(
              "1-to-1 connection '" + std::string(cname) + "' in zone '" +
              rz.name + "' has out-of-range donor point index " +
              std::to_string(donor_local) + " in zone '" +
              zones[donor_zone].name + "'");
        }
        rz.conn_pairs.push_back({local, donor_zone, donor_local});
      }
    }
  }

  // ---- 4. Globalize nodes, merge 1-to-1 interface nodes ----
  mesh.nodes.reserve(mesh.n_nodes);
  for (const RawZone& rz : zones) {
    mesh.nodes.insert(mesh.nodes.end(), rz.coords.begin(), rz.coords.end());
  }

  UnionFind uf(mesh.n_nodes);
  for (const RawZone& rz : zones) {
    for (const RawZone::ConnPair& cp : rz.conn_pairs) {
      uf.unite(rz.node_offset + cp.local,
               zones[cp.donor_zone].node_offset + cp.donor_local);
    }
  }
  // Canonical global id for a raw (zone, zone-local 0-based) node pair.
  const auto canon = [&](const RawZone& rz, int local) {
    return uf.find(rz.node_offset + local);
  };

  // ---- 5. Cells (canonical node ids) and boundary edge tags ----
  std::vector<Cell> cells;
  cells.reserve(cell_offset);
  for (int zi = 0; zi < nzones; ++zi) {
    const RawZone& rz = zones[zi];
    if (static_cast<int>(rz.cell_conn.size()) != rz.n_cells) {
      std::printf("Warning: zone '%s' cell count (%d) differs from section "
                  "cell count (%zu)\n",
                  rz.name.c_str(), rz.n_cells, rz.cell_conn.size());
    }
    for (size_t e = 0; e < rz.cell_conn.size(); ++e) {
      Cell c;
      c.id = rz.cell_offset + static_cast<int>(e);
      c.n_nodes = rz.cell_npe[e];
      for (int k = 0; k < c.n_nodes; ++k) {
        c.nodes[k] = canon(rz, rz.cell_conn[e][k]);
      }
      c.zone = zi;
      cells.push_back(c);
    }
    for (uint8_t npe : rz.cell_npe) {
      if (npe == 3) {
        ++mesh.n_tri;
      } else {
        ++mesh.n_quad;
      }
    }
  }

  // Boundary (BAR_2) edges keyed by canonical node pair. Duplicate keys (the
  // same interface edge seen from both zones) simply overwrite; the tag is
  // only used for genuine single-cell boundary faces.
  std::unordered_map<uint64_t, BoundaryTag> boundary_edges;
  for (const RawZone& rz : zones) {
    for (const RawZone::BarSection& bar : rz.bar_sections) {
      for (const auto& [a, b] : bar.edges) {
        boundary_edges[make_edge_key(canon(rz, a), canon(rz, b))] = bar.tag;
      }
    }
  }

  if (mesh.n_tri + mesh.n_quad == 0) {
    throw std::runtime_error("no TRI_3/QUAD_4 cells found in CGNS file '" +
                             cgns_path + "'");
  }
  mesh.cells = std::move(cells);

  // ---- 6. Cell geometry (centroid, volume, orientation sign) ----
  int n_positive = 0, n_negative = 0;
  for (Cell& cell : mesh.cells) {
    Vec2 sum;
    for (int k = 0; k < cell.n_nodes; ++k) {
      sum += mesh.nodes[cell.nodes[k]];
    }
    cell.centroid = sum / static_cast<double>(cell.n_nodes);

    // Polygon area via the shoelace formula (2D "volume" per unit depth).
    // Keep the sign: consistently oriented meshes have the same sign on all
    // cells (mesh consistency check).
    double twice_area = 0.0;
    for (int k = 0; k < cell.n_nodes; ++k) {
      const Vec2& p0 = mesh.nodes[cell.nodes[k]];
      const Vec2& p1 = mesh.nodes[cell.nodes[(k + 1) % cell.n_nodes]];
      twice_area += p0.x * p1.y - p1.x * p0.y;
    }
    cell.shoelace_sign = (twice_area >= 0.0) ? 1.0 : -1.0;
    if (twice_area >= 0.0) {
      ++n_positive;
    } else {
      ++n_negative;
    }
    cell.volume = std::abs(twice_area) * 0.5;
  }
  if (n_positive > 0 && n_negative > 0) {
    std::printf("Warning: %d cell(s) have opposite orientation from the "
                "majority (%d positive, %d negative)\n",
                std::min(n_positive, n_negative), n_positive, n_negative);
  }

  // ---- 7. Face list ----
  // Each cell edge is hashed (canonical node pair). Edges seen by two cells
  // are internal faces (interface faces if the cells belong to different
  // zones); edges seen by one cell are boundary faces.
  std::unordered_map<uint64_t, std::vector<int>> edge_to_cells;
  for (const Cell& cell : mesh.cells) {
    for (int k = 0; k < cell.n_nodes; ++k) {
      const uint64_t key =
          make_edge_key(cell.nodes[k], cell.nodes[(k + 1) % cell.n_nodes]);
      edge_to_cells[key].push_back(cell.id);
    }
  }

  // Deterministic face ordering: iterate edge keys sorted.
  std::vector<uint64_t> keys;
  keys.reserve(edge_to_cells.size());
  for (const auto& [key, unused] : edge_to_cells) {
    (void)unused;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());

  std::vector<Face> faces;
  std::vector<BoundaryFace> boundary_faces;
  std::vector<int> interface_face_ids;
  int n_unmatched_boundary = 0;
  int n_bar_used = 0;      // BAR_2 edges consumed by boundary faces
  int n_bar_on_internal = 0;  // BAR_2 edges that matched internal faces

  for (const uint64_t key : keys) {
    const std::vector<int>& cell_list = edge_to_cells.at(key);
    if (cell_list.size() > 2) {
      const auto [na, nb] = edge_key_nodes(key);
      throw std::runtime_error("non-manifold edge (" + std::to_string(na) +
                               "," + std::to_string(nb) + ") shared by " +
                               std::to_string(cell_list.size()) + " cells");
    }

    const auto [ka, kb] = edge_key_nodes(key);
    const Vec2& pa = mesh.nodes[ka];
    const Vec2& pb = mesh.nodes[kb];

    Face f;
    f.id = static_cast<int>(faces.size());
    f.nodes = {ka, kb};
    f.centroid = Vec2((pa.x + pb.x) * 0.5, (pa.y + pb.y) * 0.5);
    f.area = (pb - pa).norm();

    // Normal candidates: edge vector rotated by +/- 90 degrees. The rotated
    // vector already has length == edge length, i.e. the 2D face area.
    const Vec2 e = pb - pa;
    const Vec2 n_ccw(-e.y, e.x);
    const Vec2 n_cw(e.y, -e.x);

    const bool tagged = boundary_edges.count(key) > 0;

    if (cell_list.size() == 2) {
      // Internal face: normal points from left cell to right cell.
      f.left_cell = cell_list[0];
      f.right_cell = cell_list[1];
      const Vec2 d = mesh.cells[f.right_cell].centroid -
                     mesh.cells[f.left_cell].centroid;
      f.normal = (n_ccw.dot(d) >= 0.0) ? n_ccw : n_cw;
      faces.push_back(f);

      if (mesh.cells[f.left_cell].zone != mesh.cells[f.right_cell].zone) {
        // Edge between cells of different zones: a 1-to-1 interface face.
        interface_face_ids.push_back(f.id);
        if (tagged) {
          // The BAR_2 tag (from a con-* section) matched this interface
          // edge: it was consumed, not left dangling.
          ++n_bar_on_internal;
        }
      } else if (tagged) {
        // A BAR_2-tagged edge shared by two cells of the same zone: not a
        // boundary, so the tag is ignored (non-conforming data, but keep the
        // count for diagnostics).
        ++n_bar_on_internal;
      }
    } else {
      // Boundary face: normal points outward from the adjacent cell.
      f.left_cell = cell_list[0];
      f.right_cell = -1;
      const Vec2 d = f.centroid - mesh.cells[f.left_cell].centroid;
      f.normal = (n_ccw.dot(d) >= 0.0) ? n_ccw : n_cw;
      faces.push_back(f);

      BoundaryFace bf;
      bf.face_id = f.id;
      bf.cell = f.left_cell;

      const auto it = boundary_edges.find(key);
      if (it != boundary_edges.end()) {
        bf.family = it->second.family;
        bf.bc_type = it->second.bc_type;
        ++n_bar_used;
      } else {
        bf.family = "(unmatched)";
        bf.bc_type = BCType::Invalid;
        ++n_unmatched_boundary;
      }
      boundary_faces.push_back(std::move(bf));
    }
  }

  mesh.faces = std::move(faces);
  mesh.boundary_faces = std::move(boundary_faces);
  mesh.interface_face_ids = std::move(interface_face_ids);

  // ---- 8. Collect boundary family names (sorted, unique) ----
  {
    std::set<std::string> fams;
    for (const BoundaryFace& bf : mesh.boundary_faces) {
      fams.insert(bf.family);
    }
    mesh.boundary_families.assign(fams.begin(), fams.end());
  }

  // ---- 9. Cell adjacency CSR (faces and neighbors per cell) ----
  {
    // Count faces per cell, then prefix-sum into offsets.
    std::vector<int> face_counts(mesh.cells.size(), 0);
    for (const Face& f : mesh.faces) {
      if (f.left_cell >= 0) {
        ++face_counts[f.left_cell];
      }
      if (f.right_cell >= 0) {
        ++face_counts[f.right_cell];
      }
    }
    mesh.cell_faces_offsets.resize(mesh.cells.size() + 1);
    mesh.cell_faces_offsets[0] = 0;
    for (size_t i = 0; i < mesh.cells.size(); ++i) {
      mesh.cell_faces_offsets[i + 1] =
          mesh.cell_faces_offsets[i] + face_counts[i];
    }
    mesh.cell_faces_data.assign(
        mesh.cell_faces_offsets.back(), 0);
    std::vector<int> fill_pos = mesh.cell_faces_offsets;
    for (const Face& f : mesh.faces) {
      if (f.left_cell >= 0) {
        mesh.cell_faces_data[fill_pos[f.left_cell]++] = f.id;
      }
      if (f.right_cell >= 0) {
        mesh.cell_faces_data[fill_pos[f.right_cell]++] = f.id;
      }
    }

    // Neighbors: symmetric relation from internal faces (right_cell >= 0).
    std::vector<int> neighbor_counts(mesh.cells.size(), 0);
    for (const Face& f : mesh.faces) {
      if (f.right_cell >= 0) {
        ++neighbor_counts[f.left_cell];
        ++neighbor_counts[f.right_cell];
      }
    }
    mesh.cell_neighbors_offsets.resize(mesh.cells.size() + 1);
    mesh.cell_neighbors_offsets[0] = 0;
    for (size_t i = 0; i < mesh.cells.size(); ++i) {
      mesh.cell_neighbors_offsets[i + 1] =
          mesh.cell_neighbors_offsets[i] + neighbor_counts[i];
    }
    mesh.cell_neighbors_data.assign(mesh.cell_neighbors_offsets.back(), 0);
    fill_pos = mesh.cell_neighbors_offsets;
    for (const Face& f : mesh.faces) {
      if (f.right_cell >= 0) {
        mesh.cell_neighbors_data[fill_pos[f.left_cell]++] = f.right_cell;
        mesh.cell_neighbors_data[fill_pos[f.right_cell]++] = f.left_cell;
      }
    }
  }

  // ---- 10. Sanity checks ----
  if (n_unmatched_boundary > 0) {
    std::printf("Warning: %d boundary edge(s) did not match any BAR_2 "
                "boundary element\n",
                n_unmatched_boundary);
  }
  const int n_bar_edges = static_cast<int>(boundary_edges.size());
  if (n_bar_used + n_bar_on_internal != n_bar_edges) {
    std::printf("Warning: %d BAR_2 boundary element(s) matched neither a "
                "boundary nor an internal face (%d used as boundary, %d on "
                "internal faces)\n",
                n_bar_edges - n_bar_used - n_bar_on_internal, n_bar_used,
                n_bar_on_internal);
  }
  for (const auto& [family, bc] : bc_map) {
    const bool in_base_families = family_names.count(family) > 0;
    const bool has_faces =
        std::find(mesh.boundary_families.begin(), mesh.boundary_families.end(),
                  family) != mesh.boundary_families.end();
    if (!has_faces) {
      if (in_base_families) {
        std::printf("Note: BC family '%s' (%s) is defined in the mesh but has "
                    "no faces in any zone\n",
                    family.c_str(), bc_type_to_string(bc));
      } else {
        std::printf("Note: BC family '%s' (%s) from the case file is not "
                    "defined in the mesh\n",
                    family.c_str(), bc_type_to_string(bc));
      }
    }
  }

  return mesh;
}

// ---------------------------------------------------------------------------
// Summary printing
// ---------------------------------------------------------------------------
void print_mesh_summary(const Mesh& mesh) {
  int n_internal = 0;
  for (const Face& f : mesh.faces) {
    if (f.right_cell >= 0) {
      ++n_internal;
    }
  }
  const int n_boundary = static_cast<int>(mesh.boundary_faces.size());
  const int n_interface = static_cast<int>(mesh.interface_face_ids.size());
  const int n_faces = static_cast<int>(mesh.faces.size());

  // Face counts per boundary family.
  std::map<std::string, int> family_counts;
  std::map<std::string, int> family_bc_counts;  // faces with a valid BC type
  for (const BoundaryFace& bf : mesh.boundary_faces) {
    family_counts[bf.family]++;
    if (bf.bc_type != BCType::Invalid) {
      family_bc_counts[bf.family]++;
    }
  }

  std::printf("\n=== Mesh Summary ===\n");
  std::printf("File    : %s\n", mesh.file_path.c_str());
  if (mesh.zone_names.size() > 1) {
    std::printf("Zones   : %zu (", mesh.zone_names.size());
    for (size_t i = 0; i < mesh.zone_names.size(); ++i) {
      std::printf("%s%s", i > 0 ? ", " : "", mesh.zone_names[i].c_str());
    }
    std::printf(")\n");
  } else {
    std::printf("Zone    : %s (Unstructured)\n", mesh.zone_name.c_str());
  }
  std::printf("Nodes   : %d\n", mesh.n_nodes);
  std::printf("Cells   : %d  (TRI_3: %d, QUAD_4: %d)\n",
              static_cast<int>(mesh.cells.size()), mesh.n_tri, mesh.n_quad);
  std::printf("Faces   : %d total (%d internal, %d boundary, %d interface)\n",
              n_faces, n_internal, n_boundary, n_interface);
  std::printf("Boundary families (%zu):\n", family_counts.size());
  for (const auto& [family, count] : family_counts) {
    const int n_tagged = family_bc_counts[family];
    std::printf("  %-22s %5d faces", family.c_str(), count);
    if (n_tagged == count) {
      std::printf("   (tagged)\n");
    } else if (n_tagged > 0) {
      std::printf("   (%d tagged, %d untagged)\n", n_tagged,
                  count - n_tagged);
    } else {
      std::printf("   (untagged)\n");
    }
  }

  // Domain sanity checks.
  double total_cell_volume = 0.0;
  for (const Cell& c : mesh.cells) {
    total_cell_volume += c.volume;
  }
  double total_boundary_length = 0.0;
  for (const BoundaryFace& bf : mesh.boundary_faces) {
    total_boundary_length += mesh.faces[bf.face_id].area;
  }
  std::printf("Domain  : cell area sum = %.12g, boundary length = %.12g\n",
              total_cell_volume, total_boundary_length);

  // Adjacency CSR sanity: per-cell face/neighbor counts.
  if (!mesh.cell_faces_offsets.empty()) {
    const int max_faces = static_cast<int>(mesh.cell_faces_offsets.size()) > 1
                              ? mesh.cell_faces_offsets[1] -
                                    mesh.cell_faces_offsets[0]
                              : 0;
    const int max_neighbors =
        static_cast<int>(mesh.cell_neighbors_offsets.size()) > 1
            ? mesh.cell_neighbors_offsets[1] -
                  mesh.cell_neighbors_offsets[0]
            : 0;
    std::printf("Adjacency: %zu cell-face entries, %zu cell-neighbor entries "
                "(max faces/cell = %d, max neighbors/cell = %d)\n",
                mesh.cell_faces_data.size(), mesh.cell_neighbors_data.size(),
                max_faces, max_neighbors);
  }
  std::printf("=== End Mesh Summary ===\n");
}

}  // namespace cfd
