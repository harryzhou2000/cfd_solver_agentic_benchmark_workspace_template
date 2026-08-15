#include "physics/inviscid_flux.hpp"

#include <algorithm>
#include <cmath>

namespace cfd {
namespace {

// Euler flux through a face, F(U).n (not multiplied by area).
void euler_flux_vector(const double* U, const PrimitiveState& prim,
                       const Vector3& n, double* F) {
  const double vn = prim.u * n.x + prim.v * n.y;
  F[0] = prim.rho * vn;
  F[1] = prim.rho * prim.u * vn + prim.p * n.x;
  F[2] = prim.rho * prim.v * vn + prim.p * n.y;
  F[3] = (U[3] + prim.p) * vn;
}

}  // namespace

void rusanov_flux(const double* U_L, const double* U_R,
                  const Vector3& normal, double area, double gamma,
                  double* flux, double rusanov_scale, double R) {
  const PrimitiveState primL = cons_to_prim(U_L, gamma, R);
  const PrimitiveState primR = cons_to_prim(U_R, gamma, R);

  const double vnL = primL.u * normal.x + primL.v * normal.y;
  const double vnR = primR.u * normal.x + primR.v * normal.y;
  const double lambda =
      rusanov_scale * std::max(std::fabs(vnL) + primL.a,
                               std::fabs(vnR) + primR.a);

  double FL[NVARS], FR[NVARS];
  euler_flux_vector(U_L, primL, normal, FL);
  euler_flux_vector(U_R, primR, normal, FR);

  for (int k = 0; k < NVARS; ++k)
    flux[k] = (0.5 * (FL[k] + FR[k]) - 0.5 * lambda * (U_R[k] - U_L[k])) * area;
}

void inviscid_flux_vector(const double* U, const Vector3& normal,
                          double gamma, double* flux) {
  // R is unused by the euler flux evaluation itself (only U and prim matter);
  // pass any positive value.
  const PrimitiveState prim = cons_to_prim(U, gamma, 1.0);
  euler_flux_vector(U, prim, normal, flux);
}

void euler_flux_jacobian(const double* U, const Vector3& normal, double gamma,
                         double* out) {
  const PrimitiveState prim = cons_to_prim(U, gamma, 1.0);
  const double u = prim.u, v = prim.v;
  const double q2 = u * u + v * v;
  const double phi = 0.5 * (gamma - 1.0) * q2;
  const double H = (U[3] + prim.p) / prim.rho;  // total enthalpy
  const double nx = normal.x, ny = normal.y;
  const double vn = u * nx + v * ny;
  // Row 1
  out[0] = 0.0;
  out[1] = nx;
  out[2] = ny;
  out[3] = 0.0;
  // Row 2
  out[4] = -u * vn + phi * nx;
  out[5] = vn - (gamma - 2.0) * u * nx;
  out[6] = u * ny - (gamma - 1.0) * v * nx;
  out[7] = (gamma - 1.0) * nx;
  // Row 3
  out[8] = -v * vn + phi * ny;
  out[9] = v * nx - (gamma - 1.0) * u * ny;
  out[10] = vn - (gamma - 2.0) * v * ny;
  out[11] = (gamma - 1.0) * ny;
  // Row 4
  out[12] = vn * (phi - H);
  out[13] = nx * (H - (gamma - 1.0) * u * u) - (gamma - 1.0) * u * v * ny;
  out[14] = ny * (H - (gamma - 1.0) * v * v) - (gamma - 1.0) * u * v * nx;
  out[15] = gamma * vn;
}

}  // namespace cfd
