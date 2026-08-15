#include "mesh.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include <cgnslib.h>

namespace cfd {

namespace {

static void cg_check(int ierr) {
    if (ierr != CG_OK) {
        cg_error_print();
        throw std::runtime_error("CGNS error");
    }
}

// Per-zone scratch data from the CGNS file.
struct RawZone {
    std::string name;
    std::vector<Real> x, y;
    Index n_vert = 0;
    Index n_cell = 0;
    std::vector<std::vector<Index>> cell_nodes;
    std::map<std::string, std::vector<std::pair<Index, Index>>> boundary_edges;

    struct Connection {
        std::string donor_zone;
        std::vector<std::pair<Index, Index>> node_map; // (local, donor) 0-based
    };
    std::vector<Connection> connections;
};

static void read_zone(int fn, int B, int Z, RawZone& rz) {
    char zname[128];
    cgsize_t sizes[9];
    cg_check(cg_zone_read(fn, B, Z, zname, sizes));
    rz.name = zname;
    rz.n_vert = sizes[0];
    rz.n_cell = sizes[1];

    rz.x.resize(rz.n_vert);
    rz.y.resize(rz.n_vert);
    cgsize_t rmin = 1, rmax = rz.n_vert;
    cg_check(cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble),
                            &rmin, &rmax, rz.x.data()));
    cg_check(cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble),
                            &rmin, &rmax, rz.y.data()));

    int nsections;
    cg_check(cg_nsections(fn, B, Z, &nsections));
    rz.cell_nodes.resize(rz.n_cell);

    for (int S = 1; S <= nsections; S++) {
        char sname[128];
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t start, end;
        int nbndry, pflag;
        cg_check(cg_section_read(fn, B, Z, S, sname, &etype, &start, &end, &nbndry, &pflag));

        std::string fam;
        {
            char fam_name[128] = {0};
            if (cg_goto(fn, B, "Zone_t", Z, "Elements_t", S, nullptr) == CG_OK) {
                if (cg_famname_read(fam_name) == CG_OK && fam_name[0]) fam = fam_name;
            }
        }
        if (fam.empty()) fam = sname;

        cgsize_t data_size;
        cg_check(cg_ElementDataSize(fn, B, Z, S, &data_size));
        std::vector<cgsize_t> conn(data_size);
        cg_check(cg_elements_read(fn, B, Z, S, conn.data(), nullptr));

        if (etype == CGNS_ENUMV(TRI_3)) {
            cgsize_t pos = 0;
            for (cgsize_t e = start; e <= end; e++) {
                Index cid = e - 1;
                if (cid < 0 || cid >= rz.n_cell) continue;
                for (int k = 0; k < 3; k++) rz.cell_nodes[cid].push_back(conn[pos++] - 1);
            }
        } else if (etype == CGNS_ENUMV(QUAD_4)) {
            cgsize_t pos = 0;
            for (cgsize_t e = start; e <= end; e++) {
                Index cid = e - 1;
                if (cid < 0 || cid >= rz.n_cell) continue;
                for (int k = 0; k < 4; k++) rz.cell_nodes[cid].push_back(conn[pos++] - 1);
            }
        } else if (etype == CGNS_ENUMV(BAR_2)) {
            for (cgsize_t e = start; e <= end; e++) {
                const cgsize_t pos = (e - start) * 2;
                rz.boundary_edges[fam].push_back({conn[pos] - 1, conn[pos + 1] - 1});
            }
        } else if (etype == CGNS_ENUMV(MIXED)) {
            cgsize_t pos = 0;
            cgsize_t elem_idx = start;
            while (pos < data_size) {
                const CGNS_ENUMT(ElementType_t) mt =
                    (CGNS_ENUMT(ElementType_t))conn[pos++];
                int mnpe = 0;
                if (mt == CGNS_ENUMV(TRI_3)) mnpe = 3;
                else if (mt == CGNS_ENUMV(QUAD_4)) mnpe = 4;
                else if (mt == CGNS_ENUMV(BAR_2)) mnpe = 2;
                else break;
                if (pos + mnpe > data_size) break;
                if (mnpe == 2) {
                    rz.boundary_edges[fam].push_back({conn[pos] - 1, conn[pos + 1] - 1});
                } else {
                    const Index cid = elem_idx - 1;
                    if (cid >= 0 && cid < rz.n_cell) {
                        for (int k = 0; k < mnpe; k++) rz.cell_nodes[cid].push_back(conn[pos + k] - 1);
                    }
                }
                pos += mnpe;
                elem_idx++;
            }
        }
    }

    int nconns;
    if (cg_nconns(fn, B, Z, &nconns) == CG_OK) {
        for (int ic = 1; ic <= nconns; ic++) {
            char cname[128], donor[128];
            CGNS_ENUMT(GridLocation_t) loc;
            CGNS_ENUMT(GridConnectivityType_t) ctype;
            CGNS_ENUMT(PointSetType_t) ptype, donor_ptype;
            CGNS_ENUMT(ZoneType_t) donor_ztype;
            CGNS_ENUMT(DataType_t) dtype;
            cgsize_t npnts, ndata;
            cg_check(cg_conn_info(fn, B, Z, ic, cname, &loc, &ctype, &ptype,
                                    &npnts, donor, &donor_ztype, &donor_ptype,
                                    &dtype, &ndata));
            if (ctype != CGNS_ENUMV(Abutting1to1)) continue;
            std::vector<cgsize_t> pl(npnts), pld(npnts);
            cg_check(cg_conn_read(fn, B, Z, ic, pl.data(), dtype, pld.data()));
            RawZone::Connection conn;
            conn.donor_zone = donor;
            for (cgsize_t i = 0; i < npnts; i++) {
                conn.node_map.push_back({pl[i] - 1, pld[i] - 1});
            }
            rz.connections.push_back(std::move(conn));
        }
    }
}

