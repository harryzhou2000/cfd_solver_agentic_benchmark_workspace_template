#include "fluxes.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {

State rusanov_flux(const State& UL, const State& UR,
                   Real nx, Real ny, Real S,
                   Real gamma, Real R, Real diss_scale) {
    const Prims qL = prims_from_state(UL, gamma, R);
    const Prims qR = prims_from_state(UR, gamma, R);

    // Inviscid flux vectors
    auto inviscid = [&](const Prims& q) -> State {
        const Real un = q.u * nx + q.v * ny;
        const Real H = (q.e + q.p / q.rho) + 0.5 * (q.u * q.u + q.v * q.v);
        State F;
        F[0] = q.rho * un;
        F[1] = q.rho * q.u * un + q.p * nx;
        F[2] = q.rho * q.v * un + q.p * ny;
        F[3] = q.rho * H * un;
        return F;
    };

    const State FL = inviscid(qL) * S;
    const State FR = inviscid(qR) * S;

    const Real unL = qL.u * nx + qL.v * ny;
    const Real unR = qR.u * nx + qR.v * ny;
    const Real lambda = diss_scale * std::max(std::abs(unL) + qL.a, std::abs(unR) + qR.a);

    State flux = (FL + FR) * 0.5;
    State jump = (UR - UL) * (0.5 * S * lambda);
    flux -= jump;
    return flux;
}

State pressure_flux(Real p, Real nx, Real ny, Real S) {
    State F;
    F << 0.0, p * nx * S, p * ny * S, 0.0;
    return F;
}

State viscous_flux(const Prims& qL, const Prims& qR,
                   const Vec2& gu, const Vec2& gv, const Vec2& gT,
                   Real nx, Real ny, Real S,
                   Real mu, Real gamma, Real R, Real prandtl) {
    const Real ux = gu[0], uy = gu[1];
    const Real vx = gv[0], vy = gv[1];
    const Real Tx = gT[0], Ty = gT[1];
    const Real div = ux + vy;

    const Real tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
    const Real tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
    const Real tau_xy = mu * (uy + vx);

    const Real txx_n = tau_xx * nx + tau_xy * ny;
    const Real tyy_n = tau_xy * nx + tau_yy * ny;

    // Fourier heat flux: q = -k grad T, k = mu cp / Pr
    const Real k = mu * gamma * R / ((gamma - 1.0) * prandtl);
    const Real qn = (-k * Tx) * nx + (-k * Ty) * ny;

    const Real u_avg = 0.5 * (qL.u + qR.u);
    const Real v_avg = 0.5 * (qL.v + qR.v);

    State F;
    F[0] = 0.0;
    F[1] = txx_n * S;
    F[2] = tyy_n * S;
    // Energy: Fv = u·tau - q·n = u·tau + k·gradT·n (since q = -k·gradT)
    F[3] = (u_avg * txx_n + v_avg * tyy_n - qn) * S;
    return F;
}

} // namespace cfd
