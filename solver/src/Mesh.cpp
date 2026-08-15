#include "cfd/Mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace cfd {
namespace {

void cgns_check(int code, const std::string& context) {
  if (code != CG_OK) {
    throw std::runtime_error(context + ": " + cg_get_error());
  }
}

unsigned long long edge_key(int a, int b) {
  const auto lo = static_cast<std::uint32_t>(std::min(a, b));
  const auto hi = static_cast<std::uint32_t>(std::max(a, b));
  return (static_cast<unsigned long long>(lo) << 32U) | hi;
}

struct CoordinateKey {
  std::int64_t x;
  std::int64_t y;
  bool operator<(const CoordinateKey& other) const {
    return std::tie(x, y) < std::tie(other.x, other.y);
  }
};

CoordinateKey coordinate_key(double x, double y, double tolerance) {
  return {static_cast<std::int64_t>(std::llround(x / tolerance)),
          static_cast<std::int64_t>(std::llround(y / tolerance))};
}

std::vector<int> element_nodes(ElementType_t type, const cgsize_t*& cursor) {
  int count = 0;
  switch (type) {
    case TRI_3:
      count = 3;
      break;
    case QUAD_4:
      count = 4;
      break;
    case BAR_2:
      count = 2;
      break;
    default:
      throw std::runtime_error("unsupported CGNS element type " +
                               std::to_string(static_cast<int>(type)));
  }
  std::vector<int> result;
  result.reserve(static_cast<std::size_t>(count));
  for (int i = 0; i < count; ++i) result.push_back(static_cast<int>(*cursor++));
  return result;
}

double polygon_signed_area(const std::vector<int>& nodes, const std::vector<Vec2>& points) {
  double twice_area = 0.0;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const Vec2& a = points.at(static_cast<std::size_t>(nodes[i]));
    const Vec2& b = points.at(static_cast<std::size_t>(nodes[(i + 1) % nodes.size()]));
    twice_area += a.x * b.y - b.x * a.y;
  }
  return 0.5 * twice_area;
}

Vec2 polygon_centroid(const std::vector<int>& nodes, const std::vector<Vec2>& points,
                      double signed_area) {
  double cx = 0.0;
  double cy = 0.0;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    const Vec2& a = points.at(static_cast<std::size_t>(nodes[i]));
    const Vec2& b = points.at(static_cast<std::size_t>(nodes[(i + 1) % nodes.size()]));
    const double cross = a.x * b.y - b.x * a.y;
    cx += (a.x + b.x) * cross;
    cy += (a.y + b.y) * cross;
  }
  const double scale = 1.0 / (6.0 * signed_area);
  return {cx * scale, cy * scale};
}

}  // namespace

