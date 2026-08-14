#pragma once
#include "types.hpp"
#include "gas.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

// Roe flux with Harten-Hyman entropy fix
// WL, WR: primitive states (rho, u, v, p, T)
// nx, ny: unit normal (from left to right)
// Returns: numerical flux F_n (4 components)
inline ConsState roeFlux(const GasModel& gas, const PrimState& WL, const PrimState& WR,
                         Real nx, Real ny, Real entropyScale = 0.1) {
    Real g = gas.gamma, gm1 = gas.gamma_m1;

    Real rhoL = WL[0], uL = WL[1], vL = WL[2], pL = WL[3];
    Real rhoR = WR[0], uR = WR[1], vR = WR[2], pR = WR[3];

    // Enforce positivity
    rhoL = std::max(rhoL, 1e-12);
    rhoR = std::max(rhoR, 1e-12);
    pL = std::max(pL, 1e-12);
    pR = std::max(pR, 1e-12);

    Real aL = gas.soundSpeed(rhoL, pL);
    Real aR = gas.soundSpeed(rhoR, pR);
    Real HL = aL*aL / gm1 + 0.5*(uL*uL + vL*vL);
    Real HR = aR*aR / gm1 + 0.5*(uR*uR + vR*vR);

    // Roe averages
    Real sqL = std::sqrt(rhoL), sqR = std::sqrt(rhoR);
    Real sqSum = sqL + sqR;
    Real rho_t = sqL * sqR;
    Real u_t = (sqL*uL + sqR*uR) / sqSum;
    Real v_t = (sqL*vL + sqR*vR) / sqSum;
    Real H_t = (sqL*HL + sqR*HR) / sqSum;
    Real a_t2 = gm1 * (H_t - 0.5*(u_t*u_t + v_t*v_t));
    a_t2 = std::max(a_t2, 1e-14);
    Real a_t = std::sqrt(a_t2);

    Real un_t = u_t*nx + v_t*ny;
    Real tx = -ny, ty = nx; // tangent
    Real ut_t = u_t*tx + v_t*ty;

    // Physical fluxes (normal direction)
    ConsState FL, FR;
    gas.inviscidFluxNormal(WL, nx, ny, FL);
    gas.inviscidFluxNormal(WR, nx, ny, FR);

    // Jump
    Real drho = rhoR - rhoL;
    Real du = uR - uL, dv = vR - vL;
    Real dp = pR - pL;
    Real dun = du*nx + dv*ny;
    Real dut = du*tx + dv*ty;

    // Wave strengths
    Real w1 = (dp - rho_t*a_t*dun) / (2.0*a_t2);
    Real w2 = drho - dp/a_t2;
    Real w3 = rho_t * dut;
    Real w4 = (dp + rho_t*a_t*dun) / (2.0*a_t2);

    // Entropy fix (Harten-Hyman)
    Real delta = entropyScale * a_t;
    auto absFix = [delta](Real x) -> Real {
        Real ax = std::abs(x);
        if (ax < delta) return (x*x + delta*delta) / (2.0*delta);
        return ax;
    };

    Real l1 = absFix(un_t - a_t);
    Real l2 = absFix(un_t);
    Real l3 = absFix(un_t);
    Real l4 = absFix(un_t + a_t);

    // Dissipation: D = sum |lambda_k| * w_k * r_k
    // r1 = [1, u-a*nx, v-a*ny, H-a*un]
    // r2 = [1, u, v, q2/2]
    // r3 = [0, tx, ty, ut]
    // r4 = [1, u+a*nx, v+a*ny, H+a*un]
    Real q2_t = u_t*u_t + v_t*v_t;

    ConsState D = {};
    // Wave 1
    Real f1 = l1 * w1;
    D[0] += f1 * 1.0;
    D[1] += f1 * (u_t - a_t*nx);
    D[2] += f1 * (v_t - a_t*ny);
    D[3] += f1 * (H_t - a_t*un_t);
    // Wave 2
    Real f2 = l2 * w2;
    D[0] += f2 * 1.0;
    D[1] += f2 * u_t;
    D[2] += f2 * v_t;
    D[3] += f2 * 0.5*q2_t;
    // Wave 3 (shear)
    Real f3 = l3 * w3;
    D[0] += 0.0;
    D[1] += f3 * tx;
    D[2] += f3 * ty;
    D[3] += f3 * ut_t;
    // Wave 4
    Real f4 = l4 * w4;
    D[0] += f4 * 1.0;
    D[1] += f4 * (u_t + a_t*nx);
    D[2] += f4 * (v_t + a_t*ny);
    D[3] += f4 * (H_t + a_t*un_t);

    ConsState Fn;
    for (int k = 0; k < NEQ; k++)
        Fn[k] = 0.5*(FL[k] + FR[k]) - 0.5*D[k];
    return Fn;
}

// Rusanov (local Lax-Friedrichs) flux
inline ConsState rusanovFlux(const GasModel& gas, const PrimState& WL, const PrimState& WR,
                             Real nx, Real ny, Real dissScale = 1.0) {
    Real rhoL = std::max(WL[0], 1e-12);
    Real rhoR = std::max(WR[0], 1e-12);
    Real pL = std::max(WL[3], 1e-12);
    Real pR = std::max(WR[3], 1e-12);

    Real aL = gas.soundSpeed(rhoL, pL);
    Real aR = gas.soundSpeed(rhoR, pR);
    Real unL = WL[1]*nx + WL[2]*ny;
    Real unR = WR[1]*nx + WR[2]*ny;
    Real sMax = std::max(std::abs(unL) + aL, std::abs(unR) + aR) * dissScale;

    ConsState FL, FR;
    gas.inviscidFluxNormal(WL, nx, ny, FL);
    gas.inviscidFluxNormal(WR, nx, ny, FR);

    ConsState UL = gas.primToCons(WL);
    ConsState UR = gas.primToCons(WR);

    ConsState Fn;
    for (int k = 0; k < NEQ; k++)
        Fn[k] = 0.5*(FL[k] + FR[k]) - 0.5*sMax*(UR[k] - UL[k]);
    return Fn;
}

// Viscous flux contribution through a face
// Uses averaged gradients at the face
// dudx, dudy, dvdx, dvdy, dTdx, dTdy: averaged gradients
// nx, ny: unit normal
// mu: dynamic viscosity, k: thermal conductivity
inline ConsState viscousFluxNormal(const GasModel& gas,
                                    Real u, Real v,
                                    Real dudx, Real dudy, Real dvdx, Real dvdy,
                                    Real dTdx, Real dTdy,
                                    Real nx, Real ny, Real mu, Real k) {
    Real div = dudx + dvdy;
    Real tauxx = mu * (2.0*dudx - 2.0/3.0*div);
    Real tauyy = mu * (2.0*dvdy - 2.0/3.0*div);
    Real tauxy = mu * (dudy + dvdx);
    Real qx = -k * dTdx;
    Real qy = -k * dTdy;

    // Viscous flux in x and y
    // Fv = [0, tauxx, tauxy, tauxx*u + tauxy*v - qx]
    // Gv = [0, tauxy, tauyy, tauxy*u + tauyy*v - qy]
    // Normal: Fv*nx + Gv*ny
    ConsState Fv;
    Fv[0] = 0.0;
    Fv[1] = tauxx*nx + tauxy*ny;
    Fv[2] = tauxy*nx + tauyy*ny;
    Fv[3] = (tauxx*u + tauxy*v)*nx + (tauxy*u + tauyy*v)*ny - qx*nx - qy*ny;
    return Fv;
}

} // namespace cfd
