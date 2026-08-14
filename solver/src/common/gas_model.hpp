#pragma once

#include "types.hpp"
#include <cmath>

// Calorically perfect gas equation of state
struct GasModel {
    Real gamma   = 1.4;
    Real R       = 1.0;
    Real Pr      = 0.72;

    // Derived: cp = gamma * R / (gamma-1),  cv = R / (gamma-1)
    Real cp() const { return gamma * R / (gamma - 1.0); }
    Real cv() const { return R / (gamma - 1.0); }

    // Pressure from conserved state
    Real pressure(const StateVector& U) const {
        Real rho  = U[0];
        Real rhoe = U[3];
        Real ke   = 0.5 * (U[1]*U[1] + U[2]*U[2]) / (rho + 1e-20);
        Real e    = (rhoe - ke) / (rho + 1e-20);
        return (gamma - 1.0) * rho * e;
    }

    // Temperature from conserved state: T = e / cv
    Real temperature(const StateVector& U) const {
        Real rho  = U[0];
        Real rhoe = U[3];
        Real ke   = 0.5 * (U[1]*U[1] + U[2]*U[2]) / (rho + 1e-20);
        Real e    = (rhoe - ke) / (rho + 1e-20);
        return e / cv();
    }

    // Speed of sound: a = sqrt(gamma * p / rho)
    Real sound_speed(const StateVector& U) const {
        Real p   = pressure(U);
        Real rho = U[0];
        return std::sqrt(gamma * std::max(p, 1e-14) / std::max(rho, 1e-14));
    }

    // Mach from U and velocity
    Real mach_number(const StateVector& U) const {
        Real rho = U[0];
        Real u   = U[1] / rho;
        Real v   = U[2] / rho;
        Real vel = std::sqrt(u*u + v*v);
        return vel / sound_speed(U);
    }

    // Conservative -> primitive
    PrimVector conservative_to_primitive(const StateVector& U) const {
        Real rho  = U[0];
        Real u    = U[1] / rho;
        Real v    = U[2] / rho;
        Real p    = pressure(U);
        PrimVector W;
        W << rho, u, v, p;
        return W;
    }

    // Primitive -> conservative
    StateVector primitive_to_conservative(const PrimVector& W) const {
        Real rho = W[0];
        Real u   = W[1];
        Real v   = W[2];
        Real p   = W[3];
        Real e   = p / ((gamma - 1.0) * rho);
        Real E   = e + 0.5 * (u*u + v*v);
        StateVector U;
        U << rho, rho*u, rho*v, rho*E;
        return U;
    }

    // Conservative -> velocity
    Vec2 velocity(const StateVector& U) const {
        Real rho = U[0];
        return Vec2(U[1]/rho, U[2]/rho);
    }

    // Sutherland's law viscosity (nondimensional) — optional
    static Real sutherland_viscosity(Real T, Real T_ref, Real mu_ref, Real S) {
        Real t_ratio = T / T_ref;
        return mu_ref * t_ratio * std::sqrt(t_ratio) * (T_ref + S) / (T + S);
    }

    // Constant viscosity from Reynolds number
    Real constant_viscosity(Real Re) const {
        return 1.0 / Re;  // rho_inf * U_inf * L_ref assumed 1.0 in nondim
    }

    // Thermal conductivity: k = mu * cp / Pr
    Real conductivity(Real mu) const {
        return mu * cp() / Pr;
    }
};
