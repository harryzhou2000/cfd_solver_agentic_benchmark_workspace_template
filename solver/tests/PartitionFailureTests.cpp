#include "cfd/DistributedMesh.hpp"

#include <mpi.h>

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int ranks = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);

  int local_threw = 0;
  try {
    (void)cfd::MeshDistributor::load_partition("definitely_missing_mesh_for_mpi_failure_test.cgns",
                                                MPI_COMM_WORLD);
  } catch (const std::runtime_error&) {
    local_threw = 1;
  }
  int throws = 0;
  MPI_Allreduce(&local_threw, &throws, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  const int result = throws == ranks ? 0 : 1;
  if (rank == 0 && result == 0) std::cout << "Partition failure propagated to every rank\n";
  MPI_Finalize();
  return result;
}
