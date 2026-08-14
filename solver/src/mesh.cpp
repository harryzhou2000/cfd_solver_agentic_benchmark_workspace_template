#include "mesh.hpp"
#include <cgnslib.h>
#include <cassert>
#include <cstring>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <numeric>
#include <cmath>
#include <limits>

namespace cfd {

static void cgns_check(int ierr) {
    if (ierr != CG_OK) {
        cg_error_print();
        throw std::runtime_error("CGNS error");
    }
}

// A single zone with its geometry and element connectivity
struct ZoneData {
    std::string name;
    std::vector<real_t> x, y;
    idx_t n_vertices;
    idx_t n_cells;
    // cell connectivity (0-based local node ids)
    std::vector<std::vector<idx_t>> cell_nodes;
    // boundary faces per family: list of (node0, node1) 0-based
    std::map<std::string, std::vector<std::pair<idx_t, idx_t>>> boundary_edges;
    // 1to1 connections: donor zone name and mapping (this-zone-node -> donor-zone-node)
    struct Connection {
        std::string donor_zone;
        std::vector<std::pair<idx_t, idx_t>> node_map; // (local_node, donor_node) 0-based
    };
    std::vector<Connection> connections;
};

static std::string zone_name_clean(const std::string& s) {
    // strip anything after whitespace
    return s;
}

// Read a single CGNS zone into ZoneData
static void read_zone(int fn, int B, int Z, ZoneData& zd) {
    char zname[128];
    cgsize_t sizes[9];
    cgns_check(cg_zone_read(fn, B, Z, zname, sizes));
    zd.name = zname;
    zd.n_vertices = sizes[0];
    zd.n_cells = sizes[1];

    zd.x.resize(zd.n_vertices);
    zd.y.resize(zd.n_vertices);
    cgsize_t rmin = 1, rmax = zd.n_vertices;
    cgns_check(cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.x.data()));
    cgns_check(cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.y.data()));

    // sections
    int nsections;
    cgns_check(cg_nsections(fn, B, Z, &nsections));
    zd.cell_nodes.resize(zd.n_cells);

    for (int S = 1; S <= nsections; S++) {
        char sname[128];
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t start, end;
        int nbndry, pflag;
        cgns_check(cg_section_read(fn, B, Z, S, sname, &etype, &start, &end, &nbndry, &pflag));

        // family name
        std::string fam;
        {
            char fam_name[128] = {0};
            // try the section's family name
            if (cg_goto(fn, B, "Zone_t", Z, "Elements_t", S, nullptr) == CG_OK) {
                if (cg_famname_read(fam_name) == CG_OK && fam_name[0]) fam = fam_name;
            }
        }
        if (fam.empty()) fam = sname;

        cgsize_t data_size;
        cgns_check(cg_ElementDataSize(fn, B, Z, S, &data_size));
        std::vector<cgsize_t> conn(data_size);
        cgns_check(cg_elements_read(fn, B, Z, S, conn.data(), nullptr));

        int npe = 0;
        if (etype == CGNS_ENUMV(TRI_3)) npe = 3;
        else if (etype == CGNS_ENUMV(QUAD_4)) npe = 4;
        else if (etype == CGNS_ENUMV(BAR_2)) npe = 2;
        else if (etype == CGNS_ENUMV(MIXED)) npe = -1;
        else continue;

        if (npe == 3 || npe == 4) {
            cgsize_t pos = 0;
            for (cgsize_t e = start; e <= end; e++) {
                idx_t cid = e - 1;
                if (cid < 0 || cid >= zd.n_cells) continue;
                auto& nodes = zd.cell_nodes[cid];
                for (int k = 0; k < npe; k++) nodes.push_back(conn[pos++] - 1);
            }
        } else if (npe == 2) {
            for (cgsize_t e = start; e <= end; e++) {
                cgsize_t pos = (e - start) * 2;
                zd.boundary_edges[fam].push_back({conn[pos] - 1, conn[pos+1] - 1});
            }
        } else if (npe == -1) {
            // mixed section
            cgsize_t pos = 0;
            cgsize_t elem_idx = start;
            while (pos < data_size) {
                CGNS_ENUMT(ElementType_t) mt = (CGNS_ENUMT(ElementType_t))conn[pos++];
                int mnpe = 0;
                if (mt == CGNS_ENUMV(TRI_3)) mnpe = 3;
                else if (mt == CGNS_ENUMV(QUAD_4)) mnpe = 4;
                else if (mt == CGNS_ENUMV(BAR_2)) mnpe = 2;
                else break;
                if (pos + mnpe > data_size) break;
                if (mnpe == 2) {
                    zd.boundary_edges[fam].push_back({conn[pos]-1, conn[pos+1]-1});
                } else {
                    idx_t cid = elem_idx - 1;
                    if (cid >= 0 && cid < zd.n_cells) {
                        auto& nodes = zd.cell_nodes[cid];
                        for (int k = 0; k < mnpe; k++) nodes.push_back(conn[pos+k]-1);
                    }
                }
                pos += mnpe;
                elem_idx++;
            }
        }
    }

    // 1to1 connections
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
            cgns_check(cg_conn_info(fn, B, Z, ic, cname, &loc, &ctype, &ptype,
                                    &npnts, donor, &donor_ztype, &donor_ptype,
                                    &dtype, &ndata));
            if (ctype != CGNS_ENUMV(Abutting1to1)) continue;
            // read point lists
            std::vector<cgsize_t> pl(npnts), pld(npnts);
            cgns_check(cg_conn_read(fn, B, Z, ic, pl.data(), dtype, pld.data()));
            ZoneData::Connection conn;
            conn.donor_zone = donor;
            for (cgsize_t i = 0; i < npnts; i++) {
                conn.node_map.push_back({pl[i]-1, pld[i]-1});
            }
            zd.connections.push_back(conn);
        }
    }
}