struct MergedNodes {
    std::vector<Real> x, y;
    std::vector<std::vector<Index>> cell_nodes;
    std::map<std::string, std::vector<std::pair<Index, Index>>> boundary_edges;
    Index n_cells = 0;
};

static MergedNodes merge_zones(const std::vector<RawZone>& zones) {
    MergedNodes mm;
    std::vector<std::vector<Index>> zone_global(zones.size());

    // Start with zone 0
    {
        auto& zg = zone_global[0];
        zg.resize(zones[0].n_vert);
        mm.x = zones[0].x;
        mm.y = zones[0].y;
        for (Index i = 0; i < zones[0].n_vert; i++) zg[i] = i;
        for (Index c = 0; c < zones[0].n_cell; c++) {
            std::vector<Index> gn;
            for (Index n : zones[0].cell_nodes[c]) gn.push_back(n);
            mm.cell_nodes.push_back(std::move(gn));
        }
    }

    std::map<std::string, Index> zone_index;
    for (size_t i = 0; i < zones.size(); i++) zone_index[zones[i].name] = i;

    for (size_t z = 1; z < zones.size(); z++) {
        const auto& zd = zones[z];
        auto& zg = zone_global[z];
        zg.assign(zd.n_vert, -1);

        std::set<Index> seam_nodes;
        for (const auto& conn : zd.connections) {
            auto it = zone_index.find(conn.donor_zone);
            if (it == zone_index.end() || it->second >= z) continue;
            const Index dz = it->second;
            for (const auto& [ln, dn] : conn.node_map) {
                const Index gid = zone_global[dz][dn];
                if (gid < 0) continue;
                if (zg[ln] < 0) zg[ln] = gid;
                seam_nodes.insert(gid);
            }
        }

        for (Index i = 0; i < zd.n_vert; i++) {
            if (zg[i] < 0) {
                zg[i] = (Index)mm.x.size();
                mm.x.push_back(zd.x[i]);
                mm.y.push_back(zd.y[i]);
            } else {
                const Index gid = zg[i];
                const Real dx = mm.x[gid] - zd.x[i];
                const Real dy = mm.y[gid] - zd.y[i];
                if (dx * dx + dy * dy > 1e-10) {
                    zg[i] = (Index)mm.x.size();
                    mm.x.push_back(zd.x[i]);
                    mm.y.push_back(zd.y[i]);
                }
            }
        }

        for (Index c = 0; c < zd.n_cell; c++) {
            std::vector<Index> gn;
            for (Index n : zd.cell_nodes[c]) gn.push_back(zg[n]);
            mm.cell_nodes.push_back(std::move(gn));
        }

        for (const auto& [fam, edges] : zd.boundary_edges) {
            for (const auto& [a, b] : edges) {
                const Index ga = zg[a], gb = zg[b];
                if (ga < 0 || gb < 0) continue;
                bool on_seam = seam_nodes.count(ga) && seam_nodes.count(gb);
                if (!on_seam) {
                    mm.boundary_edges[fam].push_back({ga, gb});
                }
            }
        }
    }

    // Add zone 0's boundary edges, skipping those on 1-to-1 seams.
    {
        std::set<Index> seam_nodes;
        for (size_t z = 1; z < zones.size(); z++) {
            for (const auto& conn : zones[z].connections) {
                auto it = zone_index.find(conn.donor_zone);
                if (it == zone_index.end()) continue;
                const Index dz = it->second;
                if (dz >= z) continue;
                for (const auto& [ln, dn] : conn.node_map) {
                    const Index gid = zone_global[dz][dn];
                    if (gid >= 0) seam_nodes.insert(gid);
                }
            }
        }
        for (const auto& [fam, edges] : zones[0].boundary_edges) {
            for (const auto& [a, b] : edges) {
                const Index ga = zone_global[0][a], gb = zone_global[0][b];
                if (seam_nodes.count(ga) && seam_nodes.count(gb)) continue;
                mm.boundary_edges[fam].push_back({ga, gb});
            }
        }
    }

    mm.n_cells = (Index)mm.cell_nodes.size();
    return mm;
}

