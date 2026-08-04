#include "cfd/mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cfd {
namespace {

constexpr std::size_t cgns_name_buffer_size = 33;  // CGNS names are at most 32 bytes.

void cgns_check(int status, const std::string& operation) {
  if (status != CG_OK) throw std::runtime_error(operation + ": " + cg_get_error());
}

class CgnsFile {
 public:
  explicit CgnsFile(const std::filesystem::path& path) {
    cgns_check(cg_open(path.string().c_str(), CG_MODE_READ, &handle_), "cg_open(" + path.string() + ")");
  }
  ~CgnsFile() {
    if (handle_ >= 0) cg_close(handle_);
  }
  CgnsFile(const CgnsFile&) = delete;
  CgnsFile& operator=(const CgnsFile&) = delete;
  [[nodiscard]] int get() const { return handle_; }

 private:
  int handle_ = -1;
};

class DisjointSet {
 public:
  explicit DisjointSet(std::size_t n) : parent_(n), rank_(n, 0) {
    std::iota(parent_.begin(), parent_.end(), std::size_t{0});
  }
  std::size_t find(std::size_t a) {
    if (parent_[a] != a) parent_[a] = find(parent_[a]);
    return parent_[a];
  }
  void unite(std::size_t a, std::size_t b) {
    a = find(a);
    b = find(b);
    if (a == b) return;
    if (rank_[a] < rank_[b]) std::swap(a, b);
    parent_[b] = a;
    if (rank_[a] == rank_[b]) ++rank_[a];
  }

