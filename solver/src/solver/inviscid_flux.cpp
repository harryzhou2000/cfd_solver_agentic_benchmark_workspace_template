#include "inviscid_flux.hpp"
#include <cmath>
#include <algorithm>

// Rusanov / Local Lax-Friedrichs flux
// F = 0.5*(F_L + F_R) - 0.5*lambda_max*(U_R - U_L)
// where lambda_max = |V_n| + a
static StateVector inviscid_flux_vector(const PrimVector& W, const Vec2& normal, const GasModel& gas) {
    Real rho = W[0], u = W[1], v = W[2], p = W[3];
    Real E = p / ((gas.gamma - 1.0) * rho) + 0.5 * (u*u + v*v);
    Real Vn = u * normal.x() + v * normal.y();
    Real rho_Vn = rho * Vn;
    
    StateVector F;
    F(0) = rho_Vn;
    F(1) = rho_Vn * u + p * normal.x();
    F(2) = rho_Vn * v + p * normal.y();
    F(3) = rho_Vn * (E + p/rho);
    return F;
}

StateVector rusanov_flux(const PrimVector& left, const PrimVector& right,
                         const Vec2& normal, const GasModel& gas,
                         Real dissipation_scale) {
    // Convert to conservative for jump term
    StateVector UL = gas.primitive_to_conservative(left);
    StateVector UR = gas.primitive_to_conservative(right);
    
    StateVector FL = inviscid_flux_vector(left, normal, gas);
    StateVector FR = inviscid_flux_vector(right, normal, gas);
    
    // Compute max wave speed
    Real aL = std::sqrt(gas.gamma * left[3] / std::max(left[0], 1e-14));
    Real aR = std::sqrt(gas.gamma * right[3] / std::max(right[0], 1e-14));
    Real VnL = left[1] * normal.x() + left[2] * normal.y();
    Real VnR = right[1] * normal.x() + right[2] * normal.y();
    Real lambda = std::max(std::abs(VnL) + aL, std::abs(VnR) + aR);
    
    Real scale = dissipation_scale * lambda;
    return 0.5 * (FL + FR) - 0.5 * scale * (UR - UL);
}

StateVector roe_flux(const PrimVector& left, const PrimVector& right,
                     const Vec2& normal, const GasModel& gas) {
    // Fallback to Rusanov for now
    return rusanov_flux(left, right, normal, gas, 1.0);
}
