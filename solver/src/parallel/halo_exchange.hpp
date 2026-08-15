#pragma once

// Neighbor-scoped halo exchange for the partitioned mesh. Uses per-neighbor
// MPI_Isend/MPI_Irecv pairs (post-all, wait-all) — never Allgather.

#include <mpi.h>

#include <vector>

#include "partition/partition.hpp"

namespace cfd {

// Exchanges the conservative state stored in the flat array U between
// neighbors:
//   U has nvars * total_local_cells entries, cell c's state at
//   U[c*nvars ... (c+1)*nvars). Owned cells hold the authoritative state;
//   ghost cells are overwritten with the owner rank's values.
//
// For each neighbor, the cells in NeighborInfo::send_cells (owned cells on
// this rank that the neighbor holds as ghosts) are packed and sent, and data
// for NeighborInfo::recv_cells (this rank's ghosts owned by the neighbor) is
// received and unpacked. Both sides order their lists by the remote cell's
// global id, so send and receive buffers match pairwise.
//
// Blocking completion via MPI_Waitall on all posted requests.
void halo_exchange(DistributedMesh& dmesh, std::vector<double>& U, int nvars,
                   MPI_Comm comm);

}  // namespace cfd
