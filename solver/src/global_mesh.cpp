#include "global_mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace cfd {

namespace {

struct NodePairKey {
    uint64_t xa = 0, ya = 0, xb = 0, yb = 0;
    bool operator==(const NodePairKey& o) const {
        return xa == o.xa && ya == o.ya && xb == o.xb && yb == o.yb;
    }
};

struct NodePairKeyHash {
    size_t operator()(const NodePairKey& k) const {
        uint64_t h = 0x9e3779b97f4a7c15ULL;
        const auto mix = [&h](uint64_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(k.xa);
        mix(k.ya);
        mix(k.xb);
        mix(k.yb);
        return static_cast<size_t>(h);
    }
};

uint64_t bits(double v) {
    uint64_t u;
    static_assert(sizeof(u) == sizeof(v), "double must be 8 bytes");
    std::memcpy(&u, &v, sizeof(u));
    return u;
}

NodePairKey make_key(double ax, double ay, double bx, double by) {
    uint64_t xa = bits(ax), ya = bits(ay), xb = bits(bx), yb = bits(by);
    // Canonical ordering on the unsigned bit pattern so both cell records of
    // one shared edge produce the identical key.
    const bool swap_ab = (xa > xb) || (xa == xb && ya > yb);
    if (swap_ab) {
        return {xb, yb, xa, ya};
    }
    return {xa, ya, xb, yb};
}

struct EdgeRecord {
    int cell = -1;
    int local_edge = -1;
    int64_t n1 = 0;
    int64_t n2 = 0;
};

[[noreturn]] void mesh_error(const std::string& file, const std::string& msg) {
    throw std::runtime_error("mesh '" + file + "': " + msg);
}

int cgns_check(int status, const std::string& file, const std::string& what) {
    if (status != CG_OK) {
        mesh_error(file, what + " failed (cg_status=" + std::to_string(status) +
                              ")");
    }
    return status;
}

double polygon_area(const std::vector<Vec2>& p) {
    double a = 0.0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& p0 = p[i];
        const Vec2& p1 = p[(i + 1) % p.size()];
        a += p0.cross(p1);
    }
    return 0.5 * a;
}

Vec2 polygon_center(const std::vector<Vec2>& p) {
    // Area-weighted centroid via triangle fan decomposition. This is the
    // standard centroid for straight-edged polygons and is exact for
    // triangles; the vertex average is not a centroid for general polygons.
    double area2 = 0.0;
    Vec2 c{0.0, 0.0};
    for (size_t i = 1; i + 1 < p.size(); ++i) {
        const Vec2 a = p[0], b = p[i], d = p[i + 1];
        const double tri2 = (b - a).cross(d - a);  // 2 * triangle area
        area2 += tri2;
        c += (a + b + d) * (tri2 / 3.0);
    }
    if (std::fabs(area2) < 1e-300) {
        c = Vec2{0.0, 0.0};
        for (const auto& v : p) c += v;
        c = c / static_cast<double>(p.size());
        return c;
    }
    return c / area2;
}

}  // namespace

