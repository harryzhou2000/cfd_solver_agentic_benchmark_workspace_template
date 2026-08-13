#pragma once
// Phase 2: halo exchange for the rank-local mesh.
//
// exchange_halo_data() refreshes the ghost-cell entries of a locally indexed
// conservative-state vector by exchanging data only with direct neighbor
// ranks (point-to-point MPI_Isend / MPI_Irecv, no Allgather). This is the
// communication routine the Phase 3 solver iterations will call every step.
//
// Phase 4 adds exchange_gradient_halo(), which refreshes the ghost-cell
// entries of the reconstructed conservative-variable gradients the same way
// (8 doubles per cell: 4 variables x 2 components), so second-order face
// reconstruction and the viscous flux can use ghost-cell gradients at
// inter-rank faces.

#include <mpi.h>

#include <vector>

#include "gradient.h"
#include "partition.h"
#include "types.h"

namespace cfd {

// Serialize a ConsState into 4 consecutive doubles (field order: rho, rhou,
// rhov, rhoE). Explicit packing avoids any struct-layout assumptions.
inline void pack_cons_state(const ConsState& u, double* buf) {
  buf[0] = u.rho;
  buf[1] = u.rhou;
  buf[2] = u.rhov;
  buf[3] = u.rhoE;
}

// Inverse of pack_cons_state.
inline void unpack_cons_state(const double* buf, ConsState& u) {
  u.rho = buf[0];
  u.rhou = buf[1];
  u.rhov = buf[2];
  u.rhoE = buf[3];
}

// Pre-allocated communication buffers for exchange_halo_data. Construct once
// (e.g. at solver setup) and reuse across iterations so the per-call halo
// exchange performs no allocation.
struct HaloScratch {
  std::vector<std::vector<double>> send_bufs;  // one per exchange
  std::vector<std::vector<double>> recv_bufs;  // one per exchange
  std::vector<MPI_Request> requests;

  // Allocate buffers sized for the worst-case message of each exchange:
  // max(send_cells, recv_cells) * 4 doubles (4 = ConsState fields), and
  // requests for 2 * number_of_exchanges (one Isend + one Irecv each).
  void init(const LocalMesh& lm);
};

// Exchange ghost-cell conservative states between neighbor ranks.
//
// U_local must be indexed by LocalMesh local cell index and sized
// local_mesh.cells.size(). On return, the entries of U_local corresponding to
// ghost cells (indices >= local_mesh.n_owned) hold the state of the owning
// rank's cell (owned cells are untouched).
//
// For every HaloExchange: pack send_cells into a buffer, post MPI_Isend to
// the neighbor, post MPI_Irecv for recv_cells, then MPI_Waitall and unpack.
// Buffers come from `scratch` (must be init()'d against the same LocalMesh);
// no memory is allocated on the per-call path.
//
// Safe to call repeatedly (solver iterations); no state is kept between
// calls. No-op when nranks == 1 or there are no halo exchanges.
void exchange_halo_data(std::vector<ConsState>& U_local,
                        const LocalMesh& local_mesh, HaloScratch& scratch);

// ---------------------------------------------------------------------------
// Gradient halo exchange (Phase 4)
// ---------------------------------------------------------------------------

// Pre-allocated communication buffers for exchange_gradient_halo. Construct
// once (e.g. at solver setup) and reuse across steps so the per-call
// exchange performs no allocation.
struct GradientHaloScratch {
  std::vector<std::vector<double>> send_bufs;  // one per exchange
  std::vector<std::vector<double>> recv_bufs;  // one per exchange
  std::vector<MPI_Request> requests;

  // Allocate buffers sized for the worst-case message of each exchange:
  // max(send_cells, recv_cells) * 8 doubles (8 = 4 gradient variables x 2
  // components), and requests for 2 * number_of_exchanges.
  void init(const LocalMesh& lm);
};

// Exchange the ghost-cell gradients of the four conserved variables between
// neighbor ranks (same communication pattern as exchange_halo_data).
//
// The four input vectors hold the OWNED-cell gradients (one entry per owned
// cell, index-aligned with local cells 0..n_owned-1). On return the four
// output vectors hold the GHOST-cell gradients, one entry per ghost cell,
// indexed by ghost_local_index - n_owned (they are resized to n_ghost if
// needed; ownership of the buffers stays with the caller so repeated calls
// allocate nothing).
//
// Only gradients of owned cells that are ghost cells on neighbor ranks are
// sent; owned-cell entries of the output vectors are never written.
// No-op when nranks == 1 or there are no halo exchanges.
// The RAW gradients are multiplied by the per-cell limiters before packing
// (the limiter values are the per-conservative-variable phis from
// compute_limiters), so ghost cells receive the same LIMITED gradients that
// owned cells use for reconstruction: both sides of an inter-rank face then
// reconstruct the same face state (conservative flux at the partition cut).
void exchange_gradient_halo(const std::vector<Vec2>& grad_rho,
                            const std::vector<Vec2>& grad_rhou,
                            const std::vector<Vec2>& grad_rhov,
                            const std::vector<Vec2>& grad_rhoE,
                            const std::vector<double>& lim_rho,
                            const std::vector<double>& lim_rhou,
                            const std::vector<double>& lim_rhov,
                            const std::vector<double>& lim_rhoE,
                            std::vector<Vec2>& ghost_grad_rho,
                            std::vector<Vec2>& ghost_grad_rhou,
                            std::vector<Vec2>& ghost_grad_rhov,
                            std::vector<Vec2>& ghost_grad_rhoE,
                            const LocalMesh& local_mesh,
                            GradientHaloScratch& scratch);

}  // namespace cfd
