#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include "Config.hpp"
#include "MeshData.hpp"

// ============================================================
// Gas model utilities (calorically perfect gas)
// ============================================================
inline double compute_pressure(const StateVec& U, double gamma) {
    double ke = 0.5*(U[1]*U[1] + U[2]*U[2]) / U[0];
    return (gamma - 1.0) * (U[3] - ke * U[0]) / U[0] * U[0];
    // simpler: p = (gamma-1)*(rhoE - 0.5*(rhou^2+rhov^2)/rho)
}
inline double pressure(const StateVec& U, double gamma) {
    double ke = 0.5*(U[1]*U[1] + U[2]*U[2]) / U[0];
    return (gamma - 1.0) * (U[3] - ke);
}
inline double sound_speed(double rho, double p, double gamma) {
    return std::sqrt(gamma * p / rho);
}
inline double temperature(double p, double rho, double R) {
    return p / (rho * R);
}
inline double mach_number(double u, double v, double a) {
    return std::sqrt(u*u + v*v) / a;
}
// Freestream conservative state
inline StateVec freestream_state(const CaseConfig& cfg) {
    const auto& fs = cfg.freestream;
    const auto& g  = cfg.gas;
    double rho = fs.rho;
    double aoa = fs.aoa_degrees * M_PI / 180.0;
    double u   = fs.velocity * std::cos(aoa);
    double v   = fs.velocity * std::sin(aoa);
    double p   = fs.pressure;
    double E   = p / ((g.gamma - 1.0) * rho) + 0.5 * (u*u + v*v);
    return {rho, rho*u, rho*v, rho*E};
}
// Dynamic viscosity (constant model): mu = rho_inf * V_inf * L_ref / Re
inline double compute_mu(const CaseConfig& cfg) {
    if (cfg.mode == PhysicsMode::Inviscid || cfg.reynolds <= 0.0)
        return 0.0;
    return cfg.freestream.rho * cfg.freestream.velocity * cfg.reference.reynolds_length / cfg.reynolds;
}
// ============================================================
// Inviscid flux: F^i * n  (Rusanov / Local Lax-Friedrichs)
// ============================================================
// F*n = [rho*vn, rho*u*vn+p*nx, rho*v*vn+p*ny, (rhoE+p)*vn]
inline StateVec inviscid_flux_dotN(const StateVec& U, double nx, double ny, double gamma) {
    double rho = U[0], rhou = U[1], rhov = U[2], rhoE = U[3];
    double u = rhou/rho, v = rhov/rho;
    double p = pressure(U, gamma);
    double vn = u*nx + v*ny;
    return { rho*vn, rho*u*vn + p*nx, rho*v*vn + p*ny, (rhoE + p)*vn };
}
// Rusanov flux on a face with unit normal (nx,ny) pointing from L to R
inline StateVec rusanov_flux(const StateVec& UL, const StateVec& UR,
                              double nx, double ny, double gamma, double diss_scale,
                              double mach_ref = 0.0) {
    auto FL = inviscid_flux_dotN(UL, nx, ny, gamma);
    auto FR = inviscid_flux_dotN(UR, nx, ny, gamma);
    double rhoL=UL[0], uL=UL[1]/rhoL, vL=UL[2]/rhoL;
    double rhoR=UR[0], uR=UR[1]/rhoR, vR=UR[2]/rhoR;
    double pL = pressure(UL, gamma), pR = pressure(UR, gamma);
    double aL = sound_speed(rhoL, pL, gamma), aR = sound_speed(rhoR, pR, gamma);
    double vnL = uL*nx + vL*ny, vnR = uR*nx + vR*ny;
    // Low-Mach preconditioning: when mach_ref>0, use Turkel-style acoustic speed
    double aL_eff = (mach_ref > 0.0) ? std::max(std::abs(vnL) + 1e-10, mach_ref * aL) : aL;
    double aR_eff = (mach_ref > 0.0) ? std::max(std::abs(vnR) + 1e-10, mach_ref * aR) : aR;
    double smax = diss_scale * std::max(std::abs(vnL) + aL_eff, std::abs(vnR) + aR_eff);
    StateVec f;
    for (int k=0; k<4; k++)
        f[k] = 0.5*(FL[k]+FR[k]) - 0.5*smax*(UR[k]-UL[k]);
    return f;
}
// ============================================================
// Viscous flux: tau*n and q*n terms
// ============================================================
// Primitive variables at face:
struct PrimState { double rho, u, v, p, T; };
inline PrimState prim_from_cons(const StateVec& U, double gamma, double R) {
    double rho = U[0], u = U[1]/rho, v = U[2]/rho;
    double p = pressure(U, gamma);
    double T = temperature(p, rho, R);
    return {rho, u, v, p, T};
}
// Viscous flux given face primitives and velocity+temperature gradients at face
// grad_u[0..1] = du/dx, du/dy; grad_v[0..1] = dv/dx, dv/dy; grad_T[0..1]
inline StateVec viscous_flux_dotN(double u, double v,
                                   const GradVec& grad_u, const GradVec& grad_v,
                                   const GradVec& grad_T,
                                   double nx, double ny,
                                   double mu, double k_cond) {
    double ux = grad_u[0], uy = grad_u[1];
    double vx = grad_v[0], vy = grad_v[1];
    double div = ux + vy;
    double txx = mu*(2.0*ux - (2.0/3.0)*div);
    double tyy = mu*(2.0*vy - (2.0/3.0)*div);
    double txy = mu*(uy + vx);
    double qx = -k_cond * grad_T[0];
    double qy = -k_cond * grad_T[1];
    // Viscous flux dotted with normal
    double tx = txx*nx + txy*ny;
    double ty = txy*nx + tyy*ny;
    double qn = qx*nx  + qy*ny;
    double energy_visc = tx*u + ty*v - qn;
    return {0.0, tx, ty, energy_visc};
}
// ============================================================
// Boundary condition ghost states
// ============================================================
// Farfield: subsonic/supersonic in/out via Riemann invariants
inline StateVec bc_farfield(const StateVec& U_int, double nx, double ny,
                             const StateVec& U_inf, double gamma) {
    double rho = U_int[0], u = U_int[1]/rho, v = U_int[2]/rho;
    double p = pressure(U_int, gamma);
    double a = sound_speed(rho, p, gamma);
    double vn_int = u*nx + v*ny;
    double Mn = vn_int / a;

    // Freestream
    double ri = U_inf[0], ui = U_inf[1]/ri, vi = U_inf[2]/ri;
    double pi = pressure(U_inf, gamma);
    double ai = sound_speed(ri, pi, gamma);
    double vn_inf = ui*nx + vi*ny;

    if (Mn >= 1.0) {
        // Supersonic outflow: all from interior
        return U_int;
    } else if (Mn <= -1.0) {
        // Supersonic inflow: all from freestream
        return U_inf;
    } else {
        // Subsonic: use Riemann invariants
        // R+ = vn + 2a/(gamma-1) (outgoing from domain -> from interior)
        // R- = vn - 2a/(gamma-1) (incoming -> from freestream)
        double Rp = vn_int + 2.0*a/(gamma-1.0);   // from interior
        double Rm = vn_inf - 2.0*ai/(gamma-1.0);   // from freestream

        double vn_bc = 0.5*(Rp + Rm);
        double a_bc  = 0.25*(gamma-1.0)*(Rp - Rm);
        if (a_bc < 0) a_bc = 1e-10;

        double vt_int_x = u - vn_int*nx;
        double vt_int_y = v - vn_int*ny;
        double vt_inf_x = ui - vn_inf*nx;
        double vt_inf_y = vi - vn_inf*ny;

        double u_bc, v_bc, rho_bc, p_bc;
        if (vn_int > 0.0) {
            // Outflow: tangential + entropy from interior
            u_bc = vt_int_x + vn_bc*nx;
            v_bc = vt_int_y + vn_bc*ny;
            // Entropy from interior: p/rho^gamma = const
            double s_int = p / std::pow(rho, gamma);
            rho_bc = std::pow(a_bc*a_bc / (gamma*s_int), 1.0/(gamma-1.0));
            p_bc   = s_int * std::pow(rho_bc, gamma);
        } else {
            // Inflow: tangential + entropy from freestream
            u_bc = vt_inf_x + vn_bc*nx;
            v_bc = vt_inf_y + vn_bc*ny;
            double s_inf = pi / std::pow(ri, gamma);
            rho_bc = std::pow(a_bc*a_bc / (gamma*s_inf), 1.0/(gamma-1.0));
            p_bc   = s_inf * std::pow(rho_bc, gamma);
        }
        double E_bc = p_bc/((gamma-1.0)*rho_bc) + 0.5*(u_bc*u_bc + v_bc*v_bc);
        return {rho_bc, rho_bc*u_bc, rho_bc*v_bc, rho_bc*E_bc};
    }
}
// Slip wall: reflect normal velocity
inline StateVec bc_slip_wall(const StateVec& U_int, double nx, double ny) {
    double rho = U_int[0], u = U_int[1]/rho, v = U_int[2]/rho;
    double vn = u*nx + v*ny;
    double u_ghost = u - 2.0*vn*nx;
    double v_ghost = v - 2.0*vn*ny;
    return {rho, rho*u_ghost, rho*v_ghost, U_int[3]};
}
// No-slip adiabatic wall: zero velocity, zero temperature gradient
// Ghost state: mirror velocity (so face = 0), extrapolate rho and E
inline StateVec bc_no_slip_adiabatic(const StateVec& U_int) {
    double rho = U_int[0];
    // Mirror velocity -> face velocity = 0
    // E_ghost = E_int (adiabatic: dT/dn=0 means same T, same E per unit mass)
    return {rho, -U_int[1], -U_int[2], U_int[3]};
}
