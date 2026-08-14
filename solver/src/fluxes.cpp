#include "fluxes.hpp"
#include <algorithm>
#include <cmath>

namespace cfd {

StateVec rusanov_flux(const StateVec& UL, const StateVec& UR,
                      real_t nx, real_t ny, real_t S,
                      real_t gamma, real_t R, real_t diss_scale) {
    auto qL = prims_from_conservative(UL, gamma, R);
    auto qR = prims_from_conservative(UR, gamma, R);

    StateVec FL = inviscid_flux(qL, nx, ny);
    StateVec FR = inviscid_flux(qR, nx, ny);

    // Wave speed: max(|un|+a) across both sides.
    real_t unL = qL.u * nx + qL.v * ny;
    real_t unR = qR.u * nx + qR.v * ny;
    real_t lambda = std::max(std::abs(unL) + qL.a, std::abs(unR) + qR.a);
    lambda *= diss_scale;

    StateVec flux = (FL + FR) * 0.5 * S;
    StateVec jump = (UR - UL) * (0.5 * S * lambda);
    flux -= jump; // upwind dissipation
    return flux;
}

StateVec pressure_only_flux(real_t p, real_t nx, real_t ny, real_t S) {
    StateVec F;
    F << 0.0, p * nx * S, p * ny * S, 0.0;
    return F;
}

StateVec viscous_flux_face(const Prims& qL, const Prims& qR,
                           const Vec2& gu, const Vec2& gv, const Vec2& gT,
                           real_t nx, real_t ny, real_t S,
                           real_t mu, real_t gamma, real_t R, real_t prandtl) {
    // Face-averaged gradients
    real_t ux = gu[0], uy = gu[1];
    real_t vx = gv[0], vy = gv[1];
    real_t Tx = gT[0], Ty = gT[1];

    // Divergence
    real_t div = ux + vy;
    real_t tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
    real_t tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
    real_t tau_xy = mu * (uy + vx);

    // Normal traction components
    real_t txx_n = tau_xx * nx + tau_xy * ny;
    real_t tyy_n = tau_xy * nx + tau_yy * ny;

    // Heat flux
    real_t k = mu * gamma * R / ((gamma - 1.0) * prandtl);
    real_t qx = -k * Tx;
    real_t qy = -k * Ty;
    real_t qn = qx * nx + qy * ny;

    // Face-averaged velocity
    real_t u_avg = 0.5 * (qL.u + qR.u);
    real_t v_avg = 0.5 * (qL.v + qR.v);

    StateVec F;
    F[0] = 0.0;
    F[1] = txx_n * S;
    F[2] = tyy_n * S;
    // Energy: Fv = u*tau.n - q.n = u*tau.n + k gradT.n in the conservative
    // form dU/dt + div(Fc - Fv) = 0.  q = -k gradT is the physical heat-flux
    // vector, so the diffusive flux carries MINUS q.n (heat flows down the
    // temperature gradient; adding q.n would make conduction anti-diffusive
    // and drive a temperature runaway in the boundary layer).
    F[3] = (u_avg * txx_n + v_avg * tyy_n - qn) * S;
    return F;
}

} // namespace cfd
