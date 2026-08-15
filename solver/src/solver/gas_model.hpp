#pragma once

#include "types.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

// --- Perfect gas equation of state ---
struct GasModel {
    Real gamma{1.4};
    Real R{1.0};
    Real prandtl{0.72};
    Real gm1;      // gamma - 1
    Real cp;       // gamma * R / (gamma - 1)
    Real cv;       // R / (gamma - 1)

    GasModel() { update_derived(); }
    GasModel(Real g, Real r, Real pr) : gamma(g), R(r), prandtl(pr) {
        update_derived();
    }

    void update_derived() {
        gm1 = gamma - 1.0;
        cp = gamma * R / gm1;
        cv = R / gm1;
    }
};

// --- Primitive <-> Conservative state conversion ---

// Primitive: [rho, u, v, p]
// Conserved: [rho, rho*u, rho*v, rho*E]
// Energy: rho*E = p/gm1 + 0.5*rho*(u^2+v^2)

inline Primitive conserved_to_primitive(const Conserved& U, Real gm1) {
    Primitive W;
    W[0] = U[0];                          // rho
    Real inv_rho = 1.0 / U[0];
    W[1] = U[1] * inv_rho;                // u
    W[2] = U[2] * inv_rho;                // v
    Real ke = 0.5 * (W[1]*W[1] + W[2]*W[2]);
    Real e = U[3] * inv_rho - ke;         // internal energy
    W[3] = gm1 * U[0] * e;                // p = (gamma-1)*rho*e
    return W;
}

inline Conserved primitive_to_conserved(const Primitive& W, Real gm1) {
    Conserved U;
    U[0] = W[0];
    U[1] = W[0] * W[1];
    U[2] = W[0] * W[2];
    Real ke = 0.5 * (W[1]*W[1] + W[2]*W[2]);
    Real e = W[3] / (gm1 * W[0]);
    U[3] = W[0] * (e + ke);
    return U;
}

// --- Speed of sound ---
inline Real speed_of_sound(const Primitive& W, Real gamma) {
    return std::sqrt(gamma * W[3] / W[0]);
}

inline Real speed_of_sound_from_conserved(const Conserved& U, Real gamma, Real gm1) {
    auto W = conserved_to_primitive(U, gm1);
    return speed_of_sound(W, gamma);
}

// --- Temperature ---
inline Real temperature(const Primitive& W, const GasModel& gas) {
    return W[3] / (W[0] * gas.R);
}

// --- Mach number ---
inline Real mach_number(const Primitive& W, Real gamma) {
    Real u2 = W[1]*W[1] + W[2]*W[2];
    Real a = speed_of_sound(W, gamma);
    return (a > 1e-14) ? std::sqrt(u2) / a : 0.0;
}

// --- Viscosity ---
// Constant viscosity from Reynolds number
inline Real compute_viscosity(Real rho_inf, Real u_inf, Real L_ref, Real Re) {
    return rho_inf * u_inf * L_ref / Re;
}

// Sutherland's law (optional, for future use)
inline Real sutherland_viscosity(Real T, Real mu_ref, Real T_ref, Real S) {
    Real t_ratio = T / T_ref;
    return mu_ref * t_ratio * std::sqrt(t_ratio) * (T_ref + S) / (T + S);
}

// --- Positivity checks ---
inline bool is_physical(const Conserved& U, Real gm1, Real min_rho = 1e-12, Real min_p = 1e-12) {
    if (U[0] < min_rho) return false;
    auto W = conserved_to_primitive(U, gm1);
    if (W[3] < min_p) return false;
    return true;
}

inline void enforce_positivity(Conserved& U, Real gm1, Real min_rho = 1e-12, Real min_p = 1e-10) {
    U[0] = std::max(U[0], min_rho);
    auto W = conserved_to_primitive(U, gm1);
    if (W[3] < min_p) {
        W[3] = min_p;
        W[0] = std::max(W[0], min_rho);
        U = primitive_to_conserved(W, gm1);
    }
}

// --- Force coefficient computation ---
struct ForceCoeffs {
    Real cl{0}, cd{0}, cmz{0};                // total coefficients
    Real pressure_drag{0}, viscous_drag{0};   // decomposed drag
    Real pressure_lift{0}, viscous_lift{0};   // decomposed lift
};

// Compute dynamic pressure: q_inf = 0.5 * rho_inf * u_inf^2
inline Real dynamic_pressure(Real rho, Real u_mag) {
    return 0.5 * rho * u_mag * u_mag;
}

// Nondimensionalize force to coefficient
inline Real force_to_coeff(Real force, Real q_inf, Real ref_area) {
    return force / (q_inf * ref_area);
}

inline Real moment_to_coeff(Real moment, Real q_inf, Real ref_area, Real ref_length) {
    return moment / (q_inf * ref_area * ref_length);
}

} // namespace cfd
