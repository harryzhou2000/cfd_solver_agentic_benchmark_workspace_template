#include "solver/partition_types.hpp"

#include <mpi.h>

#include <vector>

namespace solver {

namespace {
// Tag for halo messages. A constant tag is safe: each rank sends at most
// one message to, and receives at most one message from, each neighbor
// rank, and messages are distinguished by source/destination.
constexpr int kHaloTag = 100;
} // namespace

void init_halo_buffers(const DistributedMesh& mesh, HaloBuffers& bufs) {
    const int nneigh = static_cast<int>(mesh.neighbors.size());
    bufs.send_bufs.resize(nneigh);
    bufs.recv_bufs.resize(nneigh);
    bufs.requests.resize(2 * nneigh);
    for (int i = 0; i < nneigh; ++i) {
        const NeighborInfo& ni = mesh.neighbors[i];
        bufs.send_bufs[i].resize(kStateSize * ni.send_indices.size());
        bufs.recv_bufs[i].resize(kStateSize * ni.recv_indices.size());
    }
}

void start_halo_exchange(const std::vector<double>& state,
                         DistributedMesh& mesh, HaloBuffers& bufs) {
    const int nneigh = static_cast<int>(mesh.neighbors.size());
    for (int i = 0; i < nneigh; ++i) {
        const NeighborInfo& ni = mesh.neighbors[i];

        // Pack owned cell state (kStateSize doubles per cell) into the send
        // buffer.
        std::vector<double>& sbuf = bufs.send_bufs[i];
        for (size_t k = 0; k < ni.send_indices.size(); ++k) {
            const int idx = ni.send_indices[k];
            for (int c = 0; c < kStateSize; ++c) {
                sbuf[kStateSize * k + c] = state[kStateSize * idx + c];
            }
        }

        // Post non-blocking send and receive with the shared halo tag.
        MPI_Isend(sbuf.data(), static_cast<int>(sbuf.size()), MPI_DOUBLE,
                  ni.rank, kHaloTag, mesh.comm, &bufs.requests[2 * i]);
        MPI_Irecv(bufs.recv_bufs[i].data(),
                  static_cast<int>(bufs.recv_bufs[i].size()), MPI_DOUBLE,
                  ni.rank, kHaloTag, mesh.comm, &bufs.requests[2 * i + 1]);
    }
}

void finish_halo_exchange(std::vector<double>& state, DistributedMesh& mesh,
                          HaloBuffers& bufs) {
    const int nneigh = static_cast<int>(mesh.neighbors.size());
    if (nneigh > 0) {
        MPI_Waitall(static_cast<int>(bufs.requests.size()),
                    bufs.requests.data(), MPI_STATUSES_IGNORE);
    }

    // Unpack received state into the ghost cells.
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    for (int i = 0; i < nneigh; ++i) {
        const NeighborInfo& ni = mesh.neighbors[i];
        const std::vector<double>& rbuf = bufs.recv_bufs[i];
        for (size_t k = 0; k < ni.recv_indices.size(); ++k) {
            const int ghost_idx = ni.recv_indices[k];
            const int base = kStateSize * (num_owned + ghost_idx);
            for (int c = 0; c < kStateSize; ++c) {
                state[base + c] = rbuf[kStateSize * k + c];
            }
        }
    }
}

} // namespace solver
