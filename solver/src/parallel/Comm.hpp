// MPI helpers: neighbour-scoped halo exchange and global reductions.
//
// The halo exchanger moves only the cell-centred data that neighbour ranks
// actually need (the ghost layer of each partition) using non-blocking
// point-to-point messages.  No collective ever carries the full state.
#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "core/Types.hpp"
#include "mesh/LocalMesh.hpp"

namespace cfd {

class HaloExchanger {
 public:
  HaloExchanger() = default;
  void setup(const LocalMesh& mesh, MPI_Comm comm);

  // Exchange `nvar` doubles per cell.  `data` is laid out as
  // data[cell * nvar + v] and must have room for owned + ghost cells.
  void exchange(Real* data, int nvar) const;

  const std::vector<int>& neighbors() const { return neighbors_; }
  std::size_t totalSend() const;
  std::size_t totalRecv() const;
  const std::string& pattern() const { return pattern_; }

 private:
  MPI_Comm comm_ = MPI_COMM_NULL;
  std::vector<int> neighbors_;
  std::vector<std::vector<Index>> send_cells_;
  std::vector<std::vector<Index>> recv_cells_;
  mutable std::vector<Real> send_buf_, recv_buf_;
  mutable std::vector<MPI_Request> requests_;
  std::vector<std::size_t> send_offset_, recv_offset_;
  std::string pattern_ = "neighbor_isend_irecv";
};

// Convenience wrappers around the global reductions used for residual and
// force norms (always over MPI_COMM_WORLD-scoped solver communicator).
Real globalSum(Real v, MPI_Comm comm);
Real globalMax(Real v, MPI_Comm comm);
Real globalMin(Real v, MPI_Comm comm);
void globalSumArray(Real* v, int n, MPI_Comm comm);
long long globalSumLL(long long v, MPI_Comm comm);

}  // namespace cfd
