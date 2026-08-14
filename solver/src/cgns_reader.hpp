#pragma once
#include "types.hpp"
#include <cgnslib.h>
#include <unordered_map>
#include <algorithm>
#include <set>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstring>

namespace cfd {

// Raw mesh read from CGNS: nodes, volume cells, boundary faces
struct RawMesh {
    std::vector<Real> x, y;           // node coordinates
    std::vector<std::vector<int>> cells; // cell -> node indices (3 or 4)
    std::vector<ElemType> cellTypes;

    // Boundary faces: each has 2 node indices + bc family name
    std::vector<std::array<int,2>> bfaceNodes;
    std::vector<std::string> bfaceFamily;

    std::vector<std::string> familyNames; // all families found

    int numNodes() const { return (int)x.size(); }
    int numCells() const { return (int)cells.size(); }
    int numBFaces() const { return (int)bfaceNodes.size(); }
};

// Spatial hash for node merging
class NodeMerger {
public:
    NodeMerger(Real tol) : tol_(tol) {}

    int findOrInsert(Real px, Real py, int origIdx) {
        long hx = (long)std::round(px / tol_);
        long hy = (long)std::round(py / tol_);
        // Check this cell and neighbors
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                long key = hashKey(hx + dx, hy + dy);
                auto it = map_.find(key);
                if (it != map_.end()) {
                    for (int idx : it->second) {
                        if (std::abs(px - px_[idx]) < tol_ &&
                            std::abs(py - py_[idx]) < tol_) {
                            return idx;
                        }
                    }
                }
            }
        }
        int newIdx = (int)px_.size();
        px_.push_back(px);
        py_.push_back(py);
        long key = hashKey(hx, hy);
        map_[key].push_back(newIdx);
        origToMerged_[origIdx] = newIdx;
        return newIdx;
    }

    Real getX(int i) const { return px_[i]; }
    Real getY(int i) const { return py_[i]; }
    int numMerged() const { return (int)px_.size(); }

private:
    Real tol_;
    std::vector<Real> px_, py_;
    std::unordered_map<long, std::vector<int>> map_;
    std::map<int,int> origToMerged_;

    long hashKey(long hx, long hy) {
        return hx * 73856093L ^ hy * 19349663L;
    }
};

