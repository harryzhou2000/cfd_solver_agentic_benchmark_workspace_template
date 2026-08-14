#include "solver/flux.hpp"

#include <algorithm>
#include <cmath>

namespace solver {

// Convert conservative [rho, rhou, rhov, rhoE] to primitive [rho, u, v, p]
Vec4 conservative_to_primitive(const Vec4& U, double gamma) {
    const double rho = U(0);
    const double u = U(1) / rho;
    const double v = U(2) / rho;
    const double E = U(3) / rho;
    const double e = E - 0.5 * (u * u + v * v);
    const double p = (gamma - 1.0) * rho * e;
    return Vec4(rho, u, v, p);
}

// Convert primitive [rho, u, v, p] to conservative [rho, rhou, rhov, rhoE]
Vec4 primitive_to_conservative(const Vec4& W, double gamma) {
    const double rho = W(0);
    const double u = W(1);
    const double v = W(2);
    const double p = W(3);
    const double E = p / ((gamma - 1.0) * rho) + 0.5 * (u * u + v * v);
    return Vec4(rho, rho * u, rho * v, rho * E);
}

// Speed of sound: a = sqrt(gamma * p / rho)
double speed_of_sound(double rho, double p, double gamma) {
    return std::sqrt(gamma * p / rho);
}

// Inviscid flux F(U) * n (flux in face-normal direction)
// F[0] = rho*Vn, F[1] = rho*u*Vn + p*nx, F[2] = rho*v*Vn + p*ny, F[3] = (rho*E+p)*Vn
Vec4 inviscid_flux_term(const Vec4& U, const Vec2& normal, double gamma) {
    const double rho = U(0);
    const double rhou = U(1);
    const double rhov = U(2);
    const double rhoE = U(3);

    const double u = rhou / rho;
    const double v = rhov / rho;
    const double p = (gamma - 1.0) * (rhoE - 0.5 * (rhou * rhou + rhov * rhov) / rho);

    const double Vn = u * normal(0) + v * normal(1);

    Vec4 F;
    F(0) = rho * Vn;
    F(1) = rhou * Vn + p * normal(0);
    F(2) = rhov * Vn + p * normal(1);
    F(3) = (rhoE + p) * Vn;
    return F;
}

// Rusanov / Local Lax-Friedrichs flux
// lambda = max(|Vn_L| + a_L, |Vn_R| + a_R)
// F = 0.5*(F_L + F_R) - 0.5*lambda*dissipation_scale*(U_R - U_L)
Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
                  double gamma, double dissipation_scale) {
    const Vec4 FL = inviscid_flux_term(UL, normal, gamma);
    const Vec4 FR = inviscid_flux_term(UR, normal, gamma);

    const Vec4 W_L = conservative_to_primitive(UL, gamma);
    // Clamp density/pressure for the wave-speed estimate so that unphysical
    // inputs (e.g. negative pressure, which the Roe fallback hands to this
    // flux) cannot produce NaN from sqrt(negative). A no-op for every
    // physical state.
    const double rho_L = std::max(W_L(0), 1e-10);
    const double p_L = std::max(W_L(3), 1e-10);
    const double a_L = std::sqrt(gamma * p_L / rho_L);
    const double Vn_L = W_L(1) * normal(0) + W_L(2) * normal(1);

    const Vec4 W_R = conservative_to_primitive(UR, gamma);
    const double rho_R = std::max(W_R(0), 1e-10);
    const double p_R = std::max(W_R(3), 1e-10);
    const double a_R = std::sqrt(gamma * p_R / rho_R);
    const double Vn_R = W_R(1) * normal(0) + W_R(2) * normal(1);

    const double lambda =
        std::max(std::abs(Vn_L) + a_L, std::abs(Vn_R) + a_R);

    return 0.5 * (FL + FR) - 0.5 * lambda * dissipation_scale * (UR - UL);
}

