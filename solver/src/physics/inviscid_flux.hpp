#pragma once

// Inviscid flux functions for the 2D compressible solver.

#include "common/types.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

// Computes the Rusanov (Local Lax-Friedrichs) flux across a face.
// U_L, U_R: conservative states of the left and right cells
// normal: unit face normal pointing left -> right
// area: face length (2D)
// gamma: specific heat ratio
// flux: output, the flux * area. Positive contribution is ADDED to the left
//       cell residual and SUBTRACTED from the right cell residual.
// rusanov_scale: multiplier for the dissipation term Lambda_max
//                (case run_control.rusanov_dissipation_scale, default 1.0)
// R: specific gas constant (affects only the intermediate temperature, which
//    is not used by the inviscid flux; default 1.0).
void rusanov_flux(const double* U_L, const double* U_R,
                  const Vector3& normal, double area, double gamma,
                  double* flux, double rusanov_scale = 1.0, double R = 1.0);

// The physical inviscid flux vector F(U).n (no dissipation term). Used for
// boundary fluxes and force computation.
// flux: output, F(U).n (NOT multiplied by area).
void inviscid_flux_vector(const double* U, const Vector3& normal,
                          double gamma, double* flux);

// The Euler flux Jacobian d(F.n)/dU (row-major 4x4) for the conservative
// state U along the unit normal. Used by the block LU-SGS implicit operator.
// out: output, 16 doubles (row-major).
void euler_flux_jacobian(const double* U, const Vector3& normal, double gamma,
                         double* out);

}  // namespace cfd
