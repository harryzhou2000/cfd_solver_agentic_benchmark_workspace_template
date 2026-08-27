#include "CgnsReader.hpp"
#include <cgnslib.h>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <map>
#include <set>

// ============================================================
// Internal helpers
// ============================================================
struct ZoneData {
    std::string name;
    std::vector<double> x, y;
    int n_nodes = 0;
    // Volume elements: each row is a cell (3 or 4 node ids, 1-indexed)
    std::vector<std::vector<cgsize_t>> cells;  // 1-indexed
    // Boundary line elements: family -> list of {n0,n1} pairs (1-indexed)
    std::map<std::string, std::vector<std::array<cgsize_t,2>>> bface_elements;
};

// Get a ZoneData from one CGNS zone
static ZoneData readZone(int fn, int B, int Z) {
    ZoneData zd;
    cgsize_t sizes[3] = {};
    char zname[33] = {};
    cg_zone_read(fn, B, Z, zname, sizes);
    zd.name = zname;
    int nNodes = (int)sizes[0];
    zd.n_nodes = nNodes;
    zd.x.resize(nNodes); zd.y.resize(nNodes);

    // Read coordinates
    double* xptr = zd.x.data(), *yptr = zd.y.data();
    cgsize_t rmin = 1, rmax = nNodes;
    cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, xptr);
    cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, yptr);

    // Read element sections
    int nSections = 0;
    cg_nsections(fn, B, Z, &nSections);
    for (int S = 1; S <= nSections; S++) {
        char sname[33] = {};
        CGNS_ENUMT(ElementType_t) etype;
        cgsize_t estart, eend;
        int nbndry, parent_flag;
        cg_section_read(fn, B, Z, S, sname, &etype, &estart, &eend, &nbndry, &parent_flag);

        cgsize_t nElems = eend - estart + 1;
        int nNodes_per_elem = 0;
        bool isBoundary = false;
        if (etype == CGNS_ENUMV(TRI_3))   { nNodes_per_elem = 3; isBoundary = false; }
        else if (etype == CGNS_ENUMV(QUAD_4)) { nNodes_per_elem = 4; isBoundary = false; }
        else if (etype == CGNS_ENUMV(BAR_2))  { nNodes_per_elem = 2; isBoundary = true; }
        else { continue; }  // skip unknown types

        cgsize_t dataSize = nElems * nNodes_per_elem;
        std::vector<cgsize_t> conn(dataSize);
        cgsize_t* parentData = nullptr;
        cg_elements_read(fn, B, Z, S, conn.data(), parentData);

        if (!isBoundary) {
            // Volume elements
            for (cgsize_t e = 0; e < nElems; e++) {
                std::vector<cgsize_t> cell(nNodes_per_elem);
                for (int n = 0; n < nNodes_per_elem; n++)
                    cell[n] = conn[e*nNodes_per_elem + n];  // 1-indexed
                zd.cells.push_back(cell);
            }
        } else {
            // Boundary line elements - use section name as family
            std::string famName = sname;
            for (cgsize_t e = 0; e < nElems; e++) {
                zd.bface_elements[famName].push_back({conn[e*2], conn[e*2+1]});
            }
        }
    }

    // Also read ZoneBC to get family names for boundary conditions
    // (In case boundary elements are listed by PointRange in ZoneBC)
    int nBCs = 0;
    cg_nbocos(fn, B, Z, &nBCs);
    for (int BC = 1; BC <= nBCs; BC++) {
        char bcname[33] = {};
        CGNS_ENUMT(BCType_t) bctype;
        CGNS_ENUMT(PointSetType_t) ptset_type;
        cgsize_t npnts;
        int NormalIndex;
        cgsize_t NormalListFlag;
        CGNS_ENUMT(DataType_t) NormalDataType;
        int ndataset;
        cg_boco_info(fn, B, Z, BC, bcname, &bctype, &ptset_type, &npnts,
                     &NormalIndex, &NormalListFlag, &NormalDataType, &ndataset);
        // If the boundary is defined by element range (PointRange on Element location)
        // we may need to handle this. For now, we rely on BAR_2 sections.
        // But let's check family name for mapping
        char famname[33] = {};
        int ier = cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end");
        if (ier == 0) {
            if (cg_famname_read(famname) == 0) {
                // Family name found - the boundary elements should be in a section
                // with this name (already handled above)
            }
        }
    }

    return zd;
}

