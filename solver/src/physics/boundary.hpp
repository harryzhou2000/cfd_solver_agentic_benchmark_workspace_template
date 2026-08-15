#pragma once

// Boundary condition flux functions. Each computes the flux on a boundary
// face and returns it in `flux` (already multiplied by the face area,
// positive = added to the owning cell residual).

#include "common/types.hpp"
#include "physics/gas_model.hpp"

namespace cfd {

// Farfield boundary: characteristic-based (Riemann invariants) treatment.
// U_int: interior conservative state, U_inf: freestream conservative state.
// Subsonic: invariants R+ (from interior) and R- (from freestream) define the
// boundary normal velocity and speed of sound; entropy from the interior
// (outflow) or freestream (inflow). Supersonic outflow: interior state;
// supersonic inflow: freestream state. The resulting boundary state is used
// in a Rusanov flux against the interior state.
void farfield_flux(const double* U_int, const double* U_inf,
                   const Vector3& normal, double area, double gamma,
                   double rusanov_scale, double* flux);

// Inviscid slip wall: zero normal velocity at the wall. The wall flux is
// exactly [0, p*nx, p*ny, 0] * area with p = interior pressure.
void slip_wall_flux(const double* U_int, const Vector3& normal, double area,
                    double gamma, double* flux);

// No-slip adiabatic wall: wall velocity zero, zero normal temperature
// gradient. Computes BOTH parts of the wall flux:
//   inviscid_flux: [0, p*nx, p*ny, 0] * area  (p = interior cell pressure)
//   viscous_flux:  [0, tau_x, tau_y, tau_w*V_t] * area
// where the wall shear on the fluid is
//   tau_w = -mu * V_t / d,  (tau_x, tau_y) = tau_w * (tx, ty),
//   t = (-ny, nx, 0) is the face tangent, V_t = u*tx + v*ty is the SIGNED
//   tangential cell velocity, and d = |face_center - cell_center|.
// The residual assembly ADDS the inviscid part and SUBTRACTS the viscous
// part (res -= fv), same as for interior viscous faces.
//
// NOTE: the cell center and face center are required for the wall distance;
// they are not part of the spec's sketch signature, so they were added.
void noslip_adiabatic_wall_flux(const double* U_cell, const double* U_face,
                                const Vector3& cell_center,
                                const Vector3& face_center,
                                const Vector3& normal, double area,
                                double gamma, double R, double Pr, double mu,
                                double* inviscid_flux, double* viscous_flux);

}  // namespace cfd
