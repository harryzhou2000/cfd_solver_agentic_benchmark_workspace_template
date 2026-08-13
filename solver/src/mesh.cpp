// mesh.cpp - CGNS mesh reader, cell-face topology, geometry computation.
//
// Reads CGNS files (HDF5-backed) using the CGNS library.  Handles:
//   - Single-zone meshes (NACA0012_H2.cgns)
//   - Multi-zone meshes with 1-to-1 grid connectivity (CylinderB1.cgns)
//   - Mixed tri/quad elements
//   - Boundary condition family names mapped to solver BC types
//
// The reader merges all zones into a single global mesh with contiguous
// global cell and node IDs.  Inter-zone 1-to-1 connectivity is resolved
// by merging matching boundary faces into interior faces.
#include "cfd2d.hpp"
#include <cgnslib.h>
#include <cmath>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace cfd2d {

// Sort two node IDs to create a canonical face key
static std::pair<idx_t,idx_t> faceKey(idx_t a, idx_t b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

// Decode a family name from CGNS int8 array
static std::string decodeName(const char* data, int len) {
    std::string s(data, len);
    // Trim trailing nulls
    size_t pos = s.find('\0');
    if (pos != std::string::npos) s = s.substr(0, pos);
    return s;
}

static std::string readStringNode(int fn, int B, int Z, int S, const char* path) {
    char buf[200];
    int len;
    if (cg_goto(fn, B, "Zone_t", Z, "Elements_t", S, "end"))
        return "";
    // Try reading the name from the section
    return "";
}

// Read all zones from a CGNS file and merge into a single mesh
GlobalMesh readCGNSMesh(const std::string& path, const std::map<std::string, BCType>& bcMap) {
    GlobalMesh mesh;
    mesh.bcMap = bcMap;

    int fn;
    if (cg_open(path.c_str(), CG_MODE_READ, &fn)) {
        throw std::runtime_error("Cannot open CGNS file: " + path + " : " + cg_get_error());
    }

    int nbases;
    cg_nbases(fn, &nbases);
    if (nbases < 1) { cg_close(fn); throw std::runtime_error("No bases in " + path); }

    int B = 1; // use first base

    // Collect all families with their BC types
    int nfamilies;
    cg_nfamilies(fn, B, &nfamilies);

    // ---- Read all zones ----
    // Each zone has: coordinates, volume elements (tris + quads), boundary sections
    struct ZoneData {
        std::string name;
        int nVerts;
        std::vector<double> x, y;
        // Volume elements: store as (type, connectivity)
        std::vector<std::vector<idx_t>> volElements; // each element's vertex list
        std::vector<int> volElemType; // 3=tri, 4=quad
        // Boundary elements: (familyName, vertices[2])
        std::vector<std::pair<std::string, std::array<idx_t,2>>> bndElements;
        // Grid connectivity: 1-to-1 connections (PointList -> PointListDonor)
        struct GridConn {
            std::string name;
            std::vector<idx_t> localPoints;    // local vertex indices
            std::vector<idx_t> donorPoints;    // global donor vertex indices (converted later)
            std::string donorZone;
        };
        std::vector<GridConn> gridConns;
        idx_t nodeOffset;  // global node ID offset
        idx_t cellOffset;  // global cell ID offset (for vol elements)
    };
    std::vector<ZoneData> zones;

    int nzones;
    cg_nzones(fn, B, &nzones);

    idx_t totalNodes = 0;
    idx_t totalVolCells = 0;

    for (int z = 1; z <= nzones; z++) {
        ZoneData zd;
        char zonename[100];
        cgsize_t sizes[9];
        cg_zone_read(fn, B, z, zonename, sizes);
        zd.name = zonename;
        zd.nVerts = sizes[0];
        zd.nodeOffset = totalNodes;
        zd.cellOffset = totalVolCells;

        // Read coordinates
        zd.x.resize(zd.nVerts);
        zd.y.resize(zd.nVerts);
        cgsize_t rmin = 1, rmax = zd.nVerts;
        cg_coord_read(fn, B, z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.x.data());
        cg_coord_read(fn, B, z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, zd.y.data());

        // Read sections
        int nsections;
        cg_nsections(fn, B, z, &nsections);
        for (int s = 1; s <= nsections; s++) {
            char secname[200];
            CGNS_ENUMT(ElementType_t) etype;
            cgsize_t start, end;
            int nbndry, parentflag;
            cg_section_read(fn, B, z, s, secname, &etype, &start, &end, &nbndry, &parentflag);
            int nelem = end - start + 1;

            // Determine if this is a volume or boundary section
            // Volume: TRI_3 (5), QUAD_4 (7)
            // Boundary: BAR_2 (3) or similar 1D elements
            bool isVolume = (etype == CGNS_ENUMV(TRI_3) || etype == CGNS_ENUMV(QUAD_4));
            bool isBoundary = (etype == CGNS_ENUMV(BAR_2) || etype == CGNS_ENUMV(BAR_3));

            if (isVolume) {
                int nvpe; // nodes per element
                cg_npe(etype, &nvpe);
                std::vector<cgsize_t> conn(nvpe * nelem);
                cg_elements_read(fn, B, z, s, conn.data(), nullptr);
                for (int e = 0; e < nelem; e++) {
                    std::vector<idx_t> elem(nvpe);
                    for (int j = 0; j < nvpe; j++) {
                        elem[j] = (idx_t)conn[e*nvpe + j] - 1 + zd.nodeOffset; // 0-based + offset
                    }
                    zd.volElements.push_back(elem);
                    zd.volElemType.push_back(nvpe);
                    totalVolCells++;
                }
            } else if (isBoundary) {
                int nvpe;
                cg_npe(etype, &nvpe);
                std::vector<cgsize_t> conn(nvpe * nelem);
                cg_elements_read(fn, B, z, s, conn.data(), nullptr);
                for (int e = 0; e < nelem; e++) {
                    std::array<idx_t,2> edge;
                    edge[0] = (idx_t)conn[e*nvpe] - 1 + zd.nodeOffset;
                    edge[1] = (idx_t)conn[e*nvpe + nvpe-1] - 1 + zd.nodeOffset;
                    // The section name is the family name (e.g., "bc-2", "bc-4", "WALL", "FAR")
                    zd.bndElements.push_back({secname, edge});
                }
            }
            // Grid connectivity sections (con-2, con-3, ...) are BAR_2 too but
            // they appear as sections in the zone.  We detect them via ZoneGridConnectivity.
        }

        // Read grid connectivity (cg_conn API)
        int nconns2 = 0;
        cg_nconns(fn, B, z, &nconns2);
        for (int c = 1; c <= nconns2; c++) {
            char connname[100], donorname[100];
            CGNS_ENUMT(GridLocation_t) location;
            CGNS_ENUMT(GridConnectivityType_t) gct;
            CGNS_ENUMT(PointSetType_t) ptset_type;
            cgsize_t npnts;
            CGNS_ENUMT(ZoneType_t) donor_zonetype;
            CGNS_ENUMT(PointSetType_t) donor_ptset_type;
            CGNS_ENUMT(DataType_t) donor_datatype;
            cgsize_t ndata_donor;
            cg_conn_info(fn, B, z, c, connname, &location, &gct, &ptset_type,
                         &npnts, donorname, &donor_zonetype, &donor_ptset_type,
                         &donor_datatype, &ndata_donor);
            if (gct == CGNS_ENUMV(Abutting1to1) || gct == CGNS_ENUMV(Abutting)) {
                ZoneData::GridConn gc;
                gc.name = connname;
                gc.donorZone = donorname;
                std::vector<cgsize_t> localPts(npnts);
                std::vector<cgsize_t> donorPts(ndata_donor);
                cg_conn_read(fn, B, z, c, localPts.data(), donor_datatype, donorPts.data());
                for (cgsize_t i = 0; i < npnts; i++) {
                    gc.localPoints.push_back((idx_t)localPts[i] - 1 + zd.nodeOffset);
                    gc.donorPoints.push_back((idx_t)donorPts[i]);
                }
                zd.gridConns.push_back(gc);
            }
        }
        // Also try 1to1 connections via cg_n1to1
        int n1to1;
        cg_n1to1(fn, B, z, &n1to1);
        for (int c = 1; c <= n1to1; c++) {
            char connname[100], donorname[100];
            cgsize_t range[2], donor_range[2];
            int transform[2];
            cg_1to1_read(fn, B, z, c, connname, donorname, range, donor_range, transform);
            ZoneData::GridConn gc;
            gc.name = connname;
            gc.donorZone = donorname;
            // PointRange-based: convert to point list
            for (cgsize_t i = range[0]; i <= range[1]; i++) {
                gc.localPoints.push_back((idx_t)(i - 1) + zd.nodeOffset);
            }
            // Donor range with transform
            for (cgsize_t i = donor_range[0]; i <= donor_range[1]; i++) {
                gc.donorPoints.push_back((idx_t)i);
            }
            if (gc.localPoints.size() == gc.donorPoints.size() && !gc.localPoints.empty())
                zd.gridConns.push_back(gc);
        }

        totalNodes += zd.nVerts;
        zones.push_back(std::move(zd));
    }
    cg_close(fn);

    // ---- Merge zones into a single global mesh ----
    mesh.coordsX.resize(totalNodes);
    mesh.coordsY.resize(totalNodes);
    for (auto& zd : zones) {
        for (int i = 0; i < zd.nVerts; i++) {
            mesh.coordsX[zd.nodeOffset + i] = zd.x[i];
            mesh.coordsY[zd.nodeOffset + i] = zd.y[i];
        }
    }
    mesh.numNodes = totalNodes;

    // Build cells
    mesh.cells.resize(totalVolCells);
    idx_t cellIdx = 0;
    for (auto& zd : zones) {
        for (size_t i = 0; i < zd.volElements.size(); i++) {
            Cell& c = mesh.cells[cellIdx];
            c.nodes = zd.volElements[i];
            c.type = zd.volElemType[i];
            c.globalId = cellIdx;
            c.partition = -1;
            cellIdx++;
        }
    }
    mesh.numCellsGlobal = totalVolCells;

    // ---- Build boundary edge list from boundary sections ----
    // Map: faceKey(node0, node1) -> BC family name
    std::map<std::pair<idx_t,idx_t>, std::pair<std::string,int>> bndFaceMap;
    for (auto& zd : zones) {
        for (auto& be : zd.bndElements) {
            auto key = faceKey(be.second[0], be.second[1]);
            int bcInt = (int)BCType::Unknown;
            auto it = bcMap.find(be.first);
            if (it != bcMap.end()) bcInt = (int)it->second;
            bndFaceMap[key] = {be.first, bcInt};
        }
    }

    // ---- Handle grid connectivity: mark inter-zone interface faces ----
    // For 1-to-1 connections, we need to convert donor points (which are in the
    // donor zone's local numbering) to global node IDs.
    // Build zone name -> (nodeOffset) mapping
    std::unordered_map<std::string, idx_t> zoneNodeOffset;
    for (auto& zd : zones) zoneNodeOffset[zd.name] = zd.nodeOffset;

    // Interface faces that should NOT be treated as boundaries
    std::set<std::pair<idx_t,idx_t>> interfaceFaces;
    for (auto& zd : zones) {
        for (auto& gc : zd.gridConns) {
            idx_t donorOffset = 0;
            auto doit = zoneNodeOffset.find(gc.donorZone);
            if (doit != zoneNodeOffset.end()) donorOffset = doit->second;
            for (size_t i = 0; i < gc.localPoints.size(); i++) {
                idx_t localNode = gc.localPoints[i];
                idx_t donorNode = (gc.donorPoints[i] - 1) + donorOffset;
                auto key = faceKey(localNode, donorNode);
                interfaceFaces.insert(key);
                // Remove from boundary map if present
                bndFaceMap.erase(key);
            }
        }
    }

    // ---- Build cell-face topology ----
    // For each cell, extract its edges.  An edge shared by two cells becomes
    // an interior face.  An edge in bndFaceMap becomes a boundary face.
    // An edge in interfaceFaces is also an interior face (inter-zone).

    struct FaceInfo {
        idx_t n0, n1;
        idx_t cell;       // first cell found
        int cellLocalFace; // local face index in cell
    };
    std::map<std::pair<idx_t,idx_t>, FaceInfo> faceMap;

    auto addCellFaces = [&](idx_t cellId) {
        Cell& c = mesh.cells[cellId];
        int nv = c.nodes.size();
        for (int i = 0; i < nv; i++) {
            idx_t a = c.nodes[i];
            idx_t b = c.nodes[(i+1) % nv];
            auto key = faceKey(a, b);
            auto it = faceMap.find(key);
            if (it == faceMap.end()) {
                FaceInfo fi;
                fi.n0 = a; fi.n1 = b;
                fi.cell = cellId;
                fi.cellLocalFace = i;
                faceMap[key] = fi;
            } else {
                // Second cell: this is an interior face
                // (handled in topology build below)
            }
        }
    };

    for (idx_t c = 0; c < (idx_t)mesh.cells.size(); c++) {
        addCellFaces(c);
    }

    // Now build the face list
    mesh.faces.clear();
    // Also need to know which faces are shared by two cells
    struct FaceBuild {
        idx_t n0, n1;
        idx_t cellL;
        idx_t cellR; // -1 if boundary
        int bcInt;
        std::string bcFamily;
        bool isInterface;
    };
    std::vector<FaceBuild> faceBuilds;
    // Re-scan to find shared faces
    std::map<std::pair<idx_t,idx_t>, int> faceIndex; // key -> index in faceBuilds
    std::map<std::pair<idx_t,idx_t>, int> faceCount;

    for (idx_t c = 0; c < (idx_t)mesh.cells.size(); c++) {
        Cell& cell = mesh.cells[c];
        int nv = cell.nodes.size();
        for (int i = 0; i < nv; i++) {
            idx_t a = cell.nodes[i];
            idx_t b = cell.nodes[(i+1)%nv];
            auto key = faceKey(a, b);
            faceCount[key]++;
        }
    }

    for (idx_t c = 0; c < (idx_t)mesh.cells.size(); c++) {
        Cell& cell = mesh.cells[c];
        int nv = cell.nodes.size();
        for (int i = 0; i < nv; i++) {
            idx_t a = cell.nodes[i];
            idx_t b = cell.nodes[(i+1)%nv];
            auto key = faceKey(a, b);
            if (faceCount[key] > 2) continue; // degenerate, skip duplicates

            auto fit = faceIndex.find(key);
            if (fit == faceIndex.end()) {
                // New face
                FaceBuild fb;
                fb.n0 = a; fb.n1 = b;
                fb.cellL = c; fb.cellR = -1;
                fb.bcInt = (int)BCType::Interior;
                fb.bcFamily = "";
                fb.isInterface = false;
                // Check if it's a boundary face
                auto bit = bndFaceMap.find(key);
                if (bit != bndFaceMap.end()) {
                    fb.bcInt = bit->second.second;
                    fb.bcFamily = bit->second.first;
                }
                // Check if it's an interface face
                if (interfaceFaces.count(key)) {
                    fb.isInterface = true;
                }
                faceIndex[key] = faceBuilds.size();
                faceBuilds.push_back(fb);
            } else {
                // Second cell for this face
                FaceBuild& fb = faceBuilds[fit->second];
                fb.cellR = c;
            }
        }
    }

    // Convert FaceBuild to Face and assign to cells
    mesh.faces.resize(faceBuilds.size());
    for (size_t f = 0; f < faceBuilds.size(); f++) {
        FaceBuild& fb = faceBuilds[f];
        Face& face = mesh.faces[f];
        face.nodes = {fb.n0, fb.n1};
        face.cells = {fb.cellL, fb.cellR};
        face.isBoundary = (fb.cellR == -1) && (fb.bcInt != (int)BCType::Interior);
        face.bcType = (fb.cellR == -1) ? fb.bcInt : (int)BCType::Interior;
        face.bcFamily = fb.bcFamily;
        face.isInterface = fb.isInterface && (fb.cellR != -1);
        face.neighborRank = -1;

        // Add face to cell face lists
        mesh.cells[fb.cellL].faces.push_back(f);
        if (fb.cellR >= 0) {
            mesh.cells[fb.cellR].faces.push_back(f);
        }
    }

    mesh.numFacesGlobal = mesh.faces.size();

    // Compute geometry
    computeGeometry(mesh);

    // Build neighbor lists
    for (idx_t c = 0; c < (idx_t)mesh.cells.size(); c++) {
        Cell& cell = mesh.cells[c];
        for (idx_t f : cell.faces) {
            Face& face = mesh.faces[f];
            idx_t nb = (face.cells[0] == c) ? face.cells[1] : face.cells[0];
            cell.neighbors.push_back(nb);
        }
    }

    // Collect wall families
    for (auto& [name, type] : bcMap) {
        if (type == BCType::SlipWall || type == BCType::NoSlipAdiabatic) {
            mesh.wallFamilies.push_back(name);
        }
    }

    return mesh;
}

void computeGeometry(GlobalMesh& mesh) {
    // Compute cell centers, volumes (areas), face normals, areas, centers
    for (auto& c : mesh.cells) {
        int nv = c.nodes.size();
        double cx = 0, cy = 0;
        for (int i = 0; i < nv; i++) {
            cx += mesh.coordsX[c.nodes[i]];
            cy += mesh.coordsY[c.nodes[i]];
        }
        c.xc = cx / nv;
        c.yc = cy / nv;
        // Area via shoelace formula
        double area = 0;
        for (int i = 0; i < nv; i++) {
            double x0 = mesh.coordsX[c.nodes[i]];
            double y0 = mesh.coordsY[c.nodes[i]];
            double x1 = mesh.coordsX[c.nodes[(i+1)%nv]];
            double y1 = mesh.coordsY[c.nodes[(i+1)%nv]];
            area += x0*y1 - x1*y0;
        }
        c.volume = std::abs(area) * 0.5;
    }

    for (auto& f : mesh.faces) {
        double x0 = mesh.coordsX[f.nodes[0]];
        double y0 = mesh.coordsY[f.nodes[0]];
        double x1 = mesh.coordsX[f.nodes[1]];
        double y1 = mesh.coordsY[f.nodes[1]];
        // Face length
        double dx = x1 - x0, dy = y1 - y0;
        f.area = std::sqrt(dx*dx + dy*dy);
        f.fcx = 0.5*(x0+x1);
        f.fcy = 0.5*(y0+y1);
        // Normal: rotate edge by 90 degrees.  The normal points from cellL to cellR.
        // For a counter-clockwise oriented cell, the outward normal of edge (i, i+1) is (dy, -dx)/L.
        // But we need a consistent convention.  We'll compute the geometric normal
        // (dy, -dx)/L and then orient it to point from cellL to cellR.
        double nx_geom = dy / f.area;   // = (y1-y0)/L
        double ny_geom = -dx / f.area;  // = -(x1-x0)/L

        if (f.cells[1] >= 0) {
            // Interior face: orient from L to R
            double rx = mesh.cells[f.cells[1]].xc - mesh.cells[f.cells[0]].xc;
            double ry = mesh.cells[f.cells[1]].yc - mesh.cells[f.cells[0]].yc;
            if (nx_geom*rx + ny_geom*ry < 0) {
                nx_geom = -nx_geom;
                ny_geom = -ny_geom;
            }
        } else {
            // Boundary face: orient outward from cellL
            double rx = f.fcx - mesh.cells[f.cells[0]].xc;
            double ry = f.fcy - mesh.cells[f.cells[0]].yc;
            if (nx_geom*rx + ny_geom*ry < 0) {
                nx_geom = -nx_geom;
                ny_geom = -ny_geom;
            }
        }
        f.nx = nx_geom;
        f.ny = ny_geom;
    }
}

} // namespace cfd2d
