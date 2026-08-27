#include "parallel/halo_exchange.h"

#include "core/exceptions.h"

namespace cns2d {
namespace {
constexpr int kHaloTag = 4211;
}

HaloExchange::HaloExchange(const DistributedMesh &mesh, int max_components)
    : mesh_(mesh), max_components_(max_components) {
  const std::size_t n = mesh_.neighbors().size();
  send_buffers_.resize(n);
  recv_buffers_.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const NeighborPlan &plan = mesh_.neighbors()[i];
    send_buffers_[i].resize(plan.send_cells.size() * static_cast<std::size_t>(max_components));
    recv_buffers_[i].resize(plan.recv_cells.size() * static_cast<std::size_t>(max_components));
  }
  requests_.reserve(2 * n);
}

void HaloExchange::exchange(Real *data, int num_components, int stride) {
  if (num_components > max_components_) {
    throw CnsError("halo exchange requested " + std::to_string(num_components) +
                   " components but was sized for " + std::to_string(max_components_));
  }
  const std::size_t n = mesh_.neighbors().size();
  if (n == 0) {
    ++num_exchanges_;
    return;  // serial run: ghosts do not exist
  }

  requests_.clear();

  // Post receives first so the sends always have a matching buffer ready.
  for (std::size_t i = 0; i < n; ++i) {
    const NeighborPlan &plan = mesh_.neighbors()[i];
    const int count = static_cast<int>(plan.recv_cells.size()) * num_components;
    if (count == 0) continue;
    MPI_Request req;
    MPI_Irecv(recv_buffers_[i].data(), count, MPI_DOUBLE, plan.rank, kHaloTag, mesh_.comm(), &req);
    requests_.push_back(req);
  }

  for (std::size_t i = 0; i < n; ++i) {
    const NeighborPlan &plan = mesh_.neighbors()[i];
    const int count = static_cast<int>(plan.send_cells.size()) * num_components;
    if (count == 0) continue;
    Real *buf = send_buffers_[i].data();
    for (std::size_t k = 0; k < plan.send_cells.size(); ++k) {
      const Real *src = data + static_cast<std::size_t>(plan.send_cells[k]) * static_cast<std::size_t>(stride);
      Real *dst = buf + k * static_cast<std::size_t>(num_components);
      for (int comp = 0; comp < num_components; ++comp) dst[comp] = src[comp];
    }
    MPI_Request req;
    MPI_Isend(buf, count, MPI_DOUBLE, plan.rank, kHaloTag, mesh_.comm(), &req);
    requests_.push_back(req);
    num_doubles_sent_ += count;
  }

  if (!requests_.empty()) {
    MPI_Waitall(static_cast<int>(requests_.size()), requests_.data(), MPI_STATUSES_IGNORE);
  }

  // Unpack into the ghost slots.
  for (std::size_t i = 0; i < n; ++i) {
    const NeighborPlan &plan = mesh_.neighbors()[i];
    const Real *buf = recv_buffers_[i].data();
    for (std::size_t k = 0; k < plan.recv_cells.size(); ++k) {
      const Real *src = buf + k * static_cast<std::size_t>(num_components);
      Real *dst = data + static_cast<std::size_t>(plan.recv_cells[k]) * static_cast<std::size_t>(stride);
      for (int comp = 0; comp < num_components; ++comp) dst[comp] = src[comp];
    }
  }

  ++num_exchanges_;
}

}  // namespace cns2d
