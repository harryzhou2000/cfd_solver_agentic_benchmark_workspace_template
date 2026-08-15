/// @file halo.cpp
/// Ghost-cell halo exchange implementation using MPI_Isend / MPI_Irecv.

#include "halo.hpp"
#include "logging.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <set>

namespace cfd {

HaloExchange build_halo_exchange(const Mesh& local_mesh, MPI_Comm /*comm*/) {
    HaloExchange halo;
    const std::size_t n_owned = local_mesh.n_owned();

    // --- send_cells: owned cells that border ghosts ---------------------------
    // For each neighbor rank, collect owned cells that share a face with a
    // ghost owned by that rank.
    for (std::size_t li = 0; li < n_owned; ++li) {
        const Cell& cell = local_mesh.cells[li];
        std::set<int> neighbor_ranks;
        for (auto nbi : cell.neighbors) {
            const Cell& nb = local_mesh.cells[nbi];
            if (nb.is_ghost) {
                neighbor_ranks.insert(nb.owner_rank);
            }
        }
        for (int r : neighbor_ranks) {
            halo.send_cells[r].push_back(li);
        }
    }

    // --- recv_cells: ghost cells grouped by owning rank -----------------------
    for (std::size_t li = n_owned; li < local_mesh.n_cells(); ++li) {
        const Cell& cell = local_mesh.cells[li];
        if (cell.is_ghost) {
            halo.recv_cells[cell.owner_rank].push_back(li);
        }
    }

    return halo;
}

void exchange_ghost_states(std::vector<Vec4>& U, const HaloExchange& halo, MPI_Comm comm) {
    // Collect all neighbor ranks (union of send and recv keys)
    std::set<int> neighbors;
    for (const auto& [rank, _] : halo.send_cells) neighbors.insert(rank);
    for (const auto& [rank, _] : halo.recv_cells) neighbors.insert(rank);

    const int num_neighbors = static_cast<int>(neighbors.size());

    // Allocate request arrays
    std::vector<MPI_Request> send_reqs(num_neighbors);
    std::vector<MPI_Request> recv_reqs(num_neighbors);

    // Buffers (owned by us until MPI_Waitall completes)
    std::vector<std::vector<double>> send_bufs;
    std::vector<std::vector<double>> recv_bufs;

    send_bufs.reserve(static_cast<std::size_t>(num_neighbors));
    recv_bufs.reserve(static_cast<std::size_t>(num_neighbors));

    int req_idx = 0;

    // --- Post receives first (for better overlapping) -------------------------
    for (int rank : neighbors) {
        auto rit = halo.recv_cells.find(rank);
        std::size_t nrecv = (rit != halo.recv_cells.end()) ? rit->second.size() : 0;

        // Allocate recv buffer: nrecv cells * 4 doubles
        recv_bufs.emplace_back(nrecv * 4, 0.0);
        auto& buf = recv_bufs.back();

        if (nrecv > 0) {
            MPI_Irecv(buf.data(), static_cast<int>(nrecv * 4), MPI_DOUBLE,
                      rank, 0, comm, &recv_reqs[static_cast<std::size_t>(req_idx)]);
        } else {
            recv_reqs[static_cast<std::size_t>(req_idx)] = MPI_REQUEST_NULL;
        }
        ++req_idx;
    }

    // --- Pack and post sends --------------------------------------------------
    req_idx = 0;
    for (int rank : neighbors) {
        auto sit = halo.send_cells.find(rank);
        std::size_t nsend = (sit != halo.send_cells.end()) ? sit->second.size() : 0;

        // Allocate send buffer: nsend cells * 4 doubles
        send_bufs.emplace_back(nsend * 4, 0.0);
        auto& buf = send_bufs.back();

        if (nsend > 0) {
            // Pack conservative states
            for (std::size_t j = 0; j < nsend; ++j) {
                std::size_t li = sit->second[j];
                const Vec4& state = U[li];
                std::memcpy(buf.data() + j * 4, state.data(), 4 * sizeof(double));
            }

            MPI_Isend(buf.data(), static_cast<int>(nsend * 4), MPI_DOUBLE,
                      rank, 0, comm, &send_reqs[static_cast<std::size_t>(req_idx)]);
        } else {
            send_reqs[static_cast<std::size_t>(req_idx)] = MPI_REQUEST_NULL;
        }
        ++req_idx;
    }

    // --- Wait for receives, unpack --------------------------------------------
    req_idx = 0;
    for (int rank : neighbors) {
        auto rit = halo.recv_cells.find(rank);

        if (rit != halo.recv_cells.end() && !rit->second.empty()) {
            MPI_Wait(&recv_reqs[static_cast<std::size_t>(req_idx)], MPI_STATUS_IGNORE);

            // Unpack into ghost cells
            const auto& buf = recv_bufs[static_cast<std::size_t>(req_idx)];
            const auto& cells = rit->second;
            for (std::size_t j = 0; j < cells.size(); ++j) {
                std::size_t li = cells[j];
                U[li] = Vec4(buf[j * 4], buf[j * 4 + 1],
                             buf[j * 4 + 2], buf[j * 4 + 3]);
            }
        }
        ++req_idx;
    }

    // --- Wait for all sends to complete ---------------------------------------
    MPI_Waitall(num_neighbors, send_reqs.data(), MPI_STATUSES_IGNORE);
}

} // namespace cfd
