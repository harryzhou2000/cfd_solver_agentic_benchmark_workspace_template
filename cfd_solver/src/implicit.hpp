#pragma once
#include "solver.hpp"

namespace cfd {

// Compute residual with reconstruction, limiter, and flux evaluation
void compute_residual_full(Solver& solver, const std::vector<StateVec>& U,
                           std::vector<StateVec>& R,
                           real_t& res_l2, real_t& res_linf,
                           std::vector<Vec2>& grad_rho,
                           std::vector<Vec2>& grad_rhou,
                           std::vector<Vec2>& grad_rhov,
                           std::vector<Vec2>& grad_rhoE);

// LU-SGS implicit solver step
void lu_sgs_step(Solver& solver, std::vector<StateVec>& U,
                 const std::vector<StateVec>& R,
                 const std::vector<real_t>& dt_local,
                 real_t omega = 1.0);

// Compute local time steps
void compute_local_dt(Solver& solver, const std::vector<StateVec>& U,
                      std::vector<real_t>& dt_local, real_t cfl);

// Steady implicit driver
struct SteadyStats {
    idx_t steps_run;
    real_t final_res_l2;
    real_t final_res_linf;
    real_t residual_reduction;
    std::string convergence_status;
    std::string notes;
};

SteadyStats run_steady(Solver& solver, const std::string& output_dir);

} // namespace cfd