GlobalMesh GlobalMesh::read_cgns(const std::string& path) {
  int file = 0;
  cgns_check(cg_open(path.c_str(), CG_MODE_READ, &file), "cannot open CGNS mesh " + path);
  struct FileCloser {
    int file;
    ~FileCloser() { cg_close(file); }
  } closer{file};

  int bases = 0;
  cgns_check(cg_nbases(file, &bases), "cannot read CGNS bases");
  if (bases < 1) throw std::runtime_error("CGNS mesh has no bases");

  GlobalMesh mesh;
  std::unordered_map<unsigned long long, std::string> boundary_edges;
  std::map<CoordinateKey, std::vector<int>> coordinate_bins;
  constexpr double merge_tolerance = 1.0e-11;

  for (int base = 1; base <= bases; ++base) {
    char base_name[33]{};
    int cell_dim = 0;
    int physical_dim = 0;
    cgns_check(cg_base_read(file, base, base_name, &cell_dim, &physical_dim),
               "cannot read CGNS base");
    if (cell_dim != 2 || physical_dim < 2) {
      throw std::runtime_error("only two-dimensional CGNS bases are supported");
    }

    int zones = 0;
    cgns_check(cg_nzones(file, base, &zones), "cannot read CGNS zone count");
    for (int zone = 1; zone <= zones; ++zone) {
      char zone_name[33]{};
      cgsize_t size[9]{};
      cgns_check(cg_zone_read(file, base, zone, zone_name, size), "cannot read CGNS zone");
      ZoneType_t zone_type = ZoneTypeNull;
      cgns_check(cg_zone_type(file, base, zone, &zone_type), "cannot read CGNS zone type");
      if (zone_type != Unstructured) {
        throw std::runtime_error(std::string("zone ") + zone_name + " is not unstructured");
      }

      const auto node_count = static_cast<std::size_t>(size[0]);
      std::vector<double> x(node_count), y(node_count);
      const cgsize_t range_min = 1;
      const cgsize_t range_max = size[0];
      cgns_check(cg_coord_read(file, base, zone, "CoordinateX", RealDouble, &range_min,
                               &range_max, x.data()),
                 "cannot read CoordinateX");
      cgns_check(cg_coord_read(file, base, zone, "CoordinateY", RealDouble, &range_min,
                               &range_max, y.data()),
                 "cannot read CoordinateY");

      std::vector<int> local_to_global(node_count + 1, -1);
      for (std::size_t local = 0; local < node_count; ++local) {
        const CoordinateKey key = coordinate_key(x[local], y[local], merge_tolerance);
        int global = -1;
        auto& candidates = coordinate_bins[key];
        for (const int candidate : candidates) {
          const Vec2& point = mesh.nodes[static_cast<std::size_t>(candidate)];
          if (std::abs(point.x - x[local]) <= merge_tolerance &&
              std::abs(point.y - y[local]) <= merge_tolerance) {
            global = candidate;
            break;
          }
        }
        if (global < 0) {
          global = static_cast<int>(mesh.nodes.size());
          mesh.nodes.push_back({x[local], y[local]});
          candidates.push_back(global);
        }
        local_to_global[local + 1] = global;
      }

      int sections = 0;
      cgns_check(cg_nsections(file, base, zone, &sections), "cannot read CGNS sections");
      for (int section = 1; section <= sections; ++section) {
        char section_name[33]{};
        ElementType_t section_type = ElementTypeNull;
        cgsize_t start = 0;
        cgsize_t end = 0;
        int boundary_count = 0;
        int parent_flag = 0;
        cgns_check(cg_section_read(file, base, zone, section, section_name, &section_type,
                                   &start, &end, &boundary_count, &parent_flag),
                   "cannot read CGNS section");
        cgsize_t data_size = 0;
        cgns_check(cg_ElementDataSize(file, base, zone, section, &data_size),
                   "cannot size CGNS connectivity");
        std::vector<cgsize_t> connectivity(static_cast<std::size_t>(data_size));
        cgns_check(cg_elements_read(file, base, zone, section, connectivity.data(), nullptr),
                   "cannot read CGNS connectivity");
        const cgsize_t* cursor = connectivity.data();
        const auto element_count = static_cast<std::size_t>(end - start + 1);
        for (std::size_t element = 0; element < element_count; ++element) {
          ElementType_t type = section_type;
          if (section_type == MIXED) type = static_cast<ElementType_t>(*cursor++);
          if (type != TRI_3 && type != QUAD_4 && type != BAR_2) {
            int nodes_per_element = 0;
            cgns_check(cg_npe(type, &nodes_per_element), "cannot determine element node count");
            cursor += nodes_per_element;
            continue;
          }
          std::vector<int> local_nodes = element_nodes(type, cursor);
          for (int& node : local_nodes) {
            if (node <= 0 || static_cast<std::size_t>(node) >= local_to_global.size()) {
              throw std::runtime_error("CGNS element references an invalid node");
            }
            node = local_to_global[static_cast<std::size_t>(node)];
          }
          if (type == BAR_2) {
            const std::string tag(section_name);
            if (tag.rfind("con-", 0) != 0) {
              boundary_edges[edge_key(local_nodes[0], local_nodes[1])] = tag;
            }
          } else {
            mesh.cells.push_back({std::move(local_nodes), {}, 0.0, {}});
          }
        }
      }
    }
  }

  if (mesh.cells.empty()) throw std::runtime_error("CGNS mesh contains no supported volume cells");
  mesh.build_topology_and_geometry(boundary_edges);
  return mesh;
}