struct EdgeKey {
    Index n1, n2;
    EdgeKey(Index a, Index b) : n1(std::min(a, b)), n2(std::max(a, b)) {}
    bool operator<(const EdgeKey& o) const {
        return n1 < o.n1 || (n1 == o.n1 && n2 < o.n2);
    }
};

} // anonymous namespace

GlobalMesh read_mesh(const std::string& filename) {
    int fn, B; // base=1
    char basename[128];
    int cell_dim, phys_dim;
    cg_check(cg_open(filename.c_str(), CG_MODE_READ, &fn));
    int nbases;
    cg_check(cg_nbases(fn, &nbases));
    B = 1;
    cg_check(cg_base_read(fn, B, basename, &cell_dim, &phys_dim));

    int nzones;
    cg_check(cg_nzones(fn, B, &nzones));
    std::vector<RawZone> zones;
    for (int Z = 1; Z <= nzones; Z++) {
        RawZone rz;
        read_zone(fn, B, Z, rz);
        zones.push_back(std::move(rz));
    }
    cg_close(fn);

    MergedNodes mm;
    if (zones.size() == 1) {
        mm.x = zones[0].x;
        mm.y = zones[0].y;
        mm.n_cells = zones[0].n_cell;
        for (Index c = 0; c < zones[0].n_cell; c++) {
            std::vector<Index> gn;
            for (Index n : zones[0].cell_nodes[c]) gn.push_back(n);
            mm.cell_nodes.push_back(std::move(gn));
        }
        for (const auto& [fam, edges] : zones[0].boundary_edges) {
            for (const auto& [a, b] : edges) mm.boundary_edges[fam].push_back({a, b});
        }
    } else {
        mm = merge_zones(zones);
    }

    // ------------------------------------------------------------------
    // Build GlobalMesh from the merged node set
    // ------------------------------------------------------------------
    GlobalMesh mesh;
    const Index n_nodes = (Index)mm.x.size();
    const Index n_cells = mm.n_cells;

    mesh.n_cells = n_cells;
    mesh.nodes.resize(n_nodes);
    for (Index i = 0; i < n_nodes; i++) mesh.nodes[i] = Vec2(mm.x[i], mm.y[i]);
    mesh.cells.resize(n_cells);
    for (Index i = 0; i < n_cells; i++) mesh.cells[i].id = i;

    // Cell centroids and volumes
    for (Index c = 0; c < n_cells; c++) {
        const auto& nodes = mm.cell_nodes[c];
        auto& cell = mesh.cells[c];
        cell.nodes = nodes;
        const int nn = (int)nodes.size();
        std::vector<Vec2> verts(nn);
        for (int k = 0; k < nn; k++) verts[k] = Vec2(mm.x[nodes[k]], mm.y[nodes[k]]);
        if (nn == 3) {
            cell.centroid = (verts[0] + verts[1] + verts[2]) / 3.0;
            cell.volume = 0.5 * std::abs(
                (verts[1][0] - verts[0][0]) * (verts[2][1] - verts[0][1]) -
                (verts[1][1] - verts[0][1]) * (verts[2][0] - verts[0][0]));
        } else if (nn == 4) {
            cell.centroid = (verts[0] + verts[1] + verts[2] + verts[3]) / 4.0;
            const Real a1 = 0.5 * std::abs(
                (verts[1][0] - verts[0][0]) * (verts[2][1] - verts[0][1]) -
                (verts[1][1] - verts[0][1]) * (verts[2][0] - verts[0][0]));
            const Real a2 = 0.5 * std::abs(
                (verts[2][0] - verts[0][0]) * (verts[3][1] - verts[0][1]) -
                (verts[2][1] - verts[0][1]) * (verts[3][0] - verts[0][0]));
            cell.volume = a1 + a2;
        } else {
            Vec2 cen = Vec2::Zero();
            for (const auto& v : verts) cen += v;
            cen /= (Real)nn;
            Real vol = 0.0;
            for (int k = 0; k < nn; k++) {
                const Vec2& a = verts[k];
                const Vec2& b = verts[(k + 1) % nn];
                vol += 0.5 * std::abs(
                    (a[0] - cen[0]) * (b[1] - cen[1]) -
                    (a[1] - cen[1]) * (b[0] - cen[0]));
            }
            cell.centroid = cen;
            cell.volume = vol;
        }
        if (cell.volume <= 0.0) throw std::runtime_error("non-positive cell volume");
    }

    // Edge-to-cells map
    std::map<EdgeKey, std::vector<Index>> edge_to_cells;
    for (Index c = 0; c < n_cells; c++) {
        const auto& nodes = mm.cell_nodes[c];
        const int nn = (int)nodes.size();
        for (int k = 0; k < nn; k++) {
            edge_to_cells[EdgeKey(nodes[k], nodes[(k + 1) % nn])].push_back(c);
        }
    }

    // Boundary edge family mapping
    std::map<EdgeKey, std::string> bnd_edge_fam;
    for (const auto& [fam, edges] : mm.boundary_edges) {
        for (const auto& [a, b] : edges) {
            EdgeKey ek(a, b);
            if (!bnd_edge_fam.count(ek)) bnd_edge_fam[ek] = fam;
        }
    }

    // Tag-to-id map
    Index next_tag = 1;
    std::map<std::string, Index> tag_to_id;
    auto get_tag = [&](const std::string& tag) {
        auto it = tag_to_id.find(tag);
        if (it != tag_to_id.end()) return it->second;
        tag_to_id[tag] = next_tag;
        return next_tag++;
    };

    // Build faces
    Index face_id = 0;
    for (const auto& [ek, cells] : edge_to_cells) {
        std::set<Index> us(cells.begin(), cells.end());
        if (us.size() < 2) continue;
        const std::vector<Index> ucv(us.begin(), us.end());
        const Index c0 = ucv[0], c1 = ucv[1];

        Face face;
        face.id = face_id++;
        face.left = c0;
        face.right = c1;
        const Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
        const Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
        face.centroid = (p1 + p2) * 0.5;
        Vec2 edge_vec = p2 - p1;
        Vec2 normal(-edge_vec[1], edge_vec[0]);
        const Vec2 to_right = mesh.cells[c1].centroid - mesh.cells[c0].centroid;
        if (normal.dot(to_right) < 0.0) normal = -normal;
        face.normal = normal;
        face.bc_tag = 0;
        mesh.faces.push_back(face);
        mesh.cells[c0].faces.push_back(face.id);
        mesh.cells[c0].neighbors.push_back(c1);
        mesh.cells[c1].faces.push_back(face.id);
        mesh.cells[c1].neighbors.push_back(c0);
    }

    for (const auto& [ek, tag] : bnd_edge_fam) {
        auto it = edge_to_cells.find(ek);
        if (it == edge_to_cells.end() || it->second.empty()) continue;
        const Index cell_id = it->second[0];

        Face face;
        face.id = face_id++;
        face.left = cell_id;
        face.right = -1;
        const Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
        const Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
        face.centroid = (p1 + p2) * 0.5;
        Vec2 edge_vec = p2 - p1;
        Vec2 normal(-edge_vec[1], edge_vec[0]);
        const Vec2 to_face = face.centroid - mesh.cells[cell_id].centroid;
        if (normal.dot(to_face) < 0.0) normal = -normal;
        face.normal = normal;
        face.bc_tag = get_tag(tag);
        mesh.faces.push_back(face);
        mesh.cells[cell_id].faces.push_back(face.id);
        mesh.boundary_families[tag].push_back(face.id);
    }

    // Single-edge boundary faces without explicit tags (safety)
    for (const auto& [ek, cells] : edge_to_cells) {
        if (bnd_edge_fam.count(ek)) continue;
        std::set<Index> us(cells.begin(), cells.end());
        if (us.size() == 1) {
            const Index cell_id = *us.begin();
            Face face;
            face.id = face_id++;
            face.left = cell_id;
            face.right = -1;
            const Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
            const Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
            face.centroid = (p1 + p2) * 0.5;
            Vec2 edge_vec = p2 - p1;
            Vec2 normal(-edge_vec[1], edge_vec[0]);
            const Vec2 to_face = face.centroid - mesh.cells[cell_id].centroid;
            if (normal.dot(to_face) < 0.0) normal = -normal;
            face.normal = normal;
            face.bc_tag = get_tag("unknown");
            mesh.faces.push_back(face);
            mesh.cells[cell_id].faces.push_back(face.id);
            mesh.boundary_families["unknown"].push_back(face.id);
        }
    }

    mesh.n_faces = (Index)mesh.faces.size();
    mesh.n_boundary_faces = 0;
    for (const auto& [_, fids] : mesh.boundary_families) mesh.n_boundary_faces += (Index)fids.size();
    mesh.tag_of_family = tag_to_id;

    std::cout << "Mesh loaded: " << mesh.n_cells << " cells, "
              << mesh.n_faces << " faces, " << mesh.n_boundary_faces << " boundary faces\n";
    for (const auto& [fam, fids] : mesh.boundary_families) {
        std::cout << "  Boundary family '" << fam << "': " << fids.size() << " faces\n";
    }

    return mesh;
}

} // namespace cfd