 private:
  std::vector<std::size_t> parent_;
  std::vector<unsigned char> rank_;
};

struct ZoneData {
  std::string name;
  std::size_t raw_offset = 0;
  std::vector<Vec2> coordinates;
  std::vector<double> z;
  std::unordered_map<std::int64_t, std::pair<std::string, BoundaryType>> boundary_elements;
};

struct BoundaryEdge {
  std::string tag;
  BoundaryType type = BoundaryType::interior;
  bool used = false;
};

std::uint64_t edge_key(std::int64_t a, std::int64_t b) {
  const auto lo = static_cast<std::uint64_t>(std::min(a, b));
  const auto hi = static_cast<std::uint64_t>(std::max(a, b));
  if (hi > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("mesh has too many vertices for edge key");
  return (lo << 32U) | hi;
}

std::pair<Vec2, double> polygon_geometry(const std::vector<std::int64_t>& node_ids,
                                         const std::vector<GlobalNode>& nodes) {
  double twice_signed_area = 0.0;
  Vec2 centroid_numerator{};
  for (std::size_t i = 0; i < node_ids.size(); ++i) {
    const Vec2 a = nodes[static_cast<std::size_t>(node_ids[i])].position;
    const Vec2 b = nodes[static_cast<std::size_t>(node_ids[(i + 1) % node_ids.size()])].position;
    const double cr = cross(a, b);
    twice_signed_area += cr;
    centroid_numerator.x += (a.x + b.x) * cr;
    centroid_numerator.y += (a.y + b.y) * cr;
  }
  if (!std::isfinite(twice_signed_area) || std::abs(twice_signed_area) < 1.0e-24) {
    throw std::runtime_error("degenerate zero-area volume element");
  }
  const Vec2 center = centroid_numerator / (3.0 * twice_signed_area);
  return {center, 0.5 * twice_signed_area};
}

std::vector<int> element_corner_indices(CGNS_ENUMT(ElementType_t) type) {
  switch (type) {
    case CGNS_ENUMV(TRI_3): return {0, 1, 2};
    case CGNS_ENUMV(TRI_6): return {0, 1, 2};
    case CGNS_ENUMV(QUAD_4): return {0, 1, 2, 3};
    case CGNS_ENUMV(QUAD_8): return {0, 1, 2, 3};
    case CGNS_ENUMV(QUAD_9): return {0, 1, 2, 3};
    case CGNS_ENUMV(BAR_2): return {0, 1};
    case CGNS_ENUMV(BAR_3): return {0, 1};
    default: return {};
  }
}

bool is_volume_type(CGNS_ENUMT(ElementType_t) type) {
  return type == CGNS_ENUMV(TRI_3) || type == CGNS_ENUMV(TRI_6) ||
         type == CGNS_ENUMV(QUAD_4) || type == CGNS_ENUMV(QUAD_8) || type == CGNS_ENUMV(QUAD_9);
}

bool is_edge_type(CGNS_ENUMT(ElementType_t) type) {
  return type == CGNS_ENUMV(BAR_2) || type == CGNS_ENUMV(BAR_3);
}

std::string read_bc_family(int fn, int base, int zone, int bc, const std::string& fallback) {
  char family[cgns_name_buffer_size]{};
  const int goto_status = cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", bc, "end");
  if (goto_status == CG_OK && cg_famname_read(family) == CG_OK && family[0] != '\0') return family;
  return fallback;
}

void read_zone_boundary_elements(int fn, int base, int zone_index, ZoneData& zone,
                                 const CaseConfig& config) {
  int count = 0;
  cgns_check(cg_nbocos(fn, base, zone_index, &count), "cg_nbocos");
  for (int bc = 1; bc <= count; ++bc) {
    char name[cgns_name_buffer_size]{};
    CGNS_ENUMT(BCType_t) bc_type{};
    CGNS_ENUMT(PointSetType_t) point_set{};
    cgsize_t point_count = 0;
    int normal_index[3]{};
    cgsize_t normal_list_size = 0;
    CGNS_ENUMT(DataType_t) normal_data_type{};
    int dataset_count = 0;
    cgns_check(cg_boco_info(fn, base, zone_index, bc, name, &bc_type, &point_set, &point_count,
                            normal_index, &normal_list_size, &normal_data_type, &dataset_count),
               "cg_boco_info");
    std::vector<cgsize_t> points(static_cast<std::size_t>(point_count));
    std::vector<double> normals(static_cast<std::size_t>(normal_list_size));
    cgns_check(cg_boco_read(fn, base, zone_index, bc, points.data(),
                            normals.empty() ? nullptr : normals.data()), "cg_boco_read");

    const std::string boco_name(name);
    const std::string family = read_bc_family(fn, base, zone_index, bc, boco_name);
    auto mapping = config.boundary_conditions.find(family);
    if (mapping == config.boundary_conditions.end()) mapping = config.boundary_conditions.find(boco_name);
    if (mapping == config.boundary_conditions.end()) {
      throw std::runtime_error("mesh boundary '" + boco_name + "' (family '" + family +
                               "') has no entry in case boundary_conditions");
    }

    auto add_element = [&](cgsize_t value) {
      const auto id = static_cast<std::int64_t>(value);
      const auto inserted = zone.boundary_elements.emplace(id, std::make_pair(family, mapping->second));
      if (!inserted.second && inserted.first->second != std::make_pair(family, mapping->second)) {
        throw std::runtime_error("conflicting boundary tags on element " + std::to_string(id));
      }
    };
    if (point_set == CGNS_ENUMV(PointRange)) {
      if (points.size() != 2) throw std::runtime_error("boundary PointRange must contain two indices");
      const cgsize_t first = std::min(points[0], points[1]);
      const cgsize_t last = std::max(points[0], points[1]);
      for (cgsize_t element = first; element <= last; ++element) add_element(element);
    } else if (point_set == CGNS_ENUMV(PointList)) {
      for (cgsize_t element : points) add_element(element);
    } else {
      throw std::runtime_error("boundary '" + boco_name + "' must use an element PointRange or PointList");
    }
  }
}

void merge_zone_connections(int fn, int base, std::vector<ZoneData>& zones, DisjointSet& sets) {
  std::unordered_map<std::string, std::size_t> zone_by_name;
  for (std::size_t z = 0; z < zones.size(); ++z) zone_by_name.emplace(zones[z].name, z);
  for (std::size_t z = 0; z < zones.size(); ++z) {
    int connection_count = 0;
    cgns_check(cg_nconns(fn, base, static_cast<int>(z + 1), &connection_count), "cg_nconns");
    for (int connection = 1; connection <= connection_count; ++connection) {
      char connection_name[cgns_name_buffer_size]{};
      char donor_name[cgns_name_buffer_size]{};
      CGNS_ENUMT(GridLocation_t) location{};
      CGNS_ENUMT(GridConnectivityType_t) connectivity_type{};
      CGNS_ENUMT(PointSetType_t) point_set{};
      cgsize_t point_count = 0;
      CGNS_ENUMT(ZoneType_t) donor_zone_type{};
      CGNS_ENUMT(PointSetType_t) donor_point_set{};
      CGNS_ENUMT(DataType_t) donor_data_type{};
      cgsize_t donor_count = 0;
      cgns_check(cg_conn_info(fn, base, static_cast<int>(z + 1), connection, connection_name, &location,
                              &connectivity_type, &point_set, &point_count, donor_name, &donor_zone_type,
                              &donor_point_set, &donor_data_type, &donor_count), "cg_conn_info");
      if (location != CGNS_ENUMV(Vertex) || point_set != CGNS_ENUMV(PointList) ||
          (donor_point_set != CGNS_ENUMV(PointList) && donor_point_set != CGNS_ENUMV(PointListDonor))) {
        throw std::runtime_error("connection '" + std::string(connection_name) +
                                 "' must be a vertex PointList-to-PointList mapping");
      }
      if (point_count != donor_count) throw std::runtime_error("connection point and donor counts differ");
      const auto donor_it = zone_by_name.find(donor_name);
      if (donor_it == zone_by_name.end()) throw std::runtime_error("connection references unknown donor zone '" + std::string(donor_name) + "'");
      const std::size_t donor_zone = donor_it->second;

      std::vector<cgsize_t> receiver(static_cast<std::size_t>(point_count));
      std::vector<cgsize_t> donor(static_cast<std::size_t>(donor_count));
      if (donor_data_type != CGNS_ENUMV(LongInteger) && donor_data_type != CGNS_ENUMV(Integer)) {
        throw std::runtime_error("connection donor indices must use Integer or LongInteger storage");
      }
      cgns_check(cg_conn_read(fn, base, static_cast<int>(z + 1), connection, receiver.data(),
                              CGNS_ENUMV(LongInteger), donor.data()), "cg_conn_read");
      for (std::size_t i = 0; i < receiver.size(); ++i) {
        if (receiver[i] < 1 || static_cast<std::size_t>(receiver[i]) > zones[z].coordinates.size() ||
            donor[i] < 1 || static_cast<std::size_t>(donor[i]) > zones[donor_zone].coordinates.size()) {
          throw std::runtime_error("connection contains an out-of-range vertex index");
        }
        sets.unite(zones[z].raw_offset + static_cast<std::size_t>(receiver[i] - 1),
                   zones[donor_zone].raw_offset + static_cast<std::size_t>(donor[i] - 1));
      }
    }
  }
}

template <class Callback>
void for_each_section_element(int fn, int base, int zone, int section,
                              CGNS_ENUMT(ElementType_t) section_type,
                              cgsize_t start, cgsize_t end, Callback callback) {
  cgsize_t data_size = 0;
  cgns_check(cg_ElementDataSize(fn, base, zone, section, &data_size), "cg_ElementDataSize");
  std::vector<cgsize_t> connectivity(static_cast<std::size_t>(data_size));
  cgns_check(cg_elements_read(fn, base, zone, section, connectivity.data(), nullptr), "cg_elements_read");
  std::size_t offset = 0;
  for (cgsize_t element_id = start; element_id <= end; ++element_id) {
    CGNS_ENUMT(ElementType_t) type = section_type;
    if (section_type == CGNS_ENUMV(MIXED)) {
      if (offset >= connectivity.size()) throw std::runtime_error("truncated MIXED connectivity");
      type = static_cast<CGNS_ENUMT(ElementType_t)>(connectivity[offset++]);
    }
    int node_count = 0;
    cgns_check(cg_npe(type, &node_count), "cg_npe");
    if (node_count <= 0 || offset + static_cast<std::size_t>(node_count) > connectivity.size()) {
      throw std::runtime_error("invalid element connectivity length");
    }
    callback(static_cast<std::int64_t>(element_id), type, connectivity.data() + offset, node_count);
    offset += static_cast<std::size_t>(node_count);
  }
  if (offset != connectivity.size()) throw std::runtime_error("unused values remain in element connectivity");
}

}  // namespace

GlobalMesh read_cgns_mesh(const std::filesystem::path& path, const CaseConfig& config) {
  CgnsFile file(path);
  const int fn = file.get();
  int base_count = 0;
  cgns_check(cg_nbases(fn, &base_count), "cg_nbases");
  if (base_count != 1) throw std::runtime_error("expected exactly one CGNS base, found " + std::to_string(base_count));
  constexpr int base = 1;
  char base_name[cgns_name_buffer_size]{};
  int cell_dimension = 0;
  int physical_dimension = 0;
  cgns_check(cg_base_read(fn, base, base_name, &cell_dimension, &physical_dimension), "cg_base_read");
  if (cell_dimension != 2 || (physical_dimension != 2 && physical_dimension != 3)) {
    throw std::runtime_error("CGNS base must describe a two-dimensional mesh");
  }
  int zone_count = 0;
  cgns_check(cg_nzones(fn, base, &zone_count), "cg_nzones");
  if (zone_count <= 0) throw std::runtime_error("CGNS mesh contains no zones");

  std::vector<ZoneData> zones(static_cast<std::size_t>(zone_count));
  std::vector<Vec2> raw_coordinates;
  std::vector<double> raw_z;
  for (int zone_index = 1; zone_index <= zone_count; ++zone_index) {
    ZoneData& zone = zones[static_cast<std::size_t>(zone_index - 1)];
    char zone_name[cgns_name_buffer_size]{};
    cgsize_t size[9]{};
    cgns_check(cg_zone_read(fn, base, zone_index, zone_name, size), "cg_zone_read");
    CGNS_ENUMT(ZoneType_t) zone_type{};
    cgns_check(cg_zone_type(fn, base, zone_index, &zone_type), "cg_zone_type");
    if (zone_type != CGNS_ENUMV(Unstructured)) throw std::runtime_error("all CGNS zones must be unstructured");
    if (size[0] <= 0) throw std::runtime_error("zone contains no vertices");
    zone.name = zone_name;
    zone.raw_offset = raw_coordinates.size();
    zone.coordinates.resize(static_cast<std::size_t>(size[0]));
    zone.z.assign(static_cast<std::size_t>(size[0]), 0.0);
    std::vector<double> x(static_cast<std::size_t>(size[0]));
    std::vector<double> y(static_cast<std::size_t>(size[0]));
    cgsize_t lower = 1;
    cgsize_t upper = size[0];
    cgns_check(cg_coord_read(fn, base, zone_index, "CoordinateX", CGNS_ENUMV(RealDouble), &lower, &upper, x.data()),
               "cg_coord_read(CoordinateX)");
    cgns_check(cg_coord_read(fn, base, zone_index, "CoordinateY", CGNS_ENUMV(RealDouble), &lower, &upper, y.data()),
               "cg_coord_read(CoordinateY)");
    int coordinate_count = 0;
    cgns_check(cg_ncoords(fn, base, zone_index, &coordinate_count), "cg_ncoords");
    for (int coordinate = 1; coordinate <= coordinate_count; ++coordinate) {
      CGNS_ENUMT(DataType_t) data_type{};
      char coordinate_name[cgns_name_buffer_size]{};
      cgns_check(cg_coord_info(fn, base, zone_index, coordinate, &data_type, coordinate_name), "cg_coord_info");
      if (std::string(coordinate_name) == "CoordinateZ") {
        cgns_check(cg_coord_read(fn, base, zone_index, coordinate_name, CGNS_ENUMV(RealDouble), &lower, &upper,
                                 zone.z.data()), "cg_coord_read(CoordinateZ)");
      }
    }
    for (std::size_t i = 0; i < zone.coordinates.size(); ++i) zone.coordinates[i] = {x[i], y[i]};
    raw_coordinates.insert(raw_coordinates.end(), zone.coordinates.begin(), zone.coordinates.end());
    raw_z.insert(raw_z.end(), zone.z.begin(), zone.z.end());
    read_zone_boundary_elements(fn, base, zone_index, zone, config);
  }

  DisjointSet sets(raw_coordinates.size());
  merge_zone_connections(fn, base, zones, sets);

  GlobalMesh mesh;
  mesh.source_zone_count = zone_count;
  mesh.source_vertex_count = raw_coordinates.size();
  std::unordered_map<std::size_t, std::int64_t> root_to_global;
  std::vector<std::int64_t> raw_to_global(raw_coordinates.size(), -1);
  for (std::size_t raw = 0; raw < raw_coordinates.size(); ++raw) {
    const std::size_t root = sets.find(raw);
    auto [it, inserted] = root_to_global.emplace(root, static_cast<std::int64_t>(mesh.nodes.size()));
    if (inserted) mesh.nodes.push_back({raw_coordinates[raw]});
    raw_to_global[raw] = it->second;
    const Vec2 canonical = mesh.nodes[static_cast<std::size_t>(it->second)].position;
    const double scale = std::max({1.0, norm(canonical), norm(raw_coordinates[raw])});
    if (norm(canonical - raw_coordinates[raw]) > 1.0e-9 * scale) {
      throw std::runtime_error("CGNS connection joins vertices with inconsistent coordinates");
    }
    mesh.maximum_abs_z = std::max(mesh.maximum_abs_z, std::abs(raw_z[raw]));
  }
  mesh.merged_interface_vertices = raw_coordinates.size() - mesh.nodes.size();

  std::unordered_map<std::uint64_t, BoundaryEdge> boundary_edges;
  for (int zone_index = 1; zone_index <= zone_count; ++zone_index) {
    const ZoneData& zone = zones[static_cast<std::size_t>(zone_index - 1)];
    int section_count = 0;
    cgns_check(cg_nsections(fn, base, zone_index, &section_count), "cg_nsections");
    for (int section = 1; section <= section_count; ++section) {
      char section_name[cgns_name_buffer_size]{};
      CGNS_ENUMT(ElementType_t) section_type{};
      cgsize_t start = 0;
      cgsize_t end = 0;
      int boundary_count = 0;
      int parent_flag = 0;
      cgns_check(cg_section_read(fn, base, zone_index, section, section_name, &section_type, &start, &end,
                                 &boundary_count, &parent_flag), "cg_section_read");
      for_each_section_element(fn, base, zone_index, section, section_type, start, end,
          [&](std::int64_t element_id, CGNS_ENUMT(ElementType_t) type, const cgsize_t* connectivity, int node_count) {
            const auto corners = element_corner_indices(type);
            if (corners.empty()) {
              throw std::runtime_error("unsupported CGNS element type " + std::to_string(static_cast<int>(type)) +
                                       " in section '" + section_name + "'");
            }
            std::vector<std::int64_t> nodes;
            nodes.reserve(corners.size());
            for (int corner : corners) {
              if (corner >= node_count || connectivity[corner] < 1 ||
                  static_cast<std::size_t>(connectivity[corner]) > zone.coordinates.size()) {
                throw std::runtime_error("element contains an out-of-range node index");
              }
              const std::size_t raw = zone.raw_offset + static_cast<std::size_t>(connectivity[corner] - 1);
              nodes.push_back(raw_to_global[raw]);
            }
            if (is_volume_type(type)) {
              if (std::unordered_set<std::int64_t>(nodes.begin(), nodes.end()).size() != nodes.size()) {
                throw std::runtime_error("volume element has repeated corner vertices");
              }
              auto geometry = polygon_geometry(nodes, mesh.nodes);
              if (geometry.second < 0.0) {
                std::reverse(nodes.begin(), nodes.end());
                geometry = polygon_geometry(nodes, mesh.nodes);
              }
              const auto gid = static_cast<std::int64_t>(mesh.cells.size());
              mesh.cells.push_back({gid, std::move(nodes), geometry.first, geometry.second});
            } else if (is_edge_type(type)) {
              const auto bc_it = zone.boundary_elements.find(element_id);
              if (bc_it == zone.boundary_elements.end()) return;  // a multi-zone interface BAR, not a physical BC
              const std::uint64_t key = edge_key(nodes[0], nodes[1]);
              const BoundaryEdge candidate{bc_it->second.first, bc_it->second.second, false};
              const auto [it, inserted] = boundary_edges.emplace(key, candidate);
              if (!inserted && (it->second.tag != candidate.tag || it->second.type != candidate.type)) {
                throw std::runtime_error("conflicting physical boundary descriptions for the same edge");
              }
            }
          });
    }
  }
  if (mesh.cells.empty()) throw std::runtime_error("CGNS mesh contains no supported volume cells");

  std::unordered_map<std::uint64_t, std::size_t> face_by_edge;
  for (const GlobalCell& cell : mesh.cells) {
    for (std::size_t i = 0; i < cell.nodes.size(); ++i) {
      const std::int64_t a_id = cell.nodes[i];
      const std::int64_t b_id = cell.nodes[(i + 1) % cell.nodes.size()];
      const std::uint64_t key = edge_key(a_id, b_id);
      const auto found = face_by_edge.find(key);
      if (found == face_by_edge.end()) {
        const Vec2 a = mesh.nodes[static_cast<std::size_t>(a_id)].position;
        const Vec2 b = mesh.nodes[static_cast<std::size_t>(b_id)].position;
        const Vec2 edge = b - a;
        const double length = norm(edge);
        if (!std::isfinite(length) || length <= 0.0) throw std::runtime_error("zero-length face");
        GlobalFace face;
        face.global_id = static_cast<std::int64_t>(mesh.faces.size());
        face.nodes = {a_id, b_id};
        face.left = cell.global_id;
        face.center = (a + b) * 0.5;
        face.normal = {edge.y / length, -edge.x / length};
        face.length = length;
        face_by_edge.emplace(key, mesh.faces.size());
        mesh.faces.push_back(std::move(face));
      } else {
        GlobalFace& face = mesh.faces[found->second];
        if (face.right >= 0) throw std::runtime_error("non-manifold edge belongs to more than two cells");
        face.right = cell.global_id;
      }
    }
  }

  for (GlobalFace& face : mesh.faces) {
    const std::uint64_t key = edge_key(face.nodes[0], face.nodes[1]);
    auto boundary = boundary_edges.find(key);
    if (face.right < 0) {
      if (boundary == boundary_edges.end()) {
        std::ostringstream message;
        message << "unmatched face at (" << face.center.x << ',' << face.center.y
                << ") lacks a physical CGNS boundary tag; a multi-zone connection may be incomplete";
        throw std::runtime_error(message.str());
      }
      face.boundary_tag = boundary->second.tag;
      face.boundary_type = boundary->second.type;
      boundary->second.used = true;
    } else if (boundary != boundary_edges.end()) {
      throw std::runtime_error("physical boundary edge is shared by two volume cells");
    }
    if (dot(face.normal, face.center - mesh.cells[static_cast<std::size_t>(face.left)].center) <= 0.0) {
      throw std::runtime_error("face normal is not outward from its left cell");
    }
  }
  for (const auto& [key, boundary] : boundary_edges) {
    (void)key;
    if (!boundary.used) throw std::runtime_error("CGNS physical boundary BAR does not match a volume-cell face");
  }

  validate_global_mesh(mesh, config);
  return mesh;
}

void validate_global_mesh(const GlobalMesh& mesh, const CaseConfig& config) {
  if (mesh.nodes.empty() || mesh.cells.empty() || mesh.faces.empty()) throw std::runtime_error("mesh is empty");
  std::map<std::string, std::size_t> boundary_counts;
  std::vector<int> face_incidence(mesh.cells.size(), 0);
  for (const GlobalCell& cell : mesh.cells) {
    if (cell.global_id < 0 || static_cast<std::size_t>(cell.global_id) >= mesh.cells.size()) {
      throw std::runtime_error("invalid global cell id");
    }
    if (!std::isfinite(cell.area) || cell.area <= 0.0 || !std::isfinite(cell.center.x) || !std::isfinite(cell.center.y)) {
      throw std::runtime_error("invalid cell geometry");
    }
  }
  for (const GlobalFace& face : mesh.faces) {
    if (face.left < 0 || static_cast<std::size_t>(face.left) >= mesh.cells.size() ||
        (face.right >= 0 && static_cast<std::size_t>(face.right) >= mesh.cells.size())) {
      throw std::runtime_error("invalid cell reference on face");
    }
    ++face_incidence[static_cast<std::size_t>(face.left)];
    if (face.right >= 0) ++face_incidence[static_cast<std::size_t>(face.right)];
    if (face.right < 0) ++boundary_counts[face.boundary_tag];
  }
  for (std::size_t count : face_incidence) {
    if (count < 3) throw std::runtime_error("volume cell has fewer than three incident faces");
  }
  for (const auto& [tag, type] : config.boundary_conditions) {
    (void)type;
    if (boundary_counts[tag] == 0) throw std::runtime_error("case boundary tag '" + tag + "' has no mesh faces");
  }
  for (const auto& [tag, count] : boundary_counts) {
    (void)count;
    if (config.boundary_conditions.count(tag) == 0) throw std::runtime_error("mesh uses unmapped boundary tag '" + tag + "'");
  }
}

}  // namespace cfd
