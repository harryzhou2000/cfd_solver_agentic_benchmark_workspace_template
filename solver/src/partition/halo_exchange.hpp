#pragma once

#include "mesh/mesh.hpp"
#include "common/types.hpp"
#include <mpi.h>

// Perform neighbor-scoped halo exchange: send owned state to neighbors,
// receive to fill ghost state.
void exchange_halo(RankMesh& rm, MPI_Comm comm);

// Initialize the MPI halo communication pattern (persistent requests)
void setup_halo_pattern(RankMesh& rm, MPI_Comm comm);

// Global sum reduction for residual vectors (4 components per cell)
void global_residual_norm(const std::vector<StateVector>& local_res,
                          const std::vector<Real>& cell_volumes,
                          Real& l2_norm, Real& linf_norm,
                          MPI_Comm comm);

// Global sum for forces
struct ForceComponents {
    Real pressure_drag = 0, viscous_drag = 0;
    Real pressure_lift = 0, viscous_lift = 0;
    Real moment_z = 0;
};

void global_reduce_forces(const ForceComponents& local_f,
                          ForceComponents& global_f,
                          MPI_Comm comm);
