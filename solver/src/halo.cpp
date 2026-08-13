#include "halo.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace aerofv {
namespace {

void mpi_check(int status, const char *operation) {
  if (status == MPI_SUCCESS) {
    return;
  }
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(status, message, &length);
  throw std::runtime_error(std::string(operation) + ": " +
                           std::string(message, static_cast<std::size_t>(length)));
}

struct OrderedExchange {
  const NeighborExchange *exchange{nullptr};
};

std::vector<OrderedExchange> validate_plan(const LocalMesh &mesh,
                                           std::size_t field_size,
                                           MPI_Comm communicator, int tag) {
  int rank = 0;
  int ranks = 0;
  mpi_check(MPI_Comm_rank(communicator, &rank), "MPI_Comm_rank");
  mpi_check(MPI_Comm_size(communicator, &ranks), "MPI_Comm_size");
  int *tag_upper_bound = nullptr;
  int attribute_found = 0;
  mpi_check(MPI_Comm_get_attr(communicator, MPI_TAG_UB, &tag_upper_bound,
                              &attribute_found),
            "MPI_Comm_get_attr(MPI_TAG_UB)");
  if (attribute_found == 0 || tag_upper_bound == nullptr || tag > *tag_upper_bound) {
    throw std::invalid_argument("halo MPI tag exceeds communicator MPI_TAG_UB");
  }
  if (mesh.owned_cell_count < 0 ||
      static_cast<std::size_t>(mesh.owned_cell_count) > mesh.cells.size() ||
      mesh.cells.size() != field_size) {
    throw std::invalid_argument("halo field does not match LocalMesh owned/ghost layout");
  }

  std::vector<OrderedExchange> ordered;
  ordered.reserve(mesh.exchanges.size());
  for (const NeighborExchange &exchange : mesh.exchanges) {
    if (exchange.rank < 0 || exchange.rank >= ranks || exchange.rank == rank) {
      throw std::invalid_argument("halo exchange has an invalid neighbor rank");
    }
    auto validate_indices = [&](const std::vector<int> &indices, bool sending) {
      std::vector<int> sorted = indices;
      std::sort(sorted.begin(), sorted.end());
      if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::invalid_argument("halo exchange contains duplicate local cell indices");
      }
      for (int index : indices) {
        const bool valid = sending
                               ? mesh.is_owned_cell(index)
                               : index >= mesh.owned_cell_count &&
                                     static_cast<std::size_t>(index) < mesh.cells.size();
        if (!valid) {
          throw std::invalid_argument(sending ? "halo send index is not owned"
                                               : "halo receive index is not a ghost");
        }
      }
    };
    validate_indices(exchange.send_owned_local, true);
    validate_indices(exchange.receive_ghost_local, false);
    ordered.push_back({&exchange});
  }
  std::sort(ordered.begin(), ordered.end(), [](const OrderedExchange &a,
                                               const OrderedExchange &b) {
    return a.exchange->rank < b.exchange->rank;
  });
  for (std::size_t i = 1; i < ordered.size(); ++i) {
    if (ordered[i - 1].exchange->rank == ordered[i].exchange->rank) {
      throw std::invalid_argument("halo exchange has duplicate neighbor ranks");
    }
  }
  return ordered;
}

template <std::size_t Components, typename Value, typename ToWire,
          typename FromWire>
