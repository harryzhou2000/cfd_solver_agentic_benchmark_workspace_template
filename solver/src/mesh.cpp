#include "mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace aerofv {
namespace {

[[noreturn]] void cgns_error(const std::string &where) {
  throw std::runtime_error(where + ": " + cg_get_error());
}

void check_cgns(int status, const std::string &where) {
  if (status != CG_OK) {
    cgns_error(where);
  }
}

struct CgnsFile {
  int id{-1};
  explicit CgnsFile(const std::string &path) {
    check_cgns(cg_open(path.c_str(), CG_MODE_READ, &id), "cg_open(" + path + ")");
  }
  ~CgnsFile() {
    if (id >= 0) {
      cg_close(id);
    }
  }
  CgnsFile(const CgnsFile &) = delete;
  CgnsFile &operator=(const CgnsFile &) = delete;
};

using EdgeKey = std::array<int, 2>;

EdgeKey edge_key(int a, int b) { return {{std::min(a, b), std::max(a, b)}}; }

struct CoordinateKey {
  std::int64_t x;
  std::int64_t y;
  bool operator<(const CoordinateKey &other) const {
    return x != other.x ? x < other.x : y < other.y;
  }
};

CoordinateKey coordinate_key(const Vec2 &p, double tolerance) {
  const auto scale = 1.0 / tolerance;
  return {static_cast<std::int64_t>(std::llround(p.x * scale)),
          static_cast<std::int64_t>(std::llround(p.y * scale))};
}

double signed_area_twice(const std::vector<int> &vertices,
                         const std::vector<Vec2> &points) {
  double twice_area = 0.0;
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    twice_area += cross(points[vertices[i]], points[vertices[(i + 1) % vertices.size()]]);
  }
  return twice_area;
}

Cell make_cell(std::vector<int> vertices, const std::vector<Vec2> &points) {
  if (vertices.size() != 3 && vertices.size() != 4) {
    throw std::runtime_error("only TRI_3 and QUAD_4 volume cells are supported");
  }
  double twice_area = signed_area_twice(vertices, points);
  if (std::abs(twice_area) <= 1.0e-24) {
    throw std::runtime_error("encountered a zero-area cell");
  }
  if (twice_area < 0.0) {
    std::reverse(vertices.begin(), vertices.end());
    twice_area = -twice_area;
  }

  // Polygon centroid, rather than a vertex average, is required for quads.
  Vec2 numerator{};
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Vec2 &a = points[vertices[i]];
    const Vec2 &b = points[vertices[(i + 1) % vertices.size()]];
    const double cr = cross(a, b);
    numerator += (a + b) * cr;
  }
  Cell cell;
  cell.vertices = std::move(vertices);
  cell.area = 0.5 * twice_area;
  cell.centroid = numerator / (3.0 * twice_area);
  return cell;
}

} // namespace

bool GlobalMesh::is_boundary_face(int face) const {
  return faces.at(static_cast<std::size_t>(face)).right_cell < 0;
}

void GlobalMesh::validate_topology() const {
  if (vertices.empty() || cells.empty() || faces.empty()) {
    throw std::runtime_error("mesh has no vertices, cells, or faces");
  }
  for (std::size_t ci = 0; ci < cells.size(); ++ci) {
    const Cell &cell = cells[ci];
    if (cell.vertices.size() < 3 || cell.area <= 0.0 || !std::isfinite(cell.area)) {
      throw std::runtime_error("invalid cell geometry at cell " + std::to_string(ci));
    }
    if (cell.faces.size() != cell.vertices.size()) {
      throw std::runtime_error("cell-face topology mismatch at cell " + std::to_string(ci));
    }
    for (int vertex : cell.vertices) {
      if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertices.size()) {
        throw std::runtime_error("cell references invalid vertex");
      }
    }
  }
  for (std::size_t fi = 0; fi < faces.size(); ++fi) {
    const Face &face = faces[fi];
    if (face.left_cell < 0 || static_cast<std::size_t>(face.left_cell) >= cells.size() ||
        face.right_cell >= static_cast<int>(cells.size()) || face.length <= 0.0 ||
        !std::isfinite(face.length)) {
      throw std::runtime_error("invalid face topology at face " + std::to_string(fi));
    }
    if (face.right_cell < 0 && face.boundary_family.empty()) {
      throw std::runtime_error("physical boundary face lacks a BAR_2 family tag");
    }
    if (std::abs(norm(face.normal) - 1.0) > 1.0e-10) {
      throw std::runtime_error("face normal is not unit length");
    }
  }
}

