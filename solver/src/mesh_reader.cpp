#include "mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "cgnslib.h"

namespace cfd {

BCType parse_bc_type(const std::string& s) {
    if (s == "farfield") return BCType::Farfield;
    if (s == "slip_wall" || s == "inviscid_wall" || s == "euler_wall") return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall" || s == "no_slip_wall" || s == "viscous_wall" ||
        s == "wall") return BCType::NoSlipAdiabaticWall;
    return BCType::Invalid;
}

namespace {

struct ZoneData {
    std::vector<double> x, y;
    // element connectivity per 2-D section
    std::vector<std::vector<int>> cell_conn;   // node indices (zone-local, 1-based from CGNS)
    std::vector<int> cell_type;                // 3 or 4
    // boundary face sections: name -> connectivity (pairs of zone-local node indices)
    std::vector<std::pair<std::string, std::vector<std::pair<int, int>>>> boco_faces;
};

void check_cgns(int ierr, const char* what) {
    if (ierr != CG_OK) {
        const char* msg = cg_get_error();
        throw std::runtime_error(std::string("CGNS error in ") + what + ": " + (msg ? msg : "unknown"));
    }
}

std::vector<ZoneData> read_zones(const std::string& filename, int& cell_dim_out,
                                 int& phys_dim_out, std::string& base_name_out) {
    int index_file = 0;
    check_cgns(cg_open(filename.c_str(), CG_MODE_READ, &index_file), "cg_open");

    int nbases = 0;
    check_cgns(cg_nbases(index_file, &nbases), "cg_nbases");
    if (nbases < 1) throw std::runtime_error("CGNS file has no base node");
    int B = 1;
    int cell_dim = 0, phys_dim = 0;
    char basename[33];
    check_cgns(cg_base_read(index_file, B, basename, &cell_dim, &phys_dim), "cg_base_read");
    cell_dim_out = cell_dim;
    phys_dim_out = phys_dim;
    base_name_out = basename;

    int nzones = 0;
    check_cgns(cg_nzones(index_file, B, &nzones), "cg_nzones");
    std::vector<ZoneData> zones(nzones);

    for (int z = 1; z <= nzones; ++z) {
        char zonename[33];
        cgsize_t sizes[3];
        ZoneType_t ztype;
        check_cgns(cg_zone_read(index_file, B, z, zonename, sizes), "cg_zone_read");
        check_cgns(cg_zone_type(index_file, B, z, &ztype), "cg_zone_type");
        if (ztype != Unstructured) {
            throw std::runtime_error("Only unstructured CGNS zones are supported");
        }
        cgsize_t n_nodes = sizes[0];
        cgsize_t n_cells = sizes[1];
        (void)n_cells;
        ZoneData& zd = zones[z - 1];
        zd.x.resize(n_nodes);
        zd.y.resize(n_nodes);

        // Coordinates (X and Y; Z is ignored for 2-D).
        {
            cgsize_t rmin = 1, rmax = n_nodes;
            check_cgns(cg_coord_read(index_file, B, z, "CoordinateX", RealDouble, &rmin, &rmax,
                                     zd.x.data()),
                       "cg_coord_read X");
            check_cgns(cg_coord_read(index_file, B, z, "CoordinateY", RealDouble, &rmin, &rmax,
                                     zd.y.data()),
                       "cg_coord_read Y");
        }

        int nsections = 0;
        check_cgns(cg_nsections(index_file, B, z, &nsections), "cg_nsections");
        for (int s = 1; s <= nsections; ++s) {
            char secname[33];
            ElementType_t et;
            cgsize_t estart, eend;
            int nbndry, parent_flag;
            check_cgns(cg_section_read(index_file, B, z, s, secname, &et, &estart, &eend,
                                       &nbndry, &parent_flag),
                       "cg_section_read");
            cgsize_t nelem = eend - estart + 1;
            int nnodes_per_elem = 0;
            switch (et) {
                case TRI_3:
                    nnodes_per_elem = 3;
                    break;
                case QUAD_4:
                    nnodes_per_elem = 4;
                    break;
                case BAR_2:
                    nnodes_per_elem = 2;
                    break;
                default:
                    // Skip unsupported element types (e.g. MIXED, NODE, HEXA...) but warn.
                    std::fprintf(stderr, "[mesh] zone %d section '%s' type %d skipped\n", z,
                                 secname, (int)et);
                    continue;
            }
            std::vector<cgsize_t> conn((size_t)nelem * nnodes_per_elem);
            check_cgns(cg_elements_read(index_file, B, z, s, conn.data(), nullptr),
                       "cg_elements_read");
            if (nnodes_per_elem == 3 || nnodes_per_elem == 4) {
                for (cgsize_t e = 0; e < nelem; ++e) {
                    std::vector<int> nodes(nnodes_per_elem);
                    for (int k = 0; k < nnodes_per_elem; ++k) {
                        nodes[k] = (int)conn[(size_t)e * nnodes_per_elem + k];
                    }
                    zd.cell_conn.push_back(std::move(nodes));
                    zd.cell_type.push_back(nnodes_per_elem);
                }
            } else {  // BAR_2: boundary or interface face section
                std::vector<std::pair<int, int>> edges;
                edges.reserve(nelem);
                for (cgsize_t e = 0; e < nelem; ++e) {
                    edges.emplace_back((int)conn[(size_t)e * 2],
                                       (int)conn[(size_t)e * 2 + 1]);
                }
                std::string nm(secname);
                // Interface sections named con-* or containing 'con' are internal
                // and are ignored; faces are reconstructed from cell adjacency.
                bool is_interface = (nm.rfind("con-", 0) == 0) || (nm.find("conn") != std::string::npos);
                if (!is_interface) {
                    zd.boco_faces.emplace_back(nm, std::move(edges));
                }
            }
        }
        std::fprintf(stderr, "[mesh] zone %s: %zu nodes, %zu cells, %zu boco sections\n",
                     zonename, zd.x.size(), zd.cell_conn.size(), zd.boco_faces.size());
    }

    check_cgns(cg_close(index_file), "cg_close");
    return zones;
}

}  // namespace

