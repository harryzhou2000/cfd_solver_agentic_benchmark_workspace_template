#include "cgns_mesh.hpp"

#include <cgnslib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace {
[[noreturn]] void cgfail(const std::string& what) {
    throw std::runtime_error("CGNS error in " + what + ": " + cg_get_error());
}

int nodes_per_elem(ElementType_t et) {
    switch (et) {
    case BAR_2: return 2;
    case TRI_3: return 3;
    case QUAD_4: return 4;
    default: return 0;
    }
}

// Hash key for geometric node merging.
struct NodeKey {
    long long a, b;
    bool operator==(const NodeKey& o) const { return a == o.a && b == o.b; }
};
struct NodeKeyHash {
    size_t operator()(const NodeKey& k) const {
        return std::hash<long long>()(k.a * 0x9E3779B97F4A7C15ULL ^ k.b * 0xC2B2AE3D27D4EB4FULL);
    }
};

struct EdgeKey {
    int a, b; // a < b
    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};
struct EdgeKeyHash {
    size_t operator()(const EdgeKey& k) const {
        return std::hash<long long>()((long long)k.a * 0x100000001B3LL ^ (long long)k.b);
    }
};
} // namespace

void SerialMesh::build_faces(const std::vector<std::array<int, 2>>& bedge_nodes,
                             const std::vector<int>& bedge_family,
                             const std::vector<std::string>& family_names) {
    bc_names = family_names;
    std::unordered_map<EdgeKey, int, EdgeKeyHash> edge_face; // edge -> face index
    edge_face.reserve(num_cells * 2);

    faces.clear();
    faces.reserve(num_cells * 2);
    static const int tri_edges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
    static const int quad_edges[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};

    for (int c = 0; c < num_cells; c++) {
        int nn = cell_nnodes[c];
        for (int e = 0; e < nn; e++) {
            int l1 = (nn == 3) ? tri_edges[e][0] : quad_edges[e][0];
            int l2 = (nn == 3) ? tri_edges[e][1] : quad_edges[e][1];
            int a = cell_nodes[c][l1], b = cell_nodes[c][l2];
            EdgeKey key{std::min(a, b), std::max(a, b)};
            auto it = edge_face.find(key);
            if (it == edge_face.end()) {
                Face f;
                f.n1 = a; f.n2 = b; f.c0 = c; f.c1 = -1; f.bc_id = -1;
                edge_face.emplace(key, (int)faces.size());
                faces.push_back(f);
            } else {
                Face& f = faces[it->second];
                if (f.c1 != -1)
                    throw std::runtime_error("mesh error: edge shared by more than two cells");
                f.c1 = c;
            }
        }
    }
    num_faces = (int)faces.size();

    // match boundary edges to faces by node pair
    std::unordered_map<EdgeKey, int, EdgeKeyHash> bedge_map;
    bedge_map.reserve(bedge_nodes.size());
    for (size_t i = 0; i < bedge_nodes.size(); i++) {
        EdgeKey key{std::min(bedge_nodes[i][0], bedge_nodes[i][1]),
                    std::max(bedge_nodes[i][0], bedge_nodes[i][1])};
        bedge_map.emplace(key, (int)i);
    }
    int n_boundary = 0, n_unmatched_bedges = 0;
    for (auto& f : faces) {
        if (f.c1 == -1) {
            EdgeKey key{std::min(f.n1, f.n2), std::max(f.n1, f.n2)};
            auto it = bedge_map.find(key);
            if (it != bedge_map.end()) {
                f.bc_id = bedge_family[it->second];
                bedge_map.erase(it);
            }
            n_boundary++;
        }
    }
    n_unmatched_bedges = (int)bedge_map.size();
    if (n_unmatched_bedges > 0)
        throw std::runtime_error("mesh error: " + std::to_string(n_unmatched_bedges) +
                                 " boundary elements did not match any cell edge "
                                 "(non-conformal zone interface?)");
    // every boundary face should carry a family tag
    for (auto& f : faces)
        if (f.c1 == -1 && f.bc_id < 0)
            throw std::runtime_error("mesh error: boundary face without a family tag "
                                     "(check CGNS boundary sections)");
}

void SerialMesh::build_adjacency() {
    xadj.assign(num_cells + 1, 0);
    for (const auto& f : faces) {
        if (f.c1 >= 0) {
            xadj[f.c0 + 1]++;
            xadj[f.c1 + 1]++;
        }
    }
    for (int i = 0; i < num_cells; i++) xadj[i + 1] += xadj[i];
    adjncy.resize(xadj[num_cells]);
    std::vector<int> pos(xadj.begin(), xadj.end());
    for (const auto& f : faces) {
        if (f.c1 >= 0) {
            adjncy[pos[f.c0]++] = f.c1;
            adjncy[pos[f.c1]++] = f.c0;
        }
    }
    num_adj_edges = (long)adjncy.size() / 2;
}

