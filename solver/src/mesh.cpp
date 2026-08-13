// mesh.cpp — CGNS mesh reader, topology builder, geometry computation
#include "mesh.hpp"
#include <cgnslib.h>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <set>
#include <algorithm>

namespace cfd2d {

// Spatial hash for vertex merging
struct VertexMerger {
    double tol;
    std::unordered_map<int64_t, std::vector<int>> grid; // grid cell -> vertex indices

    VertexMerger(double tolerance) : tol(tolerance) {}

    int64_t hashKey(double x, double y) const {
        int32_t ix = (int32_t)std::round(x / tol);
        int32_t iy = (int32_t)std::round(y / tol);
        return ((int64_t)ix << 32) | (uint32_t)iy;
    }

    // Find or insert vertex. Returns global vertex index.
    int findOrInsert(std::vector<double>& vx, std::vector<double>& vy, double x, double y) {
        int64_t key = hashKey(x, y);
        // Check this cell and 8 neighbors
        int32_t ix = (int32_t)(key >> 32);
        int32_t iy = (int32_t)(key & 0xFFFFFFFF);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                int64_t nk = ((int64_t)(ix + dx) << 32) | (uint32_t)(iy + dy);
                auto it = grid.find(nk);
                if (it != grid.end()) {
                    for (int vi : it->second) {
                        double ddx = vx[vi] - x;
                        double ddy = vy[vi] - y;
                        if (ddx*ddx + ddy*ddy < tol * tol)
                            return vi;
                    }
                }
            }
        }
        // Insert new vertex
        int idx = vx.size();
        vx.push_back(x);
        vy.push_back(y);
        grid[key].push_back(idx);
        return idx;
    }
};

