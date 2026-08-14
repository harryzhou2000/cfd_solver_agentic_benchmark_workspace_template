#pragma once

#include "common.hpp"
#include "partition.hpp"
#include <string>
#include <vector>

namespace cfd {

// ---------------------------------------------------------------------------
// Rank-local mesh: owned cells + ghost cells, internal faces, boundary faces,
// and the halo exchange plan (neighbor-scoped).
// Local cell indexing: owned = [0, n_owned), ghosts = [n_owned, n_owned+n_ghost).
// ---------------------------------------------------------------------------
struct LocalMesh {
    int n_owned = 0;
    int n_ghost = 0;

    // Owned cells.
    std::vector<int> owned_global_id;
    std::vector<double> cell_cx, cell_cy, cell_vol;
    std::vector<std::vector<int>> cell_nodes_local;
    std::vector<int> cell_type;

    // Ghost cells.
    std::vector<int> ghost_global_id;
    std::vector<int> ghost_owner;
    std::vector<double> ghost_cx, ghost_cy;

    // Nodes used by owned cells.
    std::vector<double> node_x, node_y;
    std::vector<int> node_global_id;

    // Internal faces. c0, c1 are local cell indices (owned or ghost).
    struct Face {
        int c0, c1;
        double nx, ny, len, fx, fy;
    };
    std::vector<Face> faces;

    // Boundary faces (local id = n_faces_internal + index).
    struct BFace {
        int c;             // owned local cell
        int b0, b1;        // local node ids
        double nx, ny, len, fx, fy;
        BCType bc;
        int global_face_id;
        std::string family;
    };
    std::vector<BFace> bfaces;

    // cell -> local face ids (internal ids first, then boundary ids)
    std::vector<std::vector<int>> cell_faces;

    // Halo exchange plan: per neighbor rank.
    std::vector<int> neighbor_ranks;
    std::vector<std::vector<int>> send_cells;   // owned local indices to send
    std::vector<std::vector<int>> recv_ghosts;  // ghost local indices to receive

    // Global stats.
    int num_cells_global = 0;
    int num_faces_global = 0;
    int num_nodes_global = 0;
    int edge_cut = 0;
    std::string partitioner = "metis_kway";

    // Derived: for each owned cell, neighbor local indices (owned or ghost).
    std::vector<std::vector<int>> cell_neighbors;

    int n_faces_internal() const { return (int)faces.size(); }
    int n_faces_total() const { return (int)faces.size() + (int)bfaces.size(); }
};

LocalMesh load_local_mesh(const std::string& partition_file);

}  // namespace cfd
