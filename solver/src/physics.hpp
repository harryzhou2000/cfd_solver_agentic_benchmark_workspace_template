// Physics: calorically-perfect-gas fluxes, boundary-condition ghost states,
// and force integration. Implements (my own) Roe flux with a Harten-Yee
// entropy fix and a Rusanov/local-Lax-Friedrichs fallback. Viscous flux uses
// a Newtonian stress tensor + Fourier heat flux on primitive gradients.
//
// All "normal" quantities use the area-weighted face normal S=(Sx,Sy); the
// unit normal is n = S/|S|. Fluxes returned here are the physical flux F.n
// (NOT yet multiplied by the face length; the caller scales by face length).
#pragma once
#include "types.hpp"
#include "case.hpp"
#include <string>
#include <vector>

namespace cfd {

struct Physics {
  GasModel gas;
  Freestream fs;
  Reference ref;
  bool laminar = false;
  double reynolds = 0.0;
  double mu = 0.0;        // dynamic viscosity (nondimensional)
  double k_thermal = 0.0; // = mu*cp/Pr
 double rusanov_scale = 1.0;
 bool use_roe = true;
 double pstab_k = 0.0;   // CFD2D_PSTAB: Rhie-Chow-like low-Mach pressure
                         // stabilization (2nd-order, linearizable). Damps the
                         // high-freq pressure checkerboard that blows up cap-1
                         // 0 at low Mach, without damping the low-freq shedding.
 bool use_hllc = false;   // CFD2D_FLUX_HLLC: HLLC (3-wave) flux -- more low-Mach-
                          // robust than Roe (less carbuncle) and less dissipative
                          // on contact/shear than Rusanov, so it may capture the
                          // Re200 shedding without the cap-1.0 pressure blowup.
 // AUSM+-up (Liou 2006): split convective+pressure flux with low-Mach pressure
 // dissipation. Unlike pstab (a Rhie-Chow term ADDED on top of Roe, never
 // linearized in the implicit -> washed out or destabilizing), AUSM+-up
 // REPLACES the flux: convective part uses upwind Mach splitting (shear
 // dissipation ~|Un| like Roe, preserving the shedding shear layer); the
 // pressure "up" term (Ku*f(M^2)*(pR-pL)) damps the high-freq pressure
 // checkerboard (cap-1.0 blowup mode) while the low-freq von Karman shedding
 // mode (tiny per-cell p jump) is barely touched. Genuinely different from
 // pstab: consistent P5 split flux, not a flux add-on.
 bool use_ausmup = false;  // CFD2D_FLUX_AUSMUP
 double ausmup_ku = 0.75;  // CFD2D_AUSMUP_KU: p_u velocity->pressure coeff (Liou 2006 Eq.26)
 double ausmup_kp = 0.25;  // CFD2D_AUSMUP_KP: M_p pressure->Mach coeff (Liou 2006 Eq.21)
 double ausmup_mcut = 0.3; // CFD2D_AUSMUP_MCUT: Kp coupling floor (avoid M->0 blowup)
 double ausmup_lscale = 1.0; // CFD2D_AUSMUP_LSCALE: over-linearize coupling in implicit (Jacoban more dissipative than residual for inner-solve stability at large shedding amplitude)
 Cons U_inf;
  Prim W_inf;
  double q_inf = 0.0;     // 0.5*rho_inf*U_inf^2
  void init(const GasModel& g, const Freestream& f, const Reference& r,
            bool lam, double Re, double rusanov, bool roe);
};

// physical inviscid normal flux F.n for unit normal (nx,ny)
Cons inviscidNormalFlux(const GasModel& gas, const Prim& w, double nx, double ny);

// numerical inviscid flux F*(L,R) along unit normal (Roe+entropy fix or Rusanov)
Cons numericalInviscidFlux(const Physics& p, const Prim& L, const Prim& R,
                            double nx, double ny);

// physical viscous normal flux Fv.n given primitive gradients at the face
Cons viscousNormalFlux(const Physics& p, double u, double v, double T,
                        double ux, double uy, double vx, double vy,
                        double Tx, double Ty, double nx, double ny);

// ghost primitive state outside a face with outward unit normal (nx,ny)
Prim bcGhostState(const Physics& p, BCType bc, const Prim& Wc, double nx, double ny);

struct Forces { double cl, cd, cmz, pressure_drag, viscous_drag, pressure_lift, viscous_lift; };
// Body force coefficients from wall faces. wcx/wcy = wall face center;
// wSx/wSy = area-weighted outward normal (from fluid cell); wlen = length;
// wp = wall pressure; wtx/wty = full viscous traction t = tau.n (per face, the
// solver computes it from wall gradients). Skin-friction uses the tangential
// component of t only; the normal viscous traction is dropped (per rubric).
Forces computeForceCoeffs(const Physics& p,
    const std::vector<double>& wcx, const std::vector<double>& wcy,
    const std::vector<double>& wSx, const std::vector<double>& wSy,
    const std::vector<double>& wlen,
    const std::vector<double>& wp,
    const std::vector<double>& wtx, const std::vector<double>& wty);

}  // namespace cfd