void readCGNSMesh(Mesh& mesh, const std::string& filename,
                  const std::map<std::string, BCType>& bcMap) {
    mesh.meshFile = filename;
    int fn;
    if (cg_open(filename.c_str(), CG_MODE_READ, &fn) != CG_OK)
        throw std::runtime_error("Cannot open CGNS file: " + filename);

    int nbases;
    cg_nbases(fn, &nbases);
    if (nbases < 1) { cg_close(fn); throw std::runtime_error("No bases in CGNS file"); }

    // We read base 1
    int B = 1;
    char basename[128];
    int cellDim, physDim;
    cg_base_read(fn, B, basename, &cellDim, &physDim);
    if (cellDim != 2) std::cerr << "Warning: expected 2D mesh, got cellDim=" << cellDim << "\n";

    int nzones;
    cg_nzones(fn, B, &nzones);

    VertexMerger merger(1e-8);
    // Map: (min_node, max_node) -> bcTag for boundary edges
    std::map<std::pair<int,int>, int> bfaceBC;
    // Also store the bcInfos
    auto getBCTag = [&](BCType bt, const std::string& fam) -> int {
        for (int i = 0; i < (int)mesh.bcInfos.size(); i++) {
            if (mesh.bcInfos[i].type == bt && mesh.bcInfos[i].familyName == fam)
                return i;
        }
        mesh.bcInfos.push_back({bt, fam});
        return mesh.bcInfos.size() - 1;
    };

    for (int Z = 1; Z <= nzones; Z++) {
        char zonename[128];
        cgsize_t size[3];
        cg_zone_read(fn, B, Z, zonename, size);
        int nvertZone = size[0];

        // Read coordinates
        std::vector<double> xZone(nvertZone), yZone(nvertZone);
        cgsize_t rmin = 1, rmax = nvertZone;
        cg_coord_read(fn, B, Z, "CoordinateX", CGNS_ENUMV(RealDouble), &rmin, &rmax, xZone.data());
        cg_coord_read(fn, B, Z, "CoordinateY", CGNS_ENUMV(RealDouble), &rmin, &rmax, yZone.data());

        // Merge vertices and build local-to-global map
        std::vector<int> localToGlobal(nvertZone);
        for (int i = 0; i < nvertZone; i++) {
            localToGlobal[i] = merger.findOrInsert(mesh.vx, mesh.vy, xZone[i], yZone[i]);
        }
        mesh.nvert = mesh.vx.size();

        // Read element sections
        int nsections;
        cg_nsections(fn, B, Z, &nsections);
        for (int S = 1; S <= nsections; S++) {
            char secname[128];
            CGNS_ENUMT(ElementType_t) etype;
            cgsize_t start, end;
            int nbndry, parentFlag;
            cg_section_read(fn, B, Z, S, secname, &etype, &start, &end, &nbndry, &parentFlag);
            int nElem = end - start + 1;

            // Get connectivity size and read
            cgsize_t connSize;
            cg_ElementDataSize(fn, B, Z, S, &connSize);
            std::vector<cgsize_t> conn(connSize);
            cg_elements_read(fn, B, Z, S, conn.data(), nullptr);

            int nodesPerElem = connSize / nElem;

            if (etype == TRI_3 || (nodesPerElem == 3 && etype != MIXED)) {
                // Interior triangle cells
                for (int e = 0; e < nElem; e++) {
                    Cell c;
                    c.nNodes = 3;
                    c.type = ElemType::Triangle;
                    for (int k = 0; k < 3; k++)
                        c.nodes[k] = localToGlobal[conn[e*3 + k] - 1]; // CGNS 1-based
                    mesh.cells.push_back(c);
                }
            } else if (etype == QUAD_4 || (nodesPerElem == 4 && etype != MIXED)) {
                // Interior quad cells
                for (int e = 0; e < nElem; e++) {
                    Cell c;
                    c.nNodes = 4;
                    c.type = ElemType::Quadrilateral;
                    for (int k = 0; k < 4; k++)
                        c.nodes[k] = localToGlobal[conn[e*4 + k] - 1];
                    mesh.cells.push_back(c);
                }
            } else if (etype == BAR_2 || (nodesPerElem == 2 && etype != MIXED)) {
                // Boundary or interface edges
                std::string sname(secname);
                auto it = bcMap.find(sname);
                if (it != bcMap.end()) {
                    // Boundary section
                    int bcTag = getBCTag(it->second, sname);
                    for (int e = 0; e < nElem; e++) {
                        int n0 = localToGlobal[conn[e*2] - 1];
                        int n1 = localToGlobal[conn[e*2 + 1] - 1];
                        int lo = std::min(n0, n1), hi = std::max(n0, n1);
                        bfaceBC[{lo, hi}] = bcTag;
                    }
                }
                // If not in bcMap, it's an interface edge — skip (handled by face building)
            } else {
                std::cerr << "Warning: skipping element section '" << secname
                          << "' with type " << etype << " (" << nodesPerElem << " nodes/elem)\n";
            }
        }
    }
    cg_close(fn);

    mesh.ncell = mesh.cells.size();
    if (mesh.ncell == 0) throw std::runtime_error("No cells found in mesh: " + filename);

    // Compute bounding box
    mesh.xmin = mesh.xmax = mesh.vx[0];
    mesh.ymin = mesh.ymax = mesh.vy[0];
    for (int i = 0; i < mesh.nvert; i++) {
        mesh.xmin = std::min(mesh.xmin, mesh.vx[i]);
        mesh.xmax = std::max(mesh.xmax, mesh.vx[i]);
        mesh.ymin = std::min(mesh.ymin, mesh.vy[i]);
        mesh.ymax = std::max(mesh.ymax, mesh.vy[i]);
    }

    // Build faces
    buildFaces(mesh);

    // Store boundary face BC map for computeGeometry (actually buildFaces uses bfaceBC)
    // We need to pass bfaceBC to buildFaces — let's redo with a different approach
    // Actually, let's rebuild faces with BC info
    mesh.faces.clear();
    mesh.nface = 0;
    mesh.nInteriorFace = 0;
    mesh.nBoundaryFace = 0;

    // Build edge -> faces map
    std::map<std::pair<int,int>, std::vector<std::pair<int,int>>> edgeMap;
    // edge -> list of (cellIdx, edgeIdx)
    for (int ci = 0; ci < mesh.ncell; ci++) {
        const Cell& c = mesh.cells[ci];
        for (int e = 0; e < c.nNodes; e++) {
            int n0 = c.nodes[e];
            int n1 = c.nodes[(e + 1) % c.nNodes];
            int lo = std::min(n0, n1), hi = std::max(n0, n1);
            edgeMap[{lo, hi}].push_back({ci, e});
        }
    }

    for (auto& [key, cells] : edgeMap) {
        Face f;
        f.n0 = key.first;
        f.n1 = key.second;
        f.bcTag = -1;

        if (cells.size() == 2) {
            // Interior face
            f.lc = cells[0].first;
            f.rc = cells[1].first;
            // Determine normal direction: from lc to rc
            // We'll compute proper normal in computeGeometry
        } else {
            // Boundary face (only one cell)
            f.lc = cells[0].first;
            f.rc = -1;
            auto bcIt = bfaceBC.find(key);
            if (bcIt != bfaceBC.end()) {
                f.bcTag = bcIt->second;
            } else {
                // Boundary edge not found in BC sections — this shouldn't happen
                // Assign as farfield as fallback
                f.bcTag = getBCTag(BCType::Farfield, "unmatched");
            }
        }
        mesh.faces.push_back(f);
    }

    mesh.nface = mesh.faces.size();
    mesh.nInteriorFace = 0;
    mesh.nBoundaryFace = 0;
    for (auto& f : mesh.faces) {
        if (f.rc >= 0) mesh.nInteriorFace++;
        else mesh.nBoundaryFace++;
    }

    // Compute geometry
    computeGeometry(mesh);
}

