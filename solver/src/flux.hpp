#pragma once

#include <array>

#include "case.hpp"
#include "common.hpp"

namespace cfd {

using Mat44 = std::array<std::array<double, 4>, 4>;

// Rusanov / local-Lax-Friedrichs numerical flux through a face with unit
// normal (nx, ny) pointing outward from the left cell.  Returns the flux
// vector (not area-weighted); lambdaMax holds the max wave speed used.
State rusanovFlux(const State& UL, const State& UR, const GasModel& gas, double nx,
                  double ny, double dissScale, double& lambdaMax);

// Roe flux with Harten-Yee entropy fix (epsilon=0.1).  Returns the flux
// vector (not area-weighted); lambdaMax holds the max wave speed.
State roeFlux(const State& UL, const State& UR, const GasModel& gas, double nx,
              double ny, double dissScale, double& lambdaMax);

// Analytic Euler flux Jacobian A_n(U) for direction (nx, ny).
Mat44 eulerJacobian(const State& U, const GasModel& gas, double nx, double ny);

// Matrix-free application of the flux Jacobian: A_n(U) * dU (used by LU-SGS).
inline State jacobianApply(const State& U, const State& dU, const GasModel& gas,
                           double nx, double ny) {
  State f0 = eulerFlux(U, gas, nx, ny);
  State fp = eulerFlux({U[0] + dU[0], U[1] + dU[1], U[2] + dU[2], U[3] + dU[3]},
                       gas, nx, ny);
  return {fp[0] - f0[0], fp[1] - f0[1], fp[2] - f0[2], fp[3] - f0[3]};
}

// Characteristic-based farfield ghost primitive state (weak boundary
// condition via Riemann invariants normal to the boundary).
Prim farfieldGhost(const Prim& Wint, const Freestream& fs, const GasModel& gas,
                   double nx, double ny);

// Viscous numerical flux vector (not area-weighted) through a face with unit
// normal (nx, ny).  Inputs are the face velocity/temperature gradients and
// the face velocity values (for the energy flux).
State viscousFlux(double u, double v, double ux, double uy, double vx, double vy,
                  double Tx, double Ty, double mu, double kappa, double nx, double ny);

// Stress tensor components at a point given velocity gradients.
inline void stress(double ux, double uy, double vx, double vy, double mu,
                   double& txx, double& txy, double& tyy) {
  double div = ux + vy;
  txx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  tyy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  txy = mu * (uy + vx);
}

// 4x4 Gaussian elimination with partial pivoting: solve A x = b.
State solve44(const Mat44& A, const State& b);

}  // namespace cfd
