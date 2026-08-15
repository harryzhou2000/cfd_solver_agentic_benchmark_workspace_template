#include "solver/setup.hpp"

#include <fmt/format.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "parallel/halo_exchange.hpp"
#include "partition/partition.hpp"

namespace cfd {

// ---------------------------------------------------------------------------
// Replicated serial preprocessing (allowed by TASK.md section 49):
//
//   - The full serial mesh is read ONCE at initialization only (in main.cpp,
//     every rank), and never touched again during iterations.
//   - During iterations the solver accesses ONLY the DistributedMesh (owned
//     cells + ghosts) and the local flat state array U.
//   - There is NO full-state Allgather/Allgatherv during iterations; halo
//     exchange is strictly neighbor-scoped Isend/Irecv pairs.
//   - Result metadata will report:
//       full_mesh_replication_during_iterations:   false
//       full_state_replication_during_iterations:  false
// ---------------------------------------------------------------------------
SolverSetup setup_distributed(const Mesh2D& mesh, int rank, int nranks,
                              MPI_Comm comm) {
  (void)comm;  // serial preprocessing: no collective communication needed
  SolverSetup s;
  s.nvars = kNumConservedVars;

  const std::vector<int> partition = partition_cells(mesh, nranks);
  s.dmesh = build_distributed_mesh(mesh, rank, nranks, partition);

  // Initialize U from cell geometry: used by the halo self-check so that a
  // correctly received ghost slot matches the ghost's own stored data. Ghost
  // slots start as NaN — only a real exchange can fill them, so an unfilled
  // ghost fails the check.
  const long long nlocal = static_cast<long long>(s.dmesh.cells.size());
  s.U.assign(static_cast<std::size_t>(nlocal) * s.nvars,
             std::numeric_limits<double>::quiet_NaN());
  for (long long c = 0; c < s.dmesh.n_owned; ++c) {
    const Cell2D& cell = s.dmesh.cells[c];
    double* u = s.U.data() + static_cast<std::size_t>(c) * s.nvars;
    u[0] = cell.cell_center.x;
    u[1] = cell.cell_center.y;
    u[2] = cell.volume;
    u[3] = static_cast<double>(s.dmesh.global_cell_id[c]);
  }
  return s;
}

double run_halo_selfcheck(SolverSetup& setup, MPI_Comm comm) {
  halo_exchange(setup.dmesh, setup.U, setup.nvars, comm);

  const DistributedMesh& dm = setup.dmesh;
  // Expected ghost values: the owner rank initialized its cell from the same
  // serial mesh data, so a correctly exchanged ghost slot must match the
  // ghost's own stored geometry/global id exactly.
  double max_err = 0.0;
  for (long long c = dm.n_owned; c < static_cast<long long>(dm.cells.size());
       ++c) {
    const Cell2D& ghost = dm.cells[c];
    const double* u = setup.U.data() + static_cast<std::size_t>(c) * setup.nvars;

    // NaN check FIRST: std::max(x, NaN) silently keeps x, so an unfilled
    // ghost would otherwise report zero error.
    for (int k = 0; k < setup.nvars; ++k) {
      if (std::isnan(u[k])) {
        fmt::print(stderr,
                   "[halo] FAIL: rank {} ghost cell {} (global {}, owner rank "
                   "{}) variable {} is NaN (slot never filled)\n",
                   dm.rank, c, dm.global_cell_id[c], dm.owner_rank[c], k);
        return std::numeric_limits<double>::infinity();
      }
    }

    max_err = std::max(max_err, std::fabs(u[0] - ghost.cell_center.x));
    max_err = std::max(max_err, std::fabs(u[1] - ghost.cell_center.y));
    max_err = std::max(max_err, std::fabs(u[2] - ghost.volume));
    max_err = std::max(max_err, std::fabs(
        u[3] - static_cast<double>(dm.global_cell_id[c])));
  }
  return max_err;
}

}  // namespace cfd
