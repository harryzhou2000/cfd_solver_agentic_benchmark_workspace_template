// Phase 2: halo exchange implementation (see halo.h).

#include "halo.h"

#include <algorithm>
#include <cstddef>

namespace cfd {

namespace {
constexpr int kConsDoubles = 4;   // rho, rhou, rhov, rhoE per cell
constexpr int kGradDoubles = 8;   // 4 gradient variables x 2 components
}  // namespace

// Serialize the four gradients of one cell into 8 consecutive doubles
// (field order: rho.x, rho.y, rhou.x, rhou.y, rhov.x, rhov.y, rhoE.x,
// rhoE.y). Explicit packing avoids any struct-layout assumptions.
inline void pack_gradients(const Vec2& g_rho, const Vec2& g_rhou,
                           const Vec2& g_rhov, const Vec2& g_rhoE,
                           double* buf) {
  buf[0] = g_rho.x;
  buf[1] = g_rho.y;
  buf[2] = g_rhou.x;
  buf[3] = g_rhou.y;
  buf[4] = g_rhov.x;
  buf[5] = g_rhov.y;
  buf[6] = g_rhoE.x;
  buf[7] = g_rhoE.y;
}

inline void unpack_gradients(const double* buf, Vec2& g_rho, Vec2& g_rhou,
                             Vec2& g_rhov, Vec2& g_rhoE) {
  g_rho = Vec2(buf[0], buf[1]);
  g_rhou = Vec2(buf[2], buf[3]);
  g_rhov = Vec2(buf[4], buf[5]);
  g_rhoE = Vec2(buf[6], buf[7]);
}

void HaloScratch::init(const LocalMesh& local_mesh) {
  const size_t n_exch = local_mesh.halo_exchanges.size();
  send_bufs.resize(n_exch);
  recv_bufs.resize(n_exch);
  for (size_t i = 0; i < n_exch; ++i) {
    // Worst-case sizing covers asymmetric send/recv counts in one allocation.
    const size_t max_cells = std::max(
        local_mesh.halo_exchanges[i].send_cells.size(),
        local_mesh.halo_exchanges[i].recv_cells.size());
    send_bufs[i].assign(max_cells * kConsDoubles, 0.0);
    recv_bufs[i].assign(max_cells * kConsDoubles, 0.0);
  }
  requests.resize(2 * n_exch);
}

void exchange_halo_data(std::vector<ConsState>& U_local,
                        const LocalMesh& local_mesh, HaloScratch& scratch) {
  if (local_mesh.nranks <= 1 || local_mesh.halo_exchanges.empty()) {
    return;  // nothing to exchange
  }

  // Message tag: any value works because each neighbor appears in exactly one
  // HaloExchange and every message is matched within this call (Waitall
  // before returning). A single fixed tag keeps the pattern simple.
  constexpr int kTag = 4242;

  const size_t n_exch = local_mesh.halo_exchanges.size();
  // Buffers were pre-allocated by HaloScratch::init against this LocalMesh;
  // each is large enough for the exchange's send and recv messages.
  for (size_t i = 0; i < n_exch; ++i) {
    const LocalMesh::HaloExchange& ex = local_mesh.halo_exchanges[i];

    double* send = scratch.send_bufs[i].data();
    for (size_t j = 0; j < ex.send_cells.size(); ++j) {
      pack_cons_state(U_local[ex.send_cells[j]], send + j * kConsDoubles);
    }

    MPI_Isend(send,
              static_cast<int>(ex.send_cells.size() * kConsDoubles),
              MPI_DOUBLE, ex.neighbor_rank, kTag, MPI_COMM_WORLD,
              &scratch.requests[2 * i]);
    MPI_Irecv(scratch.recv_bufs[i].data(),
              static_cast<int>(ex.recv_cells.size() * kConsDoubles),
              MPI_DOUBLE, ex.neighbor_rank, kTag, MPI_COMM_WORLD,
              &scratch.requests[2 * i + 1]);
  }

  // All sends and receives are posted before waiting: with every rank
  // following the same pattern, the communication graph is symmetric and
  // cannot deadlock.
  MPI_Waitall(static_cast<int>(scratch.requests.size()),
              scratch.requests.data(), MPI_STATUSES_IGNORE);

  for (size_t i = 0; i < n_exch; ++i) {
    const LocalMesh::HaloExchange& ex = local_mesh.halo_exchanges[i];
    const double* recv = scratch.recv_bufs[i].data();
    for (size_t j = 0; j < ex.recv_cells.size(); ++j) {
      unpack_cons_state(recv + j * kConsDoubles, U_local[ex.recv_cells[j]]);
    }
  }
}

// ---------------------------------------------------------------------------
// Gradient halo exchange
// ---------------------------------------------------------------------------

void GradientHaloScratch::init(const LocalMesh& local_mesh) {
  const size_t n_exch = local_mesh.halo_exchanges.size();
  send_bufs.resize(n_exch);
  recv_bufs.resize(n_exch);
  for (size_t i = 0; i < n_exch; ++i) {
    // Worst-case sizing covers asymmetric send/recv counts in one allocation.
    const size_t max_cells = std::max(
        local_mesh.halo_exchanges[i].send_cells.size(),
        local_mesh.halo_exchanges[i].recv_cells.size());
    send_bufs[i].assign(max_cells * kGradDoubles, 0.0);
    recv_bufs[i].assign(max_cells * kGradDoubles, 0.0);
  }
  requests.resize(2 * n_exch);
}

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
                            GradientHaloScratch& scratch) {
  if (local_mesh.nranks <= 1 || local_mesh.halo_exchanges.empty()) {
    return;  // nothing to exchange
  }

  // Ghost output buffers are indexed by ghost_local_index - n_owned; resize
  // once to n_ghost (subsequent calls reuse the allocation).
  const size_t n_ghost = static_cast<size_t>(local_mesh.n_ghost);
  if (ghost_grad_rho.size() != n_ghost) {
    ghost_grad_rho.resize(n_ghost);
    ghost_grad_rhou.resize(n_ghost);
    ghost_grad_rhov.resize(n_ghost);
    ghost_grad_rhoE.resize(n_ghost);
  }

  // Message tag: any value works because each neighbor appears in exactly one
  // HaloExchange and every message is matched within this call (Waitall
  // before returning).
  constexpr int kTag = 4244;  // distinct from the state-exchange tag

  const size_t n_exch = local_mesh.halo_exchanges.size();
  for (size_t i = 0; i < n_exch; ++i) {
    const LocalMesh::HaloExchange& ex = local_mesh.halo_exchanges[i];

    double* send = scratch.send_bufs[i].data();
    for (size_t j = 0; j < ex.send_cells.size(); ++j) {
      const int c = ex.send_cells[j];
      // Send the LIMITED gradients (phi * grad per variable): the ghost
      // receives the same gradient the owner uses in reconstruction, so the
      // face states on both sides of the partition cut are consistent.
      pack_gradients(grad_rho[c] * lim_rho[static_cast<size_t>(c)],
                     grad_rhou[c] * lim_rhou[static_cast<size_t>(c)],
                     grad_rhov[c] * lim_rhov[static_cast<size_t>(c)],
                     grad_rhoE[c] * lim_rhoE[static_cast<size_t>(c)],
                     send + j * kGradDoubles);
    }

    MPI_Isend(send,
              static_cast<int>(ex.send_cells.size() * kGradDoubles),
              MPI_DOUBLE, ex.neighbor_rank, kTag, MPI_COMM_WORLD,
              &scratch.requests[2 * i]);
    MPI_Irecv(scratch.recv_bufs[i].data(),
              static_cast<int>(ex.recv_cells.size() * kGradDoubles),
              MPI_DOUBLE, ex.neighbor_rank, kTag, MPI_COMM_WORLD,
              &scratch.requests[2 * i + 1]);
  }

  MPI_Waitall(static_cast<int>(scratch.requests.size()),
              scratch.requests.data(), MPI_STATUSES_IGNORE);

  for (size_t i = 0; i < n_exch; ++i) {
    const LocalMesh::HaloExchange& ex = local_mesh.halo_exchanges[i];
    const double* recv = scratch.recv_bufs[i].data();
    for (size_t j = 0; j < ex.recv_cells.size(); ++j) {
      const int gi = ex.recv_cells[j] - local_mesh.n_owned;
      unpack_gradients(recv + j * kGradDoubles, ghost_grad_rho[gi],
                       ghost_grad_rhou[gi], ghost_grad_rhov[gi],
                       ghost_grad_rhoE[gi]);
    }
  }
}

}  // namespace cfd
