#pragma once

#include <array>
#include <cmath>

#include "case_io.hpp"

namespace cfd {

// Conservative state: [rho, rho*u, rho*v, rho*E]
struct Cons {
  double rho, rhou, rhov, rhoE;
};

// Primitive state: [rho, u, v, p]
struct Prim {
  double rho, u, v, p;
};

struct Gas {
  double gamma, R, prandtl;
  double cp;  // gamma*R/(gamma-1)
  double cv;
  double p_ref = 1.0;  // freestream pressure (positivity floor reference)

  Gas(const Case& c) : gamma(c.gamma), R(c.R_gas), prandtl(c.prandtl) {
    cp = gamma * R / (gamma - 1.0);
    cv = R / (gamma - 1.0);
    p_ref = c.p_inf;
  }
};

inline Prim cons2prim(const Cons& U, const Gas& g) {
  Prim q;
  q.rho = U.rho;
  q.u = U.rhou / U.rho;
  q.v = U.rhov / U.rho;
  const double ek = 0.5 * (q.u * q.u + q.v * q.v);
  q.p = (g.gamma - 1.0) * (U.rhoE - U.rho * ek);
  // Pressure floor for positivity: the sharp trailing-edge expansion of the
  // supersonic cases can drive the corner cells to (numerically) negative
  // pressure; the flux then evaluates sqrt(p) and produces NaNs. Clamping to
  // a tiny positive value keeps the run stable with negligible force impact.
  if (q.p < 1e-6 * g.p_ref) q.p = 1e-6 * g.p_ref;
  return q;
}

inline Cons prim2cons(const Prim& q, const Gas& g) {
  Cons U;
  U.rho = q.rho;
  U.rhou = q.rho * q.u;
  U.rhov = q.rho * q.v;
  U.rhoE = q.p / (g.gamma - 1.0) + 0.5 * q.rho * (q.u * q.u + q.v * q.v);
  return U;
}

inline double speed_of_sound(const Prim& q, const Gas& g) {
  return std::sqrt(g.gamma * q.p / q.rho);
}

inline double temperature(const Prim& q, const Gas& g) {
  return q.p / (q.rho * g.R);
}

// ---------------------------------------------------------------------------
// Low-Mach dissipation factor for the Rusanov/LLF flux.
//
// The standard dissipation speed |vn| + a is dominated by the acoustic speed
// at low Mach number (a ~ 10 |u| at M = 0.1), which over-dissipates the
// momentum and creates a spurious stagnation-pressure overshoot. Following
// the standard low-Mach LLF/Rusanov fix, the acoustic contribution is scaled
// by the local Mach number, phi(M) = min(1, max(M, M_min)), while a
// pressure-jump shock sensor restores the full dissipation near shocks.
// ---------------------------------------------------------------------------
inline double rusanov_dissipation_speed(const Prim& qL, const Prim& qR,
                                        double vnL, double vnR, double aL,
                                        double aR) {
  const double M_mag_L = std::hypot(qL.u, qL.v) / aL;
  const double M_mag_R = std::hypot(qR.u, qR.v) / aR;
  const double Mf = std::max(
      {M_mag_L, M_mag_R, std::abs(vnL) / aL, std::abs(vnR) / aR});
  double phi = std::min(1.0, std::max(Mf, 1e-3));
  // Shock sensor: full dissipation where the pressure jumps by ~10%.
  const double sjump = std::abs(qR.p - qL.p) / std::max(qL.p, qR.p);
  phi = std::max(phi, std::min(1.0, sjump / 0.1));
  return std::max(std::abs(vnL) + phi * aL, std::abs(vnR) + phi * aR);
}

// ---------------------------------------------------------------------------
// Inviscid flux in the direction of unit normal n: AUSM+-up (Liou 2006).
// All-speed scheme: reduces to AUSM+ at supersonic Mach numbers and retains
// the pressure-velocity coupling at low Mach via the K_p/K_u corrections.
// mach_inf is the freestream Mach number used for the f_a cutoff.
// ---------------------------------------------------------------------------
inline void inviscid_flux(const Cons& UL, const Cons& UR, const Prim& qL,
                          const Prim& qR, double nx, double ny, const Gas& g,
                          double mach_inf, Cons& F) {
  const double vnL = qL.u * nx + qL.v * ny;
  const double vnR = qR.u * nx + qR.v * ny;
  const double aL = speed_of_sound(qL, g);
  const double aR = speed_of_sound(qR, g);
  const char* flux_env = getenv("CFD_FLUX");
  const bool use_rusanov =
      flux_env && (std::strcmp(flux_env, "rusanov") == 0 ||
                   std::strcmp(flux_env, "rusanov_plain") == 0);
  const bool use_central =
      flux_env && std::strcmp(flux_env, "central") == 0;
  if (use_central) {
    // Central average of the physical fluxes (no upwind dissipation). For the
    // low-Mach viscous wake this is the least dissipative option available;
    // the background JST k4 term in the residual supplies the odd-even mode
    // damping. Intended for the transient vortex-street case, where upwind
    // dissipation at the coarse wake spacing suppresses the physical
    // instability and locks the flow onto the steady asymmetric branch.
    const double FLc[4] = {UL.rho * vnL,
                           UL.rho * qL.u * vnL + qL.p * nx,
                           UL.rho * qL.v * vnL + qL.p * ny,
                           (UL.rhoE + qL.p) * vnL};
    const double FRc[4] = {UR.rho * vnR,
                           UR.rho * qR.u * vnR + qR.p * nx,
                           UR.rho * qR.v * vnR + qR.p * ny,
                           (UR.rhoE + qR.p) * vnR};
    F.rho = 0.5 * (FLc[0] + FRc[0]);
    F.rhou = 0.5 * (FLc[1] + FRc[1]);
    F.rhov = 0.5 * (FLc[2] + FRc[2]);
    F.rhoE = 0.5 * (FLc[3] + FRc[3]);
    return;
  }
  if (use_rusanov) {
    // Plain Rusanov / local Lax-Friedrichs: F = 0.5(F_L+F_R) - 0.5 lam (U_R-U_L).
    const double FLr[4] = {UL.rho * vnL,
                           UL.rho * qL.u * vnL + qL.p * nx,
                           UL.rho * qL.v * vnL + qL.p * ny,
                           (UL.rhoE + qL.p) * vnL};
    const double FRr[4] = {UR.rho * vnR,
                           UR.rho * qR.u * vnR + qR.p * nx,
                           UR.rho * qR.v * vnR + qR.p * ny,
                           (UR.rhoE + qR.p) * vnR};
    const double lam =
        std::max(std::abs(vnL) + aL, std::abs(vnR) + aR);
    const double s = 0.5 * lam;
    F.rho = 0.5 * (FLr[0] + FRr[0]) - s * (UR.rho - UL.rho);
    F.rhou = 0.5 * (FLr[1] + FRr[1]) - s * (UR.rhou - UL.rhou);
    F.rhov = 0.5 * (FLr[2] + FRr[2]) - s * (UR.rhov - UL.rhov);
    F.rhoE = 0.5 * (FLr[3] + FRr[3]) - s * (UR.rhoE - UL.rhoE);
    return;
  }
  const double C = 0.5 * (aL + aR);
  const double ML = vnL / C;
  const double MR = vnR / C;
  const double Mb2 = 0.5 * (vnL * vnL + vnR * vnR) / (C * C);
  const double Mo2 =
      std::min(1.0, std::max(Mb2, mach_inf * mach_inf));
  const double Mo = std::sqrt(Mo2);
  const double fa = Mo * (2.0 - Mo);
  const double alpha = (3.0 / 16.0) * (-4.0 + 5.0 * fa * fa);

  auto M4p = [](double M) {
    if (std::abs(M) >= 1.0) return 0.5 * (M + std::abs(M));
    return 0.25 * (M + 1.0) * (M + 1.0) +
           0.125 * (M * M - 1.0) * (M * M - 1.0);
  };
  auto M4m = [](double M) {
    if (std::abs(M) >= 1.0) return 0.5 * (M - std::abs(M));
    return -0.25 * (M - 1.0) * (M - 1.0) -
           0.125 * (M * M - 1.0) * (M * M - 1.0);
  };
  auto P5p = [alpha](double M) {
    if (std::abs(M) >= 1.0) return 0.5 * (1.0 + (M > 0.0 ? 1.0 : -1.0));
    return 0.25 * (M + 1.0) * (M + 1.0) * (2.0 - M) +
           alpha * M * (M * M - 1.0) * (M * M - 1.0);
  };
  auto P5m = [alpha](double M) {
    if (std::abs(M) >= 1.0) return 0.5 * (1.0 - (M > 0.0 ? 1.0 : -1.0));
    return 0.25 * (M - 1.0) * (M - 1.0) * (2.0 + M) -
           alpha * M * (M * M - 1.0) * (M * M - 1.0);
  };

  const double FmL = M4p(ML);
  const double FmR = M4m(MR);
  double Kp = 0.25;
  const char* kp_env = getenv("CFD_KP");
  if (kp_env) Kp = std::max(0.0, std::atof(kp_env));
  double Ku = 0.75;
  const char* ku_env = getenv("CFD_KU");
  if (ku_env) Ku = std::max(0.0, std::atof(ku_env));
  const double rho_sum = qL.rho + qR.rho;
  const double Mp = -2.0 * Kp * std::max(1.0 - Mb2, 0.0) * (qR.p - qL.p) /
                    (fa * rho_sum * C * C);
  const double Mface = FmL + FmR + Mp;

  const double betaL = P5p(ML);
  const double betaR = P5m(MR);
  const double Pu = -Ku * betaL * betaR * rho_sum * fa * C * (vnR - vnL);
  const double pbar = betaL * qL.p + betaR * qR.p + Pu;

  const double massL = 0.5 * (Mface + std::abs(Mface)) * C;
  const double massR = 0.5 * (Mface - std::abs(Mface)) * C;
  const double HL = (UL.rhoE + qL.p) / UL.rho;
  const double HR = (UR.rhoE + qR.p) / UR.rho;

  F.rho = massL * UL.rho + massR * UR.rho;
  F.rhou = massL * UL.rhou + massR * UR.rhou + pbar * nx;
  F.rhov = massL * UL.rhov + massR * UR.rhov + pbar * ny;
  F.rhoE = massL * UL.rho * HL + massR * UR.rho * HR;

  // Optional small Rusanov background blend: damps odd-even modes that the
  // low-dissipation AUSM+-up pressure-velocity coupling can leave undamped
  // at low Mach number. CFD_BLEND=0.05 keeps most of the AUSM+-up accuracy.
  const char* blend_env = getenv("CFD_BLEND");
  if (blend_env && std::atof(blend_env) > 0.0) {
    const double eps = std::min(1.0, std::atof(blend_env));
    const double lam = rusanov_dissipation_speed(qL, qR, vnL, vnR, aL, aR);
    const double s = 0.5 * eps * lam;
    const double Fr[4] = {UL.rho * vnL,
                          UL.rho * qL.u * vnL + qL.p * nx,
                          UL.rho * qL.v * vnL + qL.p * ny,
                          (UL.rhoE + qL.p) * vnL};
    const double Fl[4] = {UR.rho * vnR,
                          UR.rho * qR.u * vnR + qR.p * nx,
                          UR.rho * qR.v * vnR + qR.p * ny,
                          (UR.rhoE + qR.p) * vnR};
    Cons Fb;
    Fb.rho = 0.5 * (Fr[0] + Fl[0]) - s * (UR.rho - UL.rho);
    Fb.rhou = 0.5 * (Fr[1] + Fl[1]) - s * (UR.rhou - UL.rhou);
    Fb.rhov = 0.5 * (Fr[2] + Fl[2]) - s * (UR.rhov - UL.rhov);
    Fb.rhoE = 0.5 * (Fr[3] + Fl[3]) - s * (UR.rhoE - UL.rhoE);
    F.rho = (1.0 - eps) * F.rho + eps * Fb.rho;
    F.rhou = (1.0 - eps) * F.rhou + eps * Fb.rhou;
    F.rhov = (1.0 - eps) * F.rhov + eps * Fb.rhov;
    F.rhoE = (1.0 - eps) * F.rhoE + eps * Fb.rhoE;
  }
}

// ---------------------------------------------------------------------------
// Viscous flux in the direction of unit normal n.
//   grad_face per primitive component supplied; mu_face, Pr, gamma used for
//   stress tensor and Fourier heat flux.
// ---------------------------------------------------------------------------
inline void viscous_flux(const Prim& qL, const Prim& qR,
                         const double gradL[4][2], const double gradR[4][2],
                         double dist, double nx, double ny, double mu,
                         const Gas& g, Cons& F) {
  const double u = 0.5 * (qL.u + qR.u);
  const double v = 0.5 * (qL.v + qR.v);

  // Average gradients, then apply the directional consistency correction
  // using the cell-center difference. The caller supplies gradL/gradR already
  // averaged; we correct normal derivatives with the one-dimensional formula
  // using (qR - qL)/dist. dist = |xR - xL|.
  double gq[4][2];
  for (int k = 0; k < 4; ++k) {
    gq[k][0] = 0.5 * (gradL[k][0] + gradR[k][0]);
    gq[k][1] = 0.5 * (gradL[k][1] + gradR[k][1]);
  }

  // Face normal derivative from cell values: dq/dn ~= (qR - qL)/dist
  const double dqdn[4] = {(qR.rho - qL.rho) / dist,
                          (qR.u - qL.u) / dist,
                          (qR.v - qL.v) / dist,
                          (qR.p - qL.p) / dist};
  // Correct the normal component of each gradient.
  for (int k = 0; k < 4; ++k) {
    const double gn = gq[k][0] * nx + gq[k][1] * ny;
    const double corr = gn - dqdn[k];
    gq[k][0] -= corr * nx;
    gq[k][1] -= corr * ny;
  }

  const double ux = gq[1][0], uy = gq[1][1];
  const double vx = gq[2][0], vy = gq[2][1];
  const double tx = gq[3][0], ty = gq[3][1];

  const double div = ux + vy;
  const double tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  const double tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  const double tau_xy = mu * (uy + vx);

  const double kappa = mu * g.cp / g.prandtl;
  const double qx = -kappa * tx;
  const double qy = -kappa * ty;

  const double fn_x = tau_xx * nx + tau_xy * ny;
  const double fn_y = tau_xy * nx + tau_yy * ny;
  const double fn_e = (u * tau_xx + v * tau_xy - qx) * nx +
                      (u * tau_xy + v * tau_yy - qy) * ny;

  F.rho = 0.0;
  F.rhou = fn_x;
  F.rhov = fn_y;
  F.rhoE = fn_e;
}

// Viscous flux at a no-slip adiabatic solid wall, in the direction of the
// outward face normal n. q_cell is the cell-centered primitive state and
// grad_cell its gradient; dist is the cell-center-to-face distance.
//
// The wall state is no-slip (u = v = 0) and adiabatic: density and pressure
// are extrapolated with zero normal gradient (T_wall = T_cell). The wall
// gradient's normal component is the one-sided difference
//   dq/dn|_w = (q_wall - q_cell)/dist,
// while the tangential components come from the cell gradient. The no-slip
// condition makes the stress work term vanish in the energy flux.
inline void viscous_wall_flux(const Prim& q_cell, const double* grad_cell,
                              double dist, double nx, double ny, double mu,
                              const Gas& g, Cons& F) {
  const double dqdn[4] = {0.0, -q_cell.u / dist, -q_cell.v / dist, 0.0};
  double gq[4][2];
  for (int k = 0; k < 4; ++k) {
    gq[k][0] = grad_cell[2 * k];
    gq[k][1] = grad_cell[2 * k + 1];
    const double gn = gq[k][0] * nx + gq[k][1] * ny;
    const double corr = gn - dqdn[k];
    gq[k][0] -= corr * nx;
    gq[k][1] -= corr * ny;
  }

  const double ux = gq[1][0], uy = gq[1][1];
  const double vx = gq[2][0], vy = gq[2][1];
  const double tx = gq[3][0], ty = gq[3][1];

  const double div = ux + vy;
  const double tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  const double tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  const double tau_xy = mu * (uy + vx);

  const double kappa = mu * g.cp / g.prandtl;
  const double qx = -kappa * tx;
  const double qy = -kappa * ty;

  F.rho = 0.0;
  F.rhou = tau_xx * nx + tau_xy * ny;
  F.rhov = tau_xy * nx + tau_yy * ny;
  F.rhoE = -qx * nx - qy * ny;  // u_wall = 0: no stress work
}

// Ghost-cell primitive state for a boundary face with outward normal n.
// Returns false for states that should not be used (never the case here).
inline bool boundary_ghost_state(const Prim& q_cell, double nx, double ny,
                                 BCType bc, const FreeStream& fs, const Gas& g,
                                 Prim& q_g) {
  switch (bc) {
    case BCType::Farfield: {
      // Characteristic-based (1-D Riemann invariant) farfield treatment.
      const double gamma = g.gamma;
      const double gm1 = gamma - 1.0;
      const double vn = q_cell.u * nx + q_cell.v * ny;
      const double a = std::sqrt(gamma * q_cell.p / q_cell.rho);
      const double vn_inf = fs.u * nx + fs.v * ny;
      const double a_inf = fs.a;
      double Rp = vn + 2.0 * a / gm1;
      double Rm = vn - 2.0 * a / gm1;
      // Entropy: interior if outflow (vn + a > 0), else freestream.
      const double s_inf =
          std::log(fs.p / std::pow(fs.rho, gamma));
      const double s_int = std::log(q_cell.p / std::pow(q_cell.rho, gamma));
      // The entropy and tangential-velocity characteristics convect at vn:
      // from freestream at inflow (vn < 0), from the interior at outflow.
      double s_ghost = (vn > 0.0) ? s_int : s_inf;
      // Tangential velocity: interior at outflow, freestream at inflow.
      double vt, vt_inf;
      {
        const double tx = -ny, ty = nx;
        vt = q_cell.u * tx + q_cell.v * ty;
        vt_inf = fs.u * tx + fs.v * ty;
      }
      const double vt_g = (vn > 0.0) ? vt : vt_inf;
      // Acoustic invariants: R- travels inward at speed vn - a; set it from
      // the freestream whenever the boundary is an acoustic inflow.
      if (vn - a < 0.0) Rm = vn_inf - 2.0 * a_inf / gm1;
      // R+ travels inward only for supersonic inflow (vn + a < 0).
      if (vn + a < 0.0) Rp = vn_inf + 2.0 * a_inf / gm1;
      const double vn_g = 0.5 * (Rp + Rm);
      const double a_g = 0.25 * gm1 * (Rp - Rm);
      if (a_g <= 0.0 || !std::isfinite(a_g)) {
        // Fallback: freestream ghost.
        q_g.rho = fs.rho;
        q_g.u = fs.u;
        q_g.v = fs.v;
        q_g.p = fs.p;
        return true;
      }
      // Pressure from isentropic relation with the ghost entropy.
      const double p_ref = fs.p, a_ref = a_inf;
      const double p_g =
          p_ref * std::pow(a_g / a_ref, 2.0 * gamma / gm1) *
          std::exp((s_ghost - s_inf) / (gamma - 1.0));
      const double rho_g = gamma * p_g / (a_g * a_g);
      if (rho_g <= 0.0 || !std::isfinite(p_g) || !std::isfinite(rho_g)) {
        q_g.rho = fs.rho;
        q_g.u = fs.u;
        q_g.v = fs.v;
        q_g.p = fs.p;
        return true;
      }
      const double tx = -ny, ty = nx;
      q_g.rho = rho_g;
      q_g.u = vn_g * nx + vt_g * tx;
      q_g.v = vn_g * ny + vt_g * ty;
      q_g.p = p_g;
      return true;
    }
    case BCType::SlipWall: {
      const double vn = q_cell.u * nx + q_cell.v * ny;
      q_g.rho = q_cell.rho;
      q_g.u = q_cell.u - 2.0 * vn * nx;
      q_g.v = q_cell.v - 2.0 * vn * ny;
      q_g.p = q_cell.p;
      return true;
    }
    case BCType::NoSlipAdiabaticWall: {
      q_g.rho = q_cell.rho;
      q_g.u = -q_cell.u;
      q_g.v = -q_cell.v;
      q_g.p = q_cell.p;
      return true;
    }
    default:
      return false;
  }
}

}  // namespace cfd
