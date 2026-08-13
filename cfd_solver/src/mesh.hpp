#pragma once
#include "types.hpp"

namespace cfd {

Mesh read_cgns_mesh(const std::string& filename);
void compute_face_geometry(Mesh& mesh);
void compute_cell_geometry(Mesh& mesh);

// Partition: serial preprocessing (METIS)
PartitionInfo partition_mesh_metis(const Mesh& mesh, idx_t n_parts);

// Build rank-local mesh from partition
struct LocalMesh {
    std::vector<Cell> owned_cells;
    std::vector<Cell> ghost_cells;
    std::vector<Face> faces;          // all faces needed locally
    std::vector<idx_t> owned_to_global;
    std::vector<idx_t> ghost_to_global;
    std::vector<idx_t> global_to_local; // sparse: -1 = not owned
    
    // Neighbor communication info
    struct NeighborInfo {
        idx_t rank;
        std::vector<idx_t> send_cells;    // local cell indices to send
        std::vector<idx_t> recv_cells;    // ghost cell indices receiving
        std::vector<idx_t> send_global;
        std::vector<idx_t> recv_global;
    };
    std::vector<NeighborInfo> neighbors;
    
    idx_t n_owned;
    idx_t n_ghost;
    idx_t n_total;
    idx_t n_boundary_faces;
};

void build_local_mesh(const Mesh& global_mesh, const PartitionInfo& part,
                      idx_t rank, LocalMesh& local);

} // namespace cfd
