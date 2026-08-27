#include "mesh/CgnsReader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <unordered_map>
#include <vector>

#include <cgnslib.h>
#include <cgns_io.h>

#include "core/Exception.hpp"
#include "core/Log.hpp"

namespace cfd {
namespace {

struct CgnsFile {
  int fn = -1;
  explicit CgnsFile(const std::string& path) {
    if (cg_open(path.c_str(), CG_MODE_READ, &fn) != CG_OK) {
      CFD_THROW("cannot open CGNS file '" << path << "': " << cg_get_error());
    }
  }
  ~CgnsFile() { if (fn >= 0) cg_close(fn); }
  CgnsFile(const CgnsFile&) = delete;
  CgnsFile& operator=(const CgnsFile&) = delete;
};

#define CGNS_CALL(expr, what)                                       \
  do {                                                              \
    if ((expr) != CG_OK) {                                          \
      CFD_THROW("CGNS error while " << what << ": " << cg_get_error()); \
    }                                                               \
  } while (false)

// Disjoint-set over the concatenated per-zone node numbering.
class UnionFind {
 public:
  explicit UnionFind(std::size_t n) : parent_(n) {
    std::iota(parent_.begin(), parent_.end(), static_cast<Index>(0));
  }
  Index find(Index a) {
    while (parent_[a] != a) { parent_[a] = parent_[parent_[a]]; a = parent_[a]; }
    return a;
  }
  void unite(Index a, Index b) {
    a = find(a); b = find(b);
    if (a != b) parent_[std::max(a, b)] = std::min(a, b);
  }

