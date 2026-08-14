#include "cfd/mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace cfd {
namespace {

void cgns_call(int status, const std::string& operation) {
  if (status != CG_OK) {
    throw std::runtime_error("CGNS " + operation + " failed: " + cg_get_error());
  }
}

class CgnsFile {
 public:
  explicit CgnsFile(const std::filesystem::path& path) {
    cgns_call(cg_open(path.string().c_str(), CG_MODE_READ, &handle_),
              "open of '" + path.string() + "'");
  }
  ~CgnsFile() {
    if (handle_ >= 0) cg_close(handle_);
  }
  CgnsFile(const CgnsFile&) = delete;
  CgnsFile& operator=(const CgnsFile&) = delete;
  int get() const noexcept { return handle_; }

 private:
  int handle_{-1};
};

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t size) : parent_(size), rank_(size, 0U) {
    std::iota(parent_.begin(), parent_.end(), std::size_t{0});
  }
  std::size_t find(std::size_t value) {
    if (parent_[value] != value) parent_[value] = find(parent_[value]);
    return parent_[value];
  }
  void unite(std::size_t lhs, std::size_t rhs) {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs == rhs) return;
    if (rank_[lhs] < rank_[rhs]) std::swap(lhs, rhs);
    parent_[rhs] = lhs;
    if (rank_[lhs] == rank_[rhs]) ++rank_[lhs];
  }

 private:
  std::vector<std::size_t> parent_;
  std::vector<unsigned char> rank_;
};

struct RawCell {
  std::vector<std::size_t> vertices;
  int zone{};
};

struct RawBar {
  cgsize_t element{};
  std::array<std::size_t, 2> vertices{};
};

struct RawBoundary {
  std::string bc_name;
  std::string family;
  bool family_specified{};
  bool family_name_node_present{};
  std::vector<cgsize_t> element_ids;
};

struct RawZone {
  int base{};
  int zone{};
  int global_zone{};
  std::string name;
  std::size_t vertex_offset{};
  std::size_t vertex_count{};
  std::vector<RawBar> bars;
  std::vector<RawBoundary> boundaries;
};

using Edge = std::array<GlobalId, 2>;

Edge edge_key(GlobalId lhs, GlobalId rhs) {
  if (rhs < lhs) std::swap(lhs, rhs);
  return Edge{{lhs, rhs}};
}

std::string node_name(const char* data) { return std::string(data); }

std::pair<Vec2, double> polygon_geometry(const std::vector<GlobalId>& vertices,
                                         const std::vector<Vertex>& points) {
  double cross_sum = 0.0;
  Vec2 weighted{};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Vec2& p = points.at(static_cast<std::size_t>(vertices[i])).position;
    const Vec2& q = points.at(static_cast<std::size_t>(vertices[(i + 1U) % vertices.size()])).position;
    const double weight = cross(p, q);
    cross_sum += weight;
    weighted += (p + q) * weight;
  }
  if (!(cross_sum > 0.0) || !std::isfinite(cross_sum)) {
    throw std::runtime_error("cell has non-positive or invalid signed area");
  }
  const Vec2 center = weighted / (3.0 * cross_sum);
  return {center, 0.5 * cross_sum};
}

struct FamilyReference {
  std::string name;
  bool node_present{};
};

FamilyReference read_bc_family(int file, int base, int zone, int bc,
                               const std::string& fallback,
                               bool family_specified) {
  cgns_call(cg_goto(file, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", bc, "end"),
            "navigation to BC_t '" + fallback + "'");
  std::array<char, 128> family{};
  const int status = cg_famname_read(family.data());
  if (status == CG_NODE_NOT_FOUND) {
    return FamilyReference{
        resolve_cgns_boundary_family(fallback, std::nullopt, family_specified), false};
  }
  cgns_call(status, "FamilyName_t read for BC_t '" + fallback + "'");
  return FamilyReference{resolve_cgns_boundary_family(
                             fallback, std::string_view(family.data()), family_specified),
                         true};
}

}  // namespace

bool is_nearly_planar_for_xy_projection(double xy_extent, double z_span) noexcept {
  return std::isfinite(xy_extent) && std::isfinite(z_span) && xy_extent > 0.0 &&
         z_span >= 0.0 &&
         z_span <= cgns_projection_planarity_relative_tolerance * xy_extent;
}

