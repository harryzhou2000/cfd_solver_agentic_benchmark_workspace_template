#include <iostream>

#include <mpi.h>

#include "driver.hpp"

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nRanks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nRanks);
  int rc = 0;
  if (argc < 2 || std::string(argv[1]) != "solve") {
    if (rank == 0)
      std::cerr << "usage: cfd2d solve --case <case.json> --output <dir> "
                   "[--restart <file>] [--report-level brief|full]\n";
    rc = 2;
  } else {
    rc = cfd2d::runSolve(argc, argv, rank, nRanks);
  }
  MPI_Finalize();
  return rc;
}