class CgnsReader {
public:
    static bool read(const std::string& filename,
                     const std::map<std::string, std::string>& bcMapping,
                     RawMesh& mesh,
                     std::string& err) {
        int fn = 0;
        if (cg_open(filename.c_str(), CG_MODE_READ, &fn) != CG_OK) {
            err = std::string("cg_open failed: ") + cg_get_error();
            return false;
        }

        // Collect raw data from all zones
        std::vector<std::vector<Real>> zoneX, zoneY;
        std::vector<std::vector<std::vector<int>>> zoneCells;
        std::vector<std::vector<ElemType>> zoneCellTypes;
        std::vector<std::vector<std::array<int,2>>> zoneBFaces;
        std::vector<std::vector<std::string>> zoneBFamily;
        std::vector<std::vector<std::string>> zoneBFamilies;

        int nbases = 0;
        cg_nbases(fn, &nbases);
        if (nbases < 1) { err = "No bases in CGNS file"; cg_close(fn); return false; }

        for (int B = 1; B <= nbases; B++) {
            char basename[128];
            int cell_dim = 0, phys_dim = 0;
            cg_base_read(fn, B, basename, &cell_dim, &phys_dim);

            int nzones = 0;
            cg_nzones(fn, B, &nzones);

            for (int Z = 1; Z <= nzones; Z++) {
                char zonename[128];
                cgsize_t sizes[9];
                cg_zone_read(fn, B, Z, zonename, sizes);
                int nverts = (int)sizes[0];

                std::vector<Real> zx(nverts), zy(nverts);
                int ncoords = 0;
                cg_ncoords(fn, B, Z, &ncoords);
                for (int C = 1; C <= ncoords; C++) {
                    char coordname[128];
                    DataType_t dt;
                    cg_coord_info(fn, B, Z, C, &dt, coordname);
                    std::vector<double> buf(nverts);
                    cgsize_t rmin[3] = {1, 1, 1};
                    cgsize_t rmax[3] = {nverts, nverts, nverts};
                    cg_coord_read(fn, B, Z, coordname, RealDouble, rmin, rmax, buf.data());
                    // Determine if X or Y
                    std::string cn(coordname);
                    if (cn.find('X') != std::string::npos || cn.find('x') != std::string::npos || C == 1) {
                        for (int i = 0; i < nverts; i++) zx[i] = buf[i];
                    } else if (cn.find('Y') != std::string::npos || cn.find('y') != std::string::npos || C == 2) {
                        for (int i = 0; i < nverts; i++) zy[i] = buf[i];
                    }
                }

                zoneX.push_back(zx);
                zoneY.push_back(zy);

                // Read element sections
                std::vector<std::vector<int>> zcells;
                std::vector<ElemType> zctypes;
                std::vector<std::array<int,2>> zbf;
                std::vector<std::string> zbfn;

                int nsections = 0;
                cg_nsections(fn, B, Z, &nsections);

                // Build local node offset for this zone (will be merged later)
                for (int S = 1; S <= nsections; S++) {
                    char secname[128];
                    ElementType_t etype;
                    cgsize_t start, end;
                    int nbndry, parent_flag;
                    cg_section_read(fn, B, Z, S, secname, &etype, &start, &end, &nbndry, &parent_flag);

                    int nElem = (int)(end - start + 1);
                    int nn = 0;
                    switch (etype) {
                        case TRI_3: nn = 3; break;
                        case QUAD_4: nn = 4; break;
                        case BAR_2: nn = 2; break;
                        case BAR_3: nn = 3; break;
                        default: continue; // skip unsupported
                    }

                    cgsize_t edata_size = 0;
                    cg_ElementDataSize(fn, B, Z, S, &edata_size);
                    std::vector<cgsize_t> conn(edata_size);
                    cg_elements_read(fn, B, Z, S, conn.data(), nullptr);

                    std::string sname(secname);

                    if (etype == TRI_3 || etype == QUAD_4) {
                        // Volume cells
                        for (int e = 0; e < nElem; e++) {
                            std::vector<int> cell(nn);
                            for (int k = 0; k < nn; k++)
                                cell[k] = (int)conn[e * nn + k] - 1; // 0-based
                            zcells.push_back(cell);
                            zctypes.push_back(etype == TRI_3 ? ElemType::Tri3 : ElemType::Quad4);
                        }
                    } else if (etype == BAR_2 || etype == BAR_3) {
                        // Boundary or interface faces
                        int nn2 = (etype == BAR_2) ? 2 : 3;
                        bool isBC = (bcMapping.find(sname) != bcMapping.end());
                        if (isBC) {
                            for (int e = 0; e < nElem; e++) {
                                std::array<int,2> face;
                                face[0] = (int)conn[e * nn2] - 1;
                                face[1] = (int)conn[e * nn2 + 1] - 1;
                                zbf.push_back(face);
                                zbfn.push_back(sname);
                            }
                        }
                        // Interface faces (not in bcMapping) are skipped -
                        // they'll become internal faces after node merging
                    }
                }

                zoneCells.push_back(zcells);
                zoneCellTypes.push_back(zctypes);
                zoneBFaces.push_back(zbf);
                zoneBFamily.push_back(zbfn);

                // Read families from ZoneBC
                int nbocos = 0;
                cg_nbocos(fn, B, Z, &nbocos);
                for (int BC = 1; BC <= nbocos; BC++) {
                    char boconame[128];
                    BCType_t btype;
                    PointSetType_t ptset;
                    cgsize_t npnts = 0;
                    int normalindex[3];
                    cgsize_t normallistsize;
                    DataType_t normaldatatype;
                    int ndataset;
                    cg_boco_info(fn, B, Z, BC, boconame, &btype, &ptset, &npnts,
                                 normalindex, &normallistsize, &normaldatatype, &ndataset);

                    // Read family name
                    char famname[128] = "";
                    if (cg_goto(fn, B, "Zone_t", Z, "ZoneBC_t", 1, "BC_t", BC, "end") == CG_OK) {
                        cg_famname_read(famname);
                    }
                    std::string fname = (strlen(famname) > 0) ? std::string(famname) : std::string(boconame);
                    mesh.familyNames.push_back(fname);
                }
            }
        }

        cg_close(fn);

        // Merge zones using coordinate-based node merging
        int nzones = (int)zoneX.size();

        // Compute bounding box for tolerance
        Real xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
        for (int z = 0; z < nzones; z++) {
            for (size_t i = 0; i < zoneX[z].size(); i++) {
                xmin = std::min(xmin, zoneX[z][i]);
                xmax = std::max(xmax, zoneX[z][i]);
                ymin = std::min(ymin, zoneY[z][i]);
                ymax = std::max(ymax, zoneY[z][i]);
            }
        }
        Real diag = std::sqrt((xmax-xmin)*(xmax-xmin) + (ymax-ymin)*(ymax-ymin));
        Real tol = diag * 1e-10;
        if (tol < 1e-14) tol = 1e-14;

        NodeMerger merger(tol);

        // Global node index mapping per zone: zoneLocalNode -> globalNode
        std::vector<std::vector<int>> nodeMap(nzones);

        for (int z = 0; z < nzones; z++) {
            int nv = (int)zoneX[z].size();
            nodeMap[z].resize(nv);
            for (int i = 0; i < nv; i++) {
                int origIdx = z * 100000000 + i; // unique original index
                int gidx = merger.findOrInsert(zoneX[z][i], zoneY[z][i], origIdx);
                nodeMap[z][i] = gidx;
            }
        }

        int ngnodes = merger.numMerged();
        mesh.x.resize(ngnodes);
        mesh.y.resize(ngnodes);
        for (int i = 0; i < ngnodes; i++) {
            mesh.x[i] = merger.getX(i);
            mesh.y[i] = merger.getY(i);
        }

        // Build cells with global node indices
        for (int z = 0; z < nzones; z++) {
            for (size_t c = 0; c < zoneCells[z].size(); c++) {
                std::vector<int> cell(zoneCells[z][c].size());
                for (size_t k = 0; k < cell.size(); k++) {
                    int localNode = zoneCells[z][c][k];
                    cell[k] = nodeMap[z][localNode];
                }
                mesh.cells.push_back(cell);
                mesh.cellTypes.push_back(zoneCellTypes[z][c]);
            }
        }

        // Build boundary faces with global node indices
        for (int z = 0; z < nzones; z++) {
            for (size_t b = 0; b < zoneBFaces[z].size(); b++) {
                std::array<int,2> face;
                face[0] = nodeMap[z][zoneBFaces[z][b][0]];
                face[1] = nodeMap[z][zoneBFaces[z][b][1]];
                mesh.bfaceNodes.push_back(face);
                mesh.bfaceFamily.push_back(zoneBFamily[z][b]);
            }
        }

        // Remove duplicate boundary faces (can happen at zone interfaces)
        {
            std::set<std::pair<int,int>> seen;
            std::vector<std::array<int,2>> newBF;
            std::vector<std::string> newFam;
            for (size_t b = 0; b < mesh.bfaceNodes.size(); b++) {
                int n0 = mesh.bfaceNodes[b][0];
                int n1 = mesh.bfaceNodes[b][1];
                auto key = std::minmax(n0, n1);
                if (seen.find(key) == seen.end()) {
                    seen.insert(key);
                    newBF.push_back(mesh.bfaceNodes[b]);
                    newFam.push_back(mesh.bfaceFamily[b]);
                }
            }
            mesh.bfaceNodes = newBF;
            mesh.bfaceFamily = newFam;
        }

        return true;
    }
};

} // namespace cfd
