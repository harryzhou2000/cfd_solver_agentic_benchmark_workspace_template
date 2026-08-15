#include "cfd/HaloExchange.hpp"
#include "cfd/Physics.hpp"

#include <mpi.h>

#include <cassert>
#include <iostream>
#include <vector>

namespace {

cfd::LocalMesh two_rank_mesh(int rank) {
  cfd::LocalMesh mesh;
  mesh.rank = rank;
  mesh.ranks = 2;
  mesh.global_cells = 2;
  mesh.owned_count = 1;
  mesh.cells = {
      {rank, rank, {static_cast<double>(rank), 0.0}, 1.0, {}},
      {1 - rank, 1 - rank, {static_cast<double>(1 - rank), 0.0}, 1.0, {}},
  };
  mesh.halo = {{1 - rank, {0}, {1}}};
  return mesh;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int ranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);

  int result = 0;
  if (ranks == 2) {
    const cfd::LocalMesh mesh = two_rank_mesh(rank);
    cfd::HaloExchange exchange(mesh, MPI_COMM_WORLD);
    std::vector<cfd::Conserved> values{
        {10.0 + rank, 20.0 + rank, 30.0 + rank, 40.0 + rank},
        {-1.0, -1.0, -1.0, -1.0},
    };
    exchange.exchange(values, 6101);
    for (int variable = 0; variable < 4; ++variable) {
      if (values[1][variable] != 10.0 * (variable + 1) + (1 - rank)) result = 1;
    }
  } else if (rank == 0) {
    std::cout << "Halo exchange test requires exactly two MPI ranks; skipped\n";
  }

  int global_result = 0;
  MPI_Allreduce(&result, &global_result, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Finalize();
  if (rank == 0 && global_result == 0 && ranks == 2) {
    std::cout << "Two-rank halo exchange test passed\n";
  }
  return global_result;
}
