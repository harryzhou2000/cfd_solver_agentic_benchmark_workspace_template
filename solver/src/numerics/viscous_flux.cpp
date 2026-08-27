#include "numerics/viscous_flux.h"

namespace cns2d {

ConsVec viscousNormalFlux(const PerfectGas &gas, Real mu, const PrimVec &W_face,
                          const ViscousGradients &g, Vec2 n) {
  const Real div = g.grad_u.x + g.grad_v.y;
  const Real two_thirds_mu_div = (2.0 / 3.0) * mu * div;

  const Real tau_xx = 2.0 * mu * g.grad_u.x - two_thirds_mu_div;
  const Real tau_yy = 2.0 * mu * g.grad_v.y - two_thirds_mu_div;
  const Real tau_xy = mu * (g.grad_u.y + g.grad_v.x);

  const Real k = gas.conductivity(mu);
  // q = -k grad(T); the energy flux carries -q.n plus the work of the stresses.
  const Real q_n = -k * (g.grad_T.x * n.x + g.grad_T.y * n.y);

  const Real u = W_face[kPrimU];
  const Real v = W_face[kPrimV];

  ConsVec f{};
  f[kRho] = 0.0;
  f[kRhoU] = tau_xx * n.x + tau_xy * n.y;
  f[kRhoV] = tau_xy * n.x + tau_yy * n.y;
  f[kRhoE] = u * (tau_xx * n.x + tau_xy * n.y) + v * (tau_xy * n.x + tau_yy * n.y) - q_n;
  return f;
}

Vec2 wallShearTraction(Real mu, const ViscousGradients &g, Vec2 n) {
  const Real div = g.grad_u.x + g.grad_v.y;
  const Real two_thirds_mu_div = (2.0 / 3.0) * mu * div;
  const Real tau_xx = 2.0 * mu * g.grad_u.x - two_thirds_mu_div;
  const Real tau_yy = 2.0 * mu * g.grad_v.y - two_thirds_mu_div;
  const Real tau_xy = mu * (g.grad_u.y + g.grad_v.x);

  // Full viscous traction on the surface with outward normal n.
  const Vec2 traction{tau_xx * n.x + tau_xy * n.y, tau_xy * n.x + tau_yy * n.y};
  // Remove the normal component: skin friction is the TANGENTIAL part only.
  const Real normal_part = dot(traction, n);
  return {traction.x - normal_part * n.x, traction.y - normal_part * n.y};
}

}  // namespace cns2d

