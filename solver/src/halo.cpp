#include "halo.hpp"

#include <algorithm>

namespace cfd2d {

HaloExchange::~HaloExchange() { freeRequests(); }

void HaloExchange::freeRequests() {
  if (!initialized_) return;
  for (MPI_Request& r : sendReq_)
    if (r != MPI_REQUEST_NULL) MPI_Request_free(&r);
  for (MPI_Request& r : recvReq_)
    if (r != MPI_REQUEST_NULL) MPI_Request_free(&r);
  sendReq_.clear();
  recvReq_.clear();
  initialized_ = false;
}

void HaloExchange::init(const LocalMesh& lm, int width) {
  freeRequests();
  if (width < 1) throw FatalError("HaloExchange width must be positive");
  width_ = width;
  neighbors_ = lm.neighbors;
  sendCells_ = lm.sendCells;
  recvCells_ = lm.recvCells;
  sendBuf_.resize(neighbors_.size());
  recvBuf_.resize(neighbors_.size());
  sendReq_.assign(neighbors_.size(), MPI_REQUEST_NULL);
  recvReq_.assign(neighbors_.size(), MPI_REQUEST_NULL);
  // Each HaloExchange instance uses one fixed tag; matching is unambiguous
  // because (source, tag, comm) fully identifies a message and exchanges
  // always complete (Waitall) before another exchange is posted.
  static int nextTag = 1000;
  const int tag = nextTag++;
  for (size_t i = 0; i < neighbors_.size(); ++i) {
    sendBuf_[i].resize(sendCells_[i].size() * width_);
    recvBuf_[i].resize(recvCells_[i].size() * width_);
    MPI_Send_init(sendBuf_[i].data(), static_cast<int>(sendBuf_[i].size()),
                  MPI_DOUBLE, neighbors_[i], tag, MPI_COMM_WORLD, &sendReq_[i]);
    MPI_Recv_init(recvBuf_[i].data(), static_cast<int>(recvBuf_[i].size()),
                  MPI_DOUBLE, neighbors_[i], tag, MPI_COMM_WORLD, &recvReq_[i]);
  }
  initialized_ = true;
}

void HaloExchange::exchange(std::vector<double>& field) const {
  if (!initialized_) return;
  for (size_t i = 0; i < neighbors_.size(); ++i) {
    for (size_t k = 0; k < sendCells_[i].size(); ++k) {
      const int c = sendCells_[i][k];
      std::copy_n(field.begin() + static_cast<size_t>(c) * width_, width_,
                  sendBuf_[i].begin() + static_cast<size_t>(k) * width_);
    }
    // Persistent requests are started in receive-then-send order to avoid
    // rendezvous deadlock for large partitions.
    MPI_Start(&recvReq_[i]);
    MPI_Start(&sendReq_[i]);
  }
  if (!neighbors_.empty()) {
    MPI_Waitall(static_cast<int>(neighbors_.size()),
                const_cast<MPI_Request*>(recvReq_.data()), MPI_STATUSES_IGNORE);
    MPI_Waitall(static_cast<int>(neighbors_.size()),
                const_cast<MPI_Request*>(sendReq_.data()), MPI_STATUSES_IGNORE);
  }
  for (size_t i = 0; i < neighbors_.size(); ++i) {
    for (size_t k = 0; k < recvCells_[i].size(); ++k) {
      const int c = recvCells_[i][k];
      std::copy_n(recvBuf_[i].begin() + static_cast<size_t>(k) * width_, width_,
                  field.begin() + static_cast<size_t>(c) * width_);
    }
  }
}

}  // namespace cfd2d
