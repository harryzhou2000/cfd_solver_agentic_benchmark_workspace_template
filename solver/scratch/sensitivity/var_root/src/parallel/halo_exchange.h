// cns2d -- neighbour-scoped halo exchange.
//
// All iteration-time communication goes through this class.  It is strictly
// neighbour-scoped: a rank posts one MPI_Irecv and one MPI_Isend per neighbour
// listed in its partition plan and never contacts any other rank.  There is no
// MPI_Allgather / MPI_Allgatherv of state anywhere in the solver loop; the only
// collectives used during iterations are scalar MPI_Allreduce calls for
// residual norms, force sums and inner-loop convergence tests.
//
// The send and receive orderings were fixed during setup so that the payload is
// a bare array of doubles with no index metadata on the wire.
#pragma once

#include <vector>

// The deprecated MPI-2 C++ bindings are not used; skipping them avoids pulling
// in a header that triggers function-cast warnings on modern compilers.
#ifndef OMPI_SKIP_MPICXX
#define OMPI_SKIP_MPICXX 1
#endif
#ifndef MPICH_SKIP_MPICXX
#define MPICH_SKIP_MPICXX 1
#endif
#include <mpi.h>

#include "core/types.h"
#include "parallel/distributed_mesh.h"

namespace cns2d {

class HaloExchange {
 public:
  // 'max_components' sizes the internal buffers for the widest payload that
  // will be exchanged (conserved state = kNumVars, gradients = kNumVars*kDim).
  HaloExchange(const DistributedMesh &mesh, int max_components);

  // Exchange 'num_components' values per cell.  'data' is indexed as
  // data[cell * stride + component] for cell in [0, numLocal()); owned entries
  // are read, ghost entries are written.
  void exchange(Real *data, int num_components, int stride);

  // Convenience overload for the common contiguous case (stride == components).
  void exchange(Real *data, int num_components) { exchange(data, num_components, num_components); }

  // Cumulative counters for diagnostics/reporting.
  long long numExchanges() const { return num_exchanges_; }
  long long numDoublesSent() const { return num_doubles_sent_; }
  const char *pattern() const { return "neighbor_isend_irecv"; }

 private:
  const DistributedMesh &mesh_;
  int max_components_{0};

  std::vector<std::vector<Real>> send_buffers_;
  std::vector<std::vector<Real>> recv_buffers_;
  std::vector<MPI_Request> requests_;

  long long num_exchanges_{0};
  long long num_doubles_sent_{0};
};

}  // namespace cns2d