// Build the merged mesh from multiple zones, stitching 1to1 connections.
// The approach: build a global node list from zone 1, then for each subsequent
// zone, identify which of its boundary faces coincide with donor-zone nodes via
// the 1to1 connection node map, and add only the zone's interior cells, merging
// duplicate nodes.
struct MergedMesh {
    std::vector<real_t> x, y;
    std::vector<std::vector<idx_t>> cell_nodes; // global node ids (0-based)
    std::map<std::string, std::vector<std::pair<idx_t,idx_t>>> boundary_edges; // global node ids
    idx_t n_cells;
};

static MergedMesh merge_zones(const std::vector<ZoneData>& zones) {
    MergedMesh mm;
    // Node merge map: for each zone, mapping local node -> global node
    std::vector<std::vector<idx_t>> zone_global(zones.size());

    // Start with zone 0 nodes
    {
        auto& zg = zone_global[0];
        zg.resize(zones[0].n_vertices);
        mm.x = zones[0].x;
        mm.y = zones[0].y;
        for (idx_t i = 0; i < zones[0].n_vertices; i++) zg[i] = i;
        for (idx_t c = 0; c < zones[0].n_cells; c++) {
            std::vector<idx_t> gnodes;
            for (idx_t n : zones[0].cell_nodes[c]) gnodes.push_back((idx_t)n);
            mm.cell_nodes.push_back(gnodes);
        }
    }

    // Process zone 1..N-1: resolve donor nodes via 1to1 connections.
    // Each zone's boundary faces that are on a 1to1 connection should map to
    // donor nodes already in the merged mesh.
    for (size_t z = 1; z < zones.size(); z++) {
        const auto& zd = zones[z];
        auto& zg = zone_global[z];
        zg.assign(zd.n_vertices, -1);

        // Build a map: for each donor node id (0-based in donor zone), what's
        // the global id. First resolve through the chain: the donor may be in
        // any already-processed zone.
        // For simplicity, build a lookup from (donor zone index, donor node) -> global
        // by scanning all 1to1 connections of this zone.
        std::map<std::pair<idx_t,idx_t>, idx_t> donor_to_global;
        // Find donor zone indices
        std::map<std::string, idx_t> zone_index;
        for (size_t i = 0; i < zones.size(); i++) zone_index[zones[i].name] = i;

        // First pass: map all connection node pairs
        for (const auto& conn : zd.connections) {
            auto it = zone_index.find(conn.donor_zone);
            if (it == zone_index.end()) continue;
            idx_t dz = it->second;
            if (dz >= z) continue; // donor must be processed
            for (const auto& [ln, dn] : conn.node_map) {
                idx_t gid = zone_global[dz][dn];
                if (gid < 0) continue;
                if (zg[ln] < 0) zg[ln] = gid;
                else if (zg[ln] != gid) {
                    // conflicting mapping - should not happen for valid mesh
                }
            }
        }

        // Now add remaining nodes as new global nodes
        for (idx_t i = 0; i < zd.n_vertices; i++) {
            if (zg[i] < 0) {
                zg[i] = (idx_t)mm.x.size();
                mm.x.push_back(zd.x[i]);
                mm.y.push_back(zd.y[i]);
            } else {
                // verify coordinate match
                idx_t gid = zg[i];
                real_t dx = mm.x[gid] - zd.x[i];
                real_t dy = mm.y[gid] - zd.y[i];
                if (dx*dx + dy*dy > 1e-10) {
                    // node mapped but coordinates differ - keep as new node
                    zg[i] = (idx_t)mm.x.size();
                    mm.x.push_back(zd.x[i]);
                    mm.y.push_back(zd.y[i]);
                }
            }
        }

        // Add cells
        for (idx_t c = 0; c < zd.n_cells; c++) {
            std::vector<idx_t> gnodes;
            for (idx_t n : zd.cell_nodes[c]) gnodes.push_back(zg[n]);
            mm.cell_nodes.push_back(gnodes);
        }

        // Boundary edges: only keep those NOT on a 1to1 connection (i.e. real
        // boundary faces). A boundary face is on a connection if both its nodes
        // are mapped to donor nodes via the connection node map.
        std::set<std::pair<idx_t,idx_t>> connected_node_pairs;
        for (const auto& conn : zd.connections) {
            for (const auto& [ln, dn] : conn.node_map) {
                connected_node_pairs.insert({zg[ln], zg[dn]});
            }
        }

        for (const auto& [fam, edges] : zd.boundary_edges) {
            for (const auto& [a, b] : edges) {
                idx_t ga = zg[a], gb = zg[b];
                if (ga < 0 || gb < 0) continue;
                // check if this edge is a connection face: if both endpoints
                // are connected nodes and the edge appears in the connection
                bool is_conn = false;
                // An edge is a connection edge if both endpoints map to nodes
                // that are connected across the seam. Since the connection is
                // node-based, we need to know which edges were on the seam.
                // The 1to1 connection list gives node pairs, and the seam
                // boundary section (con-2..6) gives edges whose nodes are all
                // in the connected set.
                bool a_conn = false, b_conn = false;
                for (const auto& conn : zd.connections) {
                    for (const auto& [ln, dn] : conn.node_map) {
                        if (zg[ln] == ga) a_conn = true;
                        if (zg[ln] == gb) b_conn = true;
                    }
                }
                if (a_conn && b_conn) is_conn = true;
                if (!is_conn) {
                    mm.boundary_edges[fam].push_back({ga, gb});
                }
            }
        }
    }

    mm.n_cells = (idx_t)mm.cell_nodes.size();
    return mm;
}

