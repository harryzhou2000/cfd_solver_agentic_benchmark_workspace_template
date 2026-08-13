#pragma once

#include "partition.hpp"

#include <mpi.h>

#include <array>
#include <vector>

namespace aerofv {

using PrimitiveLimiter = std::array<double, 4>;

// Exchange tags are distinct by payload type and deliberately well below the
// MPI-required minimum MPI_TAG_UB of 32767.  Each routine completes all of its
// requests before returning, so a solver may safely call them in sequence.
inline constexpr int kHaloConservativeTag = 5101;
inline constexpr int kHaloPrimitiveGradientTag = 5102;
inline constexpr int kHaloLimiterTag = 5103;
inline constexpr int kHaloIncrementTag = 5104;

// Every vector is indexed by the LocalMesh cell ordering: owned cells first,
// then ghosts.  Only NeighborExchange::send_owned_local values are sent and
// only receive_ghost_local values are overwritten.  Communication is strictly
// rank-neighbor scoped and uses MPI_Irecv/MPI_Isend; it never replicates state.
// Invalid plans, field sizes, or local indices throw std::invalid_argument.
void exchange_conservative_halo(const LocalMesh &mesh,
                                std::vector<Conservative> &state,
                                MPI_Comm communicator);
void exchange_primitive_gradient_halo(const LocalMesh &mesh,
                                      std::vector<PrimitiveGradient> &gradient,
                                      MPI_Comm communicator);
void exchange_limiter_halo(const LocalMesh &mesh,
                           std::vector<PrimitiveLimiter> &limiter,
                           MPI_Comm communicator);
void exchange_conservative_increment_halo(const LocalMesh &mesh,
                                          std::vector<Conservative> &increment,
                                          MPI_Comm communicator);

} // namespace aerofv
