#pragma once
#include <vector>
#include <array>
#include <string>
#include <map>
#include <set>
#include <cstdint>
#include "Config.hpp"

using Index = int;
using StateVec = std::array<double, 4>;
using GradVec  = std::array<double, 2>;  // gradient in x,y

// A 4-component gradient: gradients of each conservative variable in x and y
using StateGrad = std::array<GradVec, 4>;

// Global (pre-partition) mesh representation
struct GlobalMesh {
    // Nodes
    std::vector<double> x, y;  // node coordinates
    int n_nodes = 0;

    // Cells (volume elements: triangles and quads)
    // Each cell stores its node list (3 or 4 nodes)
    std::vector<std::vector<int>> cell_nodes;  // cell_nodes[i] = list of node ids
    int n_cells = 0;

    // Faces (edges in 2D)
    // face_nodes[i] = {n0, n1}, sorted
    std::vector<std::array<int,2>> face_nodes;
    std::vector<int> face_left;   // left cell (owner)
    std::vector<int> face_right;  // right cell (-1 = boundary)
    std::vector<double> face_nx, face_ny;  // outward normal from left cell, unit vector
    std::vector<double> face_area;         // edge length in 2D
    std::vector<double> face_cx, face_cy;  // face centroid
    int n_faces = 0, n_interior_faces = 0, n_boundary_faces = 0;

    // Cell geometry
    std::vector<double> cell_cx, cell_cy;  // cell centroid
    std::vector<double> cell_vol;           // cell volume (area in 2D)

    // Boundary face info (for the boundary faces only)
    std::vector<std::string> bface_family;  // boundary family name
    std::vector<BcType> bface_type;         // BC type from case config
    std::vector<int> bface_cell;            // adjacent interior cell
    std::vector<int> bface_face_id;         // global face id

    // Adjacency: cell_faces[i] = list of face ids for cell i
    std::vector<std::vector<int>> cell_faces;
    // cell_neighbors[i] = list of neighbor cell ids (or -1 for boundary)
    std::vector<std::vector<int>> cell_neighbors;

    void applyBcMapping(const std::map<std::string, BcType>& bc_map) {
        for (auto& fam : bface_family) {
            auto it = bc_map.find(fam);
            bface_type.push_back(it != bc_map.end() ? it->second : BcType::Unknown);
        }
    }
};

// Local (rank-local) mesh after partitioning
struct LocalMesh {
    int rank = 0, n_ranks = 0;

    // Owned cells: 0..n_owned-1
    // Ghost cells: n_owned..n_owned+n_ghost-1
    int n_owned = 0, n_ghost = 0;

    // Node coordinates for local cells
    std::vector<double> x, y;
    int n_nodes_local = 0;

    // Global cell id for each local cell
    std::vector<int> global_cell_id;

    // Cell geometry
    std::vector<double> cell_cx, cell_cy, cell_vol;

    // Local faces (between owned-owned or owned-ghost pairs)
    // face_nodes, face_nx, face_ny, face_area, face_cx, face_cy
    std::vector<std::array<int,2>> face_nodes_local;  // local node ids
    std::vector<int> face_left_local, face_right_local; // local cell ids
    std::vector<double> face_nx, face_ny, face_area, face_cx, face_cy;
    int n_faces_local = 0;

    // Boundary faces (only owned cells adjacent to boundaries)
    std::vector<int> bface_face_id;   // index into face_nodes_local etc
    std::vector<BcType> bface_type;
    std::vector<std::string> bface_family;
    int n_bfaces = 0;

    // Cell node list (for gradient computation and output)
    std::vector<std::vector<int>> cell_nodes_local;  // local node ids

    // Per-cell face and neighbor lists (built in Partitioner)
    std::vector<std::vector<int>> cell_face_ids_local;  // all face ids per cell
    std::vector<std::vector<int>> cell_nbrs_local_int;  // interior neighbor cell ids per cell

    // MPI halo exchange info
    // For each neighbor rank: list of owned cell local ids to send,
    // and list of ghost cell local ids to receive
    struct HaloInfo {
        int neighbor_rank;
        std::vector<int> send_cells;  // local ids of owned cells to send
        std::vector<int> recv_cells;  // local ids of ghost cells to receive
    };
    std::vector<HaloInfo> halos;

    // Partition diagnostics
    int partition_edge_cut = 0;
    std::vector<int> neighbor_ranks;
};

// Force coefficients
struct Forces {
    double cl = 0, cd = 0, cmz = 0;
    double pressure_drag = 0, viscous_drag = 0;
    double pressure_lift = 0, viscous_lift = 0;
};
