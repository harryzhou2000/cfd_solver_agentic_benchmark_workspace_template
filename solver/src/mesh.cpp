#include "cfd/mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace cfd {
namespace {

// CGNS's SIDS name limit is 32 characters.  CGNS 4.5's public C header does
// not expose a name-length macro, so keep the terminating byte explicit.
constexpr std::size_t kCgnsNameBytes = 33;

[[noreturn]] void cgns_fail(const std::string& where) {
    throw std::runtime_error(where + ": " + cg_get_error());
}

void check_cgns(int status, const std::string& where) {
    if (status != CG_OK) cgns_fail(where);
}

struct DisjointSet {
    explicit DisjointSet(std::size_t n) : parent(n), rank(n, 0) {
        std::iota(parent.begin(), parent.end(), std::size_t{0});
    }
    std::size_t find(std::size_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void unite(std::size_t a, std::size_t b) {
        a = find(a); b = find(b);
        if (a == b) return;
        if (rank[a] < rank[b]) std::swap(a, b);
        parent[b] = a;
        if (rank[a] == rank[b]) ++rank[a];
    }
    std::vector<std::size_t> parent;
    std::vector<unsigned char> rank;
};

struct RawCell {
    std::array<std::size_t, 4> vertices{};
    std::uint8_t count{};
};
struct RawBoundaryEdge {
    std::size_t a{};
    std::size_t b{};
    int zone{};
    cgsize_t element_id{};
};
struct RawConnection {
    int zone{};
    std::string donor_zone;
    std::vector<cgsize_t> points;
    std::vector<cgsize_t> donor_points;
};
struct EdgeKey {
    GlobalIndex a{};
    GlobalIndex b{};
    EdgeKey() = default;
    EdgeKey(GlobalIndex x, GlobalIndex y) : a(std::min(x, y)), b(std::max(x, y)) {}
    friend bool operator<(const EdgeKey& x, const EdgeKey& y) {
        return x.a != y.a ? x.a < y.a : x.b < y.b;
    }
};

std::string current_family_name(int fn, int base, int zone, int bc,
                                const std::string& fallback) {
    char family[kCgnsNameBytes]{};
    if (cg_goto(fn, base, "Zone_t", zone, "ZoneBC_t", 1, "BC_t", bc, "end") == CG_OK &&
        cg_famname_read(family) == CG_OK && family[0] != '\0') {
        return family;
    }
    return fallback;
}

std::size_t vertices_per(CGNS_ENUMT(ElementType_t) type) {
    switch (type) {
    case CGNS_ENUMV(BAR_2): return 2;
    case CGNS_ENUMV(TRI_3): return 3;
    case CGNS_ENUMV(QUAD_4): return 4;
    default: throw std::runtime_error("unsupported CGNS element type " + std::to_string(type));
    }
}

double signed_area(const std::array<GlobalIndex, 4>& v, std::uint8_t n,
                   const std::vector<Node>& nodes) {
    double twice = 0.0;
    for (std::uint8_t i = 0; i < n; ++i) {
        const auto& p = nodes.at(static_cast<std::size_t>(v[i]));
        const auto& q = nodes.at(static_cast<std::size_t>(v[(i + 1) % n]));
        twice += p.xy[0] * q.xy[1] - p.xy[1] * q.xy[0];
    }
    return 0.5 * twice;
}

Vec2 polygon_area_centroid(const std::array<GlobalIndex, 4>& vertices,
                           std::uint8_t count, const std::vector<Node>& nodes,
                           double signed_area_value) {
    Vec2 centroid{};
    for (std::uint8_t i = 0; i < count; ++i) {
        const Vec2& p = nodes.at(static_cast<std::size_t>(vertices[i])).xy;
        const Vec2& q =
            nodes.at(static_cast<std::size_t>(vertices[(i + 1) % count])).xy;
        const double cross = p[0] * q[1] - q[0] * p[1];
        centroid[0] += (p[0] + q[0]) * cross;
        centroid[1] += (p[1] + q[1]) * cross;
    }
    const double inverse_six_area = 1.0 / (6.0 * signed_area_value);
    centroid[0] *= inverse_six_area;
    centroid[1] *= inverse_six_area;
    return centroid;
}

} // namespace

void Mesh::clear() noexcept {
    nodes.clear(); cells.clear(); faces.clear(); cell_faces.clear(); adjacency.clear();
    cell_dimension = 0; physical_dimension = 0;
}

Mesh read_cgns_mesh(const std::filesystem::path& filename) {
    int fn = 0;
    check_cgns(cg_open(filename.c_str(), CG_MODE_READ, &fn), "opening CGNS mesh " + filename.string());
    struct Close { int fn; ~Close() { if (fn) cg_close(fn); } } close{fn};

    int nbases = 0;
    check_cgns(cg_nbases(fn, &nbases), "reading CGNS base count");
    if (nbases != 1) throw std::runtime_error("expected exactly one CGNS base");
    int cell_dim = 0, physical_dim = 0;
    char base_name[kCgnsNameBytes]{};
    check_cgns(cg_base_read(fn, 1, base_name, &cell_dim, &physical_dim), "reading CGNS base");
    if (cell_dim != 2) throw std::runtime_error("only 2-D CGNS meshes are supported");

    int nzones = 0;
    check_cgns(cg_nzones(fn, 1, &nzones), "reading CGNS zones");
    if (nzones < 1) throw std::runtime_error("CGNS mesh has no zones");

    std::vector<std::string> zone_names(static_cast<std::size_t>(nzones));
    std::vector<std::size_t> zone_offsets(static_cast<std::size_t>(nzones));
    std::vector<std::size_t> zone_vertex_counts(static_cast<std::size_t>(nzones));
    std::vector<Vec2> raw_nodes;
    std::vector<RawCell> raw_cells;
    std::vector<RawBoundaryEdge> raw_edges;
    std::vector<RawConnection> connections;
    std::map<std::pair<int, cgsize_t>, std::string> boundary_element_tags;

    // First collect all zone coordinates and ZoneBC element-id ranges.  The CGNS
    // point lists are deliberately retained as element IDs rather than section
    // positions, so mixed and separate boundary sections are handled uniformly.
    for (int z = 1; z <= nzones; ++z) {
        char zone_name[kCgnsNameBytes]{};
        cgsize_t size[3]{};
        check_cgns(cg_zone_read(fn, 1, z, zone_name, size), "reading zone");
        CGNS_ENUMT(ZoneType_t) ztype{};
        check_cgns(cg_zone_type(fn, 1, z, &ztype), "reading zone type");
        if (ztype != CGNS_ENUMV(Unstructured)) throw std::runtime_error("structured CGNS zones are unsupported");
        const auto zi = static_cast<std::size_t>(z - 1);
        zone_names[zi] = zone_name;
        zone_offsets[zi] = raw_nodes.size();
        zone_vertex_counts[zi] = static_cast<std::size_t>(size[0]);
        if (size[0] <= 0) throw std::runtime_error("zone has no vertices");

        std::vector<double> x(zone_vertex_counts[zi]), y(zone_vertex_counts[zi]);
        const cgsize_t range_min[1]{1}, range_max[1]{size[0]};
        check_cgns(cg_coord_read(fn, 1, z, "CoordinateX", CGNS_ENUMV(RealDouble), range_min, range_max, x.data()), "reading CoordinateX");
        check_cgns(cg_coord_read(fn, 1, z, "CoordinateY", CGNS_ENUMV(RealDouble), range_min, range_max, y.data()), "reading CoordinateY");
        for (std::size_t i = 0; i < x.size(); ++i) raw_nodes.push_back({x[i], y[i]});

        int nbocos = 0;
        check_cgns(cg_nbocos(fn, 1, z, &nbocos), "reading ZoneBC count");
        for (int bc = 1; bc <= nbocos; ++bc) {
            char name[kCgnsNameBytes]{};
            CGNS_ENUMT(BCType_t) bctype{};
            CGNS_ENUMT(PointSetType_t) point_set{};
            cgsize_t npnts = 0, normal_size = 0;
            int normal_index[3]{}, ndataset = 0;
            CGNS_ENUMT(DataType_t) normal_type{};
            check_cgns(cg_boco_info(fn, 1, z, bc, name, &bctype, &point_set, &npnts,
                                    normal_index, &normal_size, &normal_type, &ndataset), "reading ZoneBC info");
            CGNS_ENUMT(GridLocation_t) location{};
            check_cgns(cg_boco_gridlocation_read(fn, 1, z, bc, &location), "reading ZoneBC location");
            if (location != CGNS_ENUMV(EdgeCenter) && location != CGNS_ENUMV(FaceCenter)) {
                throw std::runtime_error("ZoneBC must be EdgeCenter/FaceCenter for an unstructured 2-D mesh");
            }
            std::vector<cgsize_t> points(static_cast<std::size_t>(npnts));
            check_cgns(cg_boco_read(fn, 1, z, bc, points.data(), nullptr), "reading ZoneBC point set");
            const std::string tag = current_family_name(fn, 1, z, bc, name);
            if (point_set == CGNS_ENUMV(PointRange)) {
                if (points.size() != 2 || points[1] < points[0]) throw std::runtime_error("invalid ZoneBC PointRange");
                for (cgsize_t e = points[0]; e <= points[1]; ++e) boundary_element_tags[{z - 1, e}] = tag;
            } else if (point_set == CGNS_ENUMV(PointList)) {
                for (const auto e : points) boundary_element_tags[{z - 1, e}] = tag;
            } else {
                throw std::runtime_error("unsupported ZoneBC point-set type");
            }
        }
    }

    // Read volume/boundary sections and the explicit multi-zone vertex maps.
    for (int z = 1; z <= nzones; ++z) {
        const int zi = z - 1;
        int nsections = 0;
        check_cgns(cg_nsections(fn, 1, z, &nsections), "reading section count");
        for (int s = 1; s <= nsections; ++s) {
            char section_name[kCgnsNameBytes]{};
            CGNS_ENUMT(ElementType_t) type{};
            cgsize_t start = 0, end = -1;
            int nbndry = 0, parent_flag = 0;
            check_cgns(cg_section_read(fn, 1, z, s, section_name, &type, &start, &end, &nbndry, &parent_flag), "reading section");
            if (end < start) continue;
            const auto count = static_cast<std::size_t>(end - start + 1);
            // TRI_3/QUAD_4/BAR_2 use at most four entries; MIXED requires a
            // leading type code and is constrained here to these same elements.
            std::vector<cgsize_t> connectivity(count * (type == CGNS_ENUMV(MIXED) ? 5U : vertices_per(type)));
            check_cgns(cg_elements_read(fn, 1, z, s, connectivity.data(), nullptr), "reading section connectivity");
            std::size_t cursor = 0;
            for (std::size_t e = 0; e < count; ++e) {
                CGNS_ENUMT(ElementType_t) local_type = type;
                if (type == CGNS_ENUMV(MIXED)) local_type = static_cast<CGNS_ENUMT(ElementType_t)>(connectivity.at(cursor++));
                const auto nv = vertices_per(local_type);
                std::array<std::size_t, 4> v{};
                for (std::size_t k = 0; k < nv; ++k) {
                    const auto node = connectivity.at(cursor++);
                    if (node < 1 || node > static_cast<cgsize_t>(zone_vertex_counts[static_cast<std::size_t>(zi)])) {
                        throw std::runtime_error("section references an invalid vertex");
                    }
                    v[k] = zone_offsets[static_cast<std::size_t>(zi)] + static_cast<std::size_t>(node - 1);
                }
                const cgsize_t element_id = start + static_cast<cgsize_t>(e);
                if (local_type == CGNS_ENUMV(BAR_2)) {
                    raw_edges.push_back({v[0], v[1], zi, element_id});
                } else {
                    raw_cells.push_back({v, static_cast<std::uint8_t>(nv)});
                }
            }
        }

        int nconns = 0;
        check_cgns(cg_nconns(fn, 1, z, &nconns), "reading connectivity count");
        for (int c = 1; c <= nconns; ++c) {
            char name[kCgnsNameBytes]{}, donor[kCgnsNameBytes]{};
            CGNS_ENUMT(GridLocation_t) location{};
            CGNS_ENUMT(GridConnectivityType_t) ctype{};
            CGNS_ENUMT(PointSetType_t) pset{}, donor_pset{};
            CGNS_ENUMT(ZoneType_t) donor_ztype{};
            CGNS_ENUMT(DataType_t) donor_dtype{};
            cgsize_t npnts = 0, ndata = 0;
            check_cgns(cg_conn_info(fn, 1, z, c, name, &location, &ctype, &pset, &npnts,
                                    donor, &donor_ztype, &donor_pset, &donor_dtype, &ndata), "reading zone connectivity");
            if (ctype != CGNS_ENUMV(Abutting1to1) || location != CGNS_ENUMV(Vertex) ||
                pset != CGNS_ENUMV(PointList) || donor_pset != CGNS_ENUMV(PointListDonor) || npnts != ndata) {
                throw std::runtime_error("only vertex PointList Abutting1to1 interfaces are supported");
            }
            RawConnection link;
            link.zone = zi; link.donor_zone = donor;
            link.points.resize(static_cast<std::size_t>(npnts));
            link.donor_points.resize(static_cast<std::size_t>(ndata));
            check_cgns(cg_conn_read(fn, 1, z, c, link.points.data(), donor_dtype, link.donor_points.data()), "reading zone connectivity points");
            connections.push_back(std::move(link));
        }
    }

    DisjointSet vertex_sets(raw_nodes.size());
    std::map<std::string, int> zone_index;
    for (int z = 0; z < nzones; ++z) zone_index.emplace(zone_names[static_cast<std::size_t>(z)], z);
    for (const auto& c : connections) {
        const auto donor_it = zone_index.find(c.donor_zone);
        if (donor_it == zone_index.end()) throw std::runtime_error("CGNS connectivity donor zone not found: " + c.donor_zone);
        const int dz = donor_it->second;
        for (std::size_t i = 0; i < c.points.size(); ++i) {
            if (c.points[i] < 1 || c.points[i] > static_cast<cgsize_t>(zone_vertex_counts[static_cast<std::size_t>(c.zone)]) ||
                c.donor_points[i] < 1 || c.donor_points[i] > static_cast<cgsize_t>(zone_vertex_counts[static_cast<std::size_t>(dz)])) {
                throw std::runtime_error("CGNS connectivity references an invalid vertex");
            }
            const auto a = zone_offsets[static_cast<std::size_t>(c.zone)] + static_cast<std::size_t>(c.points[i] - 1);
            const auto b = zone_offsets[static_cast<std::size_t>(dz)] + static_cast<std::size_t>(c.donor_points[i] - 1);
            const auto& pa = raw_nodes[a]; const auto& pb = raw_nodes[b];
            const double scale = std::max({1.0, std::abs(pa[0]), std::abs(pa[1]), std::abs(pb[0]), std::abs(pb[1])});
            if (std::hypot(pa[0] - pb[0], pa[1] - pb[1]) > 1e-11 * scale) {
                throw std::runtime_error("CGNS PointList/donor coordinates disagree");
            }
            vertex_sets.unite(a, b);
        }
    }

    Mesh mesh;
    mesh.cell_dimension = cell_dim;
    mesh.physical_dimension = physical_dim;
    std::map<std::size_t, GlobalIndex> compact_node;
    std::vector<GlobalIndex> raw_to_global(raw_nodes.size());
    for (std::size_t i = 0; i < raw_nodes.size(); ++i) {
        const auto root = vertex_sets.find(i);
        auto [it, inserted] = compact_node.emplace(root, static_cast<GlobalIndex>(compact_node.size()));
        if (inserted) mesh.nodes.push_back({it->second, raw_nodes[root]});
        raw_to_global[i] = it->second;
    }
    for (std::size_t i = 0; i < raw_cells.size(); ++i) {
        Cell cell; cell.global_id = static_cast<GlobalIndex>(i); cell.vertex_count = raw_cells[i].count;
        for (std::uint8_t j = 0; j < cell.vertex_count; ++j) cell.vertices[j] = raw_to_global[raw_cells[i].vertices[j]];
        const double a = signed_area(cell.vertices, cell.vertex_count, mesh.nodes);
        if (!(a > 0.0) || !std::isfinite(a)) throw std::runtime_error("CGNS cell is not positively oriented in XY");
        cell.area = a;
        cell.centroid =
            polygon_area_centroid(cell.vertices, cell.vertex_count, mesh.nodes, a);
        if (!std::isfinite(cell.centroid[0]) || !std::isfinite(cell.centroid[1])) {
            throw std::runtime_error("CGNS cell has a non-finite area centroid");
        }
        mesh.cells.push_back(cell);
    }

    std::map<EdgeKey, std::string> boundary_tags;
    for (const auto& edge : raw_edges) {
        const auto it = boundary_element_tags.find({edge.zone, edge.element_id});
        if (it != boundary_element_tags.end()) boundary_tags[EdgeKey(raw_to_global[edge.a], raw_to_global[edge.b])] = it->second;
    }

    std::map<EdgeKey, GlobalIndex> face_for_edge;
    mesh.cell_faces.resize(mesh.cells.size());
    mesh.adjacency.resize(mesh.cells.size());
    for (const auto& cell : mesh.cells) {
        for (std::uint8_t i = 0; i < cell.vertex_count; ++i) {
            const auto a = cell.vertices[i], b = cell.vertices[(i + 1) % cell.vertex_count];
            const EdgeKey key(a, b);
            const auto found = face_for_edge.find(key);
            if (found == face_for_edge.end()) {
                Face face; face.global_id = static_cast<GlobalIndex>(mesh.faces.size());
                face.vertices = {a, b}; face.owner = cell.global_id;
                const auto& pa = mesh.nodes[static_cast<std::size_t>(a)].xy;
                const auto& pb = mesh.nodes[static_cast<std::size_t>(b)].xy;
                const double dx = pb[0] - pa[0], dy = pb[1] - pa[1];
                face.length = std::hypot(dx, dy);
                if (!(face.length > 0.0)) throw std::runtime_error("zero-length face");
                face.center = {(pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5};
                face.normal = {dy / face.length, -dx / face.length};
                mesh.faces.push_back(std::move(face));
                face_for_edge.emplace(key, mesh.faces.back().global_id);
                mesh.cell_faces[static_cast<std::size_t>(cell.global_id)].push_back(mesh.faces.back().global_id);
            } else {
                auto& face = mesh.faces[static_cast<std::size_t>(found->second)];
                if (face.neighbor != -1) throw std::runtime_error("non-manifold mesh edge");
                face.neighbor = cell.global_id;
                mesh.cell_faces[static_cast<std::size_t>(cell.global_id)].push_back(face.global_id);
                mesh.adjacency[static_cast<std::size_t>(cell.global_id)].push_back(face.owner);
                mesh.adjacency[static_cast<std::size_t>(face.owner)].push_back(cell.global_id);
            }
        }
    }
    for (auto& face : mesh.faces) {
        if (face.neighbor == -1) {
            const auto it = boundary_tags.find(EdgeKey(face.vertices[0], face.vertices[1]));
            if (it == boundary_tags.end()) throw std::runtime_error("unclassified physical boundary edge");
            face.tag = it->second;
        }
    }
    validate_mesh(mesh);
    return mesh;
}

void validate_mesh(const Mesh& mesh) {
    if (mesh.cell_dimension != 2) throw std::runtime_error("mesh is not 2-D");
    if (mesh.cells.empty() || mesh.nodes.empty() || mesh.faces.empty()) throw std::runtime_error("mesh is empty");
    if (mesh.cell_faces.size() != mesh.cells.size() || mesh.adjacency.size() != mesh.cells.size()) throw std::runtime_error("invalid cell stencil sizes");
    for (const auto& cell : mesh.cells) {
        if (cell.global_id < 0 || cell.vertex_count < 3 || cell.vertex_count > 4 || !(cell.area > 0.0) || !std::isfinite(cell.area)) {
            throw std::runtime_error("invalid cell geometry");
        }
    }
    for (const auto& face : mesh.faces) {
        if (face.owner < 0 || static_cast<std::size_t>(face.owner) >= mesh.cells.size() || !(face.length > 0.0)) throw std::runtime_error("invalid face owner/length");
        if (face.neighbor >= 0) {
            if (static_cast<std::size_t>(face.neighbor) >= mesh.cells.size() || !face.tag.empty()) throw std::runtime_error("invalid interior face");
        } else if (face.tag.empty()) {
            throw std::runtime_error("physical boundary face lacks a family tag");
        }
    }
}

} // namespace cfd