SerialMesh read_cgns_mesh(const std::string& path) {
    int fn;
    if (cg_open(path.c_str(), CG_MODE_READ, &fn)) cgfail("cg_open(" + path + ")");

    SerialMesh mesh;
    std::vector<std::array<int, 2>> bedge_nodes;
    std::vector<int> bedge_family;
    std::vector<std::string> family_names;
    std::map<std::string, int> family_id;

    auto fam_index = [&](const std::string& name) {
        auto it = family_id.find(name);
        if (it != family_id.end()) return it->second;
        int id = (int)family_names.size();
        family_id[name] = id;
        family_names.push_back(name);
        return id;
    };

    int nbases;
    if (cg_nbases(fn, &nbases)) cgfail("cg_nbases");

    // First pass: count nodes to determine merge tolerance from domain size.
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    std::vector<std::vector<double>> zone_x, zone_y;
    std::vector<int> zone_nnodes;

    for (int b = 1; b <= nbases; b++) {
        char basename[128];
        int cdim, pdim;
        if (cg_base_read(fn, b, basename, &cdim, &pdim)) cgfail("cg_base_read");
        if (cdim != 2) throw std::runtime_error("only 2-D meshes supported (cell_dim != 2)");
        int nzones;
        if (cg_nzones(fn, b, &nzones)) cgfail("cg_nzones");
        for (int z = 1; z <= nzones; z++) {
            ZoneType_t zt;
            if (cg_zone_type(fn, b, z, &zt)) cgfail("cg_zone_type");
            if (zt != Unstructured)
                throw std::runtime_error("only unstructured zones supported");
            char zonename[128];
            cgsize_t size[9];
            if (cg_zone_read(fn, b, z, zonename, size)) cgfail("cg_zone_read");
            cgsize_t nn = size[0];
            std::vector<double> xs(nn), ys(nn);
            cgsize_t one = 1;
            if (cg_coord_read(fn, b, z, "CoordinateX", RealDouble, &one, &nn, xs.data()))
                cgfail("cg_coord_read X");
            if (cg_coord_read(fn, b, z, "CoordinateY", RealDouble, &one, &nn, ys.data()))
                cgfail("cg_coord_read Y");
            for (auto x : xs) { xmin = std::min(xmin, x); xmax = std::max(xmax, x); }
            for (auto y : ys) { ymin = std::min(ymin, y); ymax = std::max(ymax, y); }
            zone_x.push_back(std::move(xs));
            zone_y.push_back(std::move(ys));
            zone_nnodes.push_back((int)nn);
        }
    }

    const double domain = std::max(xmax - xmin, ymax - ymin);
    const double tol = domain * 1e-9;
    if (tol <= 0.0) throw std::runtime_error("degenerate mesh extent");

    std::unordered_map<NodeKey, int, NodeKeyHash> node_map;
    node_map.reserve(1 << 16);

    // Second pass: read cells and boundary elements, merging coincident nodes.
    int zi = 0;
    for (int b = 1; b <= nbases; b++) {
        int nzones;
        cg_nzones(fn, b, &nzones);
        for (int z = 1; z <= nzones; z++, zi++) {
            int nn = zone_nnodes[zi];
            std::vector<int> l2g(nn);
            for (int i = 0; i < nn; i++) {
                NodeKey key{llround(zone_x[zi][i] / tol), llround(zone_y[zi][i] / tol)};
                auto it = node_map.find(key);
                if (it == node_map.end()) {
                    int gid = mesh.num_nodes++;
                    node_map.emplace(key, gid);
                    mesh.node_x.push_back(zone_x[zi][i]);
                    mesh.node_y.push_back(zone_y[zi][i]);
                    l2g[i] = gid;
                } else {
                    l2g[i] = it->second;
                }
            }

            int nsec;
            if (cg_nsections(fn, b, z, &nsec)) cgfail("cg_nsections");
            for (int s = 1; s <= nsec; s++) {
                char secname[128];
                ElementType_t et;
                cgsize_t start, end;
                int nboundary, parent;
                if (cg_section_read(fn, b, z, s, secname, &et, &start, &end, &nboundary, &parent))
                    cgfail("cg_section_read");
                int npe = nodes_per_elem(et);
                if (npe == 0) continue; // skip non-2-node/2-D elements silently
                cgsize_t nelem = end - start + 1;
                std::vector<cgsize_t> conn(nelem * npe);
                if (cg_elements_read(fn, b, z, s, conn.data(), nullptr))
                    cgfail("cg_elements_read");
                if (et == BAR_2) {
                    // boundary/interface segment: named by section; look up its family
                    std::string famname;
                    if (cg_goto(fn, b, "Zone_t", z, "Elements_t", s, "end") == CG_OK) {
                        char fam[128];
                        if (cg_famname_read(fam) == CG_OK) famname = fam;
                    }
                    if (famname.empty()) famname = secname; // fall back to section name
                    // sections named con-* are zone interfaces: after node merging
                    // these edges coincide with interior faces and are ignored.
                    if (famname.rfind("con-", 0) == 0) continue;
                    int fid = fam_index(famname);
                    for (cgsize_t e = 0; e < nelem; e++) {
                        int g1 = l2g[conn[2 * e] - 1];
                        int g2 = l2g[conn[2 * e + 1] - 1];
                        bedge_nodes.push_back({g1, g2});
                        bedge_family.push_back(fid);
                    }
                } else {
                    for (cgsize_t e = 0; e < nelem; e++) {
                        std::array<int, 4> cn{-1, -1, -1, -1};
                        for (int k = 0; k < npe; k++) cn[k] = l2g[conn[e * npe + k] - 1];
                        mesh.cell_nodes.push_back(cn);
                        mesh.cell_nnodes.push_back(npe);
                        mesh.cell_zone.push_back(z);
                        mesh.num_cells++;
                    }
                }
            }
        }
    }
    cg_close(fn);

    mesh.build_faces(bedge_nodes, bedge_family, family_names);
    mesh.build_adjacency();
    return mesh;
}
