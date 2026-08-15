// Inviscid + viscous flux functions (Phase 3a: physics engine).
//
// Sign convention: every function returns the flux to ADD to the cell
// residual through a face with geometric normal (nx, ny), where
// |(nx, ny)| = face area. The viscous flux carries a minus sign on the
// stress terms because the conservation form is
//   dU/dt + div(F_inv - F_visc) = 0.

#include "fluxes.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {

Vector4 inviscid_flux(const Vector4& U, const Vector2& n,
                      const GasParams& gas) {
    const double rho = U.r;
    const double ux = U.u / rho;
    const double uy = U.v / rho;
    const double p = (gas.gamma - 1.0) *
                     (U.e - 0.5 * (U.u * U.u + U.v * U.v) / rho);
    const double vn = ux * n.x + uy * n.y;
    Vector4 F;
    F.r = rho * vn;
    F.u = rho * vn * ux + p * n.x;
    F.v = rho * vn * uy + p * n.y;
    F.e = (U.e + p) * vn;
    return F;
}

Vector4 inviscid_flux_rusanov(const Vector4& UL, const Vector4& UR,
                              double nx, double ny, const GasParams& gas,
                              double dissipation_scale) {
    const double rhoL = UL.r, rhoR = UR.r;
    const double uL = UL.u / rhoL, vL = UL.v / rhoL;
    const double uR = UR.u / rhoR, vR = UR.v / rhoR;
    const double pL = (gas.gamma - 1.0) *
                      (UL.e - 0.5 * (UL.u * UL.u + UL.v * UL.v) / rhoL);
    const double pR = (gas.gamma - 1.0) *
                      (UR.e - 0.5 * (UR.u * UR.u + UR.v * UR.v) / rhoR);
    const double aL = std::sqrt(gas.gamma * pL / rhoL);
    const double aR = std::sqrt(gas.gamma * pR / rhoR);

    // Fluxes dotted with the (area-scaled) face normal.
    const Vector2 n{nx, ny};
    const Vector4 FL = inviscid_flux(UL, n, gas);
    const Vector4 FR = inviscid_flux(UR, n, gas);

    // Maximum wave speed normal to the face (unit normal), scaled by the
    // dissipation factor.
    const double vnL = uL * nx + vL * ny;
    const double vnR = uR * nx + vR * ny;
    const double area = std::hypot(nx, ny);
    const double inv_area = (area > 0.0) ? 1.0 / area : 0.0;
    const double lambda =
        std::max(std::abs(vnL * inv_area) + aL, std::abs(vnR * inv_area) + aR) *
        dissipation_scale;

    // F(U_L) and F(U_R) already carry the face area (via n); the
    // dissipation term is per-unit-area wave speed times the state jump, so
    // it needs the area as well.
    const double diss = 0.5 * lambda * area;
    Vector4 F;
    F.r = 0.5 * (FL.r + FR.r) - diss * (UR.r - UL.r);
    F.u = 0.5 * (FL.u + FR.u) - diss * (UR.u - UL.u);
    F.v = 0.5 * (FL.v + FR.v) - diss * (UR.v - UL.v);
    F.e = 0.5 * (FL.e + FR.e) - diss * (UR.e - UL.e);
    return F;
}

Vector4 inviscid_flux_roe(const Vector4& UL, const Vector4& UR,
                          double nx, double ny, const GasParams& gas) {
    // Defensive fallback: Rusanov for non-physical states.
    if (UL.r <= 0.0 || UR.r <= 0.0) {
        return inviscid_flux_rusanov(UL, UR, nx, ny, gas);
    }

    const double rhoL = UL.r, rhoR = UR.r;
    const double uL = UL.u / rhoL, vL = UL.v / rhoL;
    const double uR = UR.u / rhoR, vR = UR.v / rhoR;
    const double pL = (gas.gamma - 1.0) *
                      (UL.e - 0.5 * (UL.u * UL.u + UL.v * UL.v) / rhoL);
    const double pR = (gas.gamma - 1.0) *
                      (UR.e - 0.5 * (UR.u * UR.u + UR.v * UR.v) / rhoR);

    // --- Roe-averaged state ---
    const double sqL = std::sqrt(rhoL);
    const double sqR = std::sqrt(rhoR);
    const double denom = sqL + sqR;
    const double rho_avg = sqL * sqR;
    const double u_avg = (sqL * uL + sqR * uR) / denom;
    const double v_avg = (sqL * vL + sqR * vR) / denom;
    const double HL = (UL.e + pL) / rhoL;  // specific enthalpy
    const double HR = (UR.e + pR) / rhoR;
    const double H_avg = (sqL * HL + sqR * HR) / denom;
    const double q2 = u_avg * u_avg + v_avg * v_avg;
    const double a2 = (gas.gamma - 1.0) * (H_avg - 0.5 * q2);
    if (a2 <= 0.0) {
        return inviscid_flux_rusanov(UL, UR, nx, ny, gas);
    }
    const double a = std::sqrt(a2);

    const double area = std::hypot(nx, ny);
    const double inv_area = (area > 0.0) ? 1.0 / area : 0.0;
    const double n_x = nx * inv_area;  // unit normal
    const double n_y = ny * inv_area;
    const double vn_avg = u_avg * n_x + v_avg * n_y;

    // --- Jump in conserved variables and projected jumps ---
    const double dU_r = UR.r - UL.r;
    const double dU_u = UR.u - UL.u;
    const double dU_v = UR.v - UL.v;
    const double dU_e = UR.e - UL.e;
    const double dp = (gas.gamma - 1.0) *
                      (dU_e - u_avg * dU_u - v_avg * dU_v + 0.5 * q2 * dU_r);
    const double dvn =
        (n_x * (dU_u - u_avg * dU_r) + n_y * (dU_v - v_avg * dU_r)) / rho_avg;
    const double dvt =
        (-n_y * (dU_u - u_avg * dU_r) + n_x * (dU_v - v_avg * dU_r)) / rho_avg;

    // --- Wave strengths ---
    const double a1 = (dp - rho_avg * a * dvn) / (2.0 * a2);
    const double a2w = dU_r - dp / a2;
    const double a3 = rho_avg * dvt;
    const double a4 = (dp + rho_avg * a * dvn) / (2.0 * a2);

    // --- Eigenvalues with Harten-Yee entropy fix ---
    const double delta = 0.1 * (a + std::abs(vn_avg));
    const auto fix = [delta](double lambda) {
        const double al = std::abs(lambda);
        if (al >= delta) return al;
        return (lambda * lambda + delta * delta) / (2.0 * delta);
    };
    const double l1 = fix(vn_avg - a);
    const double l2 = fix(vn_avg);
    const double l4 = fix(vn_avg + a);

    // --- Dissipation: sum_k |lambda_k| * alpha_k * r_k ---
    const double vt_avg = -u_avg * n_y + v_avg * n_x;
    // r1 = (1, u - a*nx, v - a*ny, H - a*vn)
    const double d1 = l1 * a1;
    Vector4 D;
    D.r = d1 + l2 * a2w + l4 * a4;
    D.u = d1 * (u_avg - a * n_x) + l2 * a2w * u_avg +
          l4 * a4 * (u_avg + a * n_x) + l2 * a3 * (-n_y);
    D.v = d1 * (v_avg - a * n_y) + l2 * a2w * v_avg +
          l4 * a4 * (v_avg + a * n_y) + l2 * a3 * (n_x);
    D.e = d1 * (H_avg - a * vn_avg) + l2 * a2w * 0.5 * q2 +
          l4 * a4 * (H_avg + a * vn_avg) + l2 * a3 * vt_avg;
    D = D * area;

    const Vector2 n{nx, ny};
    const Vector4 FL = inviscid_flux(UL, n, gas);
    const Vector4 FR = inviscid_flux(UR, n, gas);
    return 0.5 * (FL + FR) - 0.5 * D;
}

