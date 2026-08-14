#include "implicit_solver.hpp"

void lu_sgs_sweep(RankMesh& rm, const std::vector<Real>& dt,
                  const GasModel& gas, const CaseConfig& cfg,
                  int inner_iterations) {
    (void)rm; (void)dt; (void)gas; (void)cfg; (void)inner_iterations;
}

void jacobi_sweep(RankMesh& rm, const std::vector<Real>& dt,
                  const GasModel& gas, int iterations) {
    (void)rm; (void)dt; (void)gas; (void)iterations;
}
