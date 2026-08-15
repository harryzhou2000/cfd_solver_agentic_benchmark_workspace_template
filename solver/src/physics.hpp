#pragma once

#include "config.hpp"
#include "partition.hpp"

#include <array>
#include <cmath>
#include <cmath>
#include <vector>

namespace cfd {

// Effective Rusanov dissipation wave speed with a low-Mach scaling: the
// acoustic part is reduced by min(1, max(M, 0.05)) so that the dissipation
// does not dominate the advection at low Mach numbers (which otherwise
// leaves smooth enthalpy modes in the near-null space of the residual
// Jacobian and stalls Newton convergence).  At M >= 1 the full acoustic
// speed is retained for shock resolution.
inline double rusanov_wavespeed(double u, double v, double vn, double a) {
  const double M = std::sqrt(u * u + v * v) / a;
  const double mscale = std::min(1.0, std::max(M, 0.05));
  return std::abs(vn) + a * mscale;
}

constexpr int NVAR = 4;  // rho, rhou, rhov, rhoE

// Calorically perfect gas relations (nondimensional variables).
struct Gas {
  double gamma = 1.4;
  double R = 1.0;
  double prandtl = 0.72;

  double pressure(double rho, double rhou, double rhov, double rhoE) const;
  double temperature(double rho, double p) const { return p / (rho * R); }
  double sound_speed(double rho, double p) const { return std::sqrt(gamma * p / rho); }
};

// Boundary-state primitives (rho, u, v, p) evaluated at a boundary face from
// the adjacent cell state and the BC type.
void boundary_primitives(BcType bc, double rho, double u, double v, double p,
                         double nx, double ny,
                         const std::array<double, 4>& q_inf,
                         double gamma, std::array<double, 4>& qb);

// Inviscid (Rusanov / local Lax-Friedrichs) flux in direction n (per unit
// face length).  U_L/U_R are conservative states; returns F[4] such that the
// cell residual contribution is -F*len for the L cell.
void inviscid_flux(const std::array<double, 4>& UL, const std::array<double, 4>& UR,
                   double nx, double ny, double gamma, double diss_scale,
                   std::array<double, 4>& F);

// Inviscid flux Jacobian A = dF(U)/dU in direction n (row-major 4x4),
// evaluated from primitive state q = [rho, u, v, p].
void euler_flux_jacobian(const std::array<double, 4>& q, double nx, double ny,
                         double gamma, std::array<double, 16>& A);

// Viscous flux in direction n (per unit face length): Newtonian stress +
// Fourier heat flux.  Primitives and gradients are averaged on the face.
// grad arrays are [du/dx, du/dy, dv/dx, dv/dy, dT/dx, dT/dy].
void viscous_flux(const std::array<double, 4>& qL, const std::array<double, 4>& qR,
                  const std::array<double, 6>& gL, const std::array<double, 6>& gR,
                  double nx, double ny, double mu, double k, double gamma,
                  std::array<double, 4>& Fv);

// Viscous wall flux for a no-slip adiabatic wall from the cell gradient and
// the cell primitive state (wall velocity = 0, adiabatic: q_wall = 0).
void viscous_wall_flux(const std::array<double, 4>& q,
                       const std::array<double, 6>& grad,
                       double nx, double ny, double mu, double gamma,
                       std::array<double, 4>& Fv, double& tau_tangential,
                       double* tx_out = nullptr, double* ty_out = nullptr);

}  // namespace cfd