GlobalMesh read_cgns_mesh(const std::string& filename,
                          const std::vector<std::pair<std::string, std::string>>& bc_map) {
    int cell_dim = 0, phys_dim = 0;
    std::string base_name;
    auto zones = read_zones(filename, cell_dim, phys_dim, base_name);
    if (zones.empty()) throw std::runtime_error("CGNS file contains no zones");

    // BC lookup by family/section name.
    std::unordered_map<std::string, BCType> bc_lookup;
    for (auto& [name, type_str] : bc_map) {
        bc_lookup[name] = parse_bc_type(type_str);
    }

    GlobalMesh gm;

    // ------------------------------------------------------------------
    // 1. Merge nodes across zones (dedupe by coordinate).
    // ------------------------------------------------------------------
    auto node_key = [](double x, double y) -> uint64_t {
        auto q = [](double v) -> uint64_t {
            double s = std::round(v * 1e9);
            return (uint64_t)(int64_t)s;
        };
        return q(x) * 0x9E3779B97F4A7C15ULL ^ q(y);
    };
    struct NodeRec {
        double x, y;
        int count = 1;
    };
    std::vector<NodeRec> node_records;
    std::unordered_map<uint64_t, std::vector<int>> node_hash;
    std::vector<std::vector<int>> zone_node_map(zones.size());

    for (size_t z = 0; z < zones.size(); ++z) {
        auto& zd = zones[z];
        zone_node_map[z].resize(zd.x.size());
        for (size_t i = 0; i < zd.x.size(); ++i) {
            double x = zd.x[i], y = zd.y[i];
            uint64_t key = node_key(x, y);
            int found = -1;
            auto it = node_hash.find(key);
            if (it != node_hash.end()) {
                for (int nid : it->second) {
                    if (std::abs(node_records[nid].x - x) < 1e-9 &&
                        std::abs(node_records[nid].y - y) < 1e-9) {
                        found = nid;
                        break;
                    }
                }
            }
            if (found < 0) {
                found = (int)node_records.size();
                node_records.push_back({x, y, 1});
                node_hash[key].push_back(found);
            } else {
                node_records[found].count++;
            }
            zone_node_map[z][i] = found;
        }
    }
    gm.num_nodes_global = (int)node_records.size();
    gm.node_x.resize(gm.num_nodes_global);
    gm.node_y.resize(gm.num_nodes_global);
    for (int i = 0; i < gm.num_nodes_global; ++i) {
        gm.node_x[i] = node_records[i].x;
        gm.node_y[i] = node_records[i].y;
    }

    // ------------------------------------------------------------------
    // 2. Cells.
    // ------------------------------------------------------------------
    for (size_t z = 0; z < zones.size(); ++z) {
        auto& zd = zones[z];
        for (size_t c = 0; c < zd.cell_conn.size(); ++c) {
            std::vector<int> gnodes;
            gnodes.reserve(zd.cell_type[c]);
            for (int n : zd.cell_conn[c]) {
                gnodes.push_back(zone_node_map[z][n - 1]);
            }
            gm.cell_nodes.push_back(std::move(gnodes));
            gm.cell_type.push_back(zd.cell_type[c]);
        }
    }
    gm.num_cells_global = (int)gm.cell_nodes.size();

    // Orient cells counterclockwise (positive area).
    auto signed_area = [&gm](int c) {
        const auto& nds = gm.cell_nodes[c];
        double a = 0.0;
        int nn = (int)nds.size();
        for (int i = 0; i < nn; ++i) {
            int j = (i + 1) % nn;
            a += gm.node_x[nds[i]] * gm.node_y[nds[j]] -
                 gm.node_y[nds[i]] * gm.node_x[nds[j]];
        }
        return 0.5 * a;
    };
    for (int c = 0; c < gm.num_cells_global; ++c) {
        double a = signed_area(c);
        if (a < 0.0) {
            std::reverse(gm.cell_nodes[c].begin(), gm.cell_nodes[c].end());
            a = -a;
        }
        if (a <= 1e-20) {
            throw std::runtime_error("degenerate cell with zero area in mesh");
        }
        gm.cell_vol.push_back(a);
        double cx = 0.0, cy = 0.0;
        for (int n : gm.cell_nodes[c]) {
            cx += gm.node_x[n];
            cy += gm.node_y[n];
        }
        gm.cell_cx.push_back(cx / gm.cell_nodes[c].size());
        gm.cell_cy.push_back(cy / gm.cell_nodes[c].size());
        gm.domain_area += a;
    }

    // ------------------------------------------------------------------
    // 3. Boundary faces from boco sections.
    // ------------------------------------------------------------------
    // Map canonical undirected edge (node pair) -> BC type + family.
    auto edge_key = [](int a, int b) -> uint64_t {
        return ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
    };
    std::unordered_map<uint64_t, std::pair<BCType, std::string>> boundary_edges;
    for (size_t z = 0; z < zones.size(); ++z) {
        auto& zd = zones[z];
        for (auto& [name, edges] : zd.boco_faces) {
            BCType bc = BCType::Invalid;
            auto it = bc_lookup.find(name);
            if (it != bc_lookup.end()) {
                bc = it->second;
            } else {
                std::fprintf(stderr, "[mesh] warning: boundary family '%s' not in case BC map\n",
                             name.c_str());
            }
            for (auto& [a, b] : edges) {
                int ga = zone_node_map[z][a - 1];
                int gb = zone_node_map[z][b - 1];
                boundary_edges[edge_key(std::min(ga, gb), std::max(ga, gb))] = {bc, name};
            }
        }
    }

    // ------------------------------------------------------------------
    // 4. Cell-edge -> adjacent cells map.
    // ------------------------------------------------------------------
    struct EdgeCells {
        int c0 = -1;
        int c1 = -1;
        int na = -1, nb = -1;
        int e0a = -1, e0b = -1;  // oriented edge as traversed in cell c0
    };
    std::unordered_map<uint64_t, EdgeCells> edge_map;
    for (int c = 0; c < gm.num_cells_global; ++c) {
        const auto& nds = gm.cell_nodes[c];
        int nn = (int)nds.size();
        for (int i = 0; i < nn; ++i) {
            int a = nds[i], b = nds[(i + 1) % nn];
            uint64_t key = edge_key(std::min(a, b), std::max(a, b));
            auto it = edge_map.find(key);
            if (it == edge_map.end()) {
                edge_map[key] = {c, -1, std::min(a, b), std::max(a, b), a, b};
            } else {
                if (it->second.c1 >= 0) {
                    throw std::runtime_error("non-manifold mesh: edge shared by >2 cells");
                }
                it->second.c1 = c;
            }
        }
    }

    // ------------------------------------------------------------------
    // 5. Build faces.
    // ------------------------------------------------------------------
    gm.cell_faces.resize(gm.num_cells_global);
    for (auto& [key, ec] : edge_map) {
        GlobalMesh::Face f;
        f.c0 = ec.c0;
        f.c1 = ec.c1;
        f.b0 = ec.na;
        f.b1 = ec.nb;
        f.global_face_id = (int)gm.faces.size();

        // length and orientation (edge as traversed in c0: e0a -> e0b)
        double dx = gm.node_x[ec.e0b] - gm.node_x[ec.e0a];
        double dy = gm.node_y[ec.e0b] - gm.node_y[ec.e0a];
        f.len = std::sqrt(dx * dx + dy * dy);
        if (ec.c1 >= 0) {
            // internal: normal from c0 to c1 (edge oriented as stored in c0)
            // For CCW cell c0, outward normal is (dy, -dx) / len.
            double nx = dy / f.len, ny = -dx / f.len;
            double dxx = gm.cell_cx[ec.c1] - gm.cell_cx[ec.c0];
            double dyy = gm.cell_cy[ec.c1] - gm.cell_cy[ec.c0];
            if (nx * dxx + ny * dyy < 0.0) {
                nx = -nx;
                ny = -ny;
            }
            f.nx = nx;
            f.ny = ny;
        } else {
            // boundary: normal must point out of the domain (away from c0).
            double nx = dy / f.len, ny = -dx / f.len;
            double mx = 0.5 * (gm.node_x[ec.na] + gm.node_x[ec.nb]);
            double my = 0.5 * (gm.node_y[ec.na] + gm.node_y[ec.nb]);
            double dxx = gm.cell_cx[ec.c0] - mx;
            double dyy = gm.cell_cy[ec.c0] - my;
            if (nx * dxx + ny * dyy > 0.0) {
                nx = -nx;
                ny = -ny;
            }
            f.nx = nx;
            f.ny = ny;
            auto bit = boundary_edges.find(key);
            if (bit == boundary_edges.end()) {
                throw std::runtime_error("mesh edge has no neighbor cell and no boundary tag");
            }
            f.bc = bit->second.first;
            f.family = bit->second.second;
            gm.num_boundary_faces++;
        }
        int fid = (int)gm.faces.size();
        gm.faces.push_back(f);
        gm.cell_faces[ec.c0].push_back(fid);
        if (ec.c1 >= 0) gm.cell_faces[ec.c1].push_back(fid);
    }
    gm.num_faces_global = (int)gm.faces.size();

    return gm;
}

}  // namespace cfd
