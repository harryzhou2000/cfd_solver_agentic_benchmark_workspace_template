#pragma once

#include "common.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cfd {

// ---------------------------------------------------------------------------
// Global (preprocessing-stage) unstructured mesh.
// Cells are polygons (triangles/quads) with CCW node ordering.
// ---------------------------------------------------------------------------
struct GlobalMesh {
    std::vector<double> node_x, node_y;
    std::vector<int> cell_type;              // number of nodes per cell (3 or 4)
    std::vector<std::vector<int>> cell_nodes; // global node ids
    std::vector<double> cell_cx, cell_cy;    // centroid
    std::vector<double> cell_vol;            // area

    // Faces: internal (2 cells) or boundary (1 cell + BC).
    struct Face {
        int c0 = -1;          // cell index (always valid)
        int c1 = -1;          // neighbor cell, -1 for boundary
        int b0 = -1;          // node ids
        int b1 = -1;
        double nx = 0.0, ny = 0.0;  // unit normal c0 -> c1 (outward for boundary)
        double len = 0.0;
        BCType bc = BCType::Internal;   // boundary condition for boundary faces
        std::string family;             // family/section name
        int global_face_id = -1;
    };
    std::vector<Face> faces;

    int num_nodes_global = 0;
    int num_cells_global = 0;
    int num_faces_global = 0;
    int num_boundary_faces = 0;
    double domain_area = 0.0;

    // For each cell: list of face ids.
    std::vector<std::vector<int>> cell_faces;
};

// Build the global mesh from a CGNS file.
// bc_map: family/section name -> BC type string from the case JSON.
GlobalMesh read_cgns_mesh(const std::string& filename,
                          const std::vector<std::pair<std::string, std::string>>& bc_map);

BCType parse_bc_type(const std::string& s);

}  // namespace cfd
