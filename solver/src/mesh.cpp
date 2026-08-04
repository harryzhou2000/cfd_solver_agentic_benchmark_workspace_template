#include "cfd/mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cfd {
namespace {

[[noreturn]] void cgns_fail(const std::string& where) {
    throw std::runtime_error(where + ": " + cg_get_error());
}
void cgns_check(int status, const std::string& where) { if (status != CG_OK) cgns_fail(where); }

struct EdgeKey {
    int a, b;
    EdgeKey(int p, int q) : a(std::min(p, q)), b(std::max(p, q)) {}
    bool operator<(const EdgeKey& other) const { return a != other.a ? a < other.a : b < other.b; }
};
struct BucketKey {
    long long x, y;
    bool operator<(const BucketKey& o) const { return x != o.x ? x < o.x : y < o.y; }
};

std::string cgns_family_name(int fn, int base, int zone, int bc, const char* fallback) {
    char family[33]{};
    if (cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", bc, "end") == CG_OK &&
        cg_famname_read(family) == CG_OK && family[0] != '\0') return family;
    return fallback;
}

} // namespace

BoundaryType boundary_type_from_string(const std::string& name) {
    if (name == "farfield") return BoundaryType::farfield;
    if (name == "slip_wall") return BoundaryType::slip_wall;
    if (name == "no_slip_adiabatic_wall") return BoundaryType::no_slip_adiabatic_wall;
    throw std::invalid_argument("unsupported boundary condition type: " + name);
}
BoundaryMap boundary_map_from_strings(const std::unordered_map<std::string, std::string>& values) {
    BoundaryMap result;
    for (const auto& [name, type] : values) result.emplace(name, boundary_type_from_string(type));
    return result;
}
const char* boundary_type_name(BoundaryType type) noexcept {
    switch (type) {
    case BoundaryType::interior: return "interior";
    case BoundaryType::farfield: return "farfield";
    case BoundaryType::slip_wall: return "slip_wall";
    case BoundaryType::no_slip_adiabatic_wall: return "no_slip_adiabatic_wall";
    default: return "unspecified";
    }
}

