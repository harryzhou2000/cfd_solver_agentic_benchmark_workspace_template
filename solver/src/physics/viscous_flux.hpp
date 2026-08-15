#pragma once

// Viscous flux for interior faces (laminar mode).

#include "common/types.hpp"

namespace cfd {

// Computes the viscous flux contribution across an interior face.
// U_L, U_R: conservative states of the left and right cells
// cell_center_L/R, face_center: coordinates for the gradient length scale
// normal: unit face normal pointing left -> right
// area: face length
// mu: dynamic viscosity (constant)
// gamma, R, Pr: gas properties (k = mu*cp/Pr, cp = gamma*R/(gamma-1))
// flux: output, viscous flux * area (positive = added to left cell residual,
//       subtracted from right).
//
// Gradient approximation (Phase 3): dq/dn ~= (q_R - q_L) / |(c_R - c_L).n|,
// with the full gradient taken as dq/dn * n (tangential variations ignored,
// conservation across the face preserved).
void viscous_flux(const double* U_L, const double* U_R,
                  const Vector3& cell_center_L, const Vector3& cell_center_R,
                  const Vector3& face_center, const Vector3& normal,
                  double area, double mu, double gamma, double R, double Pr,
                  double* flux);

// Second-order viscous flux for interior faces: the face gradients are the
// average of the two cell (already limited) PRIMITIVE gradients
// ([dRho/dx, dRho/dy, du/dx, du/dy, dv/dx, dv/dy, dp/dx, dp/dy] per cell),
// the face velocity/temperature follow from the averaged cell primitives,
// and the temperature gradient follows from the averaged primitive
// gradients (T = p/(rho*R)). The full stress tensor and heat flux are
// evaluated from the face gradient (no normal-only approximation).
// grad_L, grad_R: NVARS*2 each (variable-major, as compute_gradients).
void viscous_flux_gradient(const double* U_L, const double* U_R,
                           const double* grad_L, const double* grad_R,
                           const Vector3& normal, double area, double mu,
                           double gamma, double R, double Pr, double* flux);

}  // namespace cfd