Mesh build_mesh_from_merged(const MergedMesh& mm) {
    Mesh mesh;
    idx_t n_nodes = (idx_t)mm.x.size();
    idx_t n_cells = mm.n_cells;

    mesh.n_cells = n_cells;
    mesh.nodes.resize(n_nodes);
    for (idx_t i = 0; i < n_nodes; i++) mesh.nodes[i] = Vec2(mm.x[i], mm.y[i]);
    mesh.cells.resize(n_cells);
    for (idx_t i = 0; i < n_cells; i++) mesh.cells[i].id = i;

    // cell centroids and volumes
    for (idx_t c = 0; c < n_cells; c++) {
        auto& nodes = mm.cell_nodes[c];
        auto& cell = mesh.cells[c];
        cell.node_ids = nodes;
        int nn = (int)nodes.size();
        std::vector<Vec2> verts(nn);
        for (int k = 0; k < nn; k++) verts[k] = Vec2(mm.x[nodes[k]], mm.y[nodes[k]]);
        if (nn == 3) {
            cell.centroid = (verts[0] + verts[1] + verts[2]) / 3.0;
            cell.volume = 0.5 * std::abs((verts[1][0]-verts[0][0])*(verts[2][1]-verts[0][1])
                                       - (verts[1][1]-verts[0][1])*(verts[2][0]-verts[0][0]));
        } else if (nn == 4) {
            cell.centroid = (verts[0] + verts[1] + verts[2] + verts[3]) / 4.0;
            real_t a1 = 0.5 * std::abs((verts[1][0]-verts[0][0])*(verts[2][1]-verts[0][1])
                                      - (verts[1][1]-verts[0][1])*(verts[2][0]-verts[0][0]));
            real_t a2 = 0.5 * std::abs((verts[2][0]-verts[0][0])*(verts[3][1]-verts[0][1])
                                      - (verts[2][1]-verts[0][1])*(verts[3][0]-verts[0][0]));
            cell.volume = a1 + a2;
        } else {
            // polygon: decompose into triangles from centroid
            Vec2 cen(0,0);
            for (auto& v : verts) cen += v;
            cen /= nn;
            real_t vol = 0;
            for (int k = 0; k < nn; k++) {
                const Vec2& a = verts[k];
                const Vec2& b = verts[(k+1)%nn];
                vol += 0.5 * std::abs((a[0]-cen[0])*(b[1]-cen[1]) - (a[1]-cen[1])*(b[0]-cen[0]));
            }
            cell.centroid = cen;
            cell.volume = vol;
        }
        if (cell.volume <= 0) throw std::runtime_error("Non-positive cell volume");
    }

    // Build edge -> cell map
    struct EdgeKey {
        idx_t n1, n2;
        EdgeKey(idx_t a, idx_t b) : n1(std::min(a,b)), n2(std::max(a,b)) {}
        bool operator<(const EdgeKey& o) const {
            return n1 < o.n1 || (n1 == o.n1 && n2 < o.n2);
        }
    };
    std::map<EdgeKey, std::vector<idx_t>> edge_to_cells;
    for (idx_t c = 0; c < n_cells; c++) {
        auto& nodes = mm.cell_nodes[c];
        int nn = (int)nodes.size();
        for (int k = 0; k < nn; k++) {
            EdgeKey ek(nodes[k], nodes[(k+1)%nn]);
            edge_to_cells[ek].push_back(c);
        }
    }

    // boundary edge -> family
    std::map<EdgeKey, std::string> boundary_edge_family;
    for (const auto& [fam, edges] : mm.boundary_edges) {
        for (const auto& [a, b] : edges) {
            EdgeKey ek(a, b);
            if (boundary_edge_family.count(ek)) {
                // keep the first; families should not overlap
            } else {
                boundary_edge_family[ek] = fam;
            }
        }
    }

    // Also detect edges that appear once (not covered by explicit boundary
    // sections) - these are mesh boundary faces without tags. For the supplied
    // meshes this should not happen, but be robust.
    std::map<EdgeKey, idx_t> edge_count;
    for (auto& [ek, cells] : edge_to_cells) {
        edge_count[ek] = (idx_t)cells.size();
    }

    // Build faces
    mesh.faces.clear();
    mesh.boundary_families.clear();
    idx_t face_counter = 0;
    std::map<std::string, idx_t> tag_to_bc_id;
    idx_t next_tag = 1;
    auto get_tag_id = [&](const std::string& tag) -> idx_t {
        auto it = tag_to_bc_id.find(tag);
        if (it != tag_to_bc_id.end()) return it->second;
        tag_to_bc_id[tag] = next_tag;
        return next_tag++;
    };

    // interior faces
    for (auto& [ek, cells] : edge_to_cells) {
        if (cells.size() < 2) continue;
        // dedupe
        std::set<idx_t> us(cells.begin(), cells.end());
        if (us.size() < 2) continue;
        std::vector<idx_t> ucv(us.begin(), us.end());
        // only use the first two (a valid mesh has exactly 2)
        idx_t c0 = ucv[0], c1 = ucv[1];
        if (c0 >= n_cells || c1 >= n_cells) continue;

        Face face;
        face.id = face_counter++;
        face.left_cell = c0;
        face.right_cell = c1;
        Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
        Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
        face.centroid = (p1 + p2) * 0.5;
        Vec2 edge_vec = p2 - p1;
        Vec2 edge_normal(-edge_vec[1], edge_vec[0]);
        Vec2 to_right = mesh.cells[c1].centroid - mesh.cells[c0].centroid;
        if (edge_normal.dot(to_right) < 0) edge_normal = -edge_normal;
        face.normal = edge_normal;
        face.bc_tag = 0;
        mesh.faces.push_back(face);
        mesh.cells[c0].face_ids.push_back(face.id);
        mesh.cells[c0].neighbor_ids.push_back(c1);
        mesh.cells[c1].face_ids.push_back(face.id);
        mesh.cells[c1].neighbor_ids.push_back(c0);
    }

    // boundary faces
    for (auto& [ek, tag] : boundary_edge_family) {
        auto it = edge_to_cells.find(ek);
        if (it == edge_to_cells.end() || it->second.empty()) continue;
        idx_t cell_id = it->second[0];
        if (cell_id >= n_cells) continue;

        Face face;
        face.id = face_counter++;
        face.left_cell = cell_id;
        face.right_cell = -1;
        Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
        Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
        face.centroid = (p1 + p2) * 0.5;
        Vec2 edge_vec = p2 - p1;
        Vec2 edge_normal(-edge_vec[1], edge_vec[0]);
        Vec2 to_face = face.centroid - mesh.cells[cell_id].centroid;
        if (edge_normal.dot(to_face) < 0) edge_normal = -edge_normal;
        face.normal = edge_normal;
        face.bc_tag = get_tag_id(tag);
        mesh.faces.push_back(face);
        mesh.cells[cell_id].face_ids.push_back(face.id);
        mesh.boundary_families[tag].push_back(face.id);
    }

    // Untagged single edges (safety)
    for (auto& [ek, cells] : edge_to_cells) {
        if (boundary_edge_family.count(ek)) continue;
        std::set<idx_t> us(cells.begin(), cells.end());
        if (us.size() == 1) {
            idx_t cell_id = *us.begin();
            Face face;
            face.id = face_counter++;
            face.left_cell = cell_id;
            face.right_cell = -1;
            Vec2 p1(mm.x[ek.n1], mm.y[ek.n1]);
            Vec2 p2(mm.x[ek.n2], mm.y[ek.n2]);
            face.centroid = (p1 + p2) * 0.5;
            Vec2 edge_vec = p2 - p1;
            Vec2 edge_normal(-edge_vec[1], edge_vec[0]);
            Vec2 to_face = face.centroid - mesh.cells[cell_id].centroid;
            if (edge_normal.dot(to_face) < 0) edge_normal = -edge_normal;
            face.normal = edge_normal;
            face.bc_tag = get_tag_id("unknown");
            mesh.faces.push_back(face);
            mesh.cells[cell_id].face_ids.push_back(face.id);
            mesh.boundary_families["unknown"].push_back(face.id);
        }
    }

    mesh.n_faces = mesh.faces.size();
    mesh.n_boundary_faces = 0;
    for (auto& [tag, fids] : mesh.boundary_families) mesh.n_boundary_faces += fids.size();
    mesh.bc_tag_map = tag_to_bc_id;

    return mesh;
}