Mesh read_cgns_mesh(const std::string& path, const BoundaryMap& boundary_map, double tolerance) {
    if (!(tolerance > 0.0)) throw std::invalid_argument("node merge tolerance must be positive");
    int fn = 0;
    cgns_check(cg_open(path.c_str(), CG_MODE_READ, &fn), "opening CGNS mesh " + path);
    try {
        int nbases = 0;
        cgns_check(cg_nbases(fn, &nbases), "reading CGNS base count");
        if (nbases != 1) throw std::runtime_error("CGNS reader expects exactly one base");
        char base_name[33]{}; int cell_dim = 0, phys_dim = 0;
        cgns_check(cg_base_read(fn, 1, base_name, &cell_dim, &phys_dim), "reading CGNS base");
        if (cell_dim != 2) throw std::runtime_error("only 2-D CGNS meshes are supported");

        Mesh mesh;
        std::map<BucketKey, std::vector<int>> buckets;
        std::map<EdgeKey, std::string> boundary_edges;
        int nz = 0;
        cgns_check(cg_nzones(fn, 1, &nz), "reading zone count");
        for (int z = 1; z <= nz; ++z) {
            char zone_name[33]{}; cgsize_t zone_size[9]{};
            cgns_check(cg_zone_read(fn, 1, z, zone_name, zone_size), "reading zone");
            ZoneType_t zone_type{}; cgns_check(cg_zone_type(fn, 1, z, &zone_type), "reading zone type");
            if (zone_type != CGNS_ENUMV(Unstructured)) throw std::runtime_error("structured CGNS zones are unsupported");
            const int nverts = static_cast<int>(zone_size[0]);
            std::vector<double> x(nverts), y(nverts);
            cgsize_t lo[3]{1, 1, 1}, hi[3]{nverts, 1, 1};
            cgns_check(cg_coord_read(fn, 1, z, "CoordinateX", RealDouble, lo, hi, x.data()), "reading CoordinateX");
            cgns_check(cg_coord_read(fn, 1, z, "CoordinateY", RealDouble, lo, hi, y.data()), "reading CoordinateY");
            std::vector<int> zone_node(nverts + 1);
            for (int i = 0; i < nverts; ++i) {
                const BucketKey key{static_cast<long long>(std::llround(x[i] / tolerance)), static_cast<long long>(std::llround(y[i] / tolerance))};
                int found = -1;
                for (long long dx = -1; dx <= 1 && found < 0; ++dx) for (long long dy = -1; dy <= 1 && found < 0; ++dy) {
                    auto it = buckets.find({key.x + dx, key.y + dy});
                    if (it == buckets.end()) continue;
                    for (int candidate : it->second) {
                        const Vec2& p = mesh.nodes[candidate];
                        if (std::hypot(p.x - x[i], p.y - y[i]) <= tolerance) { found = candidate; break; }
                    }
                }
                if (found < 0) { found = static_cast<int>(mesh.nodes.size()); mesh.nodes.push_back({x[i], y[i]}); buckets[key].push_back(found); }
                zone_node[i + 1] = found;
            }

            std::unordered_map<int, std::string> boco_element_name;
            int nbocos = 0; cgns_check(cg_nbocos(fn, 1, z, &nbocos), "reading BC count");
            for (int b = 1; b <= nbocos; ++b) {
                char name[33]{}; BCType_t bt{}; PointSetType_t pt{}; cgsize_t np = 0, normal_size = 0;
                int normal_index[3]{}; DataType_t normal_type{}; int datasets = 0;
                cgns_check(cg_boco_info(fn, 1, z, b, name, &bt, &pt, &np, normal_index, &normal_size, &normal_type, &datasets), "reading BC info");
                std::vector<cgsize_t> points(pt == CGNS_ENUMV(PointRange) ? 2 : static_cast<size_t>(np));
                cgns_check(cg_boco_read(fn, 1, z, b, points.data(), nullptr), "reading BC points");
                const std::string label = cgns_family_name(fn, 1, z, b, name);
                if (pt == CGNS_ENUMV(PointRange)) {
                    for (cgsize_t e = std::min(points[0], points[1]); e <= std::max(points[0], points[1]); ++e) boco_element_name[static_cast<int>(e)] = label;
                } else if (pt == CGNS_ENUMV(PointList)) {
                    for (cgsize_t e : points) boco_element_name[static_cast<int>(e)] = label;
                }
            }

            int nsections = 0; cgns_check(cg_nsections(fn, 1, z, &nsections), "reading section count");
            for (int s = 1; s <= nsections; ++s) {
                char section_name[33]{}; ElementType_t type{}; cgsize_t start = 0, end = 0;
                int nbndry = 0, parent = 0;
                cgns_check(cg_section_read(fn, 1, z, s, section_name, &type, &start, &end, &nbndry, &parent), "reading section");
                if (type != CGNS_ENUMV(TRI_3) && type != CGNS_ENUMV(QUAD_4) && type != CGNS_ENUMV(BAR_2)) continue;
                const int npe = type == CGNS_ENUMV(TRI_3) ? 3 : type == CGNS_ENUMV(QUAD_4) ? 4 : 2;
                const size_t count = static_cast<size_t>(end - start + 1);
                std::vector<cgsize_t> conn(count * npe);
                cgns_check(cg_elements_read(fn, 1, z, s, conn.data(), nullptr), "reading section connectivity");
                for (size_t e = 0; e < count; ++e) {
                    std::vector<int> nodes(npe);
                    for (int k = 0; k < npe; ++k) nodes[k] = zone_node.at(static_cast<size_t>(conn[e * npe + k]));
                    const int element_id = static_cast<int>(start + static_cast<cgsize_t>(e));
                    if (type == CGNS_ENUMV(BAR_2)) {
                        const auto it = boco_element_name.find(element_id);
                        boundary_edges.emplace(EdgeKey(nodes[0], nodes[1]), it == boco_element_name.end() ? section_name : it->second);
                        continue;
                    }
                    Cell cell; cell.nodes = std::move(nodes); cell.type = type == CGNS_ENUMV(TRI_3) ? CellType::triangle : CellType::quadrilateral;
                    double twice_area = 0.0, cx = 0.0, cy = 0.0;
                    for (int k = 0; k < npe; ++k) {
                        const Vec2& a = mesh.nodes[cell.nodes[k]]; const Vec2& b = mesh.nodes[cell.nodes[(k + 1) % npe]];
                        const double cross = a.x * b.y - b.x * a.y; twice_area += cross; cx += (a.x + b.x) * cross; cy += (a.y + b.y) * cross;
                    }
                    if (std::abs(twice_area) <= std::numeric_limits<double>::epsilon()) throw std::runtime_error("degenerate cell in CGNS mesh");
                    if (twice_area < 0.0) { std::reverse(cell.nodes.begin(), cell.nodes.end()); twice_area = -twice_area; cx = -cx; cy = -cy; }
                    cell.area = 0.5 * twice_area; cell.centroid = {cx / (3.0 * twice_area), cy / (3.0 * twice_area)};
                    mesh.cells.push_back(std::move(cell));
                }
            }
        }

        std::map<EdgeKey, int> face_index;
        for (int ci = 0; ci < static_cast<int>(mesh.cells.size()); ++ci) {
            Cell& cell = mesh.cells[ci]; cell.faces.reserve(cell.nodes.size());
            for (size_t k = 0; k < cell.nodes.size(); ++k) {
                const int a = cell.nodes[k], b = cell.nodes[(k + 1) % cell.nodes.size()]; const EdgeKey key(a, b);
                auto [it, inserted] = face_index.emplace(key, static_cast<int>(mesh.faces.size()));
                if (inserted) {
                    const Vec2& pa = mesh.nodes[a]; const Vec2& pb = mesh.nodes[b];
                    Face face; face.nodes = {{a, b}}; face.left_cell = ci; face.centroid = {(pa.x + pb.x) * .5, (pa.y + pb.y) * .5};
                    face.normal = {pb.y - pa.y, pa.x - pb.x}; face.length = std::hypot(face.normal.x, face.normal.y);
                    face.unit_normal = {face.normal.x / face.length, face.normal.y / face.length};
                    mesh.faces.push_back(std::move(face));
                } else {
                    Face& face = mesh.faces[it->second];
                    if (face.right_cell >= 0) throw std::runtime_error("non-manifold edge in CGNS mesh");
                    face.right_cell = ci;
                }
                cell.faces.push_back(it->second);
            }
        }
        for (Face& face : mesh.faces) if (face.right_cell < 0) {
            const auto it = boundary_edges.find(EdgeKey(face.nodes[0], face.nodes[1]));
            if (it != boundary_edges.end()) face.boundary_name = it->second;
            const auto mapped = boundary_map.find(face.boundary_name);
            face.boundary_type = mapped == boundary_map.end() ? BoundaryType::unspecified : mapped->second;
        }
        for (Cell& cell : mesh.cells) cell.neighbors.assign(cell.faces.size(), -1);
        for (const Face& face : mesh.faces) {
            const Cell& left = mesh.cells[face.left_cell];
            for (size_t k = 0; k < left.faces.size(); ++k) if (left.faces[k] == &face - mesh.faces.data()) mesh.cells[face.left_cell].neighbors[k] = face.right_cell;
            if (face.right_cell >= 0) {
                const Cell& right = mesh.cells[face.right_cell];
                for (size_t k = 0; k < right.faces.size(); ++k) if (right.faces[k] == &face - mesh.faces.data()) mesh.cells[face.right_cell].neighbors[k] = face.left_cell;
            }
        }
        cgns_check(cg_close(fn), "closing CGNS mesh");
        return mesh;
    } catch (...) { cg_close(fn); throw; }
}

std::vector<std::vector<int>> cell_adjacency(const Mesh& mesh) {
    std::vector<std::vector<int>> adjacency(mesh.cells.size());
    for (const Face& face : mesh.faces) if (face.left_cell >= 0 && face.right_cell >= 0) {
        adjacency[face.left_cell].push_back(face.right_cell); adjacency[face.right_cell].push_back(face.left_cell);
    }
    return adjacency;
}

} // namespace cfd