// Compute signed area of a polygon (shoelace formula)
double polyArea(const std::vector<double>& xs, const std::vector<double>& ys) {
    double a = 0;
    int n = xs.size();
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        a += xs[i] * ys[j] - xs[j] * ys[i];
    }
    return 0.5 * a;
}

void computeGeometry(Mesh& mesh) {
    mesh.cellCx.resize(mesh.ncell);
    mesh.cellCy.resize(mesh.ncell);
    mesh.cellVol.resize(mesh.ncell);

    // Cell centers and volumes
    for (int i = 0; i < mesh.ncell; i++) {
        const Cell& c = mesh.cells[i];
        double cx = 0, cy = 0;
        for (int k = 0; k < c.nNodes; k++) {
            cx += mesh.vx[c.nodes[k]];
            cy += mesh.vy[c.nodes[k]];
        }
        cx /= c.nNodes;
        cy /= c.nNodes;
        mesh.cellCx[i] = cx;
        mesh.cellCy[i] = cy;

        // Volume (area) using shoelace
        std::vector<double> xs(c.nNodes), ys(c.nNodes);
        for (int k = 0; k < c.nNodes; k++) {
            xs[k] = mesh.vx[c.nodes[k]];
            ys[k] = mesh.vy[c.nodes[k]];
        }
        double vol = std::abs(polyArea(xs, ys));
        mesh.cellVol[i] = std::max(vol, 1e-20);
    }

    // Face normals and centers
    for (auto& f : mesh.faces) {
        double x0 = mesh.vx[f.n0], y0 = mesh.vy[f.n0];
        double x1 = mesh.vx[f.n1], y1 = mesh.vy[f.n1];
        double dx = x1 - x0, dy = y1 - y0;
        f.area = std::sqrt(dx*dx + dy*dy);
        f.cx = 0.5 * (x0 + x1);
        f.cy = 0.5 * (y0 + y1);

        // Normal: rotate edge by 90 degrees. For CCW-oriented cells, the outward normal
        // of edge (n0->n1) is (dy, -dx)/|S|. We need the normal to point from lc to rc.
        double nx_raw = dy / std::max(f.area, 1e-20);
        double ny_raw = -dx / std::max(f.area, 1e-20);

        if (f.rc >= 0) {
            // Interior face: ensure normal points from lc to rc
            double dir = (mesh.cellCx[f.rc] - mesh.cellCx[f.lc]) * nx_raw +
                         (mesh.cellCy[f.rc] - mesh.cellCy[f.lc]) * ny_raw;
            if (dir < 0) {
                nx_raw = -nx_raw;
                ny_raw = -ny_raw;
                std::swap(f.n0, f.n1); // keep consistent
            }
        } else {
            // Boundary face: ensure normal points outward from lc (into the boundary)
            double dir = (f.cx - mesh.cellCx[f.lc]) * nx_raw +
                         (f.cy - mesh.cellCy[f.lc]) * ny_raw;
            if (dir < 0) {
                nx_raw = -nx_raw;
                ny_raw = -ny_raw;
                std::swap(f.n0, f.n1);
            }
        }
        f.nx = nx_raw;
        f.ny = ny_raw;
    }
}

void buildFaces(Mesh& mesh) {
    // This is called from readCGNSMesh but the actual face building is done there
    // This is a placeholder for potential standalone use
}

void printMeshStats(const Mesh& mesh) {
    std::cerr << "Mesh: " << mesh.meshFile << "\n";
    std::cerr << "  Vertices: " << mesh.nvert << "\n";
    std::cerr << "  Cells: " << mesh.ncell << "\n";
    std::cerr << "  Faces: " << mesh.nface
              << " (interior=" << mesh.nInteriorFace
              << ", boundary=" << mesh.nBoundaryFace << ")\n";
    std::cerr << "  Bounding box: [" << mesh.xmin << "," << mesh.xmax
              << "] x [" << mesh.ymin << "," << mesh.ymax << "]\n";
    for (int i = 0; i < (int)mesh.bcInfos.size(); i++) {
        std::cerr << "  BC[" << i << "]: " << mesh.bcInfos[i].familyName
                  << " -> " << bcTypeToString(mesh.bcInfos[i].type) << "\n";
    }
}

} // namespace cfd2d
