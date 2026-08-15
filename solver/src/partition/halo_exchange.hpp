#pragma once

#include "types.hpp"
#include <mpi.h>
#include <vector>

namespace cfd {

/// Halo exchange: send owned cell data to neighbors, receive ghost cell data.
///
/// Each cell has N components (default 4 for conservative state).
/// Data is packed as: [cell_0_comp_0, cell_0_comp_1, ..., cell_0_comp_N-1,
///                     cell_1_comp_0, ...]
///
/// @param data  Cell data array (in/out), size n_local_cells * n_comp
///              On input: owned cell data must be valid.
///              On output: ghost cell data is filled from neighbors.
/// @param n_comp  Number of components per cell (default 4 for 2D N-S)
/// @param send_cells  List of local cell indices to send to each neighbor
/// @param recv_cells  List of local cell indices to receive for each neighbor
/// @param neighbor_ranks  MPI ranks of each neighbor
/// @param comm  MPI communicator
void halo_exchange(double* data, int n_comp,
                   const std::vector<std::vector<Int>>& send_cells,
                   const std::vector<std::vector<Int>>& recv_cells,
                   const std::vector<int>& neighbor_ranks,
                   MPI_Comm comm);

/// Convenience wrapper for Vec4 (conservative state) arrays
void halo_exchange_conserved(std::vector<Conserved>& U,
                              const std::vector<std::vector<Int>>& send_cells,
                              const std::vector<std::vector<Int>>& recv_cells,
                              const std::vector<int>& neighbor_ranks,
                              MPI_Comm comm);

/// Global reduction: sum L2 norms across all ranks
/// Returns the global L2 norm
double global_l2_norm(double local_sum_sq, MPI_Comm comm);

/// Global reduction: maximum across all ranks
double global_max(double local_value, MPI_Comm comm);

/// Global reduction: sum across all ranks
double global_sum(double local_value, MPI_Comm comm);

/// Check that vector data is consistent across all ranks (for debugging)
bool all_ranks_agree(bool local_condition, MPI_Comm comm);

} // namespace cfd
