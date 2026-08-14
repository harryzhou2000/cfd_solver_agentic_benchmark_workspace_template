#pragma once

#include "mesh/mesh.hpp"
#include "common/types.hpp"
#include <mpi.h>
#include <vector>
#include <map>

// Distribute the full serial mesh into rank-local meshes based on METIS partition.
// Each rank receives its owned cells plus ghost cells needed for the stencil.
void distribute_mesh(const Mesh& global_mesh,
                     const std::vector<int>& cell_rank,
                     int nparts,
                     RankMesh& rank_mesh,
                     MPI_Comm comm);

// Build neighbor lists for halo exchange
void build_neighbor_lists(RankMesh& rm, const Mesh& global_mesh,
                          const std::vector<int>& cell_rank,
                          MPI_Comm comm);
