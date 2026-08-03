#pragma once
#include "mesh_types.hpp"
#include <vector>
#include <mpi.h>

namespace solver {

struct NeighborInfo {
    int rank;
    std::vector<int> send_indices;  // owned cell LOCAL indices to send
    std::vector<int> recv_indices;  // ghost cell LOCAL indices (in ghost array) to receive
};

struct HaloBuffers {
    std::vector<MPI_Request> requests;
    std::vector<std::vector<double>> send_bufs;  // per-neighbor, flat doubles (4 * nsend)
    std::vector<std::vector<double>> recv_bufs;  // per-neighbor, flat doubles (4 * nrecv)
};

struct DistributedMesh {
    int rank = 0;
    int num_ranks = 1;
    
    std::vector<Cell> owned_cells;
    std::vector<Cell> ghost_cells;
    std::vector<Face> local_faces;  // all faces touching at least one owned cell
    
    // Per-face: local cell indices (into owned+ghost combined array). -1 for boundary.
    std::vector<int> face_left;   // local owned+ghost index
    std::vector<int> face_right;  // -1 for boundary
    
    std::vector<BCFamily> bc_families;
    std::vector<NeighborInfo> neighbors;
    
    // Mapping: global_cell_id -> local index, -1 if not local (owned+ghost)
    std::vector<int> global_to_local;
    
    MPI_Comm comm;
};

// Halo: pack owned state, post Isend/Irecv, wait+unpack ghost state
void init_halo_buffers(const DistributedMesh& mesh, HaloBuffers& bufs);
void start_halo_exchange(const std::vector<double>& state, DistributedMesh& mesh, HaloBuffers& bufs);
void finish_halo_exchange(std::vector<double>& state, DistributedMesh& mesh, HaloBuffers& bufs);

} // namespace solver
