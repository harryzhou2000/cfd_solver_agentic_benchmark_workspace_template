#pragma once

#include <map>
#include <string>
#include <vector>

#include "common.hpp"

namespace cfd {

struct GlobalCell {
    std::vector<int64_t> nodes;  // global 1-based node ids
    Vec2 center{0.0, 0.0};
    double volume = 0.0;
};

struct GlobalFace {
    int left = -1;
    int right = -1;
    Vec2 center{0.0, 0.0};
    Vec2 n{0.0, 0.0};  // unit normal oriented left -> right (outward for BC)
    double area = 0.0;
    BcType bc = BcType::Interior;
    int bc_name_id = -1;  // index into boundary_names
};

struct GlobalMesh {
    std::vector<double> node_x;
    std::vector<double> node_y;
    std::vector<GlobalCell> cells;
    std::vector<GlobalFace> faces;           // all faces (interior + boundary)
    std::vector<GlobalFace> boundary_faces;  // boundary faces only
    std::vector<std::string> boundary_names; // bc family names (id <-> name)
    std::vector<int> boundary_face_of_cell_counts;

    int num_cells() const { return static_cast<int>(cells.size()); }
    int num_faces() const { return static_cast<int>(faces.size()); }
    int num_boundary_faces() const {
        return static_cast<int>(boundary_faces.size());
    }
};

// Reads a CGNS mesh with mixed TRI_3/QUAD_4 cell sections and boundary
// sections, merges zones through exact-coordinate face matching, and builds
// the global cell-face adjacency. `bc_map` maps family/section names to solver
// BC types; names not present are ignored (e.g. 1-to-1 interface sections).
GlobalMesh read_cgns_mesh(const std::string& path,
                          const std::map<std::string, BcType>& bc_map);

}  // namespace cfd
