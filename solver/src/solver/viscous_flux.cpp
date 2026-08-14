#include "viscous_flux.hpp"

StateVector viscous_flux(const PrimVector& state, const Grad2& grad_u,
                         const Grad2& grad_v, const Grad2& grad_T,
                         const Vec2& normal, const GasModel& gas,
                         Real viscosity) {
    (void)state; (void)grad_u; (void)grad_v; (void)grad_T;
    (void)normal; (void)gas; (void)viscosity;
    return StateVector::Zero();
}
