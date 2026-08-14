#include "limiter.hpp"
#include "common/gas_model.hpp"

void apply_limiter(std::vector<Vec2>& grad_rho,
                   std::vector<Grad2>& grad_u,
                   std::vector<Grad2>& grad_v,
                   std::vector<Grad2>& grad_T,
                   const std::vector<StateVector>& state,
                   const GasModel& gas) {
    (void)grad_rho; (void)grad_u; (void)grad_v; (void)grad_T;
    (void)state; (void)gas;
}