Vector4 viscous_flux(const Vector4& UL, const Vector4& UR,
                     const PrimitiveState& primL, const PrimitiveState& primR,
                     double nx, double ny, double dx, double dy,
                     const GasParams& gas, double viscosity) {
    // Zero viscosity (inviscid) or degenerate geometry: no diffusive flux.
    const double area = std::hypot(nx, ny);
    const double dist = std::hypot(dx, dy);
    if (viscosity <= 0.0 || area <= 0.0 || dist <= 0.0) return Vector4{};

    const double inv_dist = 1.0 / dist;
    const double inv_area = 1.0 / area;
    const double n_x = nx * inv_area;  // unit normal
    const double n_y = ny * inv_area;

    // First-order face gradient from the two-cell jump:
    //   grad(phi) ~= (phi_R - phi_L) / d * n_hat
    const double du = (primR.u - primL.u) * inv_dist;
    const double dv = (primR.v - primL.v) * inv_dist;
    const double dT = ((primR.p / (gas.R * primR.rho)) -
                       (primL.p / (gas.R * primL.rho))) *
                      inv_dist;

    // Gradient components projected onto the face normal.
    const double u_x = du * n_x, u_y = du * n_y;
    const double v_x = dv * n_x, v_y = dv * n_y;
    const double T_x = dT * n_x, T_y = dT * n_y;

    (void)UL;
    (void)UR;
    return viscous_flux_from_gradient(primL, primR, nx, ny, u_x, u_y, v_x,
                                      v_y, T_x, T_y, gas, viscosity);
}

Vector4 viscous_flux_from_gradient(
    const PrimitiveState& primL, const PrimitiveState& primR, double nx,
    double ny, double du_dx, double du_dy, double dv_dx, double dv_dy,
    double dT_dx, double dT_dy, const GasParams& gas, double viscosity) {
    const double area = std::hypot(nx, ny);
    if (viscosity <= 0.0 || area <= 0.0) return Vector4{};

    const double inv_area = 1.0 / area;
    const double n_x = nx * inv_area;  // unit normal
    const double n_y = ny * inv_area;

    // Newtonian stress (2D planar form with the (2/3) bulk-viscosity term).
    const double div = du_dx + dv_dy;
    const double tau_xx = viscosity * (2.0 * du_dx - (2.0 / 3.0) * div);
    const double tau_yy = viscosity * (2.0 * dv_dy - (2.0 / 3.0) * div);
    const double tau_xy = viscosity * (du_dy + dv_dx);

    // Fourier heat flux q = -k * grad T, k = mu * cp / Pr,
    // cp = gamma R/(gamma-1).
    const double cp = gas.gamma * gas.R / (gas.gamma - 1.0);
    const double k = viscosity * cp / gas.Pr;
    const double q_x = -k * dT_dx;
    const double q_y = -k * dT_dy;

    // Face traction tau*n_hat and its dot with the face velocity.
    const double tn_x = tau_xx * n_x + tau_xy * n_y;
    const double tn_y = tau_xy * n_x + tau_yy * n_y;
    const double u_face = 0.5 * (primL.u + primR.u);
    const double v_face = 0.5 * (primL.v + primR.v);

    Vector4 F;
    F.r = 0.0;
    F.u = -tn_x * area;
    F.v = -tn_y * area;
    F.e = (-(u_face * tn_x + v_face * tn_y) + (q_x * n_x + q_y * n_y)) * area;
    return F;
}

}  // namespace cfd
