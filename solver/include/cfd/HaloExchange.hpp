#pragma once

#include "cfd/DistributedMesh.hpp"

#include <mpi.h>

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cfd {

class HaloExchange {
 public:
  HaloExchange(const LocalMesh& mesh, MPI_Comm communicator)
      : mesh_(mesh), communicator_(communicator) {}

  template <typename T>
  void exchange(std::vector<T>& values, int tag) const {
    static_assert(std::is_trivially_copyable_v<T>, "halo values must be trivially copyable");
    if (values.size() != mesh_.cells.size()) {
      throw std::runtime_error("halo exchange vector size does not match local cells");
    }
    struct Buffers {
      std::vector<T> send;
      std::vector<T> receive;
    };
    std::vector<Buffers> buffers(mesh_.halo.size());
    std::vector<MPI_Request> requests;
    requests.reserve(2 * mesh_.halo.size());

    for (std::size_t i = 0; i < mesh_.halo.size(); ++i) {
      const HaloPeer& peer = mesh_.halo[i];
      Buffers& buffer = buffers[i];
      buffer.receive.resize(peer.receive_ghosts.size());
      MPI_Request request = MPI_REQUEST_NULL;
      const int bytes = static_cast<int>(buffer.receive.size() * sizeof(T));
      if (MPI_Irecv(buffer.receive.data(), bytes, MPI_BYTE, peer.rank, tag, communicator_,
                    &request) != MPI_SUCCESS) {
        throw std::runtime_error("MPI_Irecv failed during halo exchange");
      }
      requests.push_back(request);
    }
    for (std::size_t i = 0; i < mesh_.halo.size(); ++i) {
      const HaloPeer& peer = mesh_.halo[i];
      Buffers& buffer = buffers[i];
      buffer.send.reserve(peer.send_cells.size());
      for (const int cell : peer.send_cells) buffer.send.push_back(values.at(cell));
      MPI_Request request = MPI_REQUEST_NULL;
      const int bytes = static_cast<int>(buffer.send.size() * sizeof(T));
      if (MPI_Isend(buffer.send.data(), bytes, MPI_BYTE, peer.rank, tag, communicator_,
                    &request) != MPI_SUCCESS) {
        throw std::runtime_error("MPI_Isend failed during halo exchange");
      }
      requests.push_back(request);
    }
    if (!requests.empty() &&
        MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE) !=
            MPI_SUCCESS) {
      throw std::runtime_error("MPI_Waitall failed during halo exchange");
    }
    for (std::size_t i = 0; i < mesh_.halo.size(); ++i) {
      const HaloPeer& peer = mesh_.halo[i];
      const Buffers& buffer = buffers[i];
      if (buffer.receive.size() != peer.receive_ghosts.size()) {
        throw std::runtime_error("halo receive size mismatch");
      }
      for (std::size_t j = 0; j < peer.receive_ghosts.size(); ++j) {
        values.at(static_cast<std::size_t>(peer.receive_ghosts[j])) = buffer.receive[j];
      }
    }
  }

 private:
  const LocalMesh& mesh_;
  MPI_Comm communicator_;
};

}  // namespace cfd
