#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "partition.hpp"
#include "types.hpp"

namespace cfd {

// One neighbor's halo-exchange description: which local cells to send to
// `rank` and which local slots to receive that neighbor's cells into.
struct HaloMap {
    int rank = -1;                       // neighbor rank
    std::vector<int> send_cell_ids_local;  // local cell indices to send
    std::vector<int> recv_cell_ids_local;  // local indices to receive into
    MPI_Request send_req = MPI_REQUEST_NULL;
    MPI_Request recv_req = MPI_REQUEST_NULL;
};

// Vector of halo maps, one per neighbor rank.
using HaloExchangePlan = std::vector<HaloMap>;

// Small MPI helpers used across the solver.
namespace mpi {

// Returns a human-readable error string for an MPI error code.
std::string error_string(int err);

// All-reduce sum of a per-rank double vector (same size on every rank).
std::vector<double> allreduce_sum(const std::vector<double>& local,
                                  MPI_Comm comm);

// Global minimum/maximum of a scalar across the communicator.
void global_minmax(double local, double& gmin, double& gmax, MPI_Comm comm);

// Broadcasts a std::string from the root rank.
std::string bcast_string(const std::string& local, int root, MPI_Comm comm);

}  // namespace mpi

// Builds the halo-exchange plan for rank `my_rank` of a `n_parts`-way
// partition of `mesh`. `partition` maps every global cell to its owning
// rank; `owned_global_ids` lists this rank's owned cells in local-index
// order (LocalMesh::owned_cells).
//
// Phase 2b: every rank holds the full mesh and the identical partition, so
// the plan is derived locally without inter-rank communication: ghost cells
// (and the cells each neighbor must send back) follow directly from the
// face connectivity. Send/recv lists use local cell indices:
// owned cells 0..n_owned-1, ghosts n_owned..n_owned+n_ghost-1, in the same
// ordering produced by build_local_mesh_simple.
//
// Throws std::runtime_error on inconsistent input.
HaloExchangePlan build_halo_plan(const Mesh& mesh,
                                 const std::vector<idx_t>& partition,
                                 std::vector<cgsize_t>& owned_global_ids,
                                 int my_rank, int n_parts, MPI_Comm comm);

// Exchanges ghost-cell conservative states with all neighbors in `plan`
// using MPI_Isend/MPI_Irecv (non-blocking, one request pair per neighbor,
// MPI_Waitall before unpacking). `U_local` must be sized owned + ghost; the
// owned entries are the send sources and the ghost slots receive the packed
// neighbor values.
void exchange_halo(const HaloExchangePlan& plan, std::vector<Vector4>& U_local,
                   MPI_Comm comm);

}  // namespace cfd