void GlobalMesh::build_topology_and_geometry(
    const std::unordered_map<unsigned long long, std::string>& boundary_edges) {
  faces.clear();
  std::unordered_map<unsigned long long, int> face_by_edge;

  for (std::size_t cell_index = 0; cell_index < cells.size(); ++cell_index) {
    Cell& cell = cells[cell_index];
    if (cell.nodes.size() < 3) throw std::runtime_error("cell has fewer than three nodes");
    double signed_area = polygon_signed_area(cell.nodes, nodes);
    if (std::abs(signed_area) <= std::numeric_limits<double>::epsilon()) {
      throw std::runtime_error("zero-area cell in mesh");
    }
    if (signed_area < 0.0) {
      std::reverse(cell.nodes.begin(), cell.nodes.end());
      signed_area = -signed_area;
    }
    cell.volume = signed_area;
    cell.center = polygon_centroid(cell.nodes, nodes, signed_area);
    cell.faces.clear();

    for (std::size_t local_face = 0; local_face < cell.nodes.size(); ++local_face) {
      const int a = cell.nodes[local_face];
      const int b = cell.nodes[(local_face + 1) % cell.nodes.size()];
      const auto key = edge_key(a, b);
      auto existing = face_by_edge.find(key);
      if (existing == face_by_edge.end()) {
        const Vec2& pa = nodes[static_cast<std::size_t>(a)];
        const Vec2& pb = nodes[static_cast<std::size_t>(b)];
        const Vec2 edge = pb - pa;
        const double length = std::hypot(edge.x, edge.y);
        if (!(length > 0.0)) throw std::runtime_error("zero-length face in mesh");
        Face face;
        face.nodes = {a, b};
        face.left = static_cast<int>(cell_index);
        face.center = 0.5 * (pa + pb);
        face.area = length;
        face.normal = {edge.y / length, -edge.x / length};
        auto boundary = boundary_edges.find(key);
        if (boundary != boundary_edges.end()) face.boundary_tag = boundary->second;
        const int face_index = static_cast<int>(faces.size());
        faces.push_back(std::move(face));
        face_by_edge.emplace(key, face_index);
        cell.faces.push_back(face_index);
      } else {
        Face& face = faces[static_cast<std::size_t>(existing->second)];
        if (face.right >= 0) throw std::runtime_error("non-manifold face shared by more than two cells");
        face.right = static_cast<int>(cell_index);
        face.boundary_tag.clear();
        cell.faces.push_back(existing->second);
      }
    }
  }

  std::vector<std::set<int>> neighbors(cells.size());
  for (const Face& face : faces) {
    if (face.right >= 0) {
      neighbors[static_cast<std::size_t>(face.left)].insert(face.right);
      neighbors[static_cast<std::size_t>(face.right)].insert(face.left);
    }
  }
  adjacency_offsets.assign(cells.size() + 1, 0);
  adjacency.clear();
  for (std::size_t cell = 0; cell < cells.size(); ++cell) {
    adjacency.insert(adjacency.end(), neighbors[cell].begin(), neighbors[cell].end());
    adjacency_offsets[cell + 1] = static_cast<int>(adjacency.size());
  }
}

void GlobalMesh::validate(
    const std::unordered_map<std::string, std::string>& boundary_conditions) const {
  std::set<std::string> found_tags;
  for (const Face& face : faces) {
    if (face.right < 0) {
      if (face.boundary_tag.empty()) throw std::runtime_error("untagged exterior mesh face");
      found_tags.insert(face.boundary_tag);
    }
  }
  for (const auto& [tag, type] : boundary_conditions) {
    (void)type;
    if (found_tags.count(tag) == 0) {
      throw std::runtime_error("case boundary family not found in mesh: " + tag);
    }
  }
  for (const std::string& tag : found_tags) {
    if (boundary_conditions.count(tag) == 0) {
      throw std::runtime_error("mesh boundary family has no case mapping: " + tag);
    }
  }
}

}  // namespace cfd
