#include "physics/viscous_flux.hpp"

#include <cmath>

#include "physics/gas_model.hpp"

namespace cfd {

void viscous_flux(const double* U_L, const double* U_R,
                  const Vector3& cell_center_L, const Vector3& cell_center_R,
                  const Vector3& face_center, const Vector3& normal,
                  double area, double mu, double gamma, double R, double Pr,
                  double* flux) {
  for (int k = 0; k < NVARS; ++k) flux[k] = 0.0;
  (void)face_center;  // Phase 3 gradient uses only the center-to-center direction
  if (!(mu > 0.0) || !(area > 0.0)) return;

  const PrimitiveState primL = cons_to_prim(U_L, gamma, R);
  const PrimitiveState primR = cons_to_prim(U_R, gamma, R);

  // Face-averaged velocity (no reconstruction yet); the heat flux uses the
  // temperature gradient directly, so no face temperature is needed.
  const double u_f = 0.5 * (primL.u + primR.u);
  const double v_f = 0.5 * (primL.v + primR.v);

  // Normal distance between cell centers.
  const Vector3 dc = cell_center_R - cell_center_L;
  const double dLR = std::fabs(dc.x * normal.x + dc.y * normal.y);
  if (!(dLR > 0.0)) return;  // degenerate edge: no gradient information

  // Gradient along the normal (full gradient approximated as dq/dn * n).
  const double du_dn = (primR.u - primL.u) / dLR;
  const double dv_dn = (primR.v - primL.v) / dLR;
  const double dT_dn = (primR.T - primL.T) / dLR;

  const double nx = normal.x;
  const double ny = normal.y;
  const double du_dx = du_dn * nx;
  const double du_dy = du_dn * ny;
  const double dv_dx = dv_dn * nx;
  const double dv_dy = dv_dn * ny;
  const double div = du_dx + dv_dy;

  // Newtonian stress tensor.
  const double two_thirds = 2.0 / 3.0;
  const double tau_xx = 2.0 * mu * du_dx - two_thirds * mu * div;
  const double tau_yy = 2.0 * mu * dv_dy - two_thirds * mu * div;
  const double tau_xy = mu * (du_dy + dv_dx);

  // Heat conduction: q = -k * grad(T), k = mu*cp/Pr, cp = gamma*R/(gamma-1).
  const double cp = gamma * R / (gamma - 1.0);
  const double k = mu * cp / Pr;
  const double q_n = -k * dT_dn;  // heat flux along the normal (q.n)

  flux[0] = 0.0;
  flux[1] = (tau_xx * nx + tau_xy * ny) * area;
  flux[2] = (tau_xy * nx + tau_yy * ny) * area;
  flux[3] = ((tau_xx * u_f + tau_xy * v_f) * nx +
             (tau_xy * u_f + tau_yy * v_f) * ny - q_n) *
            area;
}

void viscous_flux_gradient(const double* U_L, const double* U_R,
                           const double* grad_L, const double* grad_R,
                           const Vector3& normal, double area, double mu,
                           double gamma, double R, double Pr, double* flux) {
  for (int k = 0; k < NVARS; ++k) flux[k] = 0.0;
  if (!(mu > 0.0) || !(area > 0.0)) return;

  const PrimitiveState primL = cons_to_prim(U_L, gamma, R);
  const PrimitiveState primR = cons_to_prim(U_R, gamma, R);
  const double u_f = 0.5 * (primL.u + primR.u);
  const double v_f = 0.5 * (primL.v + primR.v);

  // Face gradient = average of the left/right cell gradients (primitive
  // variables). Temperature gradient from T = p/(rho*R).
  const double du_dx = 0.5 * (grad_L[2] + grad_R[2]);
  const double du_dy = 0.5 * (grad_L[3] + grad_R[3]);
  const double dv_dx = 0.5 * (grad_L[4] + grad_R[4]);
  const double dv_dy = 0.5 * (grad_L[5] + grad_R[5]);
  const double dTdx_L =
      (grad_L[6] * primL.rho - primL.p * grad_L[0]) / (primL.rho * primL.rho * R);
  const double dTdy_L =
      (grad_L[7] * primL.rho - primL.p * grad_L[1]) / (primL.rho * primL.rho * R);
  const double dTdx_R =
      (grad_R[6] * primR.rho - primR.p * grad_R[0]) / (primR.rho * primR.rho * R);
  const double dTdy_R =
      (grad_R[7] * primR.rho - primR.p * grad_R[1]) / (primR.rho * primR.rho * R);
  const double dT_dx = 0.5 * (dTdx_L + dTdx_R);
  const double dT_dy = 0.5 * (dTdy_L + dTdy_R);

  const double nx = normal.x;
  const double ny = normal.y;
  const double div = du_dx + dv_dy;
  const double two_thirds = 2.0 / 3.0;
  const double tau_xx = 2.0 * mu * du_dx - two_thirds * mu * div;
  const double tau_yy = 2.0 * mu * dv_dy - two_thirds * mu * div;
  const double tau_xy = mu * (du_dy + dv_dx);

  // Heat conduction: q = -k * grad(T), k = mu*cp/Pr.
  const double cp = gamma * R / (gamma - 1.0);
  const double k = mu * cp / Pr;
  const double q_n = -k * (dT_dx * nx + dT_dy * ny);  // q.n

  flux[0] = 0.0;
  flux[1] = (tau_xx * nx + tau_xy * ny) * area;
  flux[2] = (tau_xy * nx + tau_yy * ny) * area;
  flux[3] = ((tau_xx * u_f + tau_xy * v_f) * nx +
             (tau_xy * u_f + tau_yy * v_f) * ny - q_n) *
            area;
}

}  // namespace cfd
