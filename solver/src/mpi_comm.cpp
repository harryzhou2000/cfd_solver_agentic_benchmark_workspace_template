#include "mpi_comm.hpp"
#include <cstring>

void HaloExchanger::init(const LocalMesh& lm, MPI_Comm comm) {
    comm_ = comm;
    MPI_Comm_rank(comm_, &rank_);
    MPI_Comm_size(comm_, &nranks_);

    neighbors_.clear();
    for (auto& he : lm.halos) {
        Neighbor n;
        n.rank = he.neighbor_rank;
        n.send_cells = he.send_cells;
        n.recv_cells = he.recv_cells;
        neighbors_.push_back(std::move(n));
    }
}

void HaloExchanger::exchange(std::vector<Vec4>& states) {
    if (neighbors_.empty()) return;

    std::vector<MPI_Request> reqs;
    std::vector<std::vector<double>> send_bufs(neighbors_.size());
    std::vector<std::vector<double>> recv_bufs(neighbors_.size());

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        recv_bufs[i].resize(nb.recv_cells.size() * 4);
        MPI_Request req;
        MPI_Irecv(recv_bufs[i].data(), (int)recv_bufs[i].size(), MPI_DOUBLE,
                  nb.rank, 0, comm_, &req);
        reqs.push_back(req);
    }

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        send_bufs[i].resize(nb.send_cells.size() * 4);
        for (int j = 0; j < (int)nb.send_cells.size(); j++) {
            auto& s = states[nb.send_cells[j]];
            std::memcpy(&send_bufs[i][j * 4], s.data(), 4 * sizeof(double));
        }
        MPI_Request req;
        MPI_Isend(send_bufs[i].data(), (int)send_bufs[i].size(), MPI_DOUBLE,
                  nb.rank, 0, comm_, &req);
        reqs.push_back(req);
    }

    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        for (int j = 0; j < (int)nb.recv_cells.size(); j++) {
            std::memcpy(states[nb.recv_cells[j]].data(), &recv_bufs[i][j * 4], 4 * sizeof(double));
        }
    }
}

void HaloExchanger::exchange_gradients(std::vector<std::array<Vec4, 2>>& grads) {
    if (neighbors_.empty()) return;

    std::vector<MPI_Request> reqs;
    std::vector<std::vector<double>> send_bufs(neighbors_.size());
    std::vector<std::vector<double>> recv_bufs(neighbors_.size());

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        recv_bufs[i].resize(nb.recv_cells.size() * 8);
        MPI_Request req;
        MPI_Irecv(recv_bufs[i].data(), (int)recv_bufs[i].size(), MPI_DOUBLE,
                  nb.rank, 1, comm_, &req);
        reqs.push_back(req);
    }

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        send_bufs[i].resize(nb.send_cells.size() * 8);
        for (int j = 0; j < (int)nb.send_cells.size(); j++) {
            int ci = nb.send_cells[j];
            std::memcpy(&send_bufs[i][j * 8], grads[ci][0].data(), 4 * sizeof(double));
            std::memcpy(&send_bufs[i][j * 8 + 4], grads[ci][1].data(), 4 * sizeof(double));
        }
        MPI_Request req;
        MPI_Isend(send_bufs[i].data(), (int)send_bufs[i].size(), MPI_DOUBLE,
                  nb.rank, 1, comm_, &req);
        reqs.push_back(req);
    }

    MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

    for (int i = 0; i < (int)neighbors_.size(); i++) {
        auto& nb = neighbors_[i];
        for (int j = 0; j < (int)nb.recv_cells.size(); j++) {
            int ci = nb.recv_cells[j];
            std::memcpy(grads[ci][0].data(), &recv_bufs[i][j * 8], 4 * sizeof(double));
            std::memcpy(grads[ci][1].data(), &recv_bufs[i][j * 8 + 4], 4 * sizeof(double));
        }
    }
}

double mpi_allreduce_sum(double val, MPI_Comm comm) {
    double result;
    MPI_Allreduce(&val, &result, 1, MPI_DOUBLE, MPI_SUM, comm);
    return result;
}

double mpi_allreduce_max(double val, MPI_Comm comm) {
    double result;
    MPI_Allreduce(&val, &result, 1, MPI_DOUBLE, MPI_MAX, comm);
    return result;
}
