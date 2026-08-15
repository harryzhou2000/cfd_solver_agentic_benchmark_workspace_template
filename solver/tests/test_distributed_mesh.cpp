#include "cfd/DistributedMesh.hpp"

#include <mpi.h>

#include <algorithm>
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int result = 0;
  try {
    if (argc != 2) throw std::runtime_error("usage: test_distributed_mesh mesh.cgns");
    const auto mesh = cfd::MeshDistributor::load_partition(argv[1], MPI_COMM_WORLD);
    int owned_sum = 0;
    MPI_Allreduce(&mesh.owned_count, &owned_sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    if (owned_sum != mesh.global_cells) throw std::runtime_error("global owned count mismatch");
    std::cout << "rank=" << rank << " owned=" << mesh.owned_count
              << " ghosts=" << mesh.ghost_count() << " faces=" << mesh.faces.size()
              << " peers=" << mesh.halo.size() << " edge_cut=" << mesh.partition_edge_cut
              << '\n';
  } catch (const std::exception& error) {
    std::cerr << "rank " << rank << ": " << error.what() << '\n';
    result = 1;
  }
  int global_result = 0;
  MPI_Allreduce(&result, &global_result, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Finalize();
  return global_result;
}
