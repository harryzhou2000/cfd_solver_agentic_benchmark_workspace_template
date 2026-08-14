#pragma once
#include "types.hpp"
#include <cmath>

namespace cfd {

struct GasModel {
    Real gamma = 1.4;
    Real R = 1.0;
    Real prandtl = 0.72;
    Real gamma_m1;
    Real cv, cp;

    GasModel(Real g = 1.4, Real r = 1.0, Real pr = 0.72)
        : gamma(g), R(r), prandtl(pr) {
        gamma_m1 = gamma - 1.0;
        cv = R / gamma_m1;
        cp = gamma * R / gamma_m1;
    }

    Real soundSpeed2(Real rho, Real p) const {
        return gamma * p / rho;
    }
    Real soundSpeed(Real rho, Real p) const {
        return std::sqrt(gamma * p / rho);
    }
    Real temperature(Real rho, Real p) const {
        return p / (rho * R);
    }
    Real pressure(const ConsState& U) const {
        Real rho = U[0];
        Real rhou = U[1], rhov = U[2], rhoE = U[3];
        Real u = rhou / rho, v = rhov / rho;
        return gamma_m1 * (rhoE - 0.5 * rho * (u*u + v*v));
    }
    Real totalEnthalpy(const ConsState& U) const {
        Real p = pressure(U);
        return (U[3] + p) / U[0];
    }

    PrimState consToPrim(const ConsState& U) const {
        PrimState W;
        W[0] = U[0];
        W[1] = U[1] / U[0];
        W[2] = U[2] / U[0];
        W[3] = gamma_m1 * (U[3] - 0.5*U[0]*(W[1]*W[1]+W[2]*W[2]));
        W[4] = W[3] / (W[0] * R);
        return W;
    }

    ConsState primToCons(const PrimState& W) const {
        ConsState U;
        U[0] = W[0];
        U[1] = W[0] * W[1];
        U[2] = W[0] * W[2];
        Real E = W[3] / gamma_m1 + 0.5*W[0]*(W[1]*W[1]+W[2]*W[2]);
        U[3] = E;
        return U;
    }

    void inviscidFluxNormal(const PrimState& W, Real nx, Real ny, ConsState& Fn) const {
        Real rho = W[0], u = W[1], v = W[2], p = W[3];
        Real un = u*nx + v*ny;
        Real rhoE = p / gamma_m1 + 0.5*rho*(u*u + v*v);
        Fn[0] = rho * un;
        Fn[1] = rho * un * u + p * nx;
        Fn[2] = rho * un * v + p * ny;
        Fn[3] = un * (rhoE + p);
    }
};

} // namespace cfd
