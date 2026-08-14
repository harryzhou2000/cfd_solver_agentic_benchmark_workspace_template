#include "physics/Gas.hpp"

#include <algorithm>

namespace cfds {

namespace {

// Positivity protection for flux evaluations: transiently invalid states
// (which can appear inside implicit sweeps before the positivity-safe update
// applies) are clamped so the Riemann solver stays well defined.
Primitive safe_primitive(const GasModel& gas, const ConsVec& U) {
  Primitive p;
  p.rho = std::max(U[0], 1e-12);
  if (U[0] > 1e-12) {
    p.u = U[1] / U[0];
    p.v = U[2] / U[0];
  } else {
    // Degenerate (non-positive density) state inside an implicit sweep:
    // treat as a near-vacuum so the Riemann solver stays well defined.
    p.u = 0.0;
    p.v = 0.0;
  }
  const double ke = 0.5 * (p.u * p.u + p.v * p.v);
  p.p = (gas.gamma - 1.0) * (U[3] - p.rho * ke);
  if (!(p.p > 1e-12)) p.p = 1e-12;
  return p;
}

}  // namespace

Primitive GasModel::to_primitive(const ConsVec& U) const {
  Primitive p;
  p.rho = U[0];
  p.u = U[1] / U[0];
  p.v = U[2] / U[0];
  const double e = U[3] / U[0] - 0.5 * (p.u * p.u + p.v * p.v);
  p.p = (gamma - 1.0) * U[0] * e;
  return p;
}

ConsVec GasModel::to_conservative(const Primitive& p) const {
  const double ke = 0.5 * (p.u * p.u + p.v * p.v);
  const double E = p.p / ((gamma - 1.0) * p.rho) + ke;
  return {p.rho, p.rho * p.u, p.rho * p.v, p.rho * E};
}

double GasModel::pressure(const ConsVec& U) const {
  const double ke = 0.5 * (U[1] * U[1] + U[2] * U[2]) / (U[0] * U[0]);
  return (gamma - 1.0) * (U[3] - U[0] * ke);
}

GasModel make_gas_model(const CaseConfig& cfg) {
  GasModel gas;
  gas.gamma = cfg.gamma;
  gas.R = cfg.gas_R;
  gas.prandtl = cfg.prandtl;
  gas.viscous = (cfg.mode == "laminar");
  if (gas.viscous) {
    if (!(cfg.reynolds > 0.0))
      fatal("laminar mode requires a positive physics.reynolds");
    const double q = cfg.rho_inf * std::sqrt(cfg.u_inf * cfg.u_inf +
                                             cfg.v_inf * cfg.v_inf);
    gas.mu = q * cfg.reynolds_length / cfg.reynolds;
  }
  return gas;
}

ConsVec euler_flux(const GasModel& gas, const ConsVec& U, const Vec2& n) {
  const Primitive p = safe_primitive(gas, U);
  const double vn = p.u * n[0] + p.v * n[1];
  ConsVec F;
  F[0] = U[0] * vn;
  F[1] = U[1] * vn + p.p * n[0];
  F[2] = U[2] * vn + p.p * n[1];
  F[3] = (U[3] + p.p) * vn;
  return F;
}

ConsVec euler_jacobian_times(const GasModel& gas, const Primitive& p,
                             const Vec2& n, const ConsVec& dU) {
  const double u = p.u, v = p.v;
  const double nx = n[0], ny = n[1];
  const double q = u * nx + v * ny;
  const double v2 = u * u + v * v;
  const double phi = 0.5 * (gas.gamma - 1.0) * v2;
  const double a2 = gas.gamma * p.p / p.rho;
  const double H = a2 / (gas.gamma - 1.0) + 0.5 * v2;
  const double d0 = dU[0], d1 = dU[1], d2 = dU[2], d3 = dU[3];
  const double w0 = nx * d1 + ny * d2;
  ConsVec J;
  J[0] = w0;
  J[1] = (-u * q + phi * nx) * d0 +
         (q - (gas.gamma - 2.0) * u * nx) * d1 +
         (u * ny - (gas.gamma - 1.0) * v * nx) * d2 +
         (gas.gamma - 1.0) * nx * d3;
  J[2] = (-v * q + phi * ny) * d0 +
         (v * nx - (gas.gamma - 1.0) * u * ny) * d1 +
         (q - (gas.gamma - 2.0) * v * ny) * d2 +
         (gas.gamma - 1.0) * ny * d3;
  J[3] = (q * (phi - H)) * d0 +
         (H * nx - (gas.gamma - 1.0) * u * q) * d1 +
         (H * ny - (gas.gamma - 1.0) * v * q) * d2 +
         gas.gamma * q * d3;
  return J;
}

double euler_jacobian_norm(const GasModel& gas, const Primitive& p,
                           const Vec2& n, double lambda, double sign) {
  // Infinity norm of A(p,n) + sign*lambda*I.
  const double u = p.u, v = p.v;
  const double nx = n[0], ny = n[1];
  const double q = u * nx + v * ny;
  const double v2 = u * u + v * v;
  const double phi = 0.5 * (gas.gamma - 1.0) * v2;
  const double a2 = gas.gamma * p.p / p.rho;
  const double H = a2 / (gas.gamma - 1.0) + 0.5 * v2;
  const double r0 = std::abs(sign * lambda) + std::abs(nx) + std::abs(ny);
  const double r1 = std::abs(-u * q + phi * nx) +
                    std::abs(q - (gas.gamma - 2.0) * u * nx + sign * lambda) +
                    std::abs(u * ny - (gas.gamma - 1.0) * v * nx) +
                    std::abs((gas.gamma - 1.0) * nx);
  const double r2 = std::abs(-v * q + phi * ny) +
                    std::abs(v * nx - (gas.gamma - 1.0) * u * ny) +
                    std::abs(q - (gas.gamma - 2.0) * v * ny + sign * lambda) +
                    std::abs((gas.gamma - 1.0) * ny);
  const double r3 = std::abs(q * (phi - H)) +
                    std::abs(H * nx - (gas.gamma - 1.0) * u * q) +
                    std::abs(H * ny - (gas.gamma - 1.0) * v * q) +
                    std::abs(gas.gamma * q + sign * lambda);
  return std::max({r0, r1, r2, r3});
}

void euler_jacobian_matrix(const GasModel& gas, const Primitive& p,
                           const Vec2& n, double mat[4][4]) {
  const double u = p.u, v = p.v;
  const double nx = n[0], ny = n[1];
  const double q = u * nx + v * ny;
  const double v2 = u * u + v * v;
  const double phi = 0.5 * (gas.gamma - 1.0) * v2;
  const double a2 = gas.gamma * p.p / p.rho;
  const double H = a2 / (gas.gamma - 1.0) + 0.5 * v2;
  mat[0][0] = 0.0; mat[0][1] = nx; mat[0][2] = ny; mat[0][3] = 0.0;
  mat[1][0] = -u * q + phi * nx;
  mat[1][1] = q - (gas.gamma - 2.0) * u * nx;
  mat[1][2] = u * ny - (gas.gamma - 1.0) * v * nx;
  mat[1][3] = (gas.gamma - 1.0) * nx;
  mat[2][0] = -v * q + phi * ny;
  mat[2][1] = v * nx - (gas.gamma - 1.0) * u * ny;
  mat[2][2] = q - (gas.gamma - 2.0) * v * ny;
  mat[2][3] = (gas.gamma - 1.0) * ny;
  mat[3][0] = q * (phi - H);
  mat[3][1] = H * nx - (gas.gamma - 1.0) * u * q;
  mat[3][2] = H * ny - (gas.gamma - 1.0) * v * q;
  mat[3][3] = gas.gamma * q;
}

ConsVec rusanov_flux(const GasModel& gas, const ConsVec& UL, const ConsVec& UR,
                     const Vec2& n, double dissipation_scale) {
  const Primitive pl = safe_primitive(gas, UL);
  const Primitive pr = safe_primitive(gas, UR);
  const double vnl = pl.u * n[0] + pl.v * n[1];
  const double vnr = pr.u * n[0] + pr.v * n[1];
  const double al = gas.sound_speed(pl);
  const double ar = gas.sound_speed(pr);
  const double lambda = dissipation_scale *
      std::max(std::abs(vnl) + al, std::abs(vnr) + ar);
  const ConsVec FL = euler_flux(gas, UL, n);
  const ConsVec FR = euler_flux(gas, UR, n);
  ConsVec F;
  for (int c = 0; c < 4; ++c)
    F[c] = 0.5 * (FL[c] + FR[c]) - 0.5 * lambda * (UR[c] - UL[c]);
  return F;
}

ConsVec viscous_flux(const GasModel& gas, const Primitive& pf,
                     const double grad[4][2], const Vec2& n) {
  // grad order: [rho, u, v, p], each [dx, dy].
  const double ux = grad[1][0], uy = grad[1][1];
  const double vx = grad[2][0], vy = grad[2][1];
  const double div = ux + vy;
  const double tau_xx = 2.0 * gas.mu * ux - (2.0 / 3.0) * gas.mu * div;
  const double tau_yy = 2.0 * gas.mu * vy - (2.0 / 3.0) * gas.mu * div;
  const double tau_xy = gas.mu * (uy + vx);

  // Temperature gradient from ideal gas p = rho R T.
  const double drho_dx = grad[0][0], drho_dy = grad[0][1];
  const double dp_dx = grad[3][0], dp_dy = grad[3][1];
  const double T = gas.temperature(pf);
  const double inv_rhoR = 1.0 / (pf.rho * gas.R);
  const double dT_dx = (dp_dx - T * gas.R * drho_dx) * inv_rhoR;
  const double dT_dy = (dp_dy - T * gas.R * drho_dy) * inv_rhoR;
  const double k = gas.mu * gas.cp() / gas.prandtl;
  const double qx = -k * dT_dx;
  const double qy = -k * dT_dy;

  const double tx = tau_xx * n[0] + tau_xy * n[1];
  const double ty = tau_xy * n[0] + tau_yy * n[1];
  const double w = pf.u * tau_xx + pf.v * tau_xy - qx;
  const double z = pf.u * tau_xy + pf.v * tau_yy - qy;
  return {0.0, tx, ty, w * n[0] + z * n[1]};
}

Primitive boundary_ghost(BcType bc, const Primitive& pc, const Vec2& n,
                         const Primitive& far) {
  Primitive g = pc;
  if (bc == BcType::Farfield) {
    g = far;
  } else if (bc == BcType::SlipWall) {
    const double vn = pc.u * n[0] + pc.v * n[1];
    g.u = pc.u - 2.0 * vn * n[0];
    g.v = pc.v - 2.0 * vn * n[1];
  } else if (bc == BcType::NoSlipAdiabaticWall) {
    g.u = -pc.u;
    g.v = -pc.v;
  }
  return g;
}

Primitive farfield_ghost(const GasModel& gas, const Primitive& pc, const Vec2& n,
                         const Primitive& far) {
  const double vn_int = pc.u * n[0] + pc.v * n[1];
  const double vn_far = far.u * n[0] + far.v * n[1];
  const double a_int = gas.sound_speed(pc);
  const double a_far = gas.sound_speed(far);
  const double gm1 = gas.gamma - 1.0;
  const double rplus = vn_int + 2.0 * a_int / gm1;
  const double rminus = vn_far - 2.0 * a_far / gm1;
  const double vn_b = 0.5 * (rplus + rminus);
  double a_b = 0.25 * gm1 * (rplus - rminus);
  a_b = std::max(a_b, 1e-8 * (a_int + a_far));
  // Entropy and tangential velocity from the upstream side.
  const bool outflow = vn_b > 0.0;
  const Primitive& src = outflow ? pc : far;
  const double s = src.p / std::pow(src.rho, gas.gamma);  // p/rho^gamma
  const double vn_src = src.u * n[0] + src.v * n[1];
  const double ut = src.u - vn_src * n[0];
  const double vt = src.v - vn_src * n[1];
  Primitive g;
  g.rho = std::pow(a_b * a_b / (gas.gamma * s), 1.0 / gm1);
  g.p = s * std::pow(g.rho, gas.gamma);
  g.u = ut + vn_b * n[0];
  g.v = vt + vn_b * n[1];
  return g;
}

ConsVec boundary_inviscid_flux(BcType bc, const GasModel& gas,
                               const Primitive& p_face, const Vec2& n,
                               const Primitive& far, double dissipation_scale) {
  if (bc == BcType::NoSlipAdiabaticWall) {
    // No-slip adiabatic wall: the velocity vanishes at the wall and the
    // momentum balance is the pure pressure flux (0, p n, 0); the wall shear
    // is imposed separately by the mirrored-ghost viscous stress. A
    // mirrored-velocity Rusanov state would add a spurious O(lambda rho u)
    // momentum sink at the wall (the mirror flips the full velocity, not
    // just the normal component), which over-dissipates the boundary layer.
    return {0.0, p_face.p * n[0], p_face.p * n[1], 0.0};
  }
  if (bc == BcType::SlipWall) {
    // Mirror the wall-normal velocity through the wall and evaluate the full
    // Rusanov flux between the face state and the ghost state. At a tangent
    // state this reduces exactly to the pressure-only flux (0, p n, 0), but
    // during the transient the numerical dissipation damps the wall-normal
    // velocity component; the bare pressure-only flux has no such damping and
    // destabilizes the wall layer (spurious backflow / stagnation overshoot
    // at the leading/trailing-edge sliver cells).
    const Primitive g = boundary_ghost(bc, p_face, n, far);
    const ConsVec UL = gas.to_conservative(p_face);
    const ConsVec UG = gas.to_conservative(g);
    return rusanov_flux(gas, UL, UG, n, dissipation_scale);
  }
  if (bc == BcType::Farfield) {
    const ConsVec UC = gas.to_conservative(p_face);
    const Primitive ghost = farfield_ghost(gas, p_face, n, far);
    const ConsVec UF = gas.to_conservative(ghost);
    return rusanov_flux(gas, UC, UF, n, dissipation_scale);
  }
  return ConsVec{0.0, 0.0, 0.0, 0.0};
}

ConsVec wall_viscous_flux(const GasModel& gas, const Primitive& pc,
                          const Vec2& n, double d_eff) {
  // Adiabatic no-slip wall: velocity zero at the wall, zero normal heat flux.
  // Mirrored-ghost wall gradient: the ghost cell at distance d across the
  // wall has zero velocity, so du/dn = -u_cell/d along the face normal.
  // This is O(1/d) and stays well-conditioned at the degenerate LE/TE sliver
  // cells (V ~ 1e-9), where the cell-centered gradient is O(1/V).
  const double inv = 1.0 / std::max(d_eff, 1e-14);
  const double ux = -pc.u * n[0] * inv;
  const double uy = -pc.u * n[1] * inv;
  const double vx = -pc.v * n[0] * inv;
  const double vy = -pc.v * n[1] * inv;
  const double div = ux + vy;
  const double tau_xx = 2.0 * gas.mu * ux - (2.0 / 3.0) * gas.mu * div;
  const double tau_yy = 2.0 * gas.mu * vy - (2.0 / 3.0) * gas.mu * div;
  const double tau_xy = gas.mu * (uy + vx);
  const double tx = tau_xx * n[0] + tau_xy * n[1];
  const double ty = tau_xy * n[0] + tau_yy * n[1];
  // Energy flux at the wall: (tau V - q).n with V=0 and q.n=0 (adiabatic).
  return {0.0, tx, ty, 0.0};
}

double wall_shear_coefficient(const GasModel& gas, const Primitive& pc,
                              const Vec2& n_body, double d_eff,
                              const Vec2& flow_dir, Vec2& tangent_out) {
  // n_body points from the body into the fluid. Tangent aligned so that its
  // component along the flow direction is positive.
  Vec2 t{-(n_body[1]), n_body[0]};
  if (dot(t, flow_dir) < 0.0) {
    t[0] = -t[0];
    t[1] = -t[1];
  }
  // Traction on the body: t_body = tau . n_body (stress on the fluid is
  // tau . n_body; the body feels the opposite, but the tangential magnitude
  // is what matters for the skin-friction coefficient).
  // Mirrored-ghost wall gradient (same construction as wall_viscous_flux).
  // n_body points into the fluid, opposite the face normal used in
  // wall_viscous_flux, so the wall-normal derivative du/dn = +u_cell/d here.
  const double inv = 1.0 / std::max(d_eff, 1e-14);
  const double ux = +pc.u * n_body[0] * inv;
  const double uy = +pc.u * n_body[1] * inv;
  const double vx = +pc.v * n_body[0] * inv;
  const double vy = +pc.v * n_body[1] * inv;
  const double div = ux + vy;
  const double tau_xx = 2.0 * gas.mu * ux - (2.0 / 3.0) * gas.mu * div;
  const double tau_yy = 2.0 * gas.mu * vy - (2.0 / 3.0) * gas.mu * div;
  const double tau_xy = gas.mu * (uy + vx);
  const double tx = tau_xx * n_body[0] + tau_xy * n_body[1];
  const double ty = tau_xy * n_body[0] + tau_yy * n_body[1];
  tangent_out = t;
  return tx * t[0] + ty * t[1];
}

}  // namespace cfds
