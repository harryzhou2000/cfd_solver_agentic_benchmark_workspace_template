#pragma once

/// @file halo.hpp
/// Ghost-cell halo exchange for MPI-parallel finite-volume solver.
///
/// Manages non-blocking MPI send/receive patterns to update ghost-cell
/// conservative states after each residual evaluation.

#include "common.hpp"

#include <mpi.h>

#include <map>
#include <vector>

namespace cfd {

/// Precomputed halo-exchange pattern.
///
/// For each neighbor rank, lists of local cell indices that need to be
/// sent (owned cells bordering that rank) and received into (ghost cells
/// owned by that rank).
struct HaloExchange {
    /// Local cell indices to send to each neighbor rank.
    std::map<int, std::vector<std::size_t>> send_cells;
    /// Local cell indices (ghost) to receive into from each neighbor rank.
    std::map<int, std::vector<std::size_t>> recv_cells;
};

/// Build the halo-exchange pattern from a partitioned local mesh.
///
/// Ghost cells must already be tagged with `is_ghost` and `owner_rank`.
/// The mapping is built by collecting, for each neighbor rank:
///   - send_cells: owned cells that have at least one ghost neighbour owned
///     by that neighbor rank
///   - recv_cells: ghost cells owned by that neighbor rank
///
/// @param  local_mesh   partitioned local mesh (owned + ghost cells)
/// @param  comm         MPI communicator
/// @return              halo exchange pattern
HaloExchange build_halo_exchange(const Mesh& local_mesh, MPI_Comm comm);

/// Exchange ghost-cell conservative states using non-blocking MPI.
///
/// Packs U[send_cells] into contiguous buffers, posts MPI_Isend for each
/// neighbour, posts MPI_Irecv for each neighbour, waits for all, and
/// unpacks into U[recv_cells].
///
/// @param  U            cell-center conservative states (in/out for ghost cells)
/// @param  halo         precomputed halo exchange pattern
/// @param  comm         MPI communicator
void exchange_ghost_states(std::vector<Vec4>& U, const HaloExchange& halo, MPI_Comm comm);

} // namespace cfd