Mesh read_cgns_mesh(const std::string& filename) {
    int fn, B, Z;
    char basename[128];
    int cell_dim, phys_dim;
    cgns_check(cg_open(filename.c_str(), CG_MODE_READ, &fn));

    int nbases;
    cgns_check(cg_nbases(fn, &nbases));
    B = 1;
    cgns_check(cg_base_read(fn, B, basename, &cell_dim, &phys_dim));

    int nzones;
    cgns_check(cg_nzones(fn, B, &nzones));

    std::vector<ZoneData> zones;
    for (Z = 1; Z <= nzones; Z++) {
        ZoneData zd;
        read_zone(fn, B, Z, zd);
        zones.push_back(zd);
    }
    cg_close(fn);

    MergedMesh mm;
    if (zones.size() == 1) {
        mm.x = zones[0].x;
        mm.y = zones[0].y;
        mm.n_cells = zones[0].n_cells;
        for (idx_t c = 0; c < zones[0].n_cells; c++) {
            std::vector<idx_t> gn;
            for (idx_t n : zones[0].cell_nodes[c]) gn.push_back(n);
            mm.cell_nodes.push_back(gn);
        }
        for (auto& [fam, edges] : zones[0].boundary_edges) {
            for (auto& [a, b] : edges) mm.boundary_edges[fam].push_back({a, b});
        }
    } else {
        mm = merge_zones(zones);
        // merge_zones processes zones 1..N-1; zone 0's own boundary edges were
        // never added to the merged boundary-edge set. Add them now, skipping
        // any edge that lies on a 1to1 seam (both nodes connected to another
        // zone through a connection).
        std::set<idx_t> seam_nodes;
        std::vector<std::vector<idx_t>> zg(zones.size());
        zg[0].resize(zones[0].n_vertices);
        for (idx_t i = 0; i < zones[0].n_vertices; i++) zg[0][i] = i;
        for (size_t z = 1; z < zones.size(); z++) {
            zg[z].assign(zones[z].n_vertices, -1);
            for (const auto& conn : zones[z].connections) {
                for (size_t zi = 0; zi < zones.size(); zi++) {
                    if (zones[zi].name == conn.donor_zone) {
                        for (const auto& [ln, dn] : conn.node_map) {
                            zg[z][ln] = zg[zi][dn];
                            if (zg[z][ln] >= 0) seam_nodes.insert(zg[z][ln]);
                        }
                        break;
                    }
                }
            }
        }
        for (auto& [fam, edges] : zones[0].boundary_edges) {
            for (auto& [a, b] : edges) {
                idx_t ga = zg[0][a], gb = zg[0][b];
                if (seam_nodes.count(ga) && seam_nodes.count(gb)) continue;
                mm.boundary_edges[fam].push_back({ga, gb});
            }
        }
    }

    Mesh mesh = build_mesh_from_merged(mm);

    std::cout << "Mesh loaded: " << mesh.n_cells << " cells, "
              << mesh.n_faces << " faces, "
              << mesh.n_boundary_faces << " boundary faces" << std::endl;
    for (auto& [tag, fids] : mesh.boundary_families) {
        std::cout << "  Boundary family '" << tag << "': " << fids.size() << " faces" << std::endl;
    }
    return mesh;
}

void compute_face_geometry(Mesh&) {}
void compute_cell_geometry(Mesh&) {}

} // namespace cfd
