#include "parallel/halo_exchange.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace cfd {

void halo_exchange(DistributedMesh& dmesh, std::vector<double>& U, int nvars,
                   MPI_Comm comm) {
  const std::size_t nneighbors = dmesh.neighbors.size();
  if (nneighbors == 0) return;
  if (static_cast<long long>(U.size()) !=
      nvars * static_cast<long long>(dmesh.cells.size()))
    throw std::invalid_argument(
        "halo_exchange: U size does not match nvars * local cell count");

  std::vector<std::vector<double>> send_bufs(nneighbors);
  std::vector<std::vector<double>> recv_bufs(nneighbors);
  std::vector<MPI_Request> requests;
  requests.reserve(2 * nneighbors);

  for (std::size_t i = 0; i < nneighbors; ++i) {
    const NeighborInfo& nbr = dmesh.neighbors[i];

    // Pack: owned cells whose state the neighbor needs as ghosts.
    const int nsend = static_cast<int>(nbr.send_cells.size());
    send_bufs[i].resize(static_cast<std::size_t>(nsend) * nvars);
    for (int j = 0; j < nsend; ++j) {
      const int cell = nbr.send_cells[j];
      std::memcpy(send_bufs[i].data() + static_cast<std::size_t>(j) * nvars,
                  U.data() + static_cast<std::size_t>(cell) * nvars,
                  static_cast<std::size_t>(nvars) * sizeof(double));
    }

    const int nrecv = static_cast<int>(nbr.recv_cells.size());
    recv_bufs[i].resize(static_cast<std::size_t>(nrecv) * nvars);

    // Tag = destination rank for sends, own rank for receives: exactly one
    // message per direction per neighbor, so tags are unambiguous.
    MPI_Request req;
    MPI_Isend(send_bufs[i].data(), nsend * nvars, MPI_DOUBLE, nbr.rank,
              nbr.rank, comm, &req);
    requests.push_back(req);
    MPI_Irecv(recv_bufs[i].data(), nrecv * nvars, MPI_DOUBLE, nbr.rank,
              dmesh.rank, comm, &req);
    requests.push_back(req);
  }

  if (!requests.empty())
    MPI_Waitall(static_cast<int>(requests.size()), requests.data(),
                MPI_STATUSES_IGNORE);

  // Unpack received data into the ghost cell slots.
  for (std::size_t i = 0; i < nneighbors; ++i) {
    const NeighborInfo& nbr = dmesh.neighbors[i];
    const int nrecv = static_cast<int>(nbr.recv_cells.size());
    for (int j = 0; j < nrecv; ++j) {
      const int cell = nbr.recv_cells[j];
      std::memcpy(U.data() + static_cast<std::size_t>(cell) * nvars,
                  recv_bufs[i].data() + static_cast<std::size_t>(j) * nvars,
                  static_cast<std::size_t>(nvars) * sizeof(double));
    }
  }
}

}  // namespace cfd