GlobalMesh read_cgns_unstructured_2d(const std::string &path, double stitch_tolerance) {
  if (!(stitch_tolerance > 0.0) || !std::isfinite(stitch_tolerance)) {
    throw std::invalid_argument("stitch tolerance must be finite and positive");
  }
  CgnsFile file(path);
  int bases = 0;
  check_cgns(cg_nbases(file.id, &bases), "cg_nbases");
  if (bases != 1) {
    throw std::runtime_error("expected exactly one CGNS base");
  }
  int cell_dimension = 0;
  int physical_dimension = 0;
  // CGNS names are limited to 32 characters by the standard.
  char base_name[33]{};
  check_cgns(cg_base_read(file.id, 1, base_name, &cell_dimension, &physical_dimension),
             "cg_base_read");
  if (cell_dimension != 2 || physical_dimension < 2) {
    throw std::runtime_error("CGNS base is not two-dimensional");
  }

  GlobalMesh mesh;
  std::map<CoordinateKey, std::vector<int>> welded_vertices;
  std::map<EdgeKey, std::string> bar_families;
  int zones = 0;
  check_cgns(cg_nzones(file.id, 1, &zones), "cg_nzones");
  for (int zone = 1; zone <= zones; ++zone) {
    CGNS_ENUMT(ZoneType_t) zone_type;
    check_cgns(cg_zone_type(file.id, 1, zone, &zone_type), "cg_zone_type");
    if (zone_type != CGNS_ENUMV(Unstructured)) {
      throw std::runtime_error("structured zones are not supported by the unstructured solver");
    }
    // CGNS writes its maximum zone-size tuple even for an unstructured zone;
    // reserve all three (vertex/cell/boundary) entries for each possible index
    // dimension instead of assuming a three-entry implementation detail.
    cgsize_t size[9]{};
    char zone_name[33]{};
    check_cgns(cg_zone_read(file.id, 1, zone, zone_name, size), "cg_zone_read");
    const auto vertex_count = static_cast<std::size_t>(size[0]);
    if (vertex_count == 0) {
      throw std::runtime_error("zone has no vertices");
    }
    std::vector<double> x(vertex_count), y(vertex_count);
    const cgsize_t range_min[1] = {1};
    const cgsize_t range_max[1] = {size[0]};
    check_cgns(cg_coord_read(file.id, 1, zone, "CoordinateX", CGNS_ENUMV(RealDouble),
                             range_min, range_max, x.data()),
               "cg_coord_read(CoordinateX)");
    check_cgns(cg_coord_read(file.id, 1, zone, "CoordinateY", CGNS_ENUMV(RealDouble),
                             range_min, range_max, y.data()),
               "cg_coord_read(CoordinateY)");

    std::vector<int> zone_to_global(vertex_count);
    for (std::size_t local = 0; local < vertex_count; ++local) {
      const Vec2 point{x[local], y[local]};
      const CoordinateKey key = coordinate_key(point, stitch_tolerance);
      int global = -1;
      auto &candidates = welded_vertices[key];
      for (int candidate : candidates) {
        if (norm(mesh.vertices[candidate] - point) <= stitch_tolerance) {
          global = candidate;
          break;
        }
      }
      if (global < 0) {
        global = static_cast<int>(mesh.vertices.size());
        mesh.vertices.push_back(point);
        candidates.push_back(global);
      }
      zone_to_global[local] = global;
    }

    int sections = 0;
    check_cgns(cg_nsections(file.id, 1, zone, &sections), "cg_nsections");
    for (int section = 1; section <= sections; ++section) {
      char section_name[33]{};
      CGNS_ENUMT(ElementType_t) element_type;
      cgsize_t start = 0, end = -1;
      int boundary_count = 0, parent_flag = 0;
      check_cgns(cg_section_read(file.id, 1, zone, section, section_name, &element_type,
                                 &start, &end, &boundary_count, &parent_flag),
                 "cg_section_read");
      // BAR_2 section names are the mesh-provided family tags.  This is the
      // portable association exposed by CGNS element sections and deliberately
      // avoids any case-specific boundary-name assumptions.
      std::string boundary_name = section_name;
      if (element_type != CGNS_ENUMV(TRI_3) && element_type != CGNS_ENUMV(QUAD_4) &&
          element_type != CGNS_ENUMV(BAR_2)) {
        continue; // Ignore unsupported non-volume metadata sections.
      }
      int nodes_per_element = 0;
      check_cgns(cg_npe(element_type, &nodes_per_element), "cg_npe");
      const std::size_t count = static_cast<std::size_t>(end - start + 1);
      std::vector<cgsize_t> connectivity(count * static_cast<std::size_t>(nodes_per_element));
      check_cgns(cg_elements_read(file.id, 1, zone, section, connectivity.data(), nullptr),
                 "cg_elements_read");
      for (std::size_t e = 0; e < count; ++e) {
        std::vector<int> vertices;
        vertices.reserve(static_cast<std::size_t>(nodes_per_element));
        for (int n = 0; n < nodes_per_element; ++n) {
          const cgsize_t cgns_vertex = connectivity[e * nodes_per_element + n];
          if (cgns_vertex < 1 || static_cast<std::size_t>(cgns_vertex) > vertex_count) {
            throw std::runtime_error("element references vertex outside its zone");
          }
          vertices.push_back(zone_to_global[static_cast<std::size_t>(cgns_vertex - 1)]);
        }
        if (element_type == CGNS_ENUMV(BAR_2)) {
          const EdgeKey key = edge_key(vertices[0], vertices[1]);
          const auto [it, inserted] = bar_families.emplace(key, boundary_name);
          if (!inserted && it->second != boundary_name) {
            throw std::runtime_error("a BAR_2 edge has conflicting family names");
          }
        } else {
          mesh.cells.push_back(make_cell(std::move(vertices), mesh.vertices));
        }
      }
    }
  }

  std::map<EdgeKey, int> face_by_edge;
  for (std::size_t cell_id = 0; cell_id < mesh.cells.size(); ++cell_id) {
    Cell &cell = mesh.cells[cell_id];
    cell.faces.reserve(cell.vertices.size());
    for (std::size_t i = 0; i < cell.vertices.size(); ++i) {
      const int a = cell.vertices[i];
      const int b = cell.vertices[(i + 1) % cell.vertices.size()];
      const EdgeKey key = edge_key(a, b);
      auto it = face_by_edge.find(key);
      if (it == face_by_edge.end()) {
        const Vec2 edge = mesh.vertices[b] - mesh.vertices[a];
        const double length = norm(edge);
        if (length <= 1.0e-14) {
          throw std::runtime_error("encountered a zero-length cell edge");
        }
        Face face;
        face.vertices = {{a, b}};
        face.left_cell = static_cast<int>(cell_id);
        face.center = (mesh.vertices[a] + mesh.vertices[b]) * 0.5;
        face.length = length;
        face.normal = {edge.y / length, -edge.x / length};
        const int face_id = static_cast<int>(mesh.faces.size());
        mesh.faces.push_back(std::move(face));
        face_by_edge.emplace(key, face_id);
        cell.faces.push_back(face_id);
      } else {
        Face &face = mesh.faces[it->second];
        if (face.right_cell >= 0) {
          throw std::runtime_error("non-manifold edge shared by more than two cells");
        }
        face.right_cell = static_cast<int>(cell_id);
        cell.faces.push_back(it->second);
      }
    }
  }
  for (Face &face : mesh.faces) {
    if (face.right_cell < 0) {
      const auto it = bar_families.find(edge_key(face.vertices[0], face.vertices[1]));
      if (it != bar_families.end()) {
        face.boundary_family = it->second;
      }
    }
  }
  mesh.validate_topology();
  return mesh;
}

} // namespace aerofv