 private:
  std::vector<Index> parent_;
};

int nodesPerElement(CGNS_ENUMT(ElementType_t) t) {
  switch (t) {
    case CGNS_ENUMV(BAR_2): return 2;
    case CGNS_ENUMV(TRI_3): return 3;
    case CGNS_ENUMV(QUAD_4): return 4;
    default: return -1;
  }
}

int elementDim(CGNS_ENUMT(ElementType_t) t) {
  switch (t) {
    case CGNS_ENUMV(BAR_2): return 1;
    case CGNS_ENUMV(TRI_3):
    case CGNS_ENUMV(QUAD_4): return 2;
    default: return -1;
  }
}

// Per-zone element inventory built while scanning sections.
struct ZoneElements {
  // 2-D elements: node lists in zone-local (1-based -> 0-based) numbering.
  std::vector<std::vector<Index>> cells;
  // 1-D elements indexed by CGNS element id, used to resolve element-located BCs.
  std::unordered_map<cgsize_t, std::pair<Index, Index>> edges;
};

std::uint64_t edgeKey(Index a, Index b) {
  const std::uint64_t lo = static_cast<std::uint64_t>(std::min(a, b));
  const std::uint64_t hi = static_cast<std::uint64_t>(std::max(a, b));
  return (hi << 32) | lo;
}

}  // namespace

CgnsMeshInfo readCgnsMesh(const std::string& path, const CgnsReadOptions& opt, GlobalMesh& mesh) {
  CgnsFile file(path);
  const int fn = file.fn;

  int nbases = 0;
  CGNS_CALL(cg_nbases(fn, &nbases), "reading number of bases");
  CFD_CHECK(nbases >= 1, "CGNS file '" << path << "' contains no base");

  int base = -1, cell_dim = 0, phys_dim = 0;
  char base_name[CGIO_MAX_NAME_LENGTH + 1] = {0};
  for (int b = 1; b <= nbases; ++b) {
    char nm[CGIO_MAX_NAME_LENGTH + 1] = {0};
    int cd = 0, pd = 0;
    CGNS_CALL(cg_base_read(fn, b, nm, &cd, &pd), "reading base " << b);
    if (cd == 2) { base = b; cell_dim = cd; phys_dim = pd; std::snprintf(base_name, sizeof(base_name), "%s", nm); break; }
  }
  CFD_CHECK(base > 0, "CGNS file '" << path << "' contains no 2-D (cell dimension 2) base; "
            << "this solver build is 2-D");
  (void)phys_dim;
  (void)cell_dim;

  int nzones = 0;
  CGNS_CALL(cg_nzones(fn, base, &nzones), "reading number of zones");
  CFD_CHECK(nzones >= 1, "base '" << base_name << "' has no zones");

  CgnsMeshInfo info;
  info.base_name = base_name;
  info.num_zones = nzones;

  // ---------------------------------------------------------------- pass 1:
  // zone sizes, coordinates, elements.
  std::vector<Index> zone_node_offset(nzones + 1, 0);
  std::vector<Index> zone_num_nodes(nzones, 0);
  std::vector<std::string> zone_names(nzones);
  std::map<std::string, int> zone_index_of_name;
  std::vector<Real> raw_x, raw_y;
  std::vector<ZoneElements> zelems(nzones);

  for (int z = 1; z <= nzones; ++z) {
    char zname[CGIO_MAX_NAME_LENGTH + 1] = {0};
    cgsize_t zsize[9] = {0};
    CGNS_CALL(cg_zone_read(fn, base, z, zname, zsize), "reading zone " << z);
    CGNS_ENUMT(ZoneType_t) ztype;
    CGNS_CALL(cg_zone_type(fn, base, z, &ztype), "reading zone type of '" << zname << "'");
    CFD_CHECK(ztype == CGNS_ENUMV(Unstructured),
              "zone '" << zname << "' is " << ZoneTypeName[ztype]
              << "; this solver reads unstructured zones only");
    zone_names[z - 1] = zname;
    zone_index_of_name[zname] = z - 1;
    const Index nn = static_cast<Index>(zsize[0]);
    zone_num_nodes[z - 1] = nn;
    zone_node_offset[z] = zone_node_offset[z - 1] + nn;

    // coordinates
    std::vector<double> cx(nn), cy(nn);
    cgsize_t rmin = 1, rmax = nn;
    CGNS_CALL(cg_coord_read(fn, base, z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, cx.data()),
              "reading CoordinateX of zone '" << zname << "'");
    CGNS_CALL(cg_coord_read(fn, base, z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, cy.data()),
              "reading CoordinateY of zone '" << zname << "'");
    raw_x.insert(raw_x.end(), cx.begin(), cx.end());
    raw_y.insert(raw_y.end(), cy.begin(), cy.end());

    // element sections
    int nsec = 0;
    CGNS_CALL(cg_nsections(fn, base, z, &nsec), "reading sections of zone '" << zname << "'");
    for (int s = 1; s <= nsec; ++s) {
      char sname[CGIO_MAX_NAME_LENGTH + 1] = {0};
      CGNS_ENUMT(ElementType_t) etype;
      cgsize_t estart = 0, eend = 0;
      int nbndry = 0, parent_flag = 0;
      CGNS_CALL(cg_section_read(fn, base, z, s, sname, &etype, &estart, &eend, &nbndry, &parent_flag),
                "reading section " << s << " of zone '" << zname << "'");
      cgsize_t dsize = 0;
      CGNS_CALL(cg_ElementDataSize(fn, base, z, s, &dsize),
                "sizing section '" << sname << "'");
      std::vector<cgsize_t> conn(static_cast<std::size_t>(dsize));
      std::vector<cgsize_t> offsets;
      if (etype == CGNS_ENUMV(MIXED)) {
        CGNS_CALL(cg_elements_read(fn, base, z, s, conn.data(), nullptr),
                  "reading MIXED section '" << sname << "'");
        cgsize_t pos = 0;
        for (cgsize_t e = estart; e <= eend; ++e) {
          CFD_CHECK(pos < dsize, "MIXED section '" << sname << "' connectivity is truncated");
          const auto sub = static_cast<CGNS_ENUMT(ElementType_t)>(conn[pos]);
          const int npe = nodesPerElement(sub);
          CFD_CHECK(npe > 0, "unsupported element type " << ElementTypeName[sub]
                    << " inside MIXED section '" << sname << "' of zone '" << zname << "'");
          std::vector<Index> nodes(npe);
          for (int k = 0; k < npe; ++k) nodes[k] = static_cast<Index>(conn[pos + 1 + k]) - 1;
          if (elementDim(sub) == 2) {
            zelems[z - 1].cells.push_back(nodes);
          } else {
            zelems[z - 1].edges[e] = {nodes[0], nodes[1]};
          }
          pos += npe + 1;
        }
      } else {
        const int npe = nodesPerElement(etype);
        CFD_CHECK(npe > 0, "unsupported element type " << ElementTypeName[etype]
                  << " in section '" << sname << "' of zone '" << zname
                  << "' (supported: BAR_2, TRI_3, QUAD_4, MIXED of those)");
        CGNS_CALL(cg_elements_read(fn, base, z, s, conn.data(), nullptr),
                  "reading section '" << sname << "'");
        const cgsize_t nelem = eend - estart + 1;
        CFD_CHECK(dsize == nelem * npe, "section '" << sname << "' data size mismatch");
        for (cgsize_t e = 0; e < nelem; ++e) {
          if (elementDim(etype) == 2) {
            std::vector<Index> nodes(npe);
            for (int k = 0; k < npe; ++k) nodes[k] = static_cast<Index>(conn[e * npe + k]) - 1;
            zelems[z - 1].cells.push_back(nodes);
          } else {
            zelems[z - 1].edges[estart + e] = {static_cast<Index>(conn[e * npe]) - 1,
                                               static_cast<Index>(conn[e * npe + 1]) - 1};
          }
        }
      }
    }
  }

  const Index total_raw_nodes = zone_node_offset[nzones];
  info.nodes_before_merge = total_raw_nodes;

  // ---------------------------------------------------------------- pass 2:
  // merge nodes across zone interfaces using vertex 1-to-1 connectivity.
  UnionFind uf(static_cast<std::size_t>(total_raw_nodes));
  Index merge_pairs = 0;
  for (int z = 1; z <= nzones; ++z) {
    int nconn = 0;
    CGNS_CALL(cg_nconns(fn, base, z, &nconn), "reading connectivities of zone " << z);
    for (int i = 1; i <= nconn; ++i) {
      char cname[CGIO_MAX_NAME_LENGTH + 1] = {0};
      char dname[CGIO_MAX_NAME_LENGTH + 1] = {0};
      CGNS_ENUMT(GridLocation_t) loc;
      CGNS_ENUMT(GridConnectivityType_t) ctype;
      CGNS_ENUMT(PointSetType_t) pst, dpst;
      CGNS_ENUMT(ZoneType_t) dzt;
      CGNS_ENUMT(DataType_t) ddt;
      cgsize_t npnts = 0, ndpnts = 0;
      CGNS_CALL(cg_conn_info(fn, base, z, i, cname, &loc, &ctype, &pst, &npnts, dname, &dzt,
                             &dpst, &ddt, &ndpnts),
                "reading connectivity " << i << " info of zone '" << zone_names[z - 1] << "'");
      if (ctype != CGNS_ENUMV(Abutting1to1)) continue;
      if (loc != CGNS_ENUMV(Vertex)) {
        LOG() << "  warning: connectivity '" << cname << "' of zone '" << zone_names[z - 1]
              << "' uses grid location " << GridLocationName[loc]
              << " which is not merged automatically; use --node-merge-tol if the mesh does "
                 "not assemble.\n";
        continue;
      }
      auto dz = zone_index_of_name.find(dname);
      CFD_CHECK(dz != zone_index_of_name.end(),
                "connectivity '" << cname << "' references unknown donor zone '" << dname << "'");
      CFD_CHECK(npnts == ndpnts,
                "connectivity '" << cname << "' has mismatched point/donor counts");
      std::vector<cgsize_t> pnts(static_cast<std::size_t>(npnts));
      std::vector<cgsize_t> dpts(static_cast<std::size_t>(ndpnts));
      CGNS_CALL(cg_conn_read(fn, base, z, i, pnts.data(), CGNS_ENUMV(Integer), dpts.data()),
                "reading connectivity '" << cname << "'");
      const Index off_z = zone_node_offset[z - 1];
      const Index off_d = zone_node_offset[dz->second];
      for (cgsize_t k = 0; k < npnts; ++k) {
        uf.unite(off_z + static_cast<Index>(pnts[k]) - 1, off_d + static_cast<Index>(dpts[k]) - 1);
        ++merge_pairs;
      }
    }
  }

  // Optional geometric fallback merge.
  if (opt.node_merge_tol > 0.0) {
    const Real tol = opt.node_merge_tol;
    std::unordered_map<std::uint64_t, std::vector<Index>> bucket;
    auto key = [&](Real xx, Real yy) {
      const std::int64_t ix = static_cast<std::int64_t>(std::floor(xx / tol));
      const std::int64_t iy = static_cast<std::int64_t>(std::floor(yy / tol));
      return (static_cast<std::uint64_t>(ix) * 0x9E3779B97F4A7C15ull) ^ static_cast<std::uint64_t>(iy);
    };
    for (Index n = 0; n < total_raw_nodes; ++n) bucket[key(raw_x[n], raw_y[n])].push_back(n);
    for (Index n = 0; n < total_raw_nodes; ++n) {
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          auto it = bucket.find(key(raw_x[n] + dx * tol, raw_y[n] + dy * tol));
          if (it == bucket.end()) continue;
          for (Index m : it->second) {
            if (m <= n) continue;
            const Real ddx = raw_x[n] - raw_x[m], ddy = raw_y[n] - raw_y[m];
            if (ddx * ddx + ddy * ddy <= tol * tol) { uf.unite(n, m); ++merge_pairs; }
          }
        }
      }
    }
  }

  // Compact the merged node numbering.
  std::vector<Index> new_id(static_cast<std::size_t>(total_raw_nodes), -1);
  mesh.x.clear(); mesh.y.clear();
  mesh.x.reserve(total_raw_nodes); mesh.y.reserve(total_raw_nodes);
  for (Index n = 0; n < total_raw_nodes; ++n) {
    const Index r = uf.find(n);
    if (new_id[r] < 0) {
      new_id[r] = static_cast<Index>(mesh.x.size());
      mesh.x.push_back(raw_x[r]);
      mesh.y.push_back(raw_y[r]);
    }
    new_id[n] = new_id[r];
    const Real gap = std::hypot(raw_x[n] - raw_x[r], raw_y[n] - raw_y[r]);
    info.max_merge_gap = std::max(info.max_merge_gap, gap);
  }
  info.nodes_merged = total_raw_nodes - static_cast<Index>(mesh.x.size());
  info.zone_names = zone_names;

  // ---------------------------------------------------------------- cells
  mesh.cell_node_offset.clear();
  mesh.cell_nodes.clear();
  mesh.cell_zone.clear();
  mesh.cell_node_offset.push_back(0);
  for (int z = 0; z < nzones; ++z) {
    for (const auto& nodes : zelems[z].cells) {
      for (Index n : nodes) {
        CFD_CHECK(n >= 0 && n < zone_num_nodes[z],
                  "element in zone '" << zone_names[z] << "' references out-of-range node "
                  << (n + 1));
        mesh.cell_nodes.push_back(new_id[zone_node_offset[z] + n]);
      }
      mesh.cell_node_offset.push_back(static_cast<Index>(mesh.cell_nodes.size()));
      mesh.cell_zone.push_back(static_cast<std::int8_t>(z));
    }
  }
  CFD_CHECK(mesh.numCells() > 0, "CGNS file '" << path << "' contains no 2-D elements");
  mesh.orientCells();

  // Boundary-edge candidates: cell edges that occur exactly once.
  std::unordered_map<std::uint64_t, int> edge_count;
  edge_count.reserve(2 * mesh.cell_nodes.size());
  for (Index c = 0; c < mesh.numCells(); ++c) {
    const int n = mesh.cellSize(c);
    const Index* nodes = mesh.cellNodePtr(c);
    for (int i = 0; i < n; ++i) edge_count[edgeKey(nodes[i], nodes[(i + 1) % n])]++;
  }

  // ---------------------------------------------------------------- BCs
  std::map<std::string, std::size_t> patch_of_name;
  std::vector<std::vector<std::pair<Index, Index>>> patch_edges;
  mesh.patches.clear();

  for (int z = 1; z <= nzones; ++z) {
    int nbocos = 0;
    CGNS_CALL(cg_nbocos(fn, base, z, &nbocos), "reading BCs of zone '" << zone_names[z - 1] << "'");
    for (int i = 1; i <= nbocos; ++i) {
      char bname[CGIO_MAX_NAME_LENGTH + 1] = {0};
      CGNS_ENUMT(BCType_t) bctype;
      CGNS_ENUMT(PointSetType_t) pst;
      cgsize_t npnts = 0, nrmlistflag = 0;
      int nrmindex[3] = {0}, ndataset = 0;
      CGNS_ENUMT(DataType_t) ndt;
      CGNS_CALL(cg_boco_info(fn, base, z, i, bname, &bctype, &pst, &npnts, nrmindex, &nrmlistflag,
                             &ndt, &ndataset),
                "reading BC " << i << " info of zone '" << zone_names[z - 1] << "'");
      CGNS_ENUMT(GridLocation_t) gloc = CGNS_ENUMV(Vertex);
      cg_boco_gridlocation_read(fn, base, z, i, &gloc);

      char famname[CGIO_MAX_NAME_LENGTH + 1] = {0};
      std::string resolved = bname;
      if (cg_goto(fn, base, "Zone_t", z, "ZoneBC_t", 1, "BC_t", i, "end") == CG_OK) {
        if (cg_famname_read(famname) == CG_OK && famname[0] != '\0') resolved = famname;
      }

      std::vector<cgsize_t> pnts(static_cast<std::size_t>(std::max<cgsize_t>(npnts, 2)));
      CGNS_CALL(cg_boco_read(fn, base, z, i, pnts.data(), nullptr),
                "reading BC '" << bname << "' point set");

      // Expand the point set into a list of indices.
      std::vector<cgsize_t> ids;
      if (pst == CGNS_ENUMV(PointRange) || pst == CGNS_ENUMV(ElementRange)) {
        CFD_CHECK(npnts == 2, "BC '" << bname << "' declares a range with " << npnts << " entries");
        for (cgsize_t v = pnts[0]; v <= pnts[1]; ++v) ids.push_back(v);
      } else if (pst == CGNS_ENUMV(PointList) || pst == CGNS_ENUMV(ElementList)) {
        ids.assign(pnts.begin(), pnts.begin() + npnts);
      } else {
        CFD_THROW("BC '" << bname << "' uses unsupported point-set type " << PointSetTypeName[pst]);
      }

      auto pit = patch_of_name.find(resolved);
      std::size_t pidx;
      if (pit == patch_of_name.end()) {
        pidx = mesh.patches.size();
        patch_of_name[resolved] = pidx;
        BoundaryPatch bp;
        bp.name = resolved;
        bp.bc_name = bname;
        mesh.patches.push_back(bp);
        patch_edges.emplace_back();
      } else {
        pidx = pit->second;
      }

      const Index off = zone_node_offset[z - 1];
      const bool element_located = (gloc == CGNS_ENUMV(EdgeCenter) ||
                                    gloc == CGNS_ENUMV(FaceCenter) ||
                                    gloc == CGNS_ENUMV(CellCenter) ||
                                    pst == CGNS_ENUMV(ElementRange) ||
                                    pst == CGNS_ENUMV(ElementList));
      if (element_located) {
        const auto& emap = zelems[z - 1].edges;
        for (cgsize_t e : ids) {
          auto it = emap.find(e);
          CFD_CHECK(it != emap.end(),
                    "BC '" << bname << "' of zone '" << zone_names[z - 1] << "' references element "
                    << e << " which is not a 1-D (BAR_2) element in this zone");
          const Index a = new_id[off + it->second.first];
          const Index b = new_id[off + it->second.second];
          auto ec = edge_count.find(edgeKey(a, b));
          CFD_CHECK(ec != edge_count.end(),
                    "BC '" << bname << "' element " << e << " is not an edge of any 2-D cell");
          CFD_CHECK(ec->second == 1,
                    "BC '" << bname << "' element " << e
                    << " is an interior edge (shared by two cells)");
          patch_edges[pidx].emplace_back(a, b);
        }
      } else {
        // Vertex-located BC: pick the boundary edges whose both endpoints are listed.
        std::vector<char> in_set(static_cast<std::size_t>(mesh.x.size()), 0);
        for (cgsize_t v : ids) {
          CFD_CHECK(v >= 1 && v <= zone_num_nodes[z - 1],
                    "BC '" << bname << "' references out-of-range vertex " << v);
          in_set[new_id[off + static_cast<Index>(v) - 1]] = 1;
        }
        for (Index c = 0; c < mesh.numCells(); ++c) {
          const int n = mesh.cellSize(c);
          const Index* nodes = mesh.cellNodePtr(c);
          for (int k = 0; k < n; ++k) {
            const Index a = nodes[k], b = nodes[(k + 1) % n];
            if (!in_set[a] || !in_set[b]) continue;
            if (edge_count[edgeKey(a, b)] != 1) continue;
            patch_edges[pidx].emplace_back(a, b);
          }
        }
      }
    }
  }
  CFD_CHECK(!mesh.patches.empty(), "CGNS file '" << path << "' declares no boundary conditions");

  mesh.buildTopology(patch_edges);

  if (opt.verbose) {
    LOG() << "CGNS base '" << info.base_name << "', " << nzones << " zone(s):";
    for (int z = 0; z < nzones; ++z) LOG() << " '" << zone_names[z] << "'";
    LOG() << "\n  node merge: " << total_raw_nodes << " raw -> " << mesh.numNodes()
          << " unique (" << info.nodes_merged << " merged from " << merge_pairs
          << " interface pairs, max gap " << info.max_merge_gap << ")\n";
    LOG() << mesh.summary() << "\n";
  }
  return info;
}

}  // namespace cfd
