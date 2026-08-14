#pragma once
#include "types.hpp"
#include <string>

namespace cfd {

Mesh read_cgns_mesh(const std::string& filename);
void compute_face_geometry(Mesh& mesh);
void compute_cell_geometry(Mesh& mesh);
PartitionInfo partition_mesh_metis(const Mesh& mesh, idx_t n_parts);
void build_local_mesh(const Mesh& global_mesh, const PartitionInfo& part,
                      idx_t rank, LocalMesh& local);

} // namespace cfd