std::string resolve_cgns_boundary_family(
    std::string_view bc_name,
    std::optional<std::string_view> family_name,
    bool family_specified) {
  if (family_name.has_value()) {
    if (family_name->empty()) {
      throw std::runtime_error("CGNS FamilyName_t for BC_t '" + std::string(bc_name) +
                               "' is empty");
    }
    return std::string(*family_name);
  }
  if (family_specified) {
    throw std::runtime_error("CGNS BC_t '" + std::string(bc_name) +
                             "' has BCType FamilySpecified but no FamilyName_t");
  }
  // For a concrete BCType, the BC_t node name remains a stable mesh-provided
  // identifier when no optional FamilyName_t is present.
  return std::string(bc_name);
}

Mesh read_cgns_mesh(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::runtime_error("CGNS mesh does not exist: " + path.string());
  }
  CgnsFile file(path);
  const int fn = file.get();

  int number_of_bases = 0;
  cgns_call(cg_nbases(fn, &number_of_bases), "base count");
  if (number_of_bases < 1) throw std::runtime_error("CGNS mesh has no bases");

  std::vector<Vec2> raw_points;
  std::vector<RawCell> raw_cells;
  std::vector<RawZone> zones;
  std::vector<std::string> family_names;

  for (int base = 1; base <= number_of_bases; ++base) {
    std::array<char, 128> base_name{};
    int cell_dimension = 0;
    int physical_dimension = 0;
    cgns_call(cg_base_read(fn, base, base_name.data(), &cell_dimension, &physical_dimension),
              "base read");
    if (cell_dimension != 2 || (physical_dimension != 2 && physical_dimension != 3)) {
      throw std::runtime_error("CGNS base '" + node_name(base_name.data()) +
                               "' is not a two-dimensional mesh");
    }

    double base_min_x = std::numeric_limits<double>::infinity();
    double base_max_x = -std::numeric_limits<double>::infinity();
    double base_min_y = std::numeric_limits<double>::infinity();
    double base_max_y = -std::numeric_limits<double>::infinity();
    double base_min_z = std::numeric_limits<double>::infinity();
    double base_max_z = -std::numeric_limits<double>::infinity();

    int number_of_families = 0;
    cgns_call(cg_nfamilies(fn, base, &number_of_families), "family count");
    for (int family = 1; family <= number_of_families; ++family) {
      std::array<char, 128> name{};
      int number_of_bcs = 0;
      int number_of_geometries = 0;
      cgns_call(cg_family_read(fn, base, family, name.data(), &number_of_bcs,
                               &number_of_geometries),
                "family read");
      family_names.emplace_back(name.data());
    }

    int number_of_zones = 0;
    cgns_call(cg_nzones(fn, base, &number_of_zones), "zone count");
    for (int zone = 1; zone <= number_of_zones; ++zone) {
      std::array<char, 128> zone_name{};
      std::array<cgsize_t, 9> size{};
      cgns_call(cg_zone_read(fn, base, zone, zone_name.data(), size.data()), "zone read");
      CGNS_ENUMT(ZoneType_t) zone_type = CGNS_ENUMV(ZoneTypeNull);
      cgns_call(cg_zone_type(fn, base, zone, &zone_type), "zone type read");
      if (zone_type != CGNS_ENUMV(Unstructured)) {
        throw std::runtime_error("zone '" + node_name(zone_name.data()) +
                                 "' is not Unstructured");
      }
      if (size[0] <= 0) throw std::runtime_error("zone has no vertices");

      RawZone raw_zone;
      raw_zone.base = base;
      raw_zone.zone = zone;
      raw_zone.global_zone = static_cast<int>(zones.size());
      raw_zone.name = node_name(zone_name.data());
      raw_zone.vertex_offset = raw_points.size();
      raw_zone.vertex_count = static_cast<std::size_t>(size[0]);

      std::vector<double> x(raw_zone.vertex_count);
      std::vector<double> y(raw_zone.vertex_count);
      std::vector<double> z;
      const cgsize_t first = 1;
      const cgsize_t last = size[0];
      cgns_call(cg_coord_read(fn, base, zone, "CoordinateX", CGNS_ENUMV(RealDouble), &first,
                              &last, x.data()),
                "CoordinateX read in zone '" + raw_zone.name + "'");
      cgns_call(cg_coord_read(fn, base, zone, "CoordinateY", CGNS_ENUMV(RealDouble), &first,
                              &last, y.data()),
                "CoordinateY read in zone '" + raw_zone.name + "'");
      if (physical_dimension == 3) {
        z.resize(raw_zone.vertex_count);
        cgns_call(cg_coord_read(fn, base, zone, "CoordinateZ", CGNS_ENUMV(RealDouble), &first,
                                &last, z.data()),
                  "CoordinateZ read in zone '" + raw_zone.name + "'");
      }
      for (std::size_t i = 0; i < raw_zone.vertex_count; ++i) {
        const Vec2 point{x[i], y[i]};
        if (!finite(point)) throw std::runtime_error("zone contains a non-finite coordinate");
        base_min_x = std::min(base_min_x, x[i]);
        base_max_x = std::max(base_max_x, x[i]);
        base_min_y = std::min(base_min_y, y[i]);
        base_max_y = std::max(base_max_y, y[i]);
        if (physical_dimension == 3) {
          if (!std::isfinite(z[i])) {
            throw std::runtime_error("zone contains a non-finite CoordinateZ value");
          }
          base_min_z = std::min(base_min_z, z[i]);
          base_max_z = std::max(base_max_z, z[i]);
        }
        raw_points.push_back(point);
      }

      int number_of_sections = 0;
      cgns_call(cg_nsections(fn, base, zone, &number_of_sections), "section count");
      for (int section = 1; section <= number_of_sections; ++section) {
        std::array<char, 128> section_name{};
        CGNS_ENUMT(ElementType_t) type = CGNS_ENUMV(ElementTypeNull);
        cgsize_t start = 0;
        cgsize_t end = 0;
        int boundary_count = 0;
        int has_parent_data = 0;
        cgns_call(cg_section_read(fn, base, zone, section, section_name.data(), &type, &start,
                                  &end, &boundary_count, &has_parent_data),
                  "section read");
        if (type != CGNS_ENUMV(TRI_3) && type != CGNS_ENUMV(QUAD_4) &&
            type != CGNS_ENUMV(BAR_2)) {
          throw std::runtime_error("unsupported CGNS element section type '" +
                                   std::string(cg_ElementTypeName(type)) + "' in section '" +
                                   node_name(section_name.data()) + "'");
        }
        int nodes_per_element = 0;
        cgns_call(cg_npe(type, &nodes_per_element), "nodes-per-element query");
        cgsize_t data_size = 0;
        cgns_call(cg_ElementDataSize(fn, base, zone, section, &data_size),
                  "element data-size query");
        const auto count = static_cast<std::size_t>(end - start + 1);
        std::vector<cgsize_t> connectivity(static_cast<std::size_t>(data_size));
        std::vector<cgsize_t> parent_data;
        if (has_parent_data != 0) parent_data.resize(count * 4U);
        cgns_call(cg_elements_read(fn, base, zone, section, connectivity.data(),
                                   parent_data.empty() ? nullptr : parent_data.data()),
                  "element connectivity read");
        if (connectivity.size() != count * static_cast<std::size_t>(nodes_per_element)) {
          throw std::runtime_error("unexpected fixed-width connectivity size in section '" +
                                   node_name(section_name.data()) + "'");
        }
        for (std::size_t element = 0; element < count; ++element) {
          std::vector<std::size_t> vertices;
          vertices.reserve(static_cast<std::size_t>(nodes_per_element));
          for (int node = 0; node < nodes_per_element; ++node) {
            const cgsize_t local =
                connectivity[element * static_cast<std::size_t>(nodes_per_element) +
                             static_cast<std::size_t>(node)];
            if (local < 1 || local > size[0]) {
              throw std::runtime_error("element connectivity references an invalid vertex");
            }
            vertices.push_back(raw_zone.vertex_offset + static_cast<std::size_t>(local - 1));
          }
          if (type == CGNS_ENUMV(BAR_2)) {
            raw_zone.bars.push_back(
                RawBar{start + static_cast<cgsize_t>(element), {{vertices[0], vertices[1]}}});
          } else {
            raw_cells.push_back(RawCell{std::move(vertices), raw_zone.global_zone});
          }
        }
      }

      int number_of_bcs = 0;
      cgns_call(cg_nbocos(fn, base, zone, &number_of_bcs), "boundary-condition count");
      for (int bc = 1; bc <= number_of_bcs; ++bc) {
        std::array<char, 128> bc_name{};
        CGNS_ENUMT(BCType_t) bc_type = CGNS_ENUMV(BCTypeNull);
        CGNS_ENUMT(PointSetType_t) point_set = CGNS_ENUMV(PointSetTypeNull);
        cgsize_t number_of_points = 0;
        std::array<int, 3> normal_index{};
        cgsize_t normal_list_size = 0;
        CGNS_ENUMT(DataType_t) normal_type = CGNS_ENUMV(DataTypeNull);
        int number_of_datasets = 0;
        cgns_call(cg_boco_info(fn, base, zone, bc, bc_name.data(), &bc_type, &point_set,
                               &number_of_points, normal_index.data(), &normal_list_size,
                               &normal_type, &number_of_datasets),
                  "boundary-condition info read");
        CGNS_ENUMT(GridLocation_t) location = ::CGNS_ENUMV(Vertex);
        cgns_call(cg_boco_gridlocation_read(fn, base, zone, bc, &location),
                  "boundary grid-location read");
        if (location != CGNS_ENUMV(EdgeCenter)) {
          throw std::runtime_error("boundary condition '" + node_name(bc_name.data()) +
                                   "' is not located on edges");
        }
        const std::size_t storage = point_set == CGNS_ENUMV(PointRange)
                                        ? std::size_t{2}
                                        : static_cast<std::size_t>(number_of_points);
        std::vector<cgsize_t> points(storage);
        cgns_call(cg_boco_read(fn, base, zone, bc, points.data(), nullptr),
                  "boundary point-set read");
        RawBoundary boundary;
        boundary.bc_name = node_name(bc_name.data());
        boundary.family_specified = bc_type == CGNS_ENUMV(FamilySpecified);
        const FamilyReference family =
            read_bc_family(fn, base, zone, bc, boundary.bc_name,
                           boundary.family_specified);
        boundary.family = family.name;
        boundary.family_name_node_present = family.node_present;
        if (point_set == CGNS_ENUMV(PointRange)) {
          const cgsize_t step = points[0] <= points[1] ? 1 : -1;
          for (cgsize_t value = points[0];; value += step) {
            boundary.element_ids.push_back(value);
            if (value == points[1]) break;
          }
        } else if (point_set == CGNS_ENUMV(PointList)) {
          boundary.element_ids = std::move(points);
        } else {
          throw std::runtime_error("boundary condition uses unsupported point-set type");
        }
        raw_zone.boundaries.push_back(std::move(boundary));
      }
      zones.push_back(std::move(raw_zone));
    }
    if (physical_dimension == 3) {
      const double xy_extent = std::hypot(base_max_x - base_min_x, base_max_y - base_min_y);
      const double z_span = base_max_z - base_min_z;
      if (!is_nearly_planar_for_xy_projection(xy_extent, z_span)) {
        std::ostringstream message;
        message << "CGNS base '" << base_name.data() << "' is materially nonplanar: Z span "
                << z_span << " exceeds "
                << cgns_projection_planarity_relative_tolerance
                << " times XY diagonal extent " << xy_extent;
        throw std::runtime_error(message.str());
      }
    }
  }

  DisjointSet vertex_sets(raw_points.size());
  for (const RawZone& zone : zones) {
    int number_of_connectivity_nodes = 0;
    cgns_call(cg_nzconns(fn, zone.base, zone.zone, &number_of_connectivity_nodes),
              "zone-connectivity node count");
    for (int connectivity_node = 1; connectivity_node <= number_of_connectivity_nodes;
         ++connectivity_node) {
      cgns_call(cg_zconn_set(fn, zone.base, zone.zone, connectivity_node),
                "zone-connectivity selection");
      int number_of_connections = 0;
      cgns_call(cg_nconns(fn, zone.base, zone.zone, &number_of_connections),
                "general-connectivity count");
      for (int connection = 1; connection <= number_of_connections; ++connection) {
        std::array<char, 128> connection_name{};
        std::array<char, 128> donor_name{};
        CGNS_ENUMT(GridLocation_t) location = CGNS_ENUMV(GridLocationNull);
        CGNS_ENUMT(GridConnectivityType_t) connection_type =
            CGNS_ENUMV(GridConnectivityTypeNull);
        CGNS_ENUMT(PointSetType_t) point_set = CGNS_ENUMV(PointSetTypeNull);
        cgsize_t number_of_points = 0;
        CGNS_ENUMT(ZoneType_t) donor_zone_type = CGNS_ENUMV(ZoneTypeNull);
        CGNS_ENUMT(PointSetType_t) donor_point_set = CGNS_ENUMV(PointSetTypeNull);
        CGNS_ENUMT(DataType_t) donor_data_type = CGNS_ENUMV(DataTypeNull);
        cgsize_t donor_data_count = 0;
        cgns_call(cg_conn_info(fn, zone.base, zone.zone, connection, connection_name.data(),
                               &location, &connection_type, &point_set, &number_of_points,
                               donor_name.data(), &donor_zone_type, &donor_point_set,
                               &donor_data_type, &donor_data_count),
                  "general-connectivity info read");
        if (connection_type != CGNS_ENUMV(Abutting1to1)) continue;
        if (location != ::CGNS_ENUMV(Vertex) || point_set != CGNS_ENUMV(PointList) ||
            donor_point_set != CGNS_ENUMV(PointListDonor) ||
            donor_zone_type != CGNS_ENUMV(Unstructured) || donor_data_count != number_of_points) {
          throw std::runtime_error("Abutting1to1 connection '" +
                                   node_name(connection_name.data()) +
                                   "' is not an unstructured vertex PointList pairing");
        }
        const auto donor_it = std::find_if(zones.begin(), zones.end(), [&](const RawZone& item) {
          return item.base == zone.base && item.name == node_name(donor_name.data());
        });
        if (donor_it == zones.end()) {
          throw std::runtime_error("connection donor zone '" + node_name(donor_name.data()) +
                                   "' was not found in its base");
        }
        std::vector<cgsize_t> points(static_cast<std::size_t>(number_of_points));
        std::vector<cgsize_t> donor_points(static_cast<std::size_t>(number_of_points));
        cgns_call(cg_conn_read(fn, zone.base, zone.zone, connection, points.data(),
                               donor_data_type, donor_points.data()),
                  "general-connectivity point-list read");
        for (std::size_t i = 0; i < points.size(); ++i) {
          if (points[i] < 1 || static_cast<std::size_t>(points[i]) > zone.vertex_count ||
              donor_points[i] < 1 ||
              static_cast<std::size_t>(donor_points[i]) > donor_it->vertex_count) {
            throw std::runtime_error("general-connectivity point list contains an invalid vertex");
          }
          vertex_sets.unite(zone.vertex_offset + static_cast<std::size_t>(points[i] - 1),
                            donor_it->vertex_offset +
                                static_cast<std::size_t>(donor_points[i] - 1));
        }
      }
    }
  }

  std::map<std::size_t, GlobalId> root_to_vertex;
  std::vector<Vec2> coordinate_sums;
  std::vector<std::size_t> coordinate_counts;
  std::vector<GlobalId> raw_to_vertex(raw_points.size(), invalid_global_id);
  for (std::size_t raw = 0; raw < raw_points.size(); ++raw) {
    const std::size_t root = vertex_sets.find(raw);
    auto [it, inserted] = root_to_vertex.emplace(root, static_cast<GlobalId>(root_to_vertex.size()));
    if (inserted) {
      coordinate_sums.push_back(Vec2{});
      coordinate_counts.push_back(0U);
    }
    const auto id = it->second;
    raw_to_vertex[raw] = id;
    coordinate_sums[static_cast<std::size_t>(id)] += raw_points[raw];
    ++coordinate_counts[static_cast<std::size_t>(id)];
  }

  Mesh mesh;
  mesh.vertices.reserve(root_to_vertex.size());
  for (std::size_t i = 0; i < coordinate_sums.size(); ++i) {
    mesh.vertices.push_back(Vertex{static_cast<GlobalId>(i),
                                   coordinate_sums[i] / static_cast<double>(coordinate_counts[i])});
  }
  for (const RawZone& zone : zones) mesh.zone_names.push_back(zone.name);
  std::sort(family_names.begin(), family_names.end());
  family_names.erase(std::unique(family_names.begin(), family_names.end()), family_names.end());
  mesh.family_names = std::move(family_names);

  std::map<Edge, std::string> physical_edges;
  for (const RawZone& zone : zones) {
    std::map<cgsize_t, const RawBar*> bars_by_id;
    for (const RawBar& bar : zone.bars) bars_by_id.emplace(bar.element, &bar);
    for (const RawBoundary& boundary : zone.boundaries) {
      mesh.boundary_metadata.push_back(BoundaryMetadata{
          boundary.bc_name, boundary.family, boundary.family_specified,
          boundary.family_name_node_present});
      for (cgsize_t element : boundary.element_ids) {
        const auto bar_it = bars_by_id.find(element);
        if (bar_it == bars_by_id.end()) {
          throw std::runtime_error("boundary family '" + boundary.family +
                                   "' references an element outside BAR_2 sections");
        }
        const RawBar& bar = *bar_it->second;
        const Edge key = edge_key(raw_to_vertex[bar.vertices[0]], raw_to_vertex[bar.vertices[1]]);
        auto [tag_it, inserted] = physical_edges.emplace(key, boundary.family);
        if (!inserted && tag_it->second != boundary.family) {
          throw std::runtime_error("one physical edge has conflicting CGNS boundary families");
        }
      }
    }
  }

  mesh.cells.reserve(raw_cells.size());
  for (const RawCell& raw : raw_cells) {
    Cell cell;
    cell.id = static_cast<GlobalId>(mesh.cells.size());
    cell.source_zone = raw.zone;
    for (std::size_t vertex : raw.vertices) cell.vertices.push_back(raw_to_vertex[vertex]);
    if (std::set<GlobalId>(cell.vertices.begin(), cell.vertices.end()).size() !=
        cell.vertices.size()) {
      throw std::runtime_error("a cell collapsed after declared interface merging");
    }
    double signed_twice_area = 0.0;
    for (std::size_t i = 0; i < cell.vertices.size(); ++i) {
      const Vec2& p = mesh.vertices[static_cast<std::size_t>(cell.vertices[i])].position;
      const Vec2& q =
          mesh.vertices[static_cast<std::size_t>(cell.vertices[(i + 1U) % cell.vertices.size()])]
              .position;
      signed_twice_area += cross(p, q);
    }
    if (signed_twice_area < 0.0) std::reverse(cell.vertices.begin(), cell.vertices.end());
    const auto geometry = polygon_geometry(cell.vertices, mesh.vertices);
    cell.center = geometry.first;
    cell.area = geometry.second;
    mesh.cells.push_back(std::move(cell));
  }

  std::map<Edge, std::vector<GlobalId>> incidences;
  for (const Cell& cell : mesh.cells) {
    for (std::size_t i = 0; i < cell.vertices.size(); ++i) {
      incidences[edge_key(cell.vertices[i], cell.vertices[(i + 1U) % cell.vertices.size()])]
          .push_back(cell.id);
    }
  }

  mesh.faces.reserve(incidences.size());
  for (const auto& entry : incidences) {
    const Edge& edge = entry.first;
    std::vector<GlobalId> adjacent = entry.second;
    if (adjacent.empty() || adjacent.size() > 2U) {
      throw std::runtime_error("non-manifold mesh edge has an invalid cell incidence count");
    }
    std::sort(adjacent.begin(), adjacent.end());
    Face face;
    face.id = static_cast<GlobalId>(mesh.faces.size());
    face.vertices = edge;
    face.left = adjacent[0];
    if (adjacent.size() == 2U) face.right = adjacent[1];
    const Vec2& p = mesh.vertices[static_cast<std::size_t>(edge[0])].position;
    const Vec2& q = mesh.vertices[static_cast<std::size_t>(edge[1])].position;
    face.center = (p + q) * 0.5;
    face.length = norm(q - p);
    if (!(face.length > 0.0) || !std::isfinite(face.length)) {
      throw std::runtime_error("mesh contains a zero-length or invalid face");
    }
    Vec2 normal{q.y - p.y, p.x - q.x};
    const Vec2 target = face.right != invalid_global_id
                            ? mesh.cells[static_cast<std::size_t>(face.right)].center -
                                  mesh.cells[static_cast<std::size_t>(face.left)].center
                            : face.center -
                                  mesh.cells[static_cast<std::size_t>(face.left)].center;
    if (dot(normal, target) < 0.0) normal *= -1.0;
    if (!(dot(normal, target) > 0.0)) {
      throw std::runtime_error("cannot orient a face normal from its adjacent cell geometry");
    }
    face.normal = normal / face.length;
    const auto physical = physical_edges.find(edge);
    if (face.right == invalid_global_id) {
      if (physical == physical_edges.end()) {
        throw std::runtime_error("open mesh edge has no physical CGNS boundary tag");
      }
      face.boundary = physical->second;
      ++mesh.boundary_face_counts[face.boundary];
    } else if (physical != physical_edges.end()) {
      throw std::runtime_error("a physical CGNS boundary edge has two adjacent cells");
    }
    mesh.faces.push_back(std::move(face));
  }
  for (const auto& tagged : physical_edges) {
    if (incidences.count(tagged.first) == 0U) {
      throw std::runtime_error("a tagged BAR_2 element is not a cell edge");
    }
  }
  for (Face& face : mesh.faces) {
    mesh.cells[static_cast<std::size_t>(face.left)].faces.push_back(face.id);
    if (face.right != invalid_global_id) {
      mesh.cells[static_cast<std::size_t>(face.right)].faces.push_back(face.id);
    }
  }

  validate_mesh(mesh);
  return mesh;
}

