/// @file boundary.cpp
/// Implementation of boundary condition routines.

#include "boundary.hpp"
#include "flux.hpp"
#include <cmath>
#include <algorithm>

namespace cfd {

Vec4 apply_boundary_condition(BoundaryType bc_type, const Vec4& U_interior,
                               const Vec2& face_normal,
                               const FreestreamConfig& freestream,
                               const GasConfig& gas, bool inviscid) {
    const Real gamma = gas.gamma;
    const Real gm1   = gamma - 1.0;

    // Interior primitive state
    const Vec4 prim_i = conservative_to_primitive(U_interior, gamma);
    const Real rho_i = prim_i(0);
    const Real u_i   = prim_i(1);
    const Real v_i   = prim_i(2);
    const Real p_i   = prim_i(3);
    const Real a_i   = speed_of_sound(rho_i, p_i, gamma);

    // Freestream state
    const Real rho_inf = freestream.rho;
    const Real u_inf   = freestream.velocity(0);
    const Real v_inf   = freestream.velocity(1);
    const Real p_inf   = freestream.pressure;
    const Real a_inf   = freestream.speed_of_sound;

    const Real nx = face_normal(0);
    const Real ny = face_normal(1);

    // Face-normal velocity (interior)
    const Real Vn_i = u_i * nx + v_i * ny;
    const Real Vn_inf = u_inf * nx + v_inf * ny;

    // Mach numbers normal to the face (positive = outward flow)
    const Real Mn_i = Vn_i / std::max(a_i, EPS);

    Vec4 prim_b;

    switch (bc_type) {
    case BoundaryType::Farfield: {
        // Characteristic-based farfield boundary condition.
        // Uses 1-D Riemann invariants along the face normal.
        // Classification is based on the LOCAL normal Mach number
        // Mn_i = Vn_i / a_i (normal points outward from the domain).

        if (Mn_i >= 1.0) {
            // Supersonic outflow: all characteristics leave the domain,
            // so extrapolate the interior state.
            prim_b << rho_i, u_i, v_i, p_i;
        } else if (Mn_i <= -1.0) {
            // Supersonic inflow: all characteristics enter from freestream.
            prim_b << rho_inf, u_inf, v_inf, p_inf;
        } else if (Mn_i > 0.0) {
            // Subsonic outflow: Vn > 0 means outward
            // Outgoing Riemann invariant from interior: R⁺ = Vn_i + 2*a_i/(γ−1)
            // Incoming from freestream:             R⁻ = Vn_inf − 2*a_inf/(γ−1)
            // Also entropy and tangential velocity from interior
            const Real Rplus  = Vn_i   + 2.0 * a_i   / gm1;  // R⁺ from interior
            const Real Rminus = Vn_inf - 2.0 * a_inf / gm1;  // R⁻ from freestream

            const Real Vn_b = 0.5 * (Rplus + Rminus);
            Real a_b       = 0.25 * gm1 * (Rplus - Rminus);
            a_b = std::max(a_b, EPS);

            // Tangential velocity from interior
            const Real Vt_i = -u_i * ny + v_i * nx;
            const Real u_b  = Vn_b * nx - Vt_i * ny;
            const Real v_b  = Vn_b * ny + Vt_i * nx;

            // Entropy from interior: s ∼ p/ρ^γ
            const Real s_i = p_i / std::pow(rho_i, gamma);
            // Compute density using characteristic relation at boundary
            // p_b / rho_b^γ = p_i / rho_i^γ, and a_b² = γ p_b / rho_b
            // so p_b = a_b² * rho_b / γ = s_i * rho_b^γ
            // → rho_b = (a_b² / (γ * s_i))^(1/(γ−1))
            const Real rho_b = std::pow(a_b * a_b / (gamma * std::max(s_i, EPS)), 1.0 / gm1);
            const Real p_b   = rho_b * a_b * a_b / gamma;

            prim_b << rho_b, u_b, v_b, p_b;
        } else {
            // Subsonic inflow: Vn < 0 means inward
            // Entropy and total enthalpy from freestream
            // Outgoing Riemann invariant from interior: R⁺ = Vn_i - 2*a_i/(γ−1)
            // Incoming from freestream:                R⁻ = Vn_inf + 2*a_inf/(γ−1)
            // Wait — for subsonic inflow, M_n < 0 (flow into domain).
            // The outgoing characteristic (going out of domain) is the negative
            // Riemann invariant R⁻ from interior.
            // The incoming characteristics are: s∞, h0∞, R⁺ from freestream.
            // Actually let's use the standard formulation:
            //
            // For any face, the normal points outward. If V_n > 0, flow is outward
            // (toward +normal direction). If V_n < 0, flow is inward.
            //
            // Outgoing chars (from interior toward boundary):
            //   If Mn > 0 (subsonic outflow):  R⁺, entropy, tangential velocity
            //   If Mn < 0 (subsonic inflow):   R⁻
            //
            // Incoming chars (from freestream toward interior):
            //   If Mn > 0 (subsonic outflow):  R⁻ from freestream
            //   If Mn < 0 (subsonic inflow):   R⁺, entropy, total enthalpy from freestream

            const Real Rminus_interior = Vn_i   + 2.0 * a_i   / gm1;  // outgoing from interior
            const Real Rplus_freestream = Vn_inf - 2.0 * a_inf / gm1; // incoming from freestream

            // Total enthalpy from freestream
            const Real h0_inf = a_inf * a_inf / gm1 + 0.5 * (u_inf * u_inf + v_inf * v_inf);
            // Entropy from freestream
            const Real s_inf = p_inf / std::pow(rho_inf, gamma);

            const Real Vn_b = 0.5 * (Rplus_freestream + Rminus_interior);
            Real a_b       = 0.25 * gm1 * (Rminus_interior - Rplus_freestream);
            a_b = std::max(a_b, EPS);

            // Tangential velocity from freestream
            const Real Vt_inf = -u_inf * ny + v_inf * nx;
            const Real u_b = Vn_b * nx - Vt_inf * ny;
            const Real v_b = Vn_b * ny + Vt_inf * nx;

            const Real rho_b = std::pow(a_b * a_b / (gamma * std::max(s_inf, EPS)), 1.0 / gm1);
            const Real p_b   = rho_b * a_b * a_b / gamma;

            prim_b << rho_b, u_b, v_b, p_b;
        }
        break;
    }

    case BoundaryType::SlipWall: {
        // Reflect normal velocity, preserve tangential velocity
        const Real Vt_i = -u_i * ny + v_i * nx;  // tangential component
        const Real Vn_b = -Vn_i;  // reflected normal

        const Real u_b = Vn_b * nx - Vt_i * ny;
        const Real v_b = Vn_b * ny + Vt_i * nx;

        prim_b << rho_i, u_b, v_b, p_i;
        break;
    }

    case BoundaryType::NoSlipAdiabaticWall: {
        // Zero velocity, adiabatic (∂T/∂n = 0 → ∂p/∂n = 0)
        const Real u_b = 0.0;
        const Real v_b = 0.0;
        // Zero normal pressure gradient: p_wall = p_interior
        prim_b << rho_i, u_b, v_b, p_i;
        break;
    }

    default:
        // Unknown BC: use interior state as-is (zero-gradient)
        prim_b << rho_i, u_i, v_i, p_i;
        break;
    }

    // Ensure positivity
    prim_b(0) = std::max(prim_b(0), EPS);
    prim_b(3) = std::max(prim_b(3), EPS);

    return primitive_to_conservative(prim_b, gamma);
}

} // namespace cfd
