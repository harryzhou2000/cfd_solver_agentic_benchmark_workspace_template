#pragma once

#include <map>
#include <vector>

#include <mpi.h>

#include "cfd/mesh.hpp"

namespace cfd {

struct HaloPlan {
  std::map<int, std::vector<int>> recv_ghost_local_indices;
  std::map<int, std::vector<int>> send_owned_local_indices;
};

struct PartitionResult {
  std::vector<int> owner;
  int edge_cut{0};
};

struct PartitionDiagnostics {
  int rank{0};
  int num_cells_owned{0};
  int num_cells_ghost{0};
  int num_boundary_faces{0};
  std::vector<int> neighbor_ranks;
  std::map<int, int> send_cells;
  std::map<int, int> recv_cells;
};

PartitionResult partition_cells_metis(const GlobalMesh& mesh, int nranks);
LocalMesh build_local_mesh(const GlobalMesh& mesh, const std::vector<int>& owner, int rank);
HaloPlan build_halo_plan(const LocalMesh& local, const std::vector<int>& owner, MPI_Comm comm);
PartitionDiagnostics diagnostics_for_rank(const LocalMesh& local, const HaloPlan& halo, int rank);

}  // namespace cfd
