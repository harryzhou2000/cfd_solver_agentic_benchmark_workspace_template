#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace cfd {

// Raw CGNS import data, before geometry construction. All indices are
// 0-based (CGNS 1-based indices are converted on read).
struct MeshData {
    // Coordinates
    std::vector<double> x, y;
    cgsize_t n_nodes = 0;

    // Volume cells
    cgsize_t n_cells = 0;
    // cell_nodes[i] = vector of 0-based node indices for cell i
    std::vector<std::vector<cgsize_t>> cell_nodes;

    // Boundary face data from CGNS import (BAR_2 boundary elements
    // referenced by ZoneBC entries, or boundary edges of volume cells).
    struct BFaceInfo {
        cgsize_t cell_id = -1;        // 0-based cell index this face belongs to (-1 if unresolved)
        std::string family_name;      // "bc-2", "WALL", "FAR", ...
        std::vector<cgsize_t> node_ids;  // 0-based node indices on this edge
    };
    std::vector<BFaceInfo> boundary_faces;

    // Zonal interface face: an internal face between two cells of different
    // zones. Multi-zone meshes declare the inter-zone boundary as BAR_2
    // element sections that no ZoneBC entry references; the reader matches
    // the coincident edges (one copy per zone) by endpoint coordinates and
    // records the pair here. `node_a`/`node_b` are the interface edge
    // endpoints from the first zone's copy (used for the face geometry).
    struct ZonalFace {
        cgsize_t cell_l = -1;  // adjacent cell on the first zone's side
        cgsize_t cell_r = -1;  // adjacent cell on the second zone's side
        cgsize_t node_a = -1;  // interface edge endpoints (zone A's nodes)
        cgsize_t node_b = -1;
    };
    std::vector<ZonalFace> zonal_faces;
};

// Reads a CGNS mesh file (all bases/zones, concatenated) into raw MeshData.
// Throws std::runtime_error with the CGNS error string on failure.
MeshData read_mesh_cgns(const std::string& filename);

}  // namespace cfd