GlobalMesh read_cgns_mesh(const std::string& path,
                          const std::map<std::string, BcType>& bc_map) {
    GlobalMesh mesh;
    int fid = -1;
    cgns_check(cg_open(path.c_str(), CG_MODE_READ, &fid), path, "cg_open");

    int nbases = 0;
    cgns_check(cg_nbases(fid, &nbases), path, "cg_nbases");
    if (nbases < 1) mesh_error(path, "file has no CGNS bases");

    // Per-zone coordinate store. Node ids are made globally unique by
    // offsetting zone-local ids.
    std::vector<std::vector<double>> zone_x, zone_y;
    std::vector<int64_t> zone_offset;  // 0-based offset per zone (0, n1, ...)

    std::vector<EdgeRecord> edge_records;
    // BC sections: key -> family/section name, discovered while reading
    // sections. The name is resolved to a solver BC type through bc_map.
    std::unordered_map<NodePairKey, std::string, NodePairKeyHash> bc_keys;

    // Collect zone info first so we can offset node ids consistently.
    struct ZoneInfo {
        int base = 0;
        int zone = 0;
        int64_t nnodes = 0;
    };
    std::vector<ZoneInfo> zones;

    for (int b = 1; b <= nbases; ++b) {
        char basename[65] = {0};
        int cell_dim = 0, phys_dim = 0;
        cgns_check(cg_base_read(fid, b, basename, &cell_dim, &phys_dim), path,
                   "cg_base_read");
        int nz = 0;
        cgns_check(cg_nzones(fid, b, &nz), path, "cg_nzones");
        for (int z = 1; z <= nz; ++z) {
            char zname[65] = {0};
            cgsize_t sizes[9] = {0};
            cgns_check(cg_zone_read(fid, b, z, zname, sizes), path,
                       "cg_zone_read");
            CGNS_ENUMT(ZoneType_t) ztype;
            cgns_check(cg_zone_type(fid, b, z, &ztype), path, "cg_zone_type");
            if (ztype != CGNS_ENUMV(Unstructured))
                mesh_error(path,
                           "zone '" + std::string(zname) +
                               "' is not unstructured");
            zones.push_back({b, z, static_cast<int64_t>(sizes[0])});
        }
    }

    int64_t offset = 0;
    for (auto& zi : zones) {
        zone_offset.push_back(offset);

        int64_t nn = zi.nnodes;
        std::vector<double> x(nn), y(nn);
        // Coordinate arrays may be RealSingle or RealDouble; read as double.
        cgsize_t one = 1;
        const bool have_x =
            cg_coord_read(fid, zi.base, zi.zone, "CoordinateX",
                          CGNS_ENUMV(RealDouble), &one, &nn, x.data()) == CG_OK;
        const bool have_y =
            cg_coord_read(fid, zi.base, zi.zone, "CoordinateY",
                          CGNS_ENUMV(RealDouble), &one, &nn, y.data()) == CG_OK;
        if (!have_x || !have_y) {
            std::vector<float> xf(nn), yf(nn);
            if (cg_coord_read(fid, zi.base, zi.zone, "CoordinateX",
                              CGNS_ENUMV(RealSingle), &one, &nn, xf.data()) ==
                    CG_OK &&
                cg_coord_read(fid, zi.base, zi.zone, "CoordinateY",
                              CGNS_ENUMV(RealSingle), &one, &nn, yf.data()) ==
                    CG_OK) {
                for (int64_t i = 0; i < nn; ++i) {
                    x[i] = xf[i];
                    y[i] = yf[i];
                }
            } else {
                mesh_error(path, "zone missing CoordinateX/CoordinateY");
            }
        }

        // Read element sections.
        int nsections = 0;
        cgns_check(cg_nsections(fid, zi.base, zi.zone, &nsections), path,
                   "cg_nsections");
        for (int s = 1; s <= nsections; ++s) {
            char secname[65] = {0};
            CGNS_ENUMT(ElementType_t) etype;
            cgsize_t start = 0, end = 0;
            int nbndry = 0, parent_flag = 0;
            cgns_check(cg_section_read(fid, zi.base, zi.zone, s, secname, &etype,
                                       &start, &end, &nbndry, &parent_flag),
                       path, "cg_section_read");
            const int64_t ncell = static_cast<int64_t>(end - start + 1);
            if (etype == CGNS_ENUMV(TRI_3) || etype == CGNS_ENUMV(QUAD_4)) {
                const int nodes_per = (etype == CGNS_ENUMV(TRI_3)) ? 3 : 4;
                cgsize_t datasize = 0;
                cgns_check(cg_ElementDataSize(fid, zi.base, zi.zone, s,
                                              &datasize),
                           path, "cg_ElementDataSize");
                std::vector<cgsize_t> conn(datasize);
                cgns_check(cg_elements_read(fid, zi.base, zi.zone, s, conn.data(),
                                            nullptr),
                           path, "cg_elements_read");
                for (int64_t e = 0; e < ncell; ++e) {
                    GlobalCell cell;
                    std::vector<Vec2> pts;
                    for (int k = 0; k < nodes_per; ++k) {
                        const int64_t local_node =
                            static_cast<int64_t>(conn[e * nodes_per + k]);
                        const int64_t gid = offset + local_node;
                        cell.nodes.push_back(gid);
                        pts.push_back({x[local_node - 1], y[local_node - 1]});
                    }
                    cell.center = polygon_center(pts);
                    cell.volume = std::fabs(polygon_area(pts));
                    if (!(cell.volume > 0.0))
                        mesh_error(path, "degenerate (zero-area) cell");
                    const int cell_id = static_cast<int>(mesh.cells.size());
                    mesh.cells.push_back(std::move(cell));
                    for (int k = 0; k < nodes_per; ++k) {
                        const int k2 = (k + 1) % nodes_per;
                        const int64_t n1 = mesh.cells[cell_id].nodes[k];
                        const int64_t n2 = mesh.cells[cell_id].nodes[k2];
                        edge_records.push_back({cell_id, k, n1, n2});
                    }
                }
            } else if (etype == CGNS_ENUMV(BAR_2)) {
                const std::string name(secname);
                const auto it = bc_map.find(name);
                if (it == bc_map.end()) continue;  // e.g. 1-to-1 con-* sections
                cgsize_t datasize = 0;
                cgns_check(cg_ElementDataSize(fid, zi.base, zi.zone, s,
                                              &datasize),
                           path, "cg_ElementDataSize (bc)");
                std::vector<cgsize_t> conn(datasize);
                cgns_check(cg_elements_read(fid, zi.base, zi.zone, s, conn.data(),
                                            nullptr),
                           path, "cg_elements_read (bc)");
                for (int64_t e = 0; e < ncell; ++e) {
                    const int64_t ln1 = static_cast<int64_t>(conn[2 * e]);
                    const int64_t ln2 = static_cast<int64_t>(conn[2 * e + 1]);
                    const int64_t i1 = ln1 - 1;
                    const int64_t i2 = ln2 - 1;
                    if (i1 < 0 || i1 >= nn || i2 < 0 || i2 >= nn)
                        mesh_error(path, "BC edge node index out of range");
                    const auto key = make_key(x[i1], y[i1], x[i2], y[i2]);
                    bc_keys[key] = name;
                }
            }
        }

        // Store coordinates for the face-matching phase.
        zone_x.push_back(std::move(x));
        zone_y.push_back(std::move(y));
        offset += nn;
    }
    cg_close(fid);

    if (mesh.cells.empty()) mesh_error(path, "no cells read from mesh");

    // Global node coordinates for geometry.
    mesh.node_x.resize(offset);
    mesh.node_y.resize(offset);
    for (size_t z = 0; z < zones.size(); ++z) {
        for (size_t i = 0; i < zone_x[z].size(); ++i) {
            const int64_t gid = zone_offset[z] + static_cast<int64_t>(i);
            mesh.node_x[gid] = zone_x[z][i];
            mesh.node_y[gid] = zone_y[z][i];
        }
    }

    // Match faces. For every cell edge build the coordinate key and accumulate
    // records; two records -> interior, one -> boundary.
    std::unordered_map<NodePairKey, std::vector<size_t>, NodePairKeyHash> key_to_records;
    for (size_t r = 0; r < edge_records.size(); ++r) {
        const EdgeRecord& er = edge_records[r];
        const int64_t i1 = er.n1 - 1;
        const int64_t i2 = er.n2 - 1;
        const auto key =
            make_key(mesh.node_x[i1], mesh.node_y[i1], mesh.node_x[i2],
                     mesh.node_y[i2]);
        key_to_records[key].push_back(r);
    }

    // Cell geometry is needed to orient outward normals.
    std::vector<std::vector<Vec2>> cell_corners(mesh.cells.size());
    for (size_t c = 0; c < mesh.cells.size(); ++c) {
        for (const int64_t gid : mesh.cells[c].nodes) {
            cell_corners[c].push_back(
                {mesh.node_x[gid - 1], mesh.node_y[gid - 1]});
        }
    }

    mesh.boundary_names.clear();
    std::map<std::string, int> name_to_id;
    const auto name_id = [&](const std::string& n) -> int {
        auto it = name_to_id.find(n);
        if (it != name_to_id.end()) return it->second;
        const int id = static_cast<int>(mesh.boundary_names.size());
        mesh.boundary_names.push_back(n);
        name_to_id[n] = id;
        return id;
    };

    int missing_bc = 0;
    int64_t total_interior = 0;
    for (const auto& [key, records] : key_to_records) {
        (void)key;
        const EdgeRecord& r0 = edge_records[records[0]];
        const int64_t i1 = r0.n1 - 1;
        const int64_t i2 = r0.n2 - 1;
        const Vec2 p1{mesh.node_x[i1], mesh.node_y[i1]};
        const Vec2 p2{mesh.node_x[i2], mesh.node_y[i2]};
        const Vec2 fcenter = (p1 + p2) * 0.5;
        const double area = (p2 - p1).norm();

        GlobalFace face;
        face.center = fcenter;
        face.area = area;

        if (records.size() == 2) {
            const EdgeRecord& ra = edge_records[records[0]];
            const EdgeRecord& rb = edge_records[records[1]];
            face.left = ra.cell;
            face.right = rb.cell;
            // Outward normal from the left cell's edge record.
            const GlobalCell& lc = mesh.cells[face.left];
            const Vec2 a = cell_corners[face.left][ra.local_edge];
            const Vec2 b =
                cell_corners[face.left][(ra.local_edge + 1) %
                                        cell_corners[face.left].size()];
            Vec2 n{-(b.y - a.y), b.x - a.x};
            n = n.normalized();
            if (n.dot(fcenter - lc.center) < 0.0) n = -n;
            face.n = n;
            face.bc = BcType::Interior;
            mesh.faces.push_back(std::move(face));
            ++total_interior;
        } else if (records.size() == 1) {
            const EdgeRecord& ra = edge_records[records[0]];
            face.left = ra.cell;
            face.right = -1;
            const GlobalCell& lc = mesh.cells[face.left];
            const Vec2 a = cell_corners[face.left][ra.local_edge];
            const Vec2 b =
                cell_corners[face.left][(ra.local_edge + 1) %
                                        cell_corners[face.left].size()];
            Vec2 n{-(b.y - a.y), b.x - a.x};
            n = n.normalized();
            if (n.dot(fcenter - lc.center) < 0.0) n = -n;
            face.n = n;

            // Look up the boundary family for this edge.
            const auto bit = bc_keys.find(make_key(p1.x, p1.y, p2.x, p2.y));
            if (bit != bc_keys.end()) {
                const std::string& tag = bit->second;
                const auto type_it = bc_map.find(tag);
                if (type_it == bc_map.end()) {
                    throw std::runtime_error(
                        "mesh '" + path + "': boundary section '" + tag +
                        "' is not present in the case boundary_conditions map");
                }
                face.bc = type_it->second;
                face.bc_name_id = name_id(tag);
            } else {
                face.bc = BcType::Unknown;
                face.bc_name_id = -1;
                ++missing_bc;
            }
            mesh.boundary_faces.push_back(face);
            mesh.faces.push_back(std::move(face));
        } else {
            mesh_error(path,
                       "face shared by more than two cells (non-conforming or "
                       "duplicated mesh)");
        }
    }

    if (missing_bc > 0) {
        throw std::runtime_error(
            "mesh '" + path + "': " + std::to_string(missing_bc) +
            " boundary faces have no matching boundary section in the case "
            "file");
    }

    // Sanity checks on tag coverage: every wall face found, and both supplied
    // cases must have at least one farfield face.
    std::map<BcType, int> bc_counts;
    for (const auto& f : mesh.boundary_faces) ++bc_counts[f.bc];
    bool has_farfield = bc_counts.count(BcType::Farfield) > 0;
    bool has_wall = bc_counts.count(BcType::SlipWall) > 0 ||
                    bc_counts.count(BcType::NoSlipAdiabaticWall) > 0;
    if (!has_farfield) mesh_error(path, "no farfield boundary faces found");
    if (!has_wall) mesh_error(path, "no wall boundary faces found");

    return mesh;
}

}  // namespace cfd
