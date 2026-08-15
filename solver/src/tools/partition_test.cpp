// Partition + halo-plan sanity test. Loads a case, distributes the mesh, and
// prints per-rank owned/ghost counts, neighbor ranks, and send/recv sizes.
#include "../case.hpp"
#include "../mesh.hpp"
#include "../partition.hpp"
#include <mpi.h>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);
  if (argc < 3) { if (rank==0) std::printf("usage: mpirun -np N partition_test <case.json> <mesh.cgns>\n"); MPI_Finalize(); return 1; }
  try {
    cfd::CaseDef cd = cfd::parse_case(argv[1]);
    cfd::Mesh global;
    if (rank == 0) global = cfd::load_cgns(argv[2], cd.bc_map);
    // broadcast cell count for non-root sanity
    int ncells = (int)global.cells.size();
    MPI_Bcast(&ncells, 1, MPI_INT, 0, MPI_COMM_WORLD);
    cfd::LocalMesh lm = cfd::distribute_mesh(global, rank, nranks);
    // free global on rank 0 (simulate solver stage)
    global.cells.clear(); global.faces.clear();
    int owned = lm.n_owned, ghost = lm.n_ghost;
    std::vector<int> all_owned(nranks), all_ghost(nranks);
    MPI_Gather(&owned, 1, MPI_INT, all_owned.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&ghost, 1, MPI_INT, all_ghost.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (rank == 0) {
      std::printf("nranks=%d cells_global=%d faces_global=%d edge_cut=%d partitioner=%s\n",
        nranks, lm.num_cells_global, lm.num_faces_global, lm.edge_cut, lm.partitioner.c_str());
      for (int r = 0; r < nranks; ++r)
        std::printf("  rank %d: owned=%d ghost=%d\n", r, all_owned[r], all_ghost[r]);
    }
    int total_owned = lm.n_owned;
    MPI_Reduce(MPI_IN_PLACE, &total_owned, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    int nbf = (int)lm.wall_face_ids.size() + (int)lm.farfield_face_ids.size();
    if (rank == 0) std::printf("  sum owned=%d (cells_global=%d)  total local boundary faces (rank0)=%d\n",
      total_owned, lm.num_cells_global, nbf);
    for (int r = 0; r < nranks; ++r) {
      if (r == rank) {
        std::printf("  rank %d neighbors:", rank);
        for (size_t k = 0; k < lm.neighbor_ranks.size(); ++k)
          std::printf(" %d(send=%zu recv=%zu)", lm.neighbor_ranks[k],
            lm.send_local[k].size(), lm.recv_local[k].size());
        std::printf("\n");
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
  } catch (std::exception& e) {
    if (rank == 0) std::printf("ERROR: %s\n", e.what());
    MPI_Finalize(); return 2;
  }
  MPI_Finalize();
  return 0;
}
