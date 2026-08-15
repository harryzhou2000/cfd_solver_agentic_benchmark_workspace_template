#pragma once

#include <cmath>

#include "types.hpp"

namespace cfd {

// Calorically perfect gas relations for a conservative state U = (rho,
// rho*u, rho*v, E). All functions are inline header-only.

inline double density(const Vector4& U) { return U.r; }

inline double velocity_x(const Vector4& U) { return U.r > 0.0 ? U.u / U.r : 0.0; }

inline double velocity_y(const Vector4& U) { return U.r > 0.0 ? U.v / U.r : 0.0; }

inline double speed_squared(const Vector4& U) {
    return (U.u * U.u + U.v * U.v) / (U.r * U.r);
}

inline double pressure(const Vector4& U, const GasParams& gas) {
    // p = (gamma - 1) * (E - 0.5 * rho * |v|^2)
    return (gas.gamma - 1.0) * (U.e - 0.5 * (U.u * U.u + U.v * U.v) / U.r);
}

inline double temperature(const Vector4& U, const GasParams& gas) {
    return pressure(U, gas) / (gas.R * U.r);
}

inline double speed_of_sound(const Vector4& U, const GasParams& gas) {
    return std::sqrt(gas.gamma * pressure(U, gas) / U.r);
}

inline double mach_number(const Vector4& U, const GasParams& gas) {
    const double a = speed_of_sound(U, gas);
    return a > 0.0 ? std::sqrt(speed_squared(U)) / a : 0.0;
}

inline double total_energy_from_primitive(double rho, double u, double v,
                                          double p, const GasParams& gas) {
    // E = p / (gamma - 1) + 0.5 * rho * (u^2 + v^2)
    return p / (gas.gamma - 1.0) + 0.5 * rho * (u * u + v * v);
}

inline double kinetic_energy(const Vector4& U) {
    return 0.5 * (U.u * U.u + U.v * U.v) / U.r;
}

// Converts a conservative state to primitive variables (rho, ux, uy, p).
inline PrimitiveState conservative_to_primitive(const Vector4& U,
                                                const GasParams& gas) {
    PrimitiveState P;
    P.rho = U.r;
    P.u = velocity_x(U);
    P.v = velocity_y(U);
    P.p = pressure(U, gas);
    return P;
}

// Converts primitive variables (rho, ux, uy, p) back to the conservative
// state U = [rho, rho*u, rho*v, rho*E]. Inverse of conservative_to_primitive.
inline Vector4 primitive_to_conservative(const PrimitiveState& prim,
                                         const GasParams& gas) {
    const double ke = 0.5 * (prim.u * prim.u + prim.v * prim.v);
    const double e_int = prim.p / ((gas.gamma - 1.0) * prim.rho);
    return {prim.rho, prim.rho * prim.u, prim.rho * prim.v,
            prim.rho * (e_int + ke)};
}

// Freestream conservative state from the freestream parameters (velocity
// magnitude applied at the angle of attack).
inline Vector4 freestream_to_conservative(const FreestreamParams& fs,
                                          const GasParams& gas) {
    const double alpha = fs.alpha * 3.14159265358979323846 / 180.0;
    PrimitiveState prim;
    prim.rho = fs.density;
    prim.u = fs.velocity * std::cos(alpha);
    prim.v = fs.velocity * std::sin(alpha);
    prim.p = fs.pressure;
    return primitive_to_conservative(prim, gas);
}

// Laminar reference viscosity for constant-viscosity flows:
//   mu = rho_inf * U_inf * L_ref / Re
// Returns 0 for inviscid runs (Re == 0).
inline double laminar_viscosity(const FreestreamParams& fs,
                                const ReferenceParams& ref,
                                double reynolds) {
    if (reynolds <= 0.0) return 0.0;
    return fs.density * fs.velocity * ref.reynolds_length / reynolds;
}

}  // namespace cfd
