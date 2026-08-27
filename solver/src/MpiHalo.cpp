#include "MpiHalo.hpp"
#include <cassert>
#include <vector>

static constexpr int TAG_BASE = 1000;

void haloExchange(std::vector<StateVec>& states, const LocalMesh& lm, MPI_Comm comm) {
    if (lm.n_ranks == 1) return;
    int nh = (int)lm.halos.size();
    std::vector<MPI_Request> reqs;
    reqs.reserve(2*nh);

    // Pack send buffers and post receives
    std::vector<std::vector<double>> send_bufs(nh), recv_bufs(nh);
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int ns = (int)halo.send_cells.size();
        int nr = (int)halo.recv_cells.size();
        send_bufs[h].resize(ns*4);
        recv_bufs[h].resize(nr*4);
        for (int i = 0; i < ns; i++) {
            const auto& s = states[halo.send_cells[i]];
            for (int k=0; k<4; k++) send_bufs[h][i*4+k] = s[k];
        }
        int tag = TAG_BASE + lm.rank * 10000 + halo.neighbor_rank;
        if (ns > 0) {
            MPI_Request req;
            MPI_Isend(send_bufs[h].data(), ns*4, MPI_DOUBLE,
                      halo.neighbor_rank, tag, comm, &req);
            reqs.push_back(req);
        }
        int rtag = TAG_BASE + halo.neighbor_rank * 10000 + lm.rank;
        if (nr > 0) {
            MPI_Request req;
            MPI_Irecv(recv_bufs[h].data(), nr*4, MPI_DOUBLE,
                      halo.neighbor_rank, rtag, comm, &req);
            reqs.push_back(req);
        }
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);

    // Unpack receive buffers
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int nr = (int)halo.recv_cells.size();
        for (int i = 0; i < nr; i++) {
            auto& s = states[halo.recv_cells[i]];
            for (int k=0; k<4; k++) s[k] = recv_bufs[h][i*4+k];
        }
    }
}

void haloExchangeScalar(std::vector<double>& values, const LocalMesh& lm, MPI_Comm comm) {
    if (lm.n_ranks == 1) return;
    int nh = (int)lm.halos.size();
    std::vector<MPI_Request> reqs;
    reqs.reserve(2*nh);
    std::vector<std::vector<double>> send_bufs(nh), recv_bufs(nh);
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int ns = (int)halo.send_cells.size();
        int nr = (int)halo.recv_cells.size();
        send_bufs[h].resize(ns); recv_bufs[h].resize(nr);
        for (int i = 0; i < ns; i++) send_bufs[h][i] = values[halo.send_cells[i]];
        int tag = TAG_BASE + lm.rank * 10000 + halo.neighbor_rank + 1;
        if (ns > 0) {
            MPI_Request req;
            MPI_Isend(send_bufs[h].data(), ns, MPI_DOUBLE, halo.neighbor_rank, tag, comm, &req);
            reqs.push_back(req);
        }
        int rtag = TAG_BASE + halo.neighbor_rank * 10000 + lm.rank + 1;
        if (nr > 0) {
            MPI_Request req;
            MPI_Irecv(recv_bufs[h].data(), nr, MPI_DOUBLE, halo.neighbor_rank, rtag, comm, &req);
            reqs.push_back(req);
        }
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int nr = (int)halo.recv_cells.size();
        for (int i = 0; i < nr; i++) values[halo.recv_cells[i]] = recv_bufs[h][i];
    }
}

// Generic N-double-per-cell halo exchange (used for gradient arrays).
// TAG_BASE+3 avoids collision with states (TAG_BASE) and scalars (TAG_BASE+1).
template<int N, typename T>
static void haloExchangeND(std::vector<T>& arr, const LocalMesh& lm, MPI_Comm comm) {
    if (lm.n_ranks == 1) return;
    int nh = (int)lm.halos.size();
    std::vector<std::vector<double>> send_bufs(nh), recv_bufs(nh);
    std::vector<MPI_Request> reqs;
    reqs.reserve(2*nh);
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int ns = (int)halo.send_cells.size();
        int nr = (int)halo.recv_cells.size();
        send_bufs[h].resize(ns*N);
        recv_bufs[h].resize(nr*N);
        for (int i = 0; i < ns; i++) {
            const double* src = reinterpret_cast<const double*>(&arr[halo.send_cells[i]]);
            for (int d = 0; d < N; d++) send_bufs[h][i*N+d] = src[d];
        }
        int tag  = TAG_BASE + 3 + lm.rank * 10000 + halo.neighbor_rank;
        int rtag = TAG_BASE + 3 + halo.neighbor_rank * 10000 + lm.rank;
        if (ns > 0) { MPI_Request req; MPI_Isend(send_bufs[h].data(), ns*N, MPI_DOUBLE, halo.neighbor_rank, tag, comm, &req); reqs.push_back(req); }
        if (nr > 0) { MPI_Request req; MPI_Irecv(recv_bufs[h].data(), nr*N, MPI_DOUBLE, halo.neighbor_rank, rtag, comm, &req); reqs.push_back(req); }
    }
    if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
    for (int h = 0; h < nh; h++) {
        const auto& halo = lm.halos[h];
        int nr = (int)halo.recv_cells.size();
        for (int i = 0; i < nr; i++) {
            double* dst = reinterpret_cast<double*>(&arr[halo.recv_cells[i]]);
            for (int d = 0; d < N; d++) dst[d] = recv_bufs[h][i*N+d];
        }
    }
}

void haloExchangeGrads(std::vector<StateGrad>& grads, const LocalMesh& lm, MPI_Comm comm) {
    haloExchangeND<8>(grads, lm, comm);
}

void haloExchangePrimGrads(std::vector<PrimGrads>& prim_grads, const LocalMesh& lm, MPI_Comm comm) {
    haloExchangeND<6>(prim_grads, lm, comm);
}
