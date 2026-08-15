#include "partition/halo_exchange.hpp"
#include <algorithm>
#include <cstring>
#include <numeric>

namespace cfd {

void halo_exchange(double* data, int n_comp,
                   const std::vector<std::vector<Int>>& send_cells,
                   const std::vector<std::vector<Int>>& recv_cells,
                   const std::vector<int>& neighbor_ranks,
                   MPI_Comm comm) {

    int n_neighbors = neighbor_ranks.size();
    if (n_neighbors == 0) return;

    // Fixed tag for every message: each receive names its source rank
    // explicitly, so the (source, tag) pairs are unique without
    // per-neighbor-index tags. (Index-based tags would deadlock whenever
    // the neighbor ordering differs between two communicating ranks.)
    const int tag = 0;

    std::vector<MPI_Request> requests;
    std::vector<std::vector<double>> send_buffers(n_neighbors);
    std::vector<std::vector<double>> recv_buffers(n_neighbors);

    // Post non-blocking receives into temp buffers. Ghost cells of one
    // neighbor are NOT contiguous in the local array (build_rank_partition
    // groups them by global id), so a direct receive into `data` would
    // overwrite the wrong cells; scatter after completion instead.
    for (int ni = 0; ni < n_neighbors; ++ni) {
        // MPI counts are int; Int is signed so a cast is a safe narrow
        int count = static_cast<int>(recv_cells[ni].size()) * n_comp;
        if (count == 0) continue;
        recv_buffers[ni].resize(count);
        MPI_Request req;
        MPI_Irecv(recv_buffers[ni].data(), count, MPI_DOUBLE,
                  neighbor_ranks[ni], tag, comm, &req);
        requests.push_back(req);
    }

    // Pack and post non-blocking sends
    for (int ni = 0; ni < n_neighbors; ++ni) {
        int count = static_cast<int>(send_cells[ni].size()) * n_comp;
        if (count == 0) continue;

        send_buffers[ni].resize(count);
        for (size_t i = 0; i < send_cells[ni].size(); ++i) {
            Int cell = send_cells[ni][i];
            std::memcpy(send_buffers[ni].data() + i * n_comp,
                       data + cell * n_comp,
                       n_comp * sizeof(double));
        }

        MPI_Request req;
        MPI_Isend(send_buffers[ni].data(), count, MPI_DOUBLE,
                  neighbor_ranks[ni], tag, comm, &req);
        requests.push_back(req);
    }

    // Wait for all communication to complete
    if (!requests.empty()) {
        MPI_Waitall(requests.size(), requests.data(), MPI_STATUSES_IGNORE);
    }

    // Scatter received buffers into the (non-contiguous) ghost cells
    for (int ni = 0; ni < n_neighbors; ++ni) {
        if (recv_buffers[ni].empty()) continue;
        for (size_t i = 0; i < recv_cells[ni].size(); ++i) {
            Int cell = recv_cells[ni][i];
            std::memcpy(data + cell * n_comp,
                       recv_buffers[ni].data() + i * n_comp,
                       n_comp * sizeof(double));
        }
    }
}

void halo_exchange_conserved(std::vector<Conserved>& U,
                              const std::vector<std::vector<Int>>& send_cells,
                              const std::vector<std::vector<Int>>& recv_cells,
                              const std::vector<int>& neighbor_ranks,
                              MPI_Comm comm) {
    halo_exchange(reinterpret_cast<double*>(U.data()), 4,
                  send_cells, recv_cells, neighbor_ranks, comm);
}

double global_l2_norm(double local_sum_sq, MPI_Comm comm) {
    double global_sum_sq = 0;
    MPI_Allreduce(&local_sum_sq, &global_sum_sq, 1, MPI_DOUBLE, MPI_SUM, comm);
    return std::sqrt(global_sum_sq);
}

double global_max(double local_value, MPI_Comm comm) {
    double global = 0;
    MPI_Allreduce(&local_value, &global, 1, MPI_DOUBLE, MPI_MAX, comm);
    return global;
}

double global_sum(double local_value, MPI_Comm comm) {
    double global = 0;
    MPI_Allreduce(&local_value, &global, 1, MPI_DOUBLE, MPI_SUM, comm);
    return global;
}

bool all_ranks_agree(bool local_condition, MPI_Comm comm) {
    int local = local_condition ? 1 : 0;
    int global = 0;
    MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, comm);
    return global == 1;
}

} // namespace cfd
