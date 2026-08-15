// Neighbor-scoped MPI halo exchange. Fills the ghost-cell conservative state
// from the owning ranks using MPI_Isend/Irecv (one message per neighbor,
// restricted to the required halo payload). No full-state allgather. Global
// reductions for residuals/forces are separate (see residualL2 / run).
#include "solver.hpp"
#include <mpi.h>
#include <vector>

namespace cfd {

static constexpr int MAXRANK = 256;

void Solver::exchangeHalo() {
  const int n = (int)lm.neighbor_ranks.size();
  if (n == 0) return;  // no neighbors (single rank or isolated partition)
  std::vector<std::vector<double>> sbuf(n), rbuf(n);
  std::vector<MPI_Request> reqs;
  reqs.reserve(2 * n);
  // post receives first
  for (int k = 0; k < n; ++k) {
    int nb = lm.neighbor_ranks[k];
    int cnt = (int)lm.recv_local[k].size() * NEQ;
    rbuf[k].assign(cnt, 0.0);
    MPI_Request rr;
    int tag = 1000 + nb * MAXRANK + rank;   // sender=nb, receiver=me
    MPI_Irecv(rbuf[k].data(), cnt, MPI_DOUBLE, nb, tag, MPI_COMM_WORLD, &rr);
    reqs.push_back(rr);
  }
  // pack and send
  for (int k = 0; k < n; ++k) {
    int nb = lm.neighbor_ranks[k];
    int cnt = (int)lm.send_local[k].size() * NEQ;
    sbuf[k].assign(cnt, 0.0);
    for (int i = 0; i < (int)lm.send_local[k].size(); ++i)
      for (int e = 0; e < NEQ; ++e)
        sbuf[k][i*NEQ + e] = U[lm.send_local[k][i]*NEQ + e];
    MPI_Request sr;
    int tag = 1000 + rank * MAXRANK + nb;    // sender=me, receiver=nb
    MPI_Isend(sbuf[k].data(), cnt, MPI_DOUBLE, nb, tag, MPI_COMM_WORLD, &sr);
    reqs.push_back(sr);
  }
  std::vector<MPI_Status> stats(reqs.size());
  MPI_Waitall((int)reqs.size(), reqs.data(), stats.data());
  // unpack into ghost slots
  for (int k = 0; k < n; ++k)
    for (int i = 0; i < (int)lm.recv_local[k].size(); ++i)
      for (int e = 0; e < NEQ; ++e)
        U[lm.recv_local[k][i]*NEQ + e] = rbuf[k][i*NEQ + e];
}

}  // namespace cfd
