#include "partition.hpp"

#include <mpi.h>

#include <algorithm>
#include <cstdio>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (argc < 3) {
    if (rank == 0)
      std::fprintf(stderr, "usage: partition_probe <case.json> <outdir>\n");
    MPI_Finalize();
    return 1;
  }
  try {
    cfd::Case c = cfd::loadCase(argv[1]);
    cfd::GlobalMesh mesh = cfd::loadCgnsMesh(c.mesh_file);
    cfd::assignBoundaryConditions(mesh, c);

    cfd::PartitionResult part = cfd::partitionMesh(mesh, size, rank);
    cfd::LocalMesh lm = cfd::buildLocalMesh(mesh, part, rank, size);

    // Global consistency: sum owned cells = total.
    long sumOwned = lm.nOwned;
    long sumGhost = lm.nGhost;
    MPI_Allreduce(MPI_IN_PLACE, &sumOwned, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(MPI_IN_PLACE, &sumGhost, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD);
    long sumWall = static_cast<long>(lm.wallFaceIdx.size());
    MPI_Allreduce(MPI_IN_PLACE, &sumWall, 1, MPI_LONG, MPI_SUM, MPI_COMM_WORLD);

    // Verify ghost owner consistency: every ghost is owned by exactly its owner rank.
    int ok = 1;
    for (int g = lm.nOwned; g < lm.numLocal(); ++g) {
      if (lm.ownerRank[g] < 0 || lm.ownerRank[g] >= size) ok = 0;
    }
    int globalOk = 0;
    MPI_Allreduce(&ok, &globalOk, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    std::vector<int> counts(size, 0);
    for (int p : part.partOfCell) counts[p]++;
    int minOwned = *std::min_element(counts.begin(), counts.end());
    int maxOwned = *std::max_element(counts.begin(), counts.end());

    if (rank == 0) {
      std::printf("nRanks=%d cells=%ld edgeCut=%ld sumOwned=%ld sumGhost=%ld "
                  "sumWall=%ld min/maxOwned=%d/%d allGhostsValid=%d\n",
                  size, mesh.numCells(), part.edgeCut, sumOwned, sumGhost, sumWall,
                  minOwned, maxOwned, globalOk);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    std::printf("rank %d: owned=%d ghost=%d faces=%ld wallFaces=%zu nbrRanks=%zu "
                "send=%zu recv=%zu\n",
                rank, lm.nOwned, lm.nGhost, lm.faces.size(), lm.wallFaceIdx.size(),
                lm.neighborRanks.size(), lm.sendCells.size(), lm.recvCells.size());
    MPI_Finalize();
    return globalOk ? 0 : 2;
  } catch (const std::exception& e) {
    if (rank == 0) std::fprintf(stderr, "ERROR: %s\n", e.what());
    MPI_Finalize();
    return 1;
  }
}