void validate_mesh(const Mesh& mesh) {
  if (mesh.vertices.empty() || mesh.cells.empty() || mesh.faces.empty()) {
    throw std::runtime_error("mesh must contain vertices, cells, and faces");
  }
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    if (mesh.vertices[i].id != static_cast<GlobalId>(i) || !finite(mesh.vertices[i].position)) {
      throw std::runtime_error("mesh vertex IDs or coordinates are invalid");
    }
  }
  for (std::size_t i = 0; i < mesh.cells.size(); ++i) {
    const Cell& cell = mesh.cells[i];
    if (cell.id != static_cast<GlobalId>(i) || (cell.vertices.size() != 3U &&
                                                cell.vertices.size() != 4U) ||
        cell.faces.size() != cell.vertices.size() || !(cell.area > 0.0) ||
        !std::isfinite(cell.area) || !finite(cell.center)) {
      throw std::runtime_error("mesh cell topology or geometry is invalid");
    }
  }
  std::size_t boundary_faces = 0;
  for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
    const Face& face = mesh.faces[i];
    if (face.id != static_cast<GlobalId>(i) || face.vertices[0] >= face.vertices[1] ||
        face.vertices[0] < 0 ||
        static_cast<std::size_t>(face.vertices[1]) >= mesh.vertices.size() ||
        face.left < 0 || static_cast<std::size_t>(face.left) >= mesh.cells.size() ||
        !(face.length > 0.0) || !std::isfinite(face.length) || !finite(face.center) ||
        !finite(face.normal) || std::abs(norm(face.normal) - 1.0) > 1.0e-10) {
      throw std::runtime_error("mesh face topology or geometry is invalid");
    }
    const Vec2 edge = mesh.vertices[static_cast<std::size_t>(face.vertices[1])].position -
                      mesh.vertices[static_cast<std::size_t>(face.vertices[0])].position;
    if (std::abs(dot(face.normal, edge)) > 1.0e-12 * face.length) {
      throw std::runtime_error("mesh face normal is not perpendicular to its edge");
    }
    if (face.right == invalid_global_id) {
      ++boundary_faces;
      if (face.boundary.empty()) throw std::runtime_error("boundary face has no tag");
      const Vec2 outward = face.center - mesh.cells[static_cast<std::size_t>(face.left)].center;
      if (!(dot(face.normal, outward) > 0.0)) {
        throw std::runtime_error("boundary face normal is not outward");
      }
    } else {
      if (face.right <= face.left || static_cast<std::size_t>(face.right) >= mesh.cells.size() ||
          !face.boundary.empty()) {
        throw std::runtime_error("interior face incidence or tag is invalid");
      }
      const Vec2 left_to_right = mesh.cells[static_cast<std::size_t>(face.right)].center -
                                 mesh.cells[static_cast<std::size_t>(face.left)].center;
      if (!(dot(face.normal, left_to_right) > 0.0)) {
        throw std::runtime_error("interior face normal is not left-to-right");
      }
    }
  }
  std::size_t counted_boundaries = 0;
  for (const auto& item : mesh.boundary_face_counts) counted_boundaries += item.second;
  if (counted_boundaries != boundary_faces) {
    throw std::runtime_error("boundary diagnostics do not match boundary faces");
  }
}

}  // namespace cfd
