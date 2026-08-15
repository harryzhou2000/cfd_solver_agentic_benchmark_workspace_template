// Boundary conditions (Phase 3a): ghost/exterior state construction for
// farfield, slip wall and no-slip adiabatic wall faces. The residual
// assembly feeds U_ext back into the same numerical fluxes used on
// interior faces, so all boundary fluxes are consistent with the interior
// discretization.

#include "boundary.hpp"

#include <cmath>

namespace cfd {

BCType bc_type_from_string(const std::string& s) {
    if (s == "farfield" || s == "far_field" || s == "far-field" ||
        s == "far" || s == "FAR" || s == "freestream") {
        return BCType::FarField;
    }
    if (s == "slip_wall" || s == "slipwall" || s == "wall_slip" ||
        s == "slip" || s == "SLIP" || s == "inviscid_wall") {
        return BCType::SlipWall;
    }
    if (s == "no_slip_adiabatic_wall" || s == "no_slip_wall" ||
        s == "noslip_adiabatic_wall" || s == "isothermal_wall" ||
        s == "wall" || s == "WALL" || s == "no_slip" || s == "noslip" ||
        s == "viscous_wall") {
        return BCType::NoSlipAdiabaticWall;
    }
    return BCType::Unknown;
}

BCFlux apply_boundary_condition(const Vector4& U_int, double nx, double ny,
                                BCType bc_type,
                                const FreestreamParams& freestream,
                                const GasParams& gas, double viscosity) {
    const double len = std::hypot(nx, ny);
    const double inv_len = (len > 0.0) ? 1.0 / len : 0.0;
    const double n_x = nx * inv_len;  // unit outward normal
    const double n_y = ny * inv_len;

    BCFlux result;
    const PrimitiveState prim_int = conservative_to_primitive(U_int, gas);

    switch (bc_type) {
        case BCType::FarField: {
            // Characteristic-based (1-D Riemann) far field: the outgoing
            // acoustic invariant and the tangential velocity / entropy are
            // taken from the interior state on outflow and from the
            // freestream on inflow; the incoming acoustic invariant always
            // comes from the freestream. A pure freestream/extrapolation
            // ghost reflects the acoustic modes and leaves a slowly growing
            // smooth mode that prevents the steady residual from converging.
            const double gamma = gas.gamma;
            const double gm1 = gamma - 1.0;
            const double a_int = std::sqrt(gamma * prim_int.p / prim_int.rho);
            const double a_fs = std::sqrt(gamma * freestream.pressure /
                                          freestream.density);
            const double alpha_rad = freestream.alpha * M_PI / 180.0;
            const double vx_fs = freestream.velocity * std::cos(alpha_rad);
            const double vy_fs = freestream.velocity * std::sin(alpha_rad);
            const double vn_fs = vx_fs * n_x + vy_fs * n_y;
            const double vn_int = prim_int.u * n_x + prim_int.v * n_y;
            const bool inflow = vn_int < 0.0;
            // Outgoing (R+) / incoming (R-) acoustic Riemann invariants.
            const double R_plus = vn_int + 2.0 * a_int / gm1;
            const double R_minus = vn_fs - 2.0 * a_fs / gm1;
            const double vn_ghost = 0.5 * (R_plus + R_minus);
            const double a_ghost = 0.25 * gm1 * (R_plus - R_minus);
            const double s_int = prim_int.p / std::pow(prim_int.rho, gamma);
            const double s_fs = freestream.pressure /
                                std::pow(freestream.density, gamma);
            const double s = inflow ? s_fs : s_int;
            const double vt_int = -prim_int.u * n_y + prim_int.v * n_x;
            const double vt_fs = -vx_fs * n_y + vy_fs * n_x;
            const double vt = inflow ? vt_fs : vt_int;
            PrimitiveState prim_ext = prim_int;
            if (a_ghost > 1e-10 && s > 1e-30) {
                prim_ext.rho = std::pow(a_ghost * a_ghost / (gamma * s),
                                        1.0 / gm1);
                prim_ext.p = prim_ext.rho * a_ghost * a_ghost / gamma;
            }
            prim_ext.u = vn_ghost * n_x - vt * n_y;
            prim_ext.v = vn_ghost * n_y + vt * n_x;
            result.U_ext = primitive_to_conservative(prim_ext, gas);
            break;
        }
        case BCType::SlipWall: {
            // Mirror the velocity about the wall plane: zero normal velocity,
            // free tangential velocity; density and pressure extrapolated.
            const double vn = prim_int.u * n_x + prim_int.v * n_y;
            PrimitiveState prim_ext = prim_int;
            prim_ext.u = prim_int.u - 2.0 * vn * n_x;
            prim_ext.v = prim_int.v - 2.0 * vn * n_y;
            result.U_ext = primitive_to_conservative(prim_ext, gas);
            break;
        }
        case BCType::NoSlipAdiabaticWall: {
            // Zero wall velocity, adiabatic wall (T_wall = T_int), pressure
            // extrapolated with zero normal gradient: rho_wall = p_wall/(R T_wall).
            const double T_int = prim_int.p / (prim_int.rho * gas.R);
            const double p_wall = prim_int.p;
            const double rho_wall = p_wall / (gas.R * T_int);
            PrimitiveState prim_ext{rho_wall, 0.0, 0.0, p_wall};
            result.U_ext = primitive_to_conservative(prim_ext, gas);
            break;
        }
        case BCType::Unknown:
        default: {
            // Conservative fallback: extrapolate the interior state.
            result.U_ext = U_int;
            break;
        }
    }

    (void)viscosity;  // reserved for wall heat-flux variants in later phases.
    return result;
}

Vector4 BoundaryCondition::apply(const Vector4& interior,
                                 const Vector2& face_normal,
                                 bool face_on_boundary) const {
    (void)face_on_boundary;
    return apply_boundary_condition(interior, face_normal.x, face_normal.y,
                                    type_, freestream_, gas_)
        .U_ext;
}

std::vector<BoundaryCondition> create_boundary_conditions(
    const FreestreamParams& freestream, const GasParams& gas) {
    // One representative BC object per supported type (Phase 3a: the
    // per-face BC type is resolved from the mesh by the residual assembly,
    // which calls apply_boundary_condition directly).
    return {BoundaryCondition(BCType::FarField, freestream, gas),
            BoundaryCondition(BCType::SlipWall, freestream, gas),
            BoundaryCondition(BCType::NoSlipAdiabaticWall, freestream, gas)};
}

}  // namespace cfd
