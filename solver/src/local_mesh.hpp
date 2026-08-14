#pragma once

#include <array>
#include <string>
#include <vector>

// Boundary condition kinds used by the solver.
enum class BCType : int { Interior = 0, Farfield = 1, SlipWall = 2, NoSlipAdiabatic = 3 };

// Rank-local mesh: owned cells [0, n_own) plus one layer of ghost cells
// [n_own, n_cell). Every face touches at least one owned cell.
struct LocalMesh {
    int rank = 0, nranks = 1;

    int n_node = 0;
    std::vector<double> node_x, node_y;
    std::vector<long long> node_global; // global node id per local node

    int n_own = 0;  // owned cells
    int n_cell = 0; // owned + ghost cells
    std::vector<std::array<int, 4>> cell_nodes;
    std::vector<int> cell_nnodes;
    std::vector<long long> cell_global; // global cell id per local cell
    std::vector<double> cell_cx, cell_cy, cell_vol;

    struct LFace {
        int c0, c1;     // local cell ids; c1 >= n_own means ghost; c1 == -1 boundary
        int n1, n2;     // local node ids
        double nx, ny;  // unit normal, oriented from c0 to c1 (outward for boundary)
        double area;
        double cx, cy;  // face center
        BCType bc = BCType::Interior;
        std::string bc_name; // family name for boundary faces
    };
    std::vector<LFace> faces;
    int n_bface = 0; // number of boundary faces on this rank

    // Halo exchange maps (neighbor-scoped).
    struct Halo {
        int neighbor;
        std::vector<int> send_cells; // local owned cell ids to send
        std::vector<int> recv_cells; // local ghost cell ids filled by recv
    };
    std::vector<Halo> halos;

    // wall face lists per family for surface output / forces
    std::vector<int> wall_faces(const std::string& family) const;
};

// Global partition diagnostics collected on rank 0.
struct PartitionInfo {
    long edge_cut = 0;
    std::vector<int> owned_per_rank;
    std::vector<int> ghost_per_rank;
    std::vector<int> bface_per_rank;
    std::vector<int> num_neighbors;
    std::vector<std::vector<int>> neighbor_ranks;
    std::vector<int> send_cells;
    std::vector<int> recv_cells;
};
