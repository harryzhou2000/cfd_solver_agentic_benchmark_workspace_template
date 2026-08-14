#include "gradients.hpp"

void compute_gradients_lsq(const RankMesh& rm,
                           const std::vector<StateVector>& state,
                           std::vector<Vec2>& grad_rho,
                           std::vector<Grad2>& grad_u,
                           std::vector<Grad2>& grad_v,
                           std::vector<Grad2>& grad_T,
                           const GasModel& gas) {
    (void)rm; (void)state; (void)grad_rho;
    (void)grad_u; (void)grad_v; (void)grad_T; (void)gas;
    // TODO: implement in Phase 3
}
