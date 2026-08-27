#include "parallel/Comm.hpp"

#include <numeric>

#include "core/Exception.hpp"

namespace cfd {

void HaloExchanger::setup(const LocalMesh& mesh, MPI_Comm comm) {
  comm_ = comm;
  neighbors_ = mesh.neighbors;
  send_cells_ = mesh.send_cells;
  recv_cells_ = mesh.recv_cells;
  const std::size_t nn = neighbors_.size();
  send_offset_.assign(nn + 1, 0);
  recv_offset_.assign(nn + 1, 0);
  for (std::size_t k = 0; k < nn; ++k) {
    send_offset_[k + 1] = send_offset_[k] + send_cells_[k].size();
    recv_offset_[k + 1] = recv_offset_[k] + recv_cells_[k].size();
  }
  requests_.assign(2 * nn, MPI_REQUEST_NULL);
}

std::size_t HaloExchanger::totalSend() const {
  return send_offset_.empty() ? 0 : send_offset_.back();
}
std::size_t HaloExchanger::totalRecv() const {
  return recv_offset_.empty() ? 0 : recv_offset_.back();
}

void HaloExchanger::exchange(Real* data, int nvar) const {
  const std::size_t nn = neighbors_.size();
  if (nn == 0) return;
  send_buf_.resize(send_offset_.back() * nvar);
  recv_buf_.resize(recv_offset_.back() * nvar);

  for (std::size_t k = 0; k < nn; ++k) {
    Real* dst = send_buf_.data() + send_offset_[k] * nvar;
    const auto& list = send_cells_[k];
    for (std::size_t i = 0; i < list.size(); ++i) {
      const Real* src = data + static_cast<std::size_t>(list[i]) * nvar;
      for (int v = 0; v < nvar; ++v) dst[i * nvar + v] = src[v];
    }
  }

  int nreq = 0;
  for (std::size_t k = 0; k < nn; ++k) {
    const int count = static_cast<int>((recv_offset_[k + 1] - recv_offset_[k]) * nvar);
    MPI_Irecv(recv_buf_.data() + recv_offset_[k] * nvar, count, MPI_DOUBLE, neighbors_[k], 7701,
              comm_, &requests_[nreq++]);
  }
  for (std::size_t k = 0; k < nn; ++k) {
    const int count = static_cast<int>((send_offset_[k + 1] - send_offset_[k]) * nvar);
    MPI_Isend(send_buf_.data() + send_offset_[k] * nvar, count, MPI_DOUBLE, neighbors_[k], 7701,
              comm_, &requests_[nreq++]);
  }
  MPI_Waitall(nreq, requests_.data(), MPI_STATUSES_IGNORE);

  for (std::size_t k = 0; k < nn; ++k) {
    const Real* src = recv_buf_.data() + recv_offset_[k] * nvar;
    const auto& list = recv_cells_[k];
    for (std::size_t i = 0; i < list.size(); ++i) {
      Real* dst = data + static_cast<std::size_t>(list[i]) * nvar;
      for (int v = 0; v < nvar; ++v) dst[v] = src[i * nvar + v];
    }
  }
}

Real globalSum(Real v, MPI_Comm comm) {
  Real out = 0.0;
  MPI_Allreduce(&v, &out, 1, MPI_DOUBLE, MPI_SUM, comm);
  return out;
}

Real globalMax(Real v, MPI_Comm comm) {
  Real out = 0.0;
  MPI_Allreduce(&v, &out, 1, MPI_DOUBLE, MPI_MAX, comm);
  return out;
}

Real globalMin(Real v, MPI_Comm comm) {
  Real out = 0.0;
  MPI_Allreduce(&v, &out, 1, MPI_DOUBLE, MPI_MIN, comm);
  return out;
}

void globalSumArray(Real* v, int n, MPI_Comm comm) {
  MPI_Allreduce(MPI_IN_PLACE, v, n, MPI_DOUBLE, MPI_SUM, comm);
}

long long globalSumLL(long long v, MPI_Comm comm) {
  long long out = 0;
  MPI_Allreduce(&v, &out, 1, MPI_LONG_LONG, MPI_SUM, comm);
  return out;
}

}  // namespace cfd
