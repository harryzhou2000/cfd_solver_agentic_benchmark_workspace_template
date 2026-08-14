#pragma once
// Neighbor-scoped halo exchange for cell-centered data.
//
// Communication pattern: per neighbor rank, MPI_Isend of the owned cells the
// neighbor keeps as ghosts, and MPI_Irecv into the ghost slots owned by that
// neighbor. Persistent requests (MPI_Send_init/MPI_Recv_init) keep per-call
// latency low. No collective full-state exchanges are used.

#include <vector>

#include <mpi.h>

#include "common.hpp"
#include "partition.hpp"

namespace cfd2d {

class HaloExchange {
 public:
  HaloExchange() = default;
  ~HaloExchange();
  HaloExchange(const HaloExchange&) = delete;
  HaloExchange& operator=(const HaloExchange&) = delete;

  // Build persistent communication from the local mesh's neighbor lists.
  // width = number of doubles per cell.
  void init(const LocalMesh& lm, int width);

  // Exchange one field (width doubles per cell, size nLocal * width).
  void exchange(std::vector<double>& field) const;

  int numNeighbors() const { return static_cast<int>(neighbors_.size()); }

 private:
  void freeRequests();

  int width_ = 0;
  std::vector<int> neighbors_;
  std::vector<std::vector<int>> sendCells_;  // owned local ids
  std::vector<std::vector<int>> recvCells_;  // ghost local ids
  mutable std::vector<std::vector<double>> sendBuf_, recvBuf_;
  mutable std::vector<MPI_Request> sendReq_, recvReq_;
  bool initialized_ = false;
};

}  // namespace cfd2d