// Roe flux with Harten-Yee entropy fix (2D unstructured)
// Computes Roe-averaged state, eigenvalues, wave strengths, and characteristic flux
// delta is the entropy fix threshold (recommended: 0.05 * reference_sound_speed)
Vec4 roe_flux(const Vec4& UL, const Vec4& UR, const Vec2& normal,
              double gamma, double delta) {
    const Vec4 FL = inviscid_flux_term(UL, normal, gamma);
    const Vec4 FR = inviscid_flux_term(UR, normal, gamma);
    const Vec4 F_avg = 0.5 * (FL + FR);

    // If the states are (nearly) equal the dissipation is negligible;
    // return the average physical flux to avoid cancellation issues.
    const Vec4 dU = UR - UL;
    if (dU.norm() < 1e-14 * (1.0 + UL.norm())) {
        return F_avg;
    }

    // Roe-averaged state
    const Vec4 W_L = conservative_to_primitive(UL, gamma);
    const Vec4 W_R = conservative_to_primitive(UR, gamma);

    const double rho_L = W_L(0), u_L = W_L(1), v_L = W_L(2), p_L = W_L(3);
    const double rho_R = W_R(0), u_R = W_R(1), v_R = W_R(2), p_R = W_R(3);

    const double R = std::sqrt(rho_R / rho_L);
    const double rho_roe = R * rho_L;
    const double u_roe = (u_L + R * u_R) / (1.0 + R);
    const double v_roe = (v_L + R * v_R) / (1.0 + R);

    const double H_L = gamma * p_L / ((gamma - 1.0) * rho_L) +
                       0.5 * (u_L * u_L + v_L * v_L);
    const double H_R = gamma * p_R / ((gamma - 1.0) * rho_R) +
                       0.5 * (u_R * u_R + v_R * v_R);
    const double H_roe = (H_L + R * H_R) / (1.0 + R);

    const double q2_roe = u_roe * u_roe + v_roe * v_roe;

    // Non-positive Roe sound speed squared means a non-physical Roe average;
    // the wave-strength computation below divides by a2_roe and a_roe
    // (alpha1/alpha4), which would produce NaN/Inf. Fall back to the robust
    // Rusanov flux instead of clamping a_roe to zero.
    const double a2_roe = (gamma - 1.0) * (H_roe - 0.5 * q2_roe);
    if (a2_roe <= 0.0) {
        return rusanov_flux(UL, UR, normal, gamma, 1.0);
    }
    const double a_roe = std::sqrt(a2_roe);

    const double nx = normal(0);
    const double ny = normal(1);
    const double Vn = u_roe * nx + v_roe * ny;

    // Harten-Yee entropy fix: replace |lambda| by a smoothed value when it is
    // below the threshold delta.
    const auto entropy_fix = [delta](double lam) {
        const double abs_lam = std::abs(lam);
        if (abs_lam < delta) {
            return 0.5 * (lam * lam / delta + delta);
        }
        return abs_lam;
    };

    // Eigenvalues: Vn-a, Vn, Vn, Vn+a (with entropy fix applied)
    const double lam1 = entropy_fix(Vn - a_roe);
    const double lam2 = entropy_fix(Vn);
    const double lam4 = entropy_fix(Vn + a_roe);

    // Wave strengths from conservative jumps in Roe-averaged variables
    const double d_rho = dU(0);
    const double d_rhou = dU(1);
    const double d_rhov = dU(2);
    const double d_rhoE = dU(3);

    const double dP = (gamma - 1.0) *
                      (d_rhoE + 0.5 * q2_roe * d_rho - u_roe * d_rhou -
                       v_roe * d_rhov);
    const double dVn = (d_rhou * nx + d_rhov * ny - d_rho * Vn) / rho_roe;

    // Tangential momentum jump components (used for the shear wave; the shear
    // wave is not included in the dissipative sum below, which uses the three
    // standard eigenvectors of the 2D Euler system).
    const double dVt_x = (d_rhou - d_rho * u_roe) / rho_roe - dVn * nx;
    const double dVt_y = (d_rhov - d_rho * v_roe) / rho_roe - dVn * ny;
    (void)dVt_x;
    (void)dVt_y;

    const double alpha1 = 0.5 * (dP / a2_roe - rho_roe * dVn / a_roe);
    const double alpha2 = d_rho - dP / a2_roe;
    const double alpha4 = 0.5 * (dP / a2_roe + rho_roe * dVn / a_roe);

    // Dissipative term: sum of alpha_k * |lambda_k| * eigenvector_k over the
    // acoustic (1, 4) and entropy (2) waves.
    const Vec4 ev1(1.0, u_roe - a_roe * nx, v_roe - a_roe * ny,
                   H_roe - a_roe * Vn);
    const Vec4 ev2(1.0, u_roe, v_roe, 0.5 * q2_roe);
    const Vec4 ev4(1.0, u_roe + a_roe * nx, v_roe + a_roe * ny,
                   H_roe + a_roe * Vn);

    const Vec4 dissipation =
        alpha1 * lam1 * ev1 + alpha2 * lam2 * ev2 + alpha4 * lam4 * ev4;

    return F_avg - 0.5 * dissipation;
}

// Viscous flux in face-normal direction
// Uses face-averaged velocity gradients, stress tensor, and heat flux
// mu: dynamic viscosity, cp: specific heat at const pressure, Pr: Prandtl number
// grad_U[var][face] = face-averaged gradient component from left/right cells
// T: face-averaged temperature, grad_T: face-averaged temperature gradient
Vec4 viscous_flux_term(const Vec4& U_face,   // face-averaged conservative state
                        const Vec4& grad_rho, const Vec4& grad_rhou,
                        const Vec4& grad_rhov, const Vec4& grad_rhoE,
                        double T_face, const Vec2& grad_T,
                        const Vec2& normal,
                        double mu, double Pr, double gamma, double R) {
    (void)grad_rhoE;  // energy gradient not needed: heat flux uses grad_T
    (void)T_face;     // face temperature not needed: k derived from mu, cp, Pr

    // Face-averaged primitive velocity
    const double rho = U_face(0);
    const double u = U_face(1) / rho;
    const double v = U_face(2) / rho;

    // Velocity gradients from conservative gradients
    const double du_dx = (grad_rhou(0) - u * grad_rho(0)) / rho;
    const double du_dy = (grad_rhou(1) - u * grad_rho(1)) / rho;
    const double dv_dx = (grad_rhov(0) - v * grad_rho(0)) / rho;
    const double dv_dy = (grad_rhov(1) - v * grad_rho(1)) / rho;

    // Newtonian stress tensor
    const double divV = du_dx + dv_dy;
    const double tau_xx = 2.0 * mu * du_dx - (2.0 / 3.0) * mu * divV;
    const double tau_yy = 2.0 * mu * dv_dy - (2.0 / 3.0) * mu * divV;
    const double tau_xy = mu * (du_dy + dv_dx);

    // Heat flux (Fourier's law)
    const double cp = gamma * R / (gamma - 1.0);
    const double k = mu * cp / Pr;
    const double qx = -k * grad_T(0);
    const double qy = -k * grad_T(1);

    const double nx = normal(0);
    const double ny = normal(1);

    Vec4 Fv;
    Fv(0) = 0.0;
    Fv(1) = tau_xx * nx + tau_xy * ny;
    Fv(2) = tau_xy * nx + tau_yy * ny;
    Fv(3) = (u * tau_xx + v * tau_xy - qx) * nx +
            (u * tau_xy + v * tau_yy - qy) * ny;
    return Fv;
}

} // namespace solver
