// Phase 4: laminar viscous flux implementation (see viscous.h).

#include "viscous.h"

namespace cfd {

void viscous_flux_dot_normal(const double grad_u[2], const double grad_v[2],
                             const double grad_T[2], double u_face,
                             double v_face, const Vec2& normal, double mu,
                             double cp, double prandtl, double out[4]) {
  const double du_dx = grad_u[0];
  const double du_dy = grad_u[1];
  const double dv_dx = grad_v[0];
  const double dv_dy = grad_v[1];
  const double dT_dx = grad_T[0];
  const double dT_dy = grad_T[1];

  // Stress tensor (2D flow, 3D divergence for the volumetric part).
  const double div = du_dx + dv_dy;
  const double tau_xx = mu * (2.0 * du_dx - (2.0 / 3.0) * div);
  const double tau_yy = mu * (2.0 * dv_dy - (2.0 / 3.0) * div);
  const double tau_xy = mu * (du_dy + dv_dx);

  // Thermal conductivity: k = mu * cp / Pr.
  const double k = prandtl > 0.0 ? mu * cp / prandtl : 0.0;

  // Stress vector and heat flux, contracted with the area-vector components.
  const double fx = tau_xx * normal.x + tau_xy * normal.y;
  const double fy = tau_xy * normal.x + tau_yy * normal.y;
  const double fe = (tau_xx * u_face + tau_xy * v_face + k * dT_dx) *
                        normal.x +
                    (tau_xy * u_face + tau_yy * v_face + k * dT_dy) *
                        normal.y;

  // F_v . n = -(stress flux): add to the left residual, subtract from the
  // right residual (same sign convention as the inviscid flux).
  out[0] = 0.0;
  out[1] = -fx;
  out[2] = -fy;
  out[3] = -fe;
}

}  // namespace cfd