void exchange_impl(const LocalMesh &mesh, std::vector<Value> &values,
                   MPI_Comm communicator, int tag, ToWire to_wire,
                   FromWire from_wire) {
  const auto exchanges = validate_plan(mesh, values.size(), communicator, tag);
  if (exchanges.empty()) {
    return;
  }

  struct PendingExchange {
    const NeighborExchange *exchange{nullptr};
    std::vector<double> send;
    std::vector<double> receive;
    MPI_Request receive_request{MPI_REQUEST_NULL};
    MPI_Request send_request{MPI_REQUEST_NULL};
  };
  std::vector<PendingExchange> pending;
  pending.reserve(exchanges.size());
  for (const OrderedExchange &ordered : exchanges) {
    PendingExchange item;
    item.exchange = ordered.exchange;
    item.send.reserve(Components * item.exchange->send_owned_local.size());
    item.receive.resize(Components * item.exchange->receive_ghost_local.size());
    for (int local_cell : item.exchange->send_owned_local) {
      const auto wire = to_wire(values[static_cast<std::size_t>(local_cell)]);
      item.send.insert(item.send.end(), wire.begin(), wire.end());
    }
    if (item.send.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        item.receive.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
      throw std::invalid_argument("halo payload exceeds MPI int count range");
    }
    pending.push_back(std::move(item));
  }

  // Post all receives first, then sends.  This works for arbitrary neighbor
  // graphs without ordering assumptions and permits overlap in a solver.
  for (PendingExchange &item : pending) {
    mpi_check(MPI_Irecv(item.receive.data(), static_cast<int>(item.receive.size()),
                        MPI_DOUBLE, item.exchange->rank, tag, communicator,
                        &item.receive_request),
              "MPI_Irecv(halo)");
  }
  for (PendingExchange &item : pending) {
    mpi_check(MPI_Isend(item.send.data(), static_cast<int>(item.send.size()), MPI_DOUBLE,
                        item.exchange->rank, tag, communicator, &item.send_request),
              "MPI_Isend(halo)");
  }
  std::vector<MPI_Request> requests;
  requests.reserve(2 * pending.size());
  for (PendingExchange &item : pending) {
    requests.push_back(item.receive_request);
  }
  for (PendingExchange &item : pending) {
    requests.push_back(item.send_request);
  }
  std::vector<MPI_Status> statuses(requests.size());
  mpi_check(MPI_Waitall(static_cast<int>(requests.size()), requests.data(),
                        statuses.data()),
            "MPI_Waitall(halo)");
  for (std::size_t i = 0; i < pending.size(); ++i) {
    int received = 0;
    mpi_check(MPI_Get_count(&statuses[i], MPI_DOUBLE, &received),
              "MPI_Get_count(halo)");
    if (received != static_cast<int>(pending[i].receive.size())) {
      throw std::runtime_error("halo receive size differs from NeighborExchange plan");
    }
    const std::vector<double> &wire = pending[i].receive;
    for (std::size_t j = 0; j < pending[i].exchange->receive_ghost_local.size(); ++j) {
      const std::size_t first = Components * j;
      std::array<double, Components> value{};
      std::copy_n(wire.begin() + static_cast<std::ptrdiff_t>(first), Components,
                  value.begin());
      values[static_cast<std::size_t>(pending[i].exchange->receive_ghost_local[j])] =
          from_wire(value);
    }
  }
}

} // namespace

void exchange_conservative_halo(const LocalMesh &mesh,
                                std::vector<Conservative> &state,
                                MPI_Comm communicator) {
  exchange_impl<4>(
      mesh, state, communicator, kHaloConservativeTag,
      [](const Conservative &value) { return value; },
      [](const std::array<double, 4> &value) { return Conservative{value}; });
}

void exchange_primitive_gradient_halo(const LocalMesh &mesh,
                                      std::vector<PrimitiveGradient> &gradient,
                                      MPI_Comm communicator) {
  exchange_impl<8>(
      mesh, gradient, communicator, kHaloPrimitiveGradientTag,
      [](const PrimitiveGradient &value) {
        return std::array<double, 8>{value[0].x, value[0].y, value[1].x,
                                     value[1].y, value[2].x, value[2].y,
                                     value[3].x, value[3].y};
      },
      [](const std::array<double, 8> &value) {
        return PrimitiveGradient{{{value[0], value[1]}, {value[2], value[3]},
                                  {value[4], value[5]}, {value[6], value[7]}}};
      });
}

void exchange_limiter_halo(const LocalMesh &mesh,
                           std::vector<PrimitiveLimiter> &limiter,
                           MPI_Comm communicator) {
  exchange_impl<4>(
      mesh, limiter, communicator, kHaloLimiterTag,
      [](const PrimitiveLimiter &value) { return value; },
      [](const std::array<double, 4> &value) { return PrimitiveLimiter{value}; });
}

void exchange_conservative_increment_halo(const LocalMesh &mesh,
                                          std::vector<Conservative> &increment,
                                          MPI_Comm communicator) {
  exchange_impl<4>(
      mesh, increment, communicator, kHaloIncrementTag,
      [](const Conservative &value) { return value; },
      [](const std::array<double, 4> &value) { return Conservative{value}; });
}

} // namespace aerofv
