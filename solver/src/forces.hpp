#pragma once

#include "common.hpp"
#include "mesh_local.hpp"
#include "solver.hpp"
#include "physics.hpp"
#include <mpi.h>
#include <vector>

namespace cfd {

void compute_forces_local(const LocalMesh& mesh, const std::vector<Vec4>& U,
                          const Vec4& U_inf, const GasModel& gas, const CaseConfig& cfg,
                          double mu, double k, SolverState& s, int rank, MPI_Comm comm);

}  // namespace cfd
