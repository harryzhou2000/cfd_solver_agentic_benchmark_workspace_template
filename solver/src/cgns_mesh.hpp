#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

// Serial (rank-0) global mesh read from a CGNS file. Supports multiple
// unstructured 2-D zones with conformal zone interfaces: nodes that are
// geometrically coincident across zones are merged into a single global node
// set, after which interior faces between zones are discovered by the usual
// edge-pairing construction.
struct SerialMesh {
    int num_nodes = 0;
    std::vector<double> node_x, node_y;

    int num_cells = 0;
    std::vector<std::array<int, 4>> cell_nodes; // trailing entries unused for tris
    std::vector<int> cell_nnodes;               // 3 or 4
    std::vector<int> cell_zone;                 // originating zone (informational)

    struct Face {
        int n1, n2;   // global node ids
        int c0, c1;   // left/right cell, c1 == -1 for boundary
        int bc_id;    // index into bc_names for boundary faces, -1 interior
    };
    int num_faces = 0;
    std::vector<Face> faces;
    std::vector<std::string> bc_names; // boundary family names in order of bc_id

    // cell-to-cell adjacency through interior faces (for METIS)
    std::vector<int> xadj, adjncy;
    long num_adj_edges = 0;

    // Build faces from cell connectivity; boundary_faces carry family names.
    void build_faces(const std::vector<std::array<int, 2>>& bedge_nodes,
                     const std::vector<int>& bedge_family,
                     const std::vector<std::string>& family_names);
    void build_adjacency();
};

// Read a CGNS mesh file. Returns the merged global mesh with faces, boundary
// tags (family names from boundary BAR_2 sections / BC entries) and cell
// adjacency. Throws std::runtime_error with cgns error text on failure.
SerialMesh read_cgns_mesh(const std::string& path);
