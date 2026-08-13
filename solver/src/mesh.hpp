// mesh.hpp — Mesh data structures, CGNS reader, topology, and geometry
#pragma once
#include <vector>
#include <string>
#include <array>
#include <map>
#include <unordered_map>
#include <cstdint>
#include "case_input.hpp"

namespace cfd2d {

// Element types
enum class ElemType { Triangle = 3, Quadrilateral = 4 };

// A cell in the mesh (up to 4 nodes)
struct Cell {
    int nodes[4];
    int nNodes; // 3 or 4
    ElemType type;
};

// A face (edge in 2D)
struct Face {
    int n0, n1;       // face nodes (ordered)
    int lc, rc;       // left cell, right cell (-1 if boundary)
    int bcTag;        // BC type index, -1 if interior
    double nx, ny;    // unit normal (pointing from lc to rc)
    double area;      // face length (|S|)
    double cx, cy;    // face center
};

// Boundary condition info
struct BCInfo {
    BCType type;
    std::string familyName;
};

// Global mesh (read by all ranks during preprocessing)
struct Mesh {
    // Vertices
    std::vector<double> vx, vy;
    int nvert = 0;

    // Cells
    std::vector<Cell> cells;
    int ncell = 0;

    // Faces (built from cell connectivity)
    std::vector<Face> faces;
    int nface = 0;
    int nInteriorFace = 0;
    int nBoundaryFace = 0;

    // Cell geometry
    std::vector<double> cellCx, cellCy; // cell centers
    std::vector<double> cellVol;        // cell volumes (areas in 2D)

    // BC info: bcTag index -> BCInfo
    std::vector<BCInfo> bcInfos;

    // Mesh stats
    double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    std::string meshFile;
};

// Read CGNS mesh and build topology/geometry
// bcMap: family name -> BCType (from case file)
void readCGNSMesh(Mesh& mesh, const std::string& filename,
                  const std::map<std::string, BCType>& bcMap);

// Build cell-face adjacency from cell connectivity
void buildFaces(Mesh& mesh);

// Compute cell and face geometry
void computeGeometry(Mesh& mesh);

// Print mesh statistics
void printMeshStats(const Mesh& mesh);

} // namespace cfd2d
