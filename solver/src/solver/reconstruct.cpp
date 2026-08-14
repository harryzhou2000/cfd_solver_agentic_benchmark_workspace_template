#include "reconstruct.hpp"

void reconstruct_faces(const std::vector<StateVector>& cell_state,
                       const std::vector<Vec2>& grad_rho,
                       const std::vector<Grad2>& grad_u,
                       const std::vector<Grad2>& grad_v,
                       const std::vector<Grad2>& grad_T,
                       const GasModel& gas,
                       std::vector<PrimVector>& left_prim,
                       std::vector<PrimVector>& right_prim) {
    (void)cell_state; (void)grad_rho; (void)grad_u;
    (void)grad_v; (void)grad_T; (void)gas;
    (void)left_prim; (void)right_prim;
}
