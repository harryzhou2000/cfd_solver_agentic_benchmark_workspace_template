#include "solver/boundary.hpp"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace solver {

void set_freestream_state(const DistributedMesh& mesh,
                          std::vector<double>& state,
                          const CaseConfig& config) {
    const int n = static_cast<int>(mesh.owned_cells.size() +
                                   mesh.ghost_cells.size());
    state.resize(static_cast<size_t>(n) * kStateSize);

    const double rho_inf = config.freestream.rho;
    const double u_inf = config.freestream.u_inf;
    const double v_inf = config.freestream.v_inf;
    const double p_inf = config.freestream.pressure;
    const double gamma = config.gas.gamma;

    const double energy =
        p_inf / (gamma - 1.0) +
        0.5 * rho_inf * (u_inf * u_inf + v_inf * v_inf);

    for (int i = 0; i < n; ++i) {
        state[i * kStateSize + 0] = rho_inf;
        state[i * kStateSize + 1] = rho_inf * u_inf;
        state[i * kStateSize + 2] = rho_inf * v_inf;
        state[i * kStateSize + 3] = energy;
    }
}

Vec4 apply_boundary_condition(const Vec4& UL, const Vec2& normal,
                              const std::string& bc_type,
                              const CaseConfig& config) {
    const double gamma = config.gas.gamma;

    if (bc_type == "farfield") {
        // Freestream state
        const double rho_inf = config.freestream.rho;
        const double u_inf = config.freestream.u_inf;
        const double v_inf = config.freestream.v_inf;
        const double p_inf = config.freestream.pressure;

        // Interior state (reconstructed left state at the face)
        const Vec4 W_L = conservative_to_primitive(UL, gamma);
        const double a_L = std::sqrt(gamma * W_L(3) / W_L(0));
        const double a_inf = std::sqrt(gamma * p_inf / rho_inf);

        // Contravariant normal velocities (normal points outward)
        const double Vn_L = W_L(1) * normal.x() + W_L(2) * normal.y();
        const double Vn_inf = u_inf * normal.x() + v_inf * normal.y();

        // Riemann invariants along the boundary-normal direction
        const double R_plus = Vn_L + 2.0 * a_L / (gamma - 1.0);
        const double R_minus = Vn_inf - 2.0 * a_inf / (gamma - 1.0);

        // Boundary normal velocity and speed of sound
        const double Vn_b = 0.5 * (R_plus + R_minus);
        const double a_b = 0.25 * (gamma - 1.0) * (R_plus - R_minus);

        double rho_b = 0.0, u_b = 0.0, v_b = 0.0, p_b = 0.0;

        if (Vn_L <= -a_L) {
            // Supersonic inflow: everything from freestream
            rho_b = rho_inf;
            u_b = u_inf;
            v_b = v_inf;
            p_b = p_inf;
        } else if (Vn_L <= 0.0) {
            // Subsonic inflow: entropy and tangential velocity from
            // freestream, normal velocity and sound speed from invariants
            const double s_inf =
                p_inf / std::pow(std::max(rho_inf, 1e-10), gamma);
            const double Vt_inf_x = u_inf - Vn_inf * normal.x();
            const double Vt_inf_y = v_inf - Vn_inf * normal.y();
            u_b = Vn_b * normal.x() + Vt_inf_x;
            v_b = Vn_b * normal.y() + Vt_inf_y;
            rho_b = std::pow(a_b * a_b / (gamma * s_inf), 1.0 / (gamma - 1.0));
            p_b = a_b * a_b * rho_b / gamma;
        } else if (Vn_L >= a_L) {
            // Supersonic outflow: everything from interior
            rho_b = W_L(0);
            u_b = W_L(1);
            v_b = W_L(2);
            p_b = W_L(3);
        } else {
            // Subsonic outflow: entropy and tangential velocity from interior
            const double s_L =
                W_L(3) / std::pow(std::max(W_L(0), 1e-10), gamma);
            const double Vt_L_x = W_L(1) - Vn_L * normal.x();
            const double Vt_L_y = W_L(2) - Vn_L * normal.y();
            u_b = Vn_b * normal.x() + Vt_L_x;
            v_b = Vn_b * normal.y() + Vt_L_y;
            rho_b = std::pow(a_b * a_b / (gamma * s_L), 1.0 / (gamma - 1.0));
            p_b = a_b * a_b * rho_b / gamma;
        }

        rho_b = std::max(rho_b, 1e-10);
        p_b = std::max(p_b, 1e-10);
        return primitive_to_conservative(Vec4(rho_b, u_b, v_b, p_b), gamma);
    }

    if (bc_type == "slip_wall") {
        // Mirror the normal velocity; keep density and pressure from interior
        const Vec4 W_L = conservative_to_primitive(UL, gamma);
        const double Vn = W_L(1) * normal.x() + W_L(2) * normal.y();
        const double u_b = W_L(1) - 2.0 * Vn * normal.x();
        const double v_b = W_L(2) - 2.0 * Vn * normal.y();
        return primitive_to_conservative(Vec4(W_L(0), u_b, v_b, W_L(3)),
                                         gamma);
    }

    if (bc_type == "no_slip_adiabatic_wall") {
        // Mirror the velocity (u,v -> -u,-v) while keeping the interior
        // density and pressure (adiabatic: mirrored temperature). The ghost
        // state is [rho_L, -rhou_L, -rhov_L, rhoE_L], so the face normal
        // velocity is exactly zero (Vn_b = -Vn_L). The Rusanov flux then has
        // zero mass flux and zero energy flux through the wall, and the face
        // velocity itself is zero (no-slip), unlike the previous zero-velocity
        // ghost which left the wall permeable (non-zero Vn at the face).
        const Vec4 W_L = conservative_to_primitive(UL, gamma);
        const double rho_b = std::max(W_L(0), 1e-10);
        const double u_b = -W_L(1);  // mirror
        const double v_b = -W_L(2);  // mirror
        const double p_b = std::max(W_L(3), 1e-10);
        return primitive_to_conservative(Vec4(rho_b, u_b, v_b, p_b), gamma);
    }

    // Unknown BC type: extrapolate the interior state
    fmt::print(stderr,
               "apply_boundary_condition: warning: unknown bc_type \"{}\", "
               "using extrapolation\n",
               bc_type);
    return UL;
}

} // namespace solver
