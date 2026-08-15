#pragma once

// Pseudo-time stepping: local time steps and CFL ramp.

#include <vector>

#include "partition/partition.hpp"

namespace cfd {

// Computes the local pseudo-time step for every OWNED cell:
//   dt = CFL * V_cell / max(1e-10, lambda_c + lambda_v)
//   lambda_c = sum over all faces of (|vn_f| + a_f) * A_f   (convective)
//   lambda_v = sum over all faces of max(4/3, gamma/Pr) * (mu/rho_cell)
//              * A_f^2 / V_cell                              (viscous)
// Face values are averaged between the two cell states (boundary faces use
// the cell's own values); ghost states must be up to date.
std::vector<double> compute_local_timesteps(const std::vector<double>& U,
                                            const DistributedMesh& dmesh,
                                            double cfl, double gamma,
                                            double mu, double Pr, double R);

// Linear CFL ramp from cfl_initial to cfl_max over ramp_steps (ramp_steps <= 0
// gives cfl_max immediately).
double compute_cfl(int step, double cfl_initial, double cfl_max,
                   int ramp_steps);

}  // namespace cfd