// ============================================================
// Build face connectivity from cell-node list (2D: faces are edges)
// ============================================================
struct FaceKey {
    int n0, n1;
    FaceKey(int a, int b) : n0(std::min(a,b)), n1(std::max(a,b)) {}
    bool operator==(const FaceKey& o) const { return n0==o.n0 && n1==o.n1; }
};
struct FaceKeyHash {
    size_t operator()(const FaceKey& k) const {
        return std::hash<long long>()(((long long)k.n0 << 32) | k.n1);
    }
};

// ============================================================
// Merge multiple zones using inter-zone connectivity
// ============================================================
struct ZoneConnectivity {
    int zoneFrom, zoneTo;
    std::vector<cgsize_t> pnts_from;  // 1-indexed node ids in zoneFrom
    std::vector<cgsize_t> pnts_to;    // 1-indexed node ids in zoneTo
};

static std::vector<ZoneConnectivity> readZoneConnectivity(int fn, int B, int nZones,
    const std::vector<std::string>& zoneNames)
{
    std::vector<ZoneConnectivity> conns;
    for (int Z = 1; Z <= nZones; Z++) {
        int nconns = 0;
        cg_nconns(fn, B, Z, &nconns);
        for (int C = 1; C <= nconns; C++) {
            char cname[33], dzone[33];
            CGNS_ENUMT(GridLocation_t) location;
            CGNS_ENUMT(GridConnectivityType_t) conn_type;
            CGNS_ENUMT(PointSetType_t) ptset_type;
            cgsize_t npnts;
            CGNS_ENUMT(ZoneType_t) donor_ztype;
            CGNS_ENUMT(PointSetType_t) donor_ptset;
            CGNS_ENUMT(DataType_t) donor_dtype;
            cgsize_t ndata_donor;
            cg_conn_info(fn, B, Z, C, cname, &location, &conn_type,
                         &ptset_type, &npnts, dzone, &donor_ztype,
                         &donor_ptset, &donor_dtype, &ndata_donor);
            std::vector<cgsize_t> pnts(npnts), dpnts(ndata_donor);
            cg_conn_read(fn, B, Z, C, pnts.data(), donor_dtype, dpnts.data());

            // Find donor zone index
            int dzone_idx = -1;
            for (int i = 0; i < (int)zoneNames.size(); i++)
                if (zoneNames[i] == std::string(dzone)) { dzone_idx = i+1; break; }
            if (dzone_idx < 0) continue;

            ZoneConnectivity zc;
            zc.zoneFrom = Z;
            zc.zoneTo   = dzone_idx;
            zc.pnts_from = pnts;
            zc.pnts_to   = dpnts;
            conns.push_back(zc);
        }
    }
    return conns;
}

// Compute triangle/quad centroid and area
static void cell_geometry(const std::vector<double>& x, const std::vector<double>& y,
                            const std::vector<int>& nodes,
                            double& cx, double& cy, double& area) {
    int n = (int)nodes.size();
    cx = 0; cy = 0; area = 0;
    if (n == 3) {
        // Triangle
        for (int i=0; i<3; i++) { cx += x[nodes[i]]; cy += y[nodes[i]]; }
        cx /= 3; cy /= 3;
        double x0=x[nodes[0]],y0=y[nodes[0]];
        double x1=x[nodes[1]],y1=y[nodes[1]];
        double x2=x[nodes[2]],y2=y[nodes[2]];
        area = 0.5*std::abs((x1-x0)*(y2-y0)-(x2-x0)*(y1-y0));
    } else {
        // Quad: split into 2 triangles
        for (int i=0; i<4; i++) { cx += x[nodes[i]]; cy += y[nodes[i]]; }
        cx /= 4; cy /= 4;
        double x0=x[nodes[0]],y0=y[nodes[0]];
        double x1=x[nodes[1]],y1=y[nodes[1]];
        double x2=x[nodes[2]],y2=y[nodes[2]];
        double x3=x[nodes[3]],y3=y[nodes[3]];
        double a1 = 0.5*std::abs((x1-x0)*(y2-y0)-(x2-x0)*(y1-y0));
        double a2 = 0.5*std::abs((x2-x0)*(y3-y0)-(x3-x0)*(y2-y0));
        area = a1 + a2;
    }
}

