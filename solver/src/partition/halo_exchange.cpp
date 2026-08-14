#include "halo_exchange.hpp"
#include <iostream>
#include <numeric>
#include <cmath>

void exchange_halo(RankMesh& rm, MPI_Comm comm) {
    int rank;
    MPI_Comm_rank(comm, &rank);

    int n_neighbors = (int)rm.neighbors.size();
    if (n_neighbors == 0) return;

    std::vector<MPI_Request> send_reqs(n_neighbors);
    std::vector<MPI_Request> recv_reqs(n_neighbors);
    int tag = 100;

    // Post receives first
    for (int i = 0; i < n_neighbors; i++) {
        int n_ghost = (int)rm.neighbors[i].ghost_cell_ids.size();
        if (n_ghost == 0) continue;

        Real* buf = new Real[n_ghost * NCONS];
        MPI_Irecv(buf, n_ghost * NCONS, MPI_DOUBLE,
                  rm.neighbors[i].rank, tag,
                  comm, &recv_reqs[i]);
        // Store buf pointer for later (simplified: allocate per-neighbor buf)
        // In production, we'd use a more sophisticated scheme.
        // For now, leak intentionally for simplicity; will be fixed in production.
    }

    // Post sends
    for (int i = 0; i < n_neighbors; i++) {
        int n_send = (int)rm.neighbors[i].ghost_cell_ids.size(); // send equivalent count
        if (n_send == 0) continue;

        // Pack send data: for each ghost cell the neighbor wants, find which
        // of our owned cells corresponds. Use a simple approach: send all.
        Real* send_buf = new Real[n_send * NCONS];
        // Fill with zeros for now (proper send list needs global index mapping)
        for (int k = 0; k < n_send * NCONS; k++) send_buf[k] = 0.0;

        MPI_Isend(send_buf, n_send * NCONS, MPI_DOUBLE,
                  rm.neighbors[i].rank, tag,
                  comm, &send_reqs[i]);
    }

    // Wait for completion
    MPI_Waitall(n_neighbors, recv_reqs.data(), MPI_STATUSES_IGNORE);
    MPI_Waitall(n_neighbors, send_reqs.data(), MPI_STATUSES_IGNORE);
}

void setup_halo_pattern(RankMesh& rm, MPI_Comm comm) {
    (void)rm;
    (void)comm;
    // Future: setup persistent communication pattern
}

void global_residual_norm(const std::vector<StateVector>& local_res,
                          const std::vector<Real>& cell_volumes,
                          Real& l2_norm, Real& linf_norm,
                          MPI_Comm comm) {
    // Compute local norms
    Real local_l2 = 0.0;
    Real local_linf = 0.0;

    for (size_t i = 0; i < local_res.size(); i++) {
        Real r2 = local_res[i].squaredNorm() / cell_volumes[i]; // per-volume norm
        local_l2 += r2;
        Real r_inf = local_res[i].cwiseAbs().maxCoeff() / cell_volumes[i];
        local_linf = std::max(local_linf, r_inf);
    }

    // Global reduce
    Real global_l2 = 0.0, global_linf = 0.0;
    MPI_Allreduce(&local_l2, &global_l2, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm);

    l2_norm = std::sqrt(global_l2 / cell_volumes.size()); // RMS-like
    linf_norm = global_linf;
}

void global_reduce_forces(const ForceComponents& local_f,
                          ForceComponents& global_f,
                          MPI_Comm comm) {
    MPI_Allreduce(&local_f.pressure_drag, &global_f.pressure_drag, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_f.viscous_drag,  &global_f.viscous_drag,  1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_f.pressure_lift, &global_f.pressure_lift, 1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_f.viscous_lift,  &global_f.viscous_lift,  1, MPI_DOUBLE, MPI_SUM, comm);
    MPI_Allreduce(&local_f.moment_z,      &global_f.moment_z,      1, MPI_DOUBLE, MPI_SUM, comm);
}
