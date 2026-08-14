#pragma once
#include "types.hpp"
#include "cgns_reader.hpp"
#include <map>
#include <unordered_map>
#include <set>
#include <cmath>
#include <algorithm>

namespace cfd {

struct Face {
    int n0, n1;         // node indices (global)
    int lc, rc;         // left cell, right cell (-1 if boundary)
    Real nx, ny;        // normal vector (lc->rc), magnitude = area
    Real fx, fy;        // face center
    Real area;          // face length
    BCType bc;          // boundary condition
    std::string family; // boundary family name
    int bcFaceIdx;      // index into boundary face list (-1 if internal)
};

struct GlobalMesh {
    std::vector<Real> x, y;
    std::vector<std::vector<int>> cellNodes;
    std::vector<ElemType> cellTypes;
    std::vector<Real> cellCx, cellCy, cellVol;
    std::vector<Face> faces;
    std::vector<int> bfaceIndices; // indices into faces[] that are boundary faces
    std::vector<std::vector<int>> cellFaceList; // cell -> face indices

    int numCells() const { return (int)cellNodes.size(); }
    int numNodes() const { return (int)x.size(); }
    int numFaces() const { return (int)faces.size(); }
    int numBFaces() const { return (int)bfaceIndices.size(); }
};

class MeshBuilder {
public:
    static GlobalMesh build(const RawMesh& raw,
                            const std::map<std::string, std::string>& bcMapping,
                            std::string& err) {
        GlobalMesh mesh;
        mesh.x = raw.x;
        mesh.y = raw.y;
        mesh.cellNodes = raw.cells;
        mesh.cellTypes = raw.cellTypes;

        int nc = mesh.numCells();
        mesh.cellCx.resize(nc);
        mesh.cellCy.resize(nc);
        mesh.cellVol.resize(nc);

        // Compute cell centers and volumes (shoelace)
        for (int c = 0; c < nc; c++) {
            auto& cn = mesh.cellNodes[c];
            int nn = (int)cn.size();
            Real cx = 0, cy = 0, vol = 0;
            for (int i = 0; i < nn; i++) {
                int n0 = cn[i];
                int n1 = cn[(i + 1) % nn];
                Real dx = mesh.x[n1] - mesh.x[n0];
                Real dy = mesh.y[n1] - mesh.y[n0];
                // Shoelace: area = 0.5 * sum(x_i * y_{i+1} - x_{i+1} * y_i)
                vol += mesh.x[n0] * mesh.y[n1] - mesh.x[n1] * mesh.y[n0];
                cx += (mesh.x[n0] + mesh.x[n1]);
                cy += (mesh.y[n0] + mesh.y[n1]);
            }
            vol *= 0.5;
            // Check orientation: if negative, reverse nodes
            if (vol < 0) {
                std::reverse(mesh.cellNodes[c].begin(), mesh.cellNodes[c].end());
                vol = -vol;
            }
            // Cell center as average of edge midpoints (more accurate for quads)
            mesh.cellCx[c] = cx / (2.0 * nn);
            mesh.cellCy[c] = cy / (2.0 * nn);
            mesh.cellVol[c] = vol;
        }

        // Build edge -> cells map
        // Key: sorted pair of node indices
        struct EdgeInfo {
            int cell;
            int n0, n1; // directed edge (in cell node order)
        };
        std::map<std::pair<int,int>, std::vector<EdgeInfo>> edgeMap;

        for (int c = 0; c < nc; c++) {
            auto& cn = mesh.cellNodes[c];
            int nn = (int)cn.size();
            for (int i = 0; i < nn; i++) {
                int n0 = cn[i];
                int n1 = cn[(i + 1) % nn];
                auto key = std::minmax(n0, n1);
                edgeMap[key].push_back({c, n0, n1});
            }
        }

        // Build boundary face lookup: sorted edge -> (bc type, family)
        std::map<std::pair<int,int>, std::pair<BCType, std::string>> bcLookup;
        for (size_t b = 0; b < raw.bfaceNodes.size(); b++) {
            int n0 = raw.bfaceNodes[b][0];
            int n1 = raw.bfaceNodes[b][1];
            auto key = std::minmax(n0, n1);
            auto it = bcMapping.find(raw.bfaceFamily[b]);
            BCType bct = BCType::None;
            if (it != bcMapping.end()) {
                if (it->second == "farfield") bct = BCType::Farfield;
                else if (it->second == "slip_wall") bct = BCType::SlipWall;
                else if (it->second == "no_slip_adiabatic_wall") bct = BCType::NoSlipAdiabaticWall;
            }
            bcLookup[key] = {bct, raw.bfaceFamily[b]};
        }

        // Build faces
        for (auto& [key, infos] : edgeMap) {
            if (infos.size() == 1) {
                // Boundary face
                int c = infos[0].cell;
                int n0 = infos[0].n0, n1 = infos[0].n1;
                Face f;
                f.n0 = n0; f.n1 = n1;
                f.lc = c; f.rc = -1;

                // Compute normal: (dy, -dx) for CCW cell -> outward
                Real dx = mesh.x[n1] - mesh.x[n0];
                Real dy = mesh.y[n1] - mesh.y[n0];
                f.nx = dy;
                f.ny = -dx;
                f.area = std::sqrt(f.nx * f.nx + f.ny * f.ny);
                f.fx = 0.5 * (mesh.x[n0] + mesh.x[n1]);
                f.fy = 0.5 * (mesh.y[n0] + mesh.y[n1]);

                // Ensure normal points outward (from cell center to face center)
                Real vx = f.fx - mesh.cellCx[c];
                Real vy = f.fy - mesh.cellCy[c];
                if (f.nx * vx + f.ny * vy < 0) {
                    f.nx = -f.nx;
                    f.ny = -f.ny;
                    std::swap(f.n0, f.n1);
                }

                auto bcIt = bcLookup.find(key);
                if (bcIt != bcLookup.end()) {
                    f.bc = bcIt->second.first;
                    f.family = bcIt->second.second;
                } else {
                    f.bc = BCType::Farfield; // default
                    f.family = "unknown";
                }
                f.bcFaceIdx = (int)mesh.bfaceIndices.size();
                mesh.faces.push_back(f);
                mesh.bfaceIndices.push_back((int)mesh.faces.size() - 1);
            } else if (infos.size() == 2) {
                // Internal face
                int c0 = infos[0].cell;
                int c1 = infos[1].cell;
                int n0 = infos[0].n0, n1 = infos[0].n1;
                Face f;
                f.n0 = n0; f.n1 = n1;
                f.lc = c0; f.rc = c1;

                Real dx = mesh.x[n1] - mesh.x[n0];
                Real dy = mesh.y[n1] - mesh.y[n0];
                f.nx = dy;
                f.ny = -dx;
                f.area = std::sqrt(f.nx * f.nx + f.ny * f.ny);
                f.fx = 0.5 * (mesh.x[n0] + mesh.x[n1]);
                f.fy = 0.5 * (mesh.y[n0] + mesh.y[n1]);

                // Ensure normal points from c0 to c1
                Real vx = mesh.cellCx[c1] - mesh.cellCx[c0];
                Real vy = mesh.cellCy[c1] - mesh.cellCy[c0];
                if (f.nx * vx + f.ny * vy < 0) {
                    f.nx = -f.nx;
                    f.ny = -f.ny;
                    std::swap(f.lc, f.rc);
                    std::swap(f.n0, f.n1);
                }
                f.bc = BCType::None;
                f.family = "";
                f.bcFaceIdx = -1;
                mesh.faces.push_back(f);
            }
            // Edges with >2 cells are degenerate; skip
        }

        return mesh;
    }

    // Build cell-to-face adjacency
    static void buildCellFaces(GlobalMesh& mesh) {
        // For each cell, find its faces
        // This is used for the LU-SGS ordering and residual assembly
        // We store face indices per cell
        mesh.cellFaceList.clear();
        mesh.cellFaceList.resize(mesh.numCells());
        for (int fi = 0; fi < mesh.numFaces(); fi++) {
            auto& f = mesh.faces[fi];
            mesh.cellFaceList[f.lc].push_back(fi);
            if (f.rc >= 0) mesh.cellFaceList[f.rc].push_back(fi);
        }
    }
};

// Extended GlobalMesh with cell-face list
// (add to GlobalMesh struct above)

} // namespace cfd