// ============================================================
// Main function: readCgnsMesh
// ============================================================
GlobalMesh readCgnsMesh(const std::string& filename,
                         const std::map<std::string, BcType>& bc_map)
{
    int fn;
    if (cg_open(filename.c_str(), CG_MODE_READ, &fn) != CG_OK)
        throw std::runtime_error("Cannot open CGNS file: " + filename);

    int nBases = 0;
    cg_nbases(fn, &nBases);
    if (nBases == 0) throw std::runtime_error("No bases in CGNS file");
    int B = 1;  // use first base

    int cell_dim, phys_dim;
    char bname[33];
    cg_base_read(fn, B, bname, &cell_dim, &phys_dim);

    int nZones = 0;
    cg_nzones(fn, B, &nZones);

    // Read all zone names
    std::vector<std::string> zoneNames(nZones);
    for (int Z = 1; Z <= nZones; Z++) {
        cgsize_t sizes[3] = {};
        char zname[33] = {};
        cg_zone_read(fn, B, Z, zname, sizes);
        zoneNames[Z-1] = zname;
    }

    // Read all zones
    std::vector<ZoneData> zones(nZones);
    for (int Z = 1; Z <= nZones; Z++)
        zones[Z-1] = readZone(fn, B, Z);

    // Read inter-zone connectivity (for multi-zone meshes)
    auto conns = readZoneConnectivity(fn, B, nZones, zoneNames);

    cg_close(fn);

    // ============================================================
    // Merge zones into a single global mesh
    // ============================================================
    // Build node mapping: zone[z][local_1based_idx] -> global_0based_idx
    // Start with zone 0 nodes
    int total_nodes = 0;
    std::vector<std::vector<int>> zoneNodeMap(nZones);
    for (int z = 0; z < nZones; z++)
        zoneNodeMap[z].assign(zones[z].n_nodes + 1, -1);  // 1-indexed -> global

    // Zone 0 gets first N nodes
    for (int i = 1; i <= zones[0].n_nodes; i++)
        zoneNodeMap[0][i] = total_nodes++;

    // Process inter-zone connections to identify shared nodes
    // conn: zoneFrom, zoneTo, pnts_from (1-based in zoneFrom), pnts_to (1-based in zoneTo)
    for (auto& conn : conns) {
        int zF = conn.zoneFrom - 1, zT = conn.zoneTo - 1;
        if (zF >= nZones || zT >= nZones) continue;
        int nPairs = (int)conn.pnts_from.size();
        for (int i = 0; i < nPairs; i++) {
            int nF = (int)conn.pnts_from[i];
            int nT = (int)conn.pnts_to[i];
            if (nF < 1 || nF > zones[zF].n_nodes) continue;
            if (nT < 1 || nT > zones[zT].n_nodes) continue;
            int gF = zoneNodeMap[zF][nF];
            int gT = zoneNodeMap[zT][nT];
            if (gF >= 0 && gT < 0) {
                zoneNodeMap[zT][nT] = gF;  // map zone T node to zone F global id
            } else if (gT >= 0 && gF < 0) {
                zoneNodeMap[zF][nF] = gT;
            }
            // If both already mapped, they should be equal
        }
    }

    // Assign new global ids to unassigned nodes in zones 1..
    for (int z = 1; z < nZones; z++) {
        for (int i = 1; i <= zones[z].n_nodes; i++) {
            if (zoneNodeMap[z][i] < 0)
                zoneNodeMap[z][i] = total_nodes++;
        }
    }

    // Build global coordinate arrays
    GlobalMesh gm;
    gm.n_nodes = total_nodes;
    gm.x.resize(total_nodes, 0.0);
    gm.y.resize(total_nodes, 0.0);
    for (int z = 0; z < nZones; z++) {
        for (int i = 1; i <= zones[z].n_nodes; i++) {
            int g = zoneNodeMap[z][i];
            if (g >= 0 && g < total_nodes) {
                gm.x[g] = zones[z].x[i-1];
                gm.y[g] = zones[z].y[i-1];
            }
        }
    }

    // Build global cell list
    for (int z = 0; z < nZones; z++) {
        for (auto& cell : zones[z].cells) {
            std::vector<int> gcell;
            for (auto nd : cell) {
                int g = zoneNodeMap[z][(int)nd];
                if (g < 0) throw std::runtime_error("Unmapped node in cell");
                gcell.push_back(g);
            }
            gm.cell_nodes.push_back(gcell);
        }
    }
    gm.n_cells = (int)gm.cell_nodes.size();

    // ============================================================
    // Build face connectivity from cell-node list
    // ============================================================
    // For each pair of nodes forming an edge, we store which cells own it
    std::unordered_map<FaceKey, std::pair<int,int>, FaceKeyHash> edge_to_cells;
    // Also store the oriented edge (in cell winding order) for each cell
    struct CellEdge { int cell, n0, n1; };  // n0, n1 in cell order (for normal)
    std::unordered_map<FaceKey, CellEdge, FaceKeyHash> edge_first_cell;

    for (int c = 0; c < gm.n_cells; c++) {
        const auto& nodes = gm.cell_nodes[c];
        int nn = (int)nodes.size();
        for (int i = 0; i < nn; i++) {
            int n0 = nodes[i];
            int n1 = nodes[(i+1)%nn];
            FaceKey fk(n0, n1);
            auto it = edge_to_cells.find(fk);
            if (it == edge_to_cells.end()) {
                edge_to_cells[fk] = {c, -1};
                edge_first_cell[fk] = {c, n0, n1};
            } else {
                it->second.second = c;
            }
        }
    }

    // Build face arrays
    gm.face_nodes.clear();
    gm.face_left.clear(); gm.face_right.clear();
    gm.face_nx.clear(); gm.face_ny.clear();
    gm.face_area.clear(); gm.face_cx.clear(); gm.face_cy.clear();

    // First pass: interior faces
    std::unordered_map<FaceKey, int, FaceKeyHash> edge_to_face_id;
    for (auto& [fk, cells] : edge_to_cells) {
        if (cells.second >= 0) {
            // Interior face
            int fid = (int)gm.face_nodes.size();
            edge_to_face_id[fk] = fid;
            gm.face_nodes.push_back({fk.n0, fk.n1});
            // Normal from left cell (cells.first) perspective
            auto& ce = edge_first_cell[fk];
            int left = ce.cell;
            int right = (cells.first == left) ? cells.second : cells.first;
            gm.face_left.push_back(left);
            gm.face_right.push_back(right);
            // Normal: perpendicular to edge, pointing from left to right
            double ex = gm.x[ce.n1] - gm.x[ce.n0];
            double ey = gm.y[ce.n1] - gm.y[ce.n0];
            double len = std::sqrt(ex*ex + ey*ey);
            double nx = ey/len, ny = -ex/len;
            gm.face_nx.push_back(nx);
            gm.face_ny.push_back(ny);
            gm.face_area.push_back(len);
            gm.face_cx.push_back(0.5*(gm.x[ce.n0]+gm.x[ce.n1]));
            gm.face_cy.push_back(0.5*(gm.y[ce.n0]+gm.y[ce.n1]));
        }
    }
    gm.n_interior_faces = (int)gm.face_nodes.size();

    // Second pass: boundary faces (from boundary element sections)
    // boundary face family -> face id
    for (int z = 0; z < nZones; z++) {
        for (auto& [famName, bfElems] : zones[z].bface_elements) {
            BcType bctype = BcType::Unknown;
            auto it = bc_map.find(famName);
            if (it != bc_map.end()) bctype = it->second;

            for (auto& edge : bfElems) {
                int gn0 = zoneNodeMap[z][(int)edge[0]];
                int gn1 = zoneNodeMap[z][(int)edge[1]];
                if (gn0 < 0 || gn1 < 0) continue;
                FaceKey fk(gn0, gn1);
                auto face_it = edge_to_cells.find(fk);
                if (face_it == edge_to_cells.end()) continue;
                auto& cells = face_it->second;
                int bCell = (cells.first >= 0) ? cells.first : cells.second;
                if (bCell < 0) continue;

                // Check if this boundary face is already in face list
                auto fid_it = edge_to_face_id.find(fk);
                int fid;
                if (fid_it != edge_to_face_id.end()) {
                    fid = fid_it->second;
                } else {
                    // New boundary face
                    fid = (int)gm.face_nodes.size();
                    edge_to_face_id[fk] = fid;
                    gm.face_nodes.push_back({gn0, gn1});
                    gm.face_left.push_back(bCell);
                    gm.face_right.push_back(-1);
                    auto& ce = edge_first_cell[fk];
                    double ex = gm.x[ce.n1] - gm.x[ce.n0];
                    double ey = gm.y[ce.n1] - gm.y[ce.n0];
                    double len = std::sqrt(ex*ex + ey*ey);
                    // Normal pointing outward from cell (need to check orientation)
                    double nx = ey/len, ny = -ex/len;
                    // Ensure normal points away from cell centroid
                    double dx = gm.x[ce.n0] - gm.cell_cx[bCell < (int)gm.cell_cx.size() ? bCell : 0];
                    // We'll fix orientation after centroid computation
                    gm.face_nx.push_back(nx);
                    gm.face_ny.push_back(ny);
                    gm.face_area.push_back(len);
                    gm.face_cx.push_back(0.5*(gm.x[gn0]+gm.x[gn1]));
                    gm.face_cy.push_back(0.5*(gm.y[gn0]+gm.y[gn1]));
                }
                gm.bface_family.push_back(famName);
                gm.bface_cell.push_back(bCell);
                gm.bface_face_id.push_back(fid);
            }
        }
    }
    gm.n_boundary_faces = (int)gm.bface_family.size();
    gm.n_faces = (int)gm.face_nodes.size();

    // Compute cell geometry
    gm.cell_cx.resize(gm.n_cells);
    gm.cell_cy.resize(gm.n_cells);
    gm.cell_vol.resize(gm.n_cells);
    for (int c = 0; c < gm.n_cells; c++) {
        cell_geometry(gm.x, gm.y, gm.cell_nodes[c],
                      gm.cell_cx[c], gm.cell_cy[c], gm.cell_vol[c]);
    }

    // Fix boundary face normals: should point outward from interior cell
    for (int bf = 0; bf < gm.n_boundary_faces; bf++) {
        int fid = gm.bface_face_id[bf];
        int cell = gm.bface_cell[bf];
        if (fid >= (int)gm.face_nx.size()) continue;
        double fcx = gm.face_cx[fid], fcy = gm.face_cy[fid];
        double ccx = gm.cell_cx[cell], ccy = gm.cell_cy[cell];
        double nx = gm.face_nx[fid], ny = gm.face_ny[fid];
        // Normal should point from cell centroid outward
        double dot = (fcx-ccx)*nx + (fcy-ccy)*ny;
        if (dot < 0) {
            gm.face_nx[fid] = -nx;
            gm.face_ny[fid] = -ny;
        }
    }

    // Fix interior face normals: should point from left to right
    for (int f = 0; f < gm.n_interior_faces; f++) {
        int left = gm.face_left[f], right = gm.face_right[f];
        double nx = gm.face_nx[f], ny = gm.face_ny[f];
        double fcx = gm.face_cx[f], fcy = gm.face_cy[f];
        double lx = gm.cell_cx[left], ly = gm.cell_cy[left];
        // Normal from left outward: should point toward right cell
        double dot = (gm.cell_cx[right]-lx)*nx + (gm.cell_cy[right]-ly)*ny;
        if (dot < 0) {
            gm.face_nx[f] = -nx;
            gm.face_ny[f] = -ny;
        }
    }

    // Build cell_faces and cell_neighbors adjacency
    gm.cell_faces.resize(gm.n_cells);
    gm.cell_neighbors.resize(gm.n_cells);
    for (int f = 0; f < gm.n_interior_faces; f++) {
        int L = gm.face_left[f], R = gm.face_right[f];
        gm.cell_faces[L].push_back(f);
        gm.cell_neighbors[L].push_back(R);
        gm.cell_faces[R].push_back(f);
        gm.cell_neighbors[R].push_back(L);
    }
    for (int f = gm.n_interior_faces; f < gm.n_faces; f++) {
        int L = gm.face_left[f];
        gm.cell_faces[L].push_back(f);
        gm.cell_neighbors[L].push_back(-1);
    }

    // Apply BC mapping
    for (auto& fam : gm.bface_family) {
        auto it = bc_map.find(fam);
        gm.bface_type.push_back(it != bc_map.end() ? it->second : BcType::Unknown);
    }

    std::cout << "[CGNS] Mesh loaded: " << gm.n_cells << " cells, "
              << gm.n_faces << " faces (" << gm.n_interior_faces << " interior, "
              << gm.n_boundary_faces << " boundary), "
              << gm.n_nodes << " nodes" << std::endl;
    return gm;
}
