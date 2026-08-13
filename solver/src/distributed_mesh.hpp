#pragma once

#include <mpi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "global_mesh.hpp"
#include "partition.hpp"

namespace cfd {

struct LocalFace {
    int left = -1;   // local cell index (owned or ghost)
    int right = -1;  // local cell index, -1 for boundary faces
    Vec2 center{0.0, 0.0};
    Vec2 n{0.0, 0.0};  // unit normal oriented left -> right (outward if BC)
    double area = 0.0;
    BcType bc = BcType::Interior;
    int bc_name_id = -1;
};

struct HaloPlan {
    std::vector<int> neighbor_ranks;
    // Payload per ghost cell in recv: [rho, rhou, rhov, rhoE,
    //                                   drho_x, drho_y, du_x, du_y,
    //                                   dv_x, dv_y, dp_x, dp_y].
    // Indexed by neighbor position: recv_cells[n] is the list of local ghost
    // cell ids whose data arrives from neighbor_ranks[n] in that order.
    std::vector<std::vector<int>> recv_cells;
    std::vector<std::vector<int>> send_cells;  // local owned ids to send
    std::vector<std::vector<double>> send_buf;
    std::vector<std::vector<double>> recv_buf;
    std::vector<MPI_Request> requests;
};

struct DistributedMesh {
    int rank = 0;
    int nranks = 1;

    int n_owned = 0;   // cells 0..n_owned-1
    int n_local = 0;   // includes ghost cells
    int n_ghost = 0;
    int64_t n_cells_global = 0;
    int64_t n_faces_global = 0;
    int64_t n_boundary_faces_global = 0;

    std::vector<Vec2> cell_center;
    std::vector<double> cell_volume;
    std::vector<std::vector<Vec2>> cell_corners;  // owned cells only
    std::vector<std::vector<int>> cell_faces;  // n_local entries (ghost: empty)
    std::vector<LocalFace> faces;

    std::vector<int64_t> global_cell_id;  // n_local
    std::vector<int> owner_rank;          // n_local

    HaloPlan halo;

    // Precomputed geometry-only convective/viscous spectral coefficients used
    // for the local pseudo time step (filled with freestream properties).
    std::vector<double> conv_radius;   // n_local
    std::vector<double> visc_radius;   // n_local

    std::vector<std::string> boundary_names;

    int64_t partition_edge_cut = 0;
    int64_t num_boundary_faces_local = 0;

    bool is_owned(int i) const { return i >= 0 && i < n_owned; }
    bool is_ghost(int i) const { return i >= n_owned && i < n_local; }
};

// Build the rank-local distributed mesh. Rank 0 performs serial
// preprocessing: reads the mesh (already parsed into `global`), partitions
// with METIS, and distributes compact rank-local payloads. All other ranks
// only ever store their own owned cells plus one ghost layer.
DistributedMesh build_distributed_mesh(const GlobalMesh& global,
                                       const PartitionResult& part,
                                       MPI_Comm comm);

// Asynchronous neighbor exchange of conservative states and primitive
// gradients. `U` has 4 doubles per local cell; `grad` has 8 per local cell.
void begin_halo_exchange(DistributedMesh& mesh, const std::vector<double>& U,
                         const std::vector<double>& grad);
void end_halo_exchange(DistributedMesh& mesh, std::vector<double>& U,
                       std::vector<double>& grad);

}  // namespace cfd
