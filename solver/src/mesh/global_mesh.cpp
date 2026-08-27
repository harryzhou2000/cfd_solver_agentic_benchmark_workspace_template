#include "mesh/global_mesh.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "core/exceptions.h"
#include "core/logging.h"

namespace cns2d {
namespace {

// --- union-find over merged node ids --------------------------------------
class DisjointSet {
 public:
  explicit DisjointSet(std::size_t n) : parent_(n) {
    std::iota(parent_.begin(), parent_.end(), static_cast<Index>(0));
  }
  Index find(Index a) {
    while (parent_[static_cast<std::size_t>(a)] != a) {
      // Path halving keeps the structure shallow without recursion.
      parent_[static_cast<std::size_t>(a)] =
          parent_[static_cast<std::size_t>(parent_[static_cast<std::size_t>(a)])];
      a = parent_[static_cast<std::size_t>(a)];
    }
    return a;
  }
  bool unite(Index a, Index b) {
    a = find(a);
    b = find(b);
    if (a == b) return false;
    // Keep the smaller id as the representative for deterministic output.
    if (b < a) std::swap(a, b);
    parent_[static_cast<std::size_t>(b)] = a;
    return true;
  }

 private:
  std::vector<Index> parent_;
};

// Key for the unique-face hash map: the two endpoint node ids, sorted.
struct EdgeKey {
  Index a{-1};
  Index b{-1};
  bool operator==(const EdgeKey &o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash {
  std::size_t operator()(const EdgeKey &k) const {
    const std::uint64_t ua = static_cast<std::uint32_t>(k.a);
    const std::uint64_t ub = static_cast<std::uint32_t>(k.b);
    std::uint64_t h = ua * 0x9E3779B97F4A7C15ULL ^ (ub + 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return static_cast<std::size_t>(h);
  }
};

EdgeKey makeEdgeKey(Index a, Index b) {
  EdgeKey k;
  k.a = std::min(a, b);
  k.b = std::max(a, b);
  return k;
}

// Node pairs forming the edges of a 2-D cell, in cyclic order.
int cellEdges(const GlobalCell &cell, std::array<std::array<Index, 2>, 4> &edges) {
  const int n = cell.num_nodes;
  for (int e = 0; e < n; ++e) {
    edges[static_cast<std::size_t>(e)] = {cell.nodes[static_cast<std::size_t>(e)],
                                         cell.nodes[static_cast<std::size_t>((e + 1) % n)]};
  }
  return n;
}

Real signedArea(const GlobalMesh &mesh, const GlobalCell &cell) {
  Real acc = 0.0;
  const int n = cell.num_nodes;
  for (int e = 0; e < n; ++e) {
    const Index i = cell.nodes[static_cast<std::size_t>(e)];
    const Index j = cell.nodes[static_cast<std::size_t>((e + 1) % n)];
    acc += mesh.x[static_cast<std::size_t>(i)] * mesh.y[static_cast<std::size_t>(j)] -
           mesh.x[static_cast<std::size_t>(j)] * mesh.y[static_cast<std::size_t>(i)];
  }
  return 0.5 * acc;
}

}  // namespace

GlobalMesh buildGlobalMesh(const RawMesh &raw) {
  GlobalMesh mesh;

  // ---- 1. concatenate zone nodes, remembering each zone's offset ---------
  const std::size_t num_zones = raw.zones.size();
  std::vector<Index> zone_node_offset(num_zones + 1, 0);
  for (std::size_t z = 0; z < num_zones; ++z) {
    zone_node_offset[z + 1] = zone_node_offset[z] + raw.zones[z].num_nodes;
  }
  const Index total_raw_nodes = zone_node_offset[num_zones];

  std::vector<Real> raw_x(static_cast<std::size_t>(total_raw_nodes));
  std::vector<Real> raw_y(static_cast<std::size_t>(total_raw_nodes));
  for (std::size_t z = 0; z < num_zones; ++z) {
    const RawZone &zone = raw.zones[z];
    const std::size_t off = static_cast<std::size_t>(zone_node_offset[z]);
    std::copy(zone.x.begin(), zone.x.end(), raw_x.begin() + static_cast<std::ptrdiff_t>(off));
    std::copy(zone.y.begin(), zone.y.end(), raw_y.begin() + static_cast<std::ptrdiff_t>(off));
  }

  // ---- 2. fuse nodes matched by 1-to-1 zone connectivity -----------------
  std::unordered_map<std::string, std::size_t> zone_index_by_name;
  for (std::size_t z = 0; z < num_zones; ++z) {
    zone_index_by_name.emplace(raw.zones[z].name, z);
  }

  DisjointSet dsu(static_cast<std::size_t>(total_raw_nodes));
  Index merged_pairs = 0;
  Real max_merge_distance = 0.0;
  for (std::size_t z = 0; z < num_zones; ++z) {
    const RawZone &zone = raw.zones[z];
    for (const RawConnectivity &conn : zone.connectivities) {
      auto donor_it = zone_index_by_name.find(conn.donor_zone_name);
      if (donor_it == zone_index_by_name.end()) {
        throw CnsError("zone connectivity '" + conn.name + "' of zone '" + zone.name +
                       "' names unknown donor zone '" + conn.donor_zone_name + "'");
      }
      const std::size_t donor_zone = donor_it->second;
      const Index this_off = zone_node_offset[z];
      const Index donor_off = zone_node_offset[donor_zone];
      for (std::size_t k = 0; k < conn.point_list.size(); ++k) {
        // CGNS point lists for unstructured 1-to-1 patches are 1-based node ids.
        const GlobalIndex local_1based = conn.point_list[k];
        const GlobalIndex donor_1based = conn.point_list_donor[k];
        if (local_1based < 1 || local_1based > zone.num_nodes) {
          throw CnsError("connectivity '" + conn.name + "' references out-of-range node " +
                         std::to_string(local_1based) + " in zone '" + zone.name + "'");
        }
        if (donor_1based < 1 || donor_1based > raw.zones[donor_zone].num_nodes) {
          throw CnsError("connectivity '" + conn.name + "' references out-of-range donor node " +
                         std::to_string(donor_1based) + " in zone '" + conn.donor_zone_name + "'");
        }
        const Index a = this_off + static_cast<Index>(local_1based - 1);
        const Index b = donor_off + static_cast<Index>(donor_1based - 1);
        const Real dx = raw_x[static_cast<std::size_t>(a)] - raw_x[static_cast<std::size_t>(b)];
        const Real dy = raw_y[static_cast<std::size_t>(a)] - raw_y[static_cast<std::size_t>(b)];
        max_merge_distance = std::max(max_merge_distance, std::sqrt(dx * dx + dy * dy));
        if (dsu.unite(a, b)) ++merged_pairs;
      }
    }
  }
  if (max_merge_distance > 1.0e-8) {
    throw CnsError("CGNS 1-to-1 zone connectivity matches nodes that are " +
                   std::to_string(max_merge_distance) +
                   " apart; the interface is not point-matched and cannot be fused");
  }

  // Compact representatives into a dense merged numbering.
  std::vector<Index> merged_id(static_cast<std::size_t>(total_raw_nodes), -1);
  Index num_merged = 0;
  for (Index i = 0; i < total_raw_nodes; ++i) {
    const Index root = dsu.find(i);
    if (merged_id[static_cast<std::size_t>(root)] < 0) {
      merged_id[static_cast<std::size_t>(root)] = num_merged++;
    }
    merged_id[static_cast<std::size_t>(i)] = merged_id[static_cast<std::size_t>(root)];
  }
  mesh.x.assign(static_cast<std::size_t>(num_merged), 0.0);
  mesh.y.assign(static_cast<std::size_t>(num_merged), 0.0);
  for (Index i = 0; i < total_raw_nodes; ++i) {
    const Index m = merged_id[static_cast<std::size_t>(i)];
    mesh.x[static_cast<std::size_t>(m)] = raw_x[static_cast<std::size_t>(i)];
    mesh.y[static_cast<std::size_t>(m)] = raw_y[static_cast<std::size_t>(i)];
  }
  mesh.num_merged_node_pairs = merged_pairs;
  mesh.max_merge_distance = max_merge_distance;

  // ---- 3. collect 2-D elements as cells ---------------------------------
  // Boundary (1-D) sections are indexed separately so BC element ranges can be
  // resolved to node pairs afterwards.
  struct BoundaryElement {
    Index n0{-1};
    Index n1{-1};
  };
  // Per zone: CGNS element id -> boundary element nodes (merged numbering).
  std::vector<std::map<GlobalIndex, BoundaryElement>> zone_boundary_elements(num_zones);

  for (std::size_t z = 0; z < num_zones; ++z) {
    const RawZone &zone = raw.zones[z];
    const Index off = zone_node_offset[z];
    for (const RawSection &section : zone.sections) {
      const ElementTopology topo = topologyForShape(section.shape);
      if (topo.dimension == 2) {
        for (Index e = 0; e < section.num_elements; ++e) {
          GlobalCell cell;
          cell.shape = section.shape;
          cell.num_nodes = topo.num_nodes;
          cell.zone = static_cast<int>(z);
          for (int k = 0; k < topo.num_nodes; ++k) {
            const Index zone_node =
                section.connectivity[static_cast<std::size_t>(e) * topo.num_nodes + k];
            cell.nodes[static_cast<std::size_t>(k)] =
                merged_id[static_cast<std::size_t>(off + zone_node)];
          }
          mesh.cells.push_back(cell);
        }
      } else if (topo.dimension == 1) {
        for (Index e = 0; e < section.num_elements; ++e) {
          BoundaryElement be;
          be.n0 = merged_id[static_cast<std::size_t>(
              off + section.connectivity[static_cast<std::size_t>(e) * 2 + 0])];
          be.n1 = merged_id[static_cast<std::size_t>(
              off + section.connectivity[static_cast<std::size_t>(e) * 2 + 1])];
          zone_boundary_elements[z].emplace(section.first_element + e, be);
        }
      }
    }
  }
  if (mesh.cells.empty()) {
    throw CnsError("mesh '" + raw.file_path + "' contains no 2-D cells");
  }

  // ---- 4. normalise cell node ordering to counter-clockwise -------------
  Index flipped = 0;
  for (GlobalCell &cell : mesh.cells) {
    if (signedArea(mesh, cell) < 0.0) {
      std::reverse(cell.nodes.begin(), cell.nodes.begin() + cell.num_nodes);
      ++flipped;
    }
  }
  if (flipped > 0) {
    logInfo("mesh: reoriented " + std::to_string(flipped) +
            " cell(s) to counter-clockwise node ordering");
  }
  for (const GlobalCell &cell : mesh.cells) {
    if (!(signedArea(mesh, cell) > 0.0)) {
      throw CnsError("mesh contains a degenerate or zero-area cell");
    }
  }

  // ---- 5. build unique faces -------------------------------------------
  std::unordered_map<EdgeKey, Index, EdgeKeyHash> face_of_edge;
  face_of_edge.reserve(static_cast<std::size_t>(mesh.numCells()) * 3);
  for (Index c = 0; c < mesh.numCells(); ++c) {
    const GlobalCell &cell = mesh.cells[static_cast<std::size_t>(c)];
    std::array<std::array<Index, 2>, 4> edges{};
    const int ne = cellEdges(cell, edges);
    for (int e = 0; e < ne; ++e) {
      const Index a = edges[static_cast<std::size_t>(e)][0];
      const Index b = edges[static_cast<std::size_t>(e)][1];
      const EdgeKey key = makeEdgeKey(a, b);
      auto it = face_of_edge.find(key);
      if (it == face_of_edge.end()) {
        GlobalFace face;
        // Store the face with the orientation seen from its left cell so the
        // outward normal of the left cell follows from the node order.
        face.nodes = {a, b};
        face.left_cell = c;
        face.right_cell = -1;
        const Index face_id = mesh.numFaces();
        mesh.faces.push_back(face);
        face_of_edge.emplace(key, face_id);
      } else {
        GlobalFace &face = mesh.faces[static_cast<std::size_t>(it->second)];
        if (face.right_cell >= 0) {
          throw CnsError("mesh is non-manifold: an edge is shared by more than two cells");
        }
        face.right_cell = c;
      }
    }
  }

  // ---- 6. tag boundary faces with mesh family names --------------------
  std::unordered_map<std::string, int> tag_of_name;
  auto tagFor = [&](const std::string &name) {
    auto it = tag_of_name.find(name);
    if (it != tag_of_name.end()) return it->second;
    const int tag = static_cast<int>(mesh.boundary_names.size());
    mesh.boundary_names.push_back(name);
    tag_of_name.emplace(name, tag);
    return tag;
  };

  Index tagged = 0;
  for (std::size_t z = 0; z < num_zones; ++z) {
    const RawZone &zone = raw.zones[z];
    for (const RawBoco &boco : zone.bocos) {
      const std::string family = boco.family_name.empty() ? boco.name : boco.family_name;
      const int tag = tagFor(family);

      // Collect the CGNS element ids addressed by this BC patch.
      std::vector<GlobalIndex> element_ids;
      if (boco.has_element_range) {
        for (GlobalIndex e = boco.first_element; e <= boco.last_element; ++e) {
          element_ids.push_back(e);
        }
      } else {
        element_ids = boco.point_list;
      }

      for (const GlobalIndex eid : element_ids) {
        auto be_it = zone_boundary_elements[z].find(eid);
        if (be_it == zone_boundary_elements[z].end()) {
          throw CnsError("boundary condition '" + boco.name + "' of zone '" + zone.name +
                         "' references element " + std::to_string(eid) +
                         " which is not a 1-D boundary element in this zone");
        }
        const EdgeKey key = makeEdgeKey(be_it->second.n0, be_it->second.n1);
        auto face_it = face_of_edge.find(key);
        if (face_it == face_of_edge.end()) {
          throw CnsError("boundary condition '" + boco.name + "' of zone '" + zone.name +
                         "' references an edge that is not a face of any cell");
        }
        GlobalFace &face = mesh.faces[static_cast<std::size_t>(face_it->second)];
        if (face.right_cell >= 0) {
          throw CnsError("boundary condition '" + boco.name + "' of zone '" + zone.name +
                         "' is applied to an interior face (shared by two cells)");
        }
        if (face.boundary_tag >= 0 && face.boundary_tag != tag) {
          throw CnsError("face is claimed by two different boundary families ('" +
                         mesh.boundary_names[static_cast<std::size_t>(face.boundary_tag)] +
                         "' and '" + family + "')");
        }
        if (face.boundary_tag < 0) ++tagged;
        face.boundary_tag = tag;
      }
    }
  }

  // ---- 7. watertightness check -----------------------------------------
  Index untagged_boundary = 0;
  for (const GlobalFace &face : mesh.faces) {
    if (face.right_cell < 0) {
      ++mesh.num_boundary_faces;
      if (face.boundary_tag < 0) ++untagged_boundary;
    } else {
      ++mesh.num_interior_faces;
    }
  }
  if (untagged_boundary > 0) {
    throw CnsError("mesh has " + std::to_string(untagged_boundary) +
                   " boundary face(s) not covered by any CGNS boundary condition; the mesh is "
                   "either not watertight or a boundary patch is missing");
  }
  (void)tagged;

  return mesh;
}

std::vector<BCType> mapBoundaryConditions(const GlobalMesh &mesh,
                                          const std::map<std::string, BCType> &case_map) {
  std::vector<BCType> result;
  result.reserve(mesh.boundary_names.size());
  for (const std::string &name : mesh.boundary_names) {
    auto it = case_map.find(name);
    if (it == case_map.end()) {
      std::ostringstream os;
      os << "mesh boundary family '" << name
         << "' has no boundary_conditions entry in the case file; mapped families are:";
      for (const auto &kv : case_map) os << " '" << kv.first << "'";
      throw CnsError(os.str());
    }
    result.push_back(it->second);
  }
  // Warn about case entries that never matched a mesh family: this usually
  // means a typo in the case file rather than a harmless extra.
  for (const auto &kv : case_map) {
    const bool found = std::find(mesh.boundary_names.begin(), mesh.boundary_names.end(), kv.first) !=
                       mesh.boundary_names.end();
    if (!found) {
      throw CnsError("case file maps boundary family '" + kv.first +
                     "' which does not exist in the mesh");
    }
  }
  return result;
}

std::string describeGlobalMesh(const GlobalMesh &mesh) {
  std::ostringstream os;
  Index tri = 0;
  Index quad = 0;
  for (const GlobalCell &c : mesh.cells) {
    if (c.shape == ElementShape::kTri3) ++tri;
    if (c.shape == ElementShape::kQuad4) ++quad;
  }
  os << "global mesh: " << mesh.numNodes() << " nodes (" << mesh.num_merged_node_pairs
     << " zone-interface node pairs fused, max merge distance " << mesh.max_merge_distance << "), "
     << mesh.numCells() << " cells (" << tri << " tri, " << quad << " quad), " << mesh.numFaces()
     << " faces (" << mesh.num_interior_faces << " interior, " << mesh.num_boundary_faces
     << " boundary)";
  os << "\n  boundary families:";
  std::vector<Index> count(mesh.boundary_names.size(), 0);
  for (const GlobalFace &f : mesh.faces) {
    if (f.boundary_tag >= 0) ++count[static_cast<std::size_t>(f.boundary_tag)];
  }
  for (std::size_t t = 0; t < mesh.boundary_names.size(); ++t) {
    os << " '" << mesh.boundary_names[t] << "' (" << count[t] << " faces)";
  }
  return os.str();
}

}  // namespace cns2d
