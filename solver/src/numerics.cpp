#include "numerics.hpp"

#include <algorithm>
#include <numeric>

namespace cfd {
namespace {

constexpr double kTiny = 1e-14;

// Unit-normal velocity and sound speed from a state.
inline double un(const GasState& gs, double nx, double ny) {
  return gs.u * nx + gs.v * ny;
}

// Reflect a gradient across a face with normal (nx,ny).
inline void reflect_grad(const double g[2], double nx, double ny, double out[2]) {
  double gn = g[0] * nx + g[1] * ny;
  out[0] = g[0] - 2.0 * gn * nx;
  out[1] = g[1] - 2.0 * gn * ny;
}

// Face gradient from cell/ghost values and gradients (diamond-path formula).
inline void face_grad_avg(const double g[2], const double gg[2], double phi_i, double phi_g,
                          const double dx, const double dy, double out[2]) {
  double gm0 = 0.5 * (g[0] + gg[0]);
  double gm1 = 0.5 * (g[1] + gg[1]);
  double dist2 = dx * dx + dy * dy;
  double corr = dist2 > kTiny ? (phi_g - phi_i - (gm0 * dx + gm1 * dy)) / dist2 : 0.0;
  out[0] = gm0 + corr * dx;
  out[1] = gm1 + corr * dy;
}

// ---------------------------------------------------------------------------
// Limited face state (conservative) for cell c at face f.
// Falls back to first order when the limited state is nonphysical.
// ---------------------------------------------------------------------------
inline Vec4 limited_face_state(const LocalMesh& mesh, const std::vector<Vec4>& U,
                               const std::vector<PrimGrad>& grads,
                               const std::vector<Limiters>& lmt, int c, int f,
                               double gamma, double rho_eps, double p_eps) {
  static int first_order = -1;
  if (first_order < 0) {
    const char* e = std::getenv("CFD_FIRST_ORDER");
    first_order = e && std::string(e) == "1" ? 1 : 0;
  }
  if (first_order) {
    GasState gs = cons2prim(U[c], gamma);
    return prim2cons(gs.rho, gs.u, gs.v, gs.p, gamma);
  }
  const auto& cell = mesh.cells[c];
  const auto& face = mesh.faces[f];
  GasState gs = cons2prim(U[c], gamma);
  double dx = face.fx - cell.cx;
  double dy = face.fy - cell.cy;
  const PrimGrad& g = grads[c];
  const Limiters& lim = lmt[c];

  double rho_f = gs.rho + lim.lr * (g.gr[0] * dx + g.gr[1] * dy);
  double u_f = gs.u + lim.lu * (g.gu[0] * dx + g.gu[1] * dy);
  double v_f = gs.v + lim.lv * (g.gv[0] * dx + g.gv[1] * dy);
  double p_f = gs.p + lim.lp * (g.gp[0] * dx + g.gp[1] * dy);

  if (rho_f <= rho_eps || p_f <= p_eps) {
    // Positivity fallback: first-order state at this face.
    return prim2cons(gs.rho, gs.u, gs.v, gs.p, gamma);
  }
  return prim2cons(rho_f, u_f, v_f, p_f, gamma);
}

// Reconstruct face state for a cell without limiting (used by limiter eval).
inline void unlimited_face_prims(const LocalMesh& mesh, const std::vector<Vec4>& U,
                                 const std::vector<PrimGrad>& grads, int c, int f,
                                 double gamma, double& rho_f, double& u_f, double& v_f,
                                 double& p_f) {
  const auto& cell = mesh.cells[c];
  const auto& face = mesh.faces[f];
  GasState gs = cons2prim(U[c], gamma);
  double dx = face.fx - cell.cx;
  double dy = face.fy - cell.cy;
  const PrimGrad& g = grads[c];
  rho_f = gs.rho + g.gr[0] * dx + g.gr[1] * dy;
  u_f = gs.u + g.gu[0] * dx + g.gu[1] * dy;
  v_f = gs.v + g.gv[0] * dx + g.gv[1] * dy;
  p_f = gs.p + g.gp[0] * dx + g.gp[1] * dy;
}

// ---------------------------------------------------------------------------
// Boundary ghost state construction.
//   bc  : boundary condition kind
//   gs  : interior cell state
//   nx,ny : face normal (outward from the fluid domain)
// ---------------------------------------------------------------------------
inline Vec4 boundary_ghost_state(BcKind bc, const GasState& gs, const CaseConfig& cfg,
                                 double nx, double ny) {
  switch (bc) {
    case BcKind::Farfield: {
      return prim2cons(cfg.rho_inf, cfg.u_inf, cfg.v_inf, cfg.p_inf, cfg.gamma);
    }
    case BcKind::SlipWall: {
      double un = gs.u * nx + gs.v * ny;
      double ug = gs.u - 2.0 * un * nx;
      double vg = gs.v - 2.0 * un * ny;
      return prim2cons(gs.rho, ug, vg, gs.p, cfg.gamma);
    }
    case BcKind::NoSlipAdiabaticWall: {
      return prim2cons(gs.rho, -gs.u, -gs.v, gs.p, cfg.gamma);
    }
  }
  fatal("unhandled boundary condition");
}

// Gradient of the ghost state at a boundary face.
inline void boundary_ghost_grad(BcKind bc, const PrimGrad& g, double nx, double ny,
                                PrimGrad& out) {
  out.gr[0] = 0.0; out.gr[1] = 0.0;
  out.gu[0] = 0.0; out.gu[1] = 0.0;
  out.gv[0] = 0.0; out.gv[1] = 0.0;
  out.gp[0] = 0.0; out.gp[1] = 0.0;
  if (bc == BcKind::Farfield) return;
  reflect_grad(g.gr, nx, ny, out.gr);
  reflect_grad(g.gp, nx, ny, out.gp);
  reflect_grad(g.gu, nx, ny, out.gu);
  reflect_grad(g.gv, nx, ny, out.gv);
}

}  // namespace

// ---------------------------------------------------------------------------
// Rusanov / local Lax-Friedrichs flux
// ---------------------------------------------------------------------------
Vec4 rusanov_flux(const Vec4& UL, const Vec4& UR, double nx, double ny, double gamma,
                  double scale) {
  GasState L = cons2prim(UL, gamma);
  GasState R = cons2prim(UR, gamma);
  double unL = L.u * nx + L.v * ny;
  double unR = R.u * nx + R.v * ny;

  Vec4 FL{UL[1] * nx + UL[2] * ny,
          (UL[1] * unL + L.p * nx),
          (UL[2] * unL + L.p * ny),
          (UL[3] + L.p) * unL};
  Vec4 FR{UR[1] * nx + UR[2] * ny,
          (UR[1] * unR + R.p * nx),
          (UR[2] * unR + R.p * ny),
          (UR[3] + R.p) * unR};

  double lambda = std::max(std::abs(unL) + L.a, std::abs(unR) + R.a);
  return 0.5 * (FL + FR) - 0.5 * scale * lambda * (UR - UL);
}

// Physical (Euler) flux along a face with unit normal (nx, ny).
inline Vec4 euler_flux(const Vec4& U, double nx, double ny, double gamma) {
  if (U[0] <= 0.0) return {0,0,0,0};  // non-physical: return zero flux
  GasState gs = cons2prim(U, gamma);
  if (!std::isfinite(gs.p) || gs.p <= 0.0) return {0,0,0,0};
  double un = gs.u * nx + gs.v * ny;
  return {U[1] * nx + U[2] * ny, U[1] * un + gs.p * nx, U[2] * un + gs.p * ny,
          (U[3] + gs.p) * un};
}

// ---------------------------------------------------------------------------
// Roe flux with Harten-Yee entropy fix
// ---------------------------------------------------------------------------
Vec4 roe_flux(const Vec4& UL, const Vec4& UR, double nx, double ny, double gamma, double eps) {
  GasState L = cons2prim(UL, gamma);
  GasState R = cons2prim(UR, gamma);
  double unL = L.u * nx + L.v * ny;
  double unR = R.u * nx + R.v * ny;

  // Rotated velocities along the face frame (n, t).
  double qnL = unL, qnR = unR;
  double tx = -ny, ty = nx;
  double qtL = L.u * tx + L.v * ty;
  double qtR = R.u * tx + R.v * ty;
  double HL = (UL[3] + L.p) / L.rho;
  double HR = (UR[3] + R.p) / R.rho;

  double rho = std::sqrt(L.rho * R.rho);
  double rL = std::sqrt(L.rho), rR = std::sqrt(R.rho);
  double u = (rL * L.u + rR * R.u) / (rL + rR);
  double v = (rL * L.v + rR * R.v) / (rL + rR);
  double H = (rL * HL + rR * HR) / (rL + rR);
  double q2 = u * u + v * v;
  double a2 = (gamma - 1.0) * (H - 0.5 * q2);
  double a = a2 > 0.0 ? std::sqrt(a2) : 0.0;
  double qn = u * nx + v * ny;
  double qt = u * tx + v * ty;

  // Physical flux in the rotated frame.
  double f1 = rho * qn;
  double f2 = rho * qn * u + 0.0;  // unused directly; built below
  Vec4 FL{UL[1] * nx + UL[2] * ny,
          UL[1] * unL + L.p * nx,
          UL[2] * unL + L.p * ny,
          (UL[3] + L.p) * unL};
  Vec4 FR{UR[1] * nx + UR[2] * ny,
          UR[1] * unR + R.p * nx,
          UR[2] * unR + R.p * ny,
          (UR[3] + R.p) * unR};

  // Eigenvalue jumps in the rotated frame.
  double dqn = qnR - qnL;
  double dqt = qtR - qtL;
  double dp = R.p - L.p;
  double drho = R.rho - L.rho;

  double w1 = dp - a * a * drho;              // entropy wave
  double w2 = rho * a * dqn;                  // acoustic
  double w3 = rho * dqt;                      // shear

  // Harten-Yee entropy fix on the acoustic eigenvalues.
  double l1 = std::abs(qn - a);
  double l2 = std::abs(qn + a);
  double fix1 = l1 < eps ? (l1 * l1 + eps * eps) / (2.0 * eps) : l1;
  double fix2 = l2 < eps ? (l2 * l2 + eps * eps) / (2.0 * eps) : l2;

  // Dissipation in the rotated frame (q = (rho, rho qn, rho qt, rho H)).
  double d1 = fix1 * 0.5 * w1 / (a * a);
  double d2 = fix2 * 0.5 * w2 / (a * a);
  double d3 = std::abs(qn) * 0.5 * w3 / (a * a);

  double qH = q2 + H;
  double dis_rho = d1 + d2;
  double dis_qn = (qn - a) * d1 + (qn + a) * d2;
  double dis_qt = d3;
  double dis_H = (H - a * qn) * d1 + (H + a * qn) * d2 + qt * d3;

  // Rotated dissipation vector.
  double dis0 = dis_rho;
  double dis1 = qn * dis_rho + rho * dis_qn;            // rho*qn component
  double dis2 = qt * dis_rho + rho * dis_qt;            // rho*qt component
  double dis3 = 0.5 * q2 * dis_rho + rho * (qn * dis_qn + qt * dis_qt) +
                dis_H / (gamma - 1.0);

  // Rotate dissipation back to (x, y): (rho, rho u, rho v, rho E)
  // rho u = rho(qn nx + qt tx), rho v = rho(qn ny + qt ty)
  double d_rhou = dis1 * nx + dis2 * tx;
  double d_rhov = dis1 * ny + dis2 * ty;
  Vec4 diss{dis0, d_rhou, d_rhov, dis3};

  Vec4 F = 0.5 * (FL + FR) - diss;
  // Safety: if the Roe flux produced NaN, fall back to Rusanov.
  if (!std::isfinite(F[0]) || !std::isfinite(F[1]) || !std::isfinite(F[2]) || !std::isfinite(F[3]))
    return rusanov_flux(UL, UR, nx, ny, gamma, 1.0);
  return F;
}

// ---------------------------------------------------------------------------
// Viscous flux at a face
// ---------------------------------------------------------------------------
Vec4 viscous_flux_face(const GasState& gs_L, const GasState& gs_R,
                       double grad_u_L[2], double grad_u_R[2],
                       double grad_v_L[2], double grad_v_R[2],
                       double grad_T_L[2], double grad_T_R[2],
                       double nx, double ny, double area, double gamma, double mu,
                       double Pr) {
  double u_f = 0.5 * (gs_L.u + gs_R.u);
  double v_f = 0.5 * (gs_L.v + gs_R.v);

  // Face gradients (average of the two cell gradients; the caller applies the
  // diamond-path correction before invoking this function).
  double ux = 0.5 * (grad_u_L[0] + grad_u_R[0]);
  double uy = 0.5 * (grad_u_L[1] + grad_u_R[1]);
  double vx = 0.5 * (grad_v_L[0] + grad_v_R[0]);
  double vy = 0.5 * (grad_v_L[1] + grad_v_R[1]);
  double Tx = 0.5 * (grad_T_L[0] + grad_T_R[0]);
  double Ty = 0.5 * (grad_T_L[1] + grad_T_R[1]);

  double div = ux + vy;
  double tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  double tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  double tau_xy = mu * (uy + vx);

  double cp = gamma / (gamma - 1.0);
  double k = mu * cp / Pr;
  double qx = -k * Tx;
  double qy = -k * Ty;

  double tnx = tau_xx * nx + tau_xy * ny;
  double tny = tau_xy * nx + tau_yy * ny;
  double qn = qx * nx + qy * ny;

  return {0.0, tnx * area, tny * area, (u_f * tnx + v_f * tny - qn) * area};
}

// ---------------------------------------------------------------------------
// Least-squares coefficients (weights for face-neighbor stencils)
// ---------------------------------------------------------------------------
void compute_lsq_coefs(LocalMesh& mesh, std::vector<LSQCoef>& coefs) {
  coefs.resize(mesh.n_owned);
  for (int i = 0; i < mesh.n_owned; ++i) {
    LSQCoef& c = coefs[i];
    const auto& cell = mesh.cells[i];
    // Gather unique face neighbors.
    std::vector<int> nbrs;
    std::vector<double> dxs, dys;
    for (int f : mesh.cell_faces[i]) {
      int j = mesh.faces[f].c0 == i ? mesh.faces[f].c1 : mesh.faces[f].c0;
      if (j < 0) continue;
      double dx = mesh.cells[j].cx - cell.cx;
      double dy = mesh.cells[j].cy - cell.cy;
      nbrs.push_back(j);
      dxs.push_back(dx);
      dys.push_back(dy);
    }
    c.nbrs = std::move(nbrs);
    c.w_dx.assign(c.nbrs.size(), 0.0);
    c.w_dy.assign(c.nbrs.size(), 0.0);
    if (c.nbrs.size() < 2) {
      c.valid = false;
      continue;
    }
    // Normal matrix M = sum d d^T.
    double mxx = 0.0, mxy = 0.0, myy = 0.0;
    for (size_t k = 0; k < c.nbrs.size(); ++k) {
      mxx += dxs[k] * dxs[k];
      mxy += dxs[k] * dys[k];
      myy += dys[k] * dys[k];
    }
    double det = mxx * myy - mxy * mxy;
    if (std::abs(det) < 1e-24) {
      c.valid = false;
      continue;
    }
    c.valid = true;
    // M^{-1} entries; weights w_k = M^{-1} d_k.
    for (size_t k = 0; k < c.nbrs.size(); ++k) {
      c.w_dx[k] = (myy * dxs[k] - mxy * dys[k]) / det;
      c.w_dy[k] = (mxx * dys[k] - mxy * dxs[k]) / det;
    }
  }
}

// ---------------------------------------------------------------------------
// Gradients of primitives for all local cells (ghost slots exchanged by the
// driver after this call).
// ---------------------------------------------------------------------------
void compute_gradients(const LocalMesh& mesh, const std::vector<Vec4>& U,
                       const std::vector<LSQCoef>& coefs, double gamma,
                       std::vector<PrimGrad>& grads) {
  grads.assign(mesh.n_cells, PrimGrad{});
  for (int i = 0; i < mesh.n_owned; ++i) {
    const LSQCoef& c = coefs[i];
    if (!c.valid) continue;
    GasState gs = cons2prim(U[i], gamma);
    double rho_i = U[i][0];
    double u_i = gs.u, v_i = gs.v, p_i = gs.p;
    PrimGrad& g = grads[i];
    for (size_t k = 0; k < c.nbrs.size(); ++k) {
      int j = c.nbrs[k];
      GasState gj = cons2prim(U[j], gamma);
      double dr = U[j][0] - rho_i;
      double du = gj.u - u_i;
      double dv = gj.v - v_i;
      double dp = gj.p - p_i;
      g.gr[0] += c.w_dx[k] * dr;
      g.gr[1] += c.w_dy[k] * dr;
      g.gu[0] += c.w_dx[k] * du;
      g.gu[1] += c.w_dy[k] * du;
      g.gv[0] += c.w_dx[k] * dv;
      g.gv[1] += c.w_dy[k] * dv;
      g.gp[0] += c.w_dx[k] * dp;
      g.gp[1] += c.w_dy[k] * dp;
    }
  }
}

// ---------------------------------------------------------------------------
// Barth-Jespersen limiter (per primitive), computed for owned cells.
// ---------------------------------------------------------------------------
void compute_limiter(const LocalMesh& mesh, const std::vector<Vec4>& U,
                     const std::vector<PrimGrad>& grads, std::vector<Limiters>& lmt,
                     double gamma) {
  lmt.assign(mesh.n_cells, Limiters{1.0, 1.0, 1.0, 1.0});
  static int no_limiter = -1;
  if (no_limiter < 0) {
    const char* e = std::getenv("CFD_NO_LIMITER");
    no_limiter = e && std::string(e) == "1" ? 1 : 0;
  }
  if (no_limiter) return;
  for (int i = 0; i < mesh.n_owned; ++i) {
    const auto& cell = mesh.cells[i];
    GasState gs = cons2prim(U[i], gamma);
    double rho_i = U[i][0], u_i = gs.u, v_i = gs.v, p_i = gs.p;

    double rho_max = rho_i, rho_min = rho_i;
    double u_max = u_i, u_min = u_i;
    double v_max = v_i, v_min = v_i;
    double p_max = p_i, p_min = p_i;
    for (int f : mesh.cell_faces[i]) {
      int j = mesh.faces[f].c0 == i ? mesh.faces[f].c1 : mesh.faces[f].c0;
      if (j < 0) continue;
      GasState gj = cons2prim(U[j], gamma);
      rho_max = std::max(rho_max, U[j][0]);
      rho_min = std::min(rho_min, U[j][0]);
      u_max = std::max(u_max, gj.u);
      u_min = std::min(u_min, gj.u);
      v_max = std::max(v_max, gj.v);
      v_min = std::min(v_min, gj.v);
      p_max = std::max(p_max, gj.p);
      p_min = std::min(p_min, gj.p);
    }

    double psi_r = 1.0, psi_u = 1.0, psi_v = 1.0, psi_p = 1.0;
    for (int f : mesh.cell_faces[i]) {
      double rho_f, u_f, v_f, p_f;
      unlimited_face_prims(mesh, U, grads, i, f, gamma, rho_f, u_f, v_f, p_f);
      auto lim = [](double phi, double phi_f, double phi_max, double phi_min) {
        if (phi_f > phi) {
          double denom = phi_f - phi;
          return denom > 0.0 ? std::min(1.0, (phi_max - phi) / denom) : 1.0;
        } else if (phi_f < phi) {
          double denom = phi - phi_f;
          return denom > 0.0 ? std::min(1.0, (phi - phi_min) / denom) : 1.0;
        }
        return 1.0;
      };
      psi_r = std::min(psi_r, lim(rho_i, rho_f, rho_max, rho_min));
      psi_u = std::min(psi_u, lim(u_i, u_f, u_max, u_min));
      psi_v = std::min(psi_v, lim(v_i, v_f, v_max, v_min));
      psi_p = std::min(psi_p, lim(p_i, p_f, p_max, p_min));
    }
    lmt[i] = Limiters{psi_r, psi_u, psi_v, psi_p};
  }
}

// ---------------------------------------------------------------------------
// Halo exchange for a field with ncomp doubles per cell.
// ---------------------------------------------------------------------------
template <typename T>
void exchange_field(const LocalMesh& mesh, std::vector<T>& field, int ncomp,
                    MPI_Datatype dtype) {
  const int nn = (int)mesh.neighbor_ranks.size();
  std::vector<MPI_Request> reqs;
  std::vector<std::vector<double>> recv_buf(nn);
  std::vector<std::vector<double>> send_buf(nn);  // keep alive until Waitall
  for (int k = 0; k < nn; ++k) {
    const auto& send = mesh.send_cells[k];
    const auto& recv = mesh.recv_cells[k];
    recv_buf[k].resize(recv.size() * ncomp);
    send_buf[k].resize(send.size() * ncomp);
    for (size_t i = 0; i < send.size(); ++i)
      for (int c = 0; c < ncomp; ++c)
        send_buf[k][i * ncomp + c] = ((const double*)&field[send[i]])[c];
    if (!recv.empty()) {
      reqs.push_back(MPI_Request{});
      MPI_Irecv(recv_buf[k].data(), (int)recv_buf[k].size(), MPI_DOUBLE, mesh.neighbor_ranks[k],
                21, MPI_COMM_WORLD, &reqs.back());
    }
    if (!send.empty()) {
      reqs.push_back(MPI_Request{});
      MPI_Isend(send_buf[k].data(), (int)send_buf[k].size(), MPI_DOUBLE,
                mesh.neighbor_ranks[k], 21, MPI_COMM_WORLD, &reqs.back());
    }
  }
  if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
  for (int k = 0; k < nn; ++k) {
    const auto& recv = mesh.recv_cells[k];
    for (size_t i = 0; i < recv.size(); ++i)
      for (int c = 0; c < ncomp; ++c)
        ((double*)&field[recv[i]])[c] = recv_buf[k][i * ncomp + c];
  }
}

void exchange_halo(const LocalMesh& mesh, std::vector<Vec4>& U) {
  exchange_field(mesh, U, 4, MPI_DOUBLE);
}

void exchange_gradients(const LocalMesh& mesh, std::vector<PrimGrad>& grads) {
  exchange_field(mesh, grads, 8, MPI_DOUBLE);
}

void exchange_limiters(const LocalMesh& mesh, std::vector<Limiters>& lmt) {
  exchange_field(mesh, lmt, 4, MPI_DOUBLE);
}

void exchange_dU(const LocalMesh& mesh, std::vector<Vec4>& dU) {
  exchange_field(mesh, dU, 4, MPI_DOUBLE);
}

// ---------------------------------------------------------------------------
// Residual assembly
// ---------------------------------------------------------------------------
namespace {

// Adds the boundary-face contribution to res[c0]. Also returns the boundary
// flux (inviscid + viscous) for force integration when requested.
Vec4 boundary_face_flux(const LocalMesh& mesh, int f, const std::vector<Vec4>& U,
                        const std::vector<PrimGrad>& grads,
                        const std::vector<Limiters>& lmt, const CaseConfig& cfg,
                        bool viscous, double rho_eps, double p_eps, double mu) {
  const auto& face = mesh.faces[f];
  int c0 = face.c0;
  BcKind bc = (BcKind)face.bc;
  Vec4 UL = limited_face_state(mesh, U, grads, lmt, c0, f, cfg.gamma, rho_eps, p_eps);
  GasState gsL = cons2prim(UL, cfg.gamma);
  Vec4 F;
  if (bc == BcKind::SlipWall || bc == BcKind::NoSlipAdiabaticWall) {
    // Strong wall boundary condition: the wall flux is pressure-only (zero
    // normal velocity at the wall; for no-slip walls the velocity is zero).
    // Using a Riemann flux with a reflected ghost state here would inject a
    // spurious dissipation-driven momentum source at non-converged states.
    F = Vec4{0.0, gsL.p * face.nx, gsL.p * face.ny, 0.0} * face.area;
  } else {
    Vec4 UG = boundary_ghost_state(bc, gsL, cfg, face.nx, face.ny);
    if (cfg.inviscid_flux == "roe") {
      F = roe_flux(UL, UG, face.nx, face.ny, cfg.gamma) * face.area;
    } else {
      F = rusanov_flux(UL, UG, face.nx, face.ny, cfg.gamma,
                       cfg.rusanov_dissipation_scale) *
          face.area;
    }
  }

  if (viscous && bc != BcKind::SlipWall) {
    Vec4 UG = boundary_ghost_state(bc, gsL, cfg, face.nx, face.ny);
    const PrimGrad& g = grads[c0];
    PrimGrad gg;
    boundary_ghost_grad(bc, g, face.nx, face.ny, gg);
    const auto& cell = mesh.cells[c0];
    // Mirror ghost point.
    double dx = 2.0 * (face.fx - cell.cx);
    double dy = 2.0 * (face.fy - cell.cy);
    GasState gsG = cons2prim(UG, cfg.gamma);
    double gu[2], gv[2], gT[2];
    double gu_g[2], gv_g[2], gT_g[2];
    // temperature gradient from p,rho: T = p/rho -> gradT = (grad p - T grad rho)/rho
    double T_i = gsL.T, T_g = gsG.T;
    double inv_rho = 1.0 / gsL.rho;
    gT[0] = (g.gp[0] - T_i * g.gr[0]) * inv_rho;
    gT[1] = (g.gp[1] - T_i * g.gr[1]) * inv_rho;
    double inv_rho_g = 1.0 / gsG.rho;
    gT_g[0] = (gg.gp[0] - T_g * gg.gr[0]) * inv_rho_g;
    gT_g[1] = (gg.gp[1] - T_g * gg.gr[1]) * inv_rho_g;
    // Apply the diamond-path face-gradient formula for u, v, T.
    double gu_f[2], gv_f[2], gT_f[2];
    face_grad_avg(g.gu, gg.gu, gsL.u, gsG.u, dx, dy, gu_f);
    face_grad_avg(g.gv, gg.gv, gsL.v, gsG.v, dx, dy, gv_f);
    face_grad_avg(gT, gT_g, T_i, T_g, dx, dy, gT_f);
    Vec4 Fv = viscous_flux_face(gsL, gsG, gu_f, gu_f, gv_f, gv_f, gT_f, gT_f,
                                face.nx, face.ny, face.area, cfg.gamma, mu, cfg.prandtl);
    F = F - Fv;
  }
  return F;
}

}  // namespace

void assemble_residual(const LocalMesh& mesh, const std::vector<Vec4>& U,
                       const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                       const CaseConfig& cfg, bool viscous, std::vector<Vec4>& res,
                       double* res_l2, double* res_linf) {
  const double rho_eps = 1e-4 * cfg.rho_inf;
  const double p_eps = 1e-4 * cfg.p_inf;
  const double mu = viscous ? cfg.mu() : 0.0;

  res.assign(mesh.n_owned, Vec4{0, 0, 0, 0});
  double sumsq[4] = {0, 0, 0, 0};
  double linf = 0.0;

  for (const auto& face : mesh.faces) {
    int c0 = face.c0, c1 = face.c1;
    if (c1 < 0) {
      // Boundary face: contribution only to owned cell c0.
      Vec4 F = boundary_face_flux(mesh, (int)(&face - mesh.faces.data()), U, grads, lmt, cfg,
                                  viscous, rho_eps, p_eps, mu);
      res[c0] += F;
      continue;
    }
    // Interior face: limited left/right states.
    int fid = (int)(&face - mesh.faces.data());
    Vec4 UL = limited_face_state(mesh, U, grads, lmt, c0, fid, cfg.gamma, rho_eps, p_eps);
    Vec4 UR = limited_face_state(mesh, U, grads, lmt, c1, fid, cfg.gamma, rho_eps, p_eps);
    Vec4 F;
    if (cfg.inviscid_flux == "roe") {
      F = roe_flux(UL, UR, face.nx, face.ny, cfg.gamma) * face.area;
    } else {
      F = rusanov_flux(UL, UR, face.nx, face.ny, cfg.gamma, cfg.rusanov_dissipation_scale) *
          face.area;
    }
    if (viscous) {
      // Diamond-path face gradients for u, v, T.
      const auto& cell0 = mesh.cells[c0];
      const auto& cell1 = mesh.cells[c1];
      GasState g0 = cons2prim(U[c0], cfg.gamma);
      GasState g1 = cons2prim(U[c1], cfg.gamma);
      double dx = cell1.cx - cell0.cx;
      double dy = cell1.cy - cell0.cy;
      const PrimGrad& ga = grads[c0];
      const PrimGrad& gb = grads[c1];
      double T0 = g0.T, T1 = g1.T;
      double inv0 = 1.0 / g0.rho, inv1 = 1.0 / g1.rho;
      double gT0[2] = {(ga.gp[0] - T0 * ga.gr[0]) * inv0, (ga.gp[1] - T0 * ga.gr[1]) * inv0};
      double gT1[2] = {(gb.gp[0] - T1 * gb.gr[0]) * inv1, (gb.gp[1] - T1 * gb.gr[1]) * inv1};
      double gu_f[2], gv_f[2], gT_f[2];
      face_grad_avg(ga.gu, gb.gu, g0.u, g1.u, dx, dy, gu_f);
      face_grad_avg(ga.gv, gb.gv, g0.v, g1.v, dx, dy, gv_f);
      face_grad_avg(gT0, gT1, T0, T1, dx, dy, gT_f);
      Vec4 Fv = viscous_flux_face(g0, g1, gu_f, gu_f, gv_f, gv_f, gT_f, gT_f,
                                  face.nx, face.ny, face.area, cfg.gamma, mu, cfg.prandtl);
      F = F - Fv;
    }
    res[c0] += F;
    res[c1] -= F;
  }

  // Global reduction of component L2 norms (RMS over owned cells).
  for (int i = 0; i < mesh.n_owned; ++i) {
    for (int k = 0; k < 4; ++k) {
      sumsq[k] += res[i][k] * res[i][k];
      linf = std::max(linf, std::abs(res[i][k]));
    }
  }
  double gsum[4];
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  double glinf;
  MPI_Allreduce(&linf, &glinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int64_t ntot = 0;
  int64_t nloc = mesh.n_owned;
  MPI_Allreduce(&nloc, &ntot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
  for (int k = 0; k < 4; ++k) gsum[k] = std::sqrt(gsum[k] / (double)ntot);
  *res_l2 = std::sqrt((gsum[0] * gsum[0] + gsum[1] * gsum[1] + gsum[2] * gsum[2] +
                       gsum[3] * gsum[3]) /
                      4.0);
  *res_linf = glinf;
}

// ---------------------------------------------------------------------------
// Pseudo time step
// ---------------------------------------------------------------------------
double compute_dt(const LocalMesh& mesh, const std::vector<Vec4>& U, const CaseConfig& cfg,
                  bool viscous, double cfl) {
  double dt_min = 1e300;
  for (int i = 0; i < mesh.n_owned; ++i) {
    GasState gs = cons2prim(U[i], cfg.gamma);
    double lambda = 0.0;
    for (int f : mesh.cell_faces[i]) {
      const auto& face = mesh.faces[f];
      double un = gs.u * face.nx + gs.v * face.ny;
      lambda += (std::abs(un) + gs.a) * face.area;
      if (viscous) {
        double mu = cfg.mu();
        lambda += (2.0 * mu / gs.rho) * face.area * face.area / mesh.cells[i].vol;
      }
    }
    double dt = lambda > kTiny ? cfl * mesh.cells[i].vol / lambda : 1e30;
    dt_min = std::min(dt_min, dt);
  }
  double gmin;
  MPI_Allreduce(&dt_min, &gmin, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
  return gmin;
}

void compute_diagonal(const LocalMesh& mesh, const std::vector<Vec4>& U,
                      const CaseConfig& cfg, bool viscous, double cfl,
                      std::vector<double>& D) {
  static double diag_frac = -1.0;
  if (diag_frac < 0.0) {
    const char* e = std::getenv("CFD_LUSGS_DIAG");
    diag_frac = e ? std::atof(e) : 1.0;
  }
  D.resize(mesh.n_owned);
  for (int i = 0; i < mesh.n_owned; ++i) {
    GasState gs = cons2prim(U[i], cfg.gamma);
    double lambda = 0.0;
    for (int f : mesh.cell_faces[i]) {
      const auto& face = mesh.faces[f];
      double un = gs.u * face.nx + gs.v * face.ny;
      lambda += (std::abs(un) + gs.a) * face.area;
      if (viscous) {
        double mu = cfg.mu();
        lambda += (2.0 * mu / gs.rho) * face.area * face.area / mesh.cells[i].vol;
      }
    }
    // Diagonal: pseudo-time term plus the diagonal part of the simplified
    // flux Jacobian (full spectral-radius sum), following the standard
    // simplified-LU-SGS form.
    D[i] = lambda / cfl + diag_frac * lambda;
    if (D[i] < kTiny) D[i] = 1e-20;
  }
}

// ---------------------------------------------------------------------------
// Surface wall rows (surface.csv body)
// ---------------------------------------------------------------------------
std::string wall_surface_rows(const LocalMesh& mesh, const std::vector<Vec4>& U,
                              const std::vector<PrimGrad>& grads,
                              const std::vector<Limiters>& lmt, const CaseConfig& cfg,
                              bool viscous, double mu) {
  const double rho_eps = 1e-4 * cfg.rho_inf;
  const double p_eps = 1e-4 * cfg.p_inf;
  double q = cfg.q_inf();
  std::string out;
  char buf[512];
  for (int fid : mesh.wall_faces) {
    const auto& face = mesh.faces[fid];
    int c0 = face.c0;
    BcKind bc = (BcKind)face.bc;
    Vec4 UL = limited_face_state(mesh, U, grads, lmt, c0, fid, cfg.gamma, rho_eps, p_eps);
    GasState gsL = cons2prim(UL, cfg.gamma);

    double p_face = gsL.p;
    double cp = (p_face - cfg.p_inf) / q;
    double rho_w = gsL.rho;
    double u_w = 0.0, v_w = 0.0, mach_w = 0.0, cf = 0.0;
    if (bc == BcKind::SlipWall) {
      double un = gsL.u * face.nx + gsL.v * face.ny;
      u_w = gsL.u - un * face.nx;
      v_w = gsL.v - un * face.ny;
      mach_w = std::sqrt(u_w * u_w + v_w * v_w) / gsL.a;
    } else if (bc == BcKind::NoSlipAdiabaticWall && viscous) {
      // Skin friction: tangential viscous traction on the body.
      const PrimGrad& g = grads[c0];
      double dx = face.fx - mesh.cells[c0].cx;
      double dy = face.fy - mesh.cells[c0].cy;
      double d = std::sqrt(dx * dx + dy * dy);
      if (d < kTiny) d = 1e-12;
      double u_cell = U[c0][1] / U[c0][0];
      double v_cell = U[c0][2] / U[c0][0];
      // Wall-normal gradient from the cell-centered gradient, with a
      // correction to impose the no-slip condition (wall velocity = 0).
      // The corrected wall-normal gradient is:
      //   du/dn|_wall = 2*(u_cell - 0)/d - du/dn|_cell
      // This is the standard correction for a linear profile from the
      // wall to the cell center, where the cell-centered gradient is at
      // the cell center (distance d from the wall).
      // Wall gradient using the corrected wall-normal gradient.
      // The wall-normal gradient at the wall is computed from the no-slip
      // condition: u_wall = 0. The cell-centered gradient gn is the gradient
      // at the cell center, and the wall-normal gradient at the wall is
      // approximated as -u_cell/d (linear profile).
      double gn = g.gu[0] * face.nx + g.gu[1] * face.ny;
      double ux_w = g.gu[0] - gn * face.nx - (u_cell / d) * face.nx;
      double uy_w = g.gu[1] - gn * face.ny - (u_cell / d) * face.ny;
      gn = g.gv[0] * face.nx + g.gv[1] * face.ny;
      double vx_w = g.gv[0] - gn * face.nx - (v_cell / d) * face.nx;
      double vy_w = g.gv[1] - gn * face.ny - (v_cell / d) * face.ny;
      double div = ux_w + vy_w;
      double tau_xx = 2.0 * mu * ux_w - (2.0 / 3.0) * mu * div;
      double tau_yy = 2.0 * mu * vy_w - (2.0 / 3.0) * mu * div;
      double tau_xy = mu * (uy_w + vx_w);
      double tnx = tau_xx * face.nx + tau_xy * face.ny;
      double tny = tau_xy * face.nx + tau_yy * face.ny;
      // Tangential component along the freestream-aligned tangent.
      double tau_t = tnx * face.tx + tny * face.ty;
      cf = tau_t / q;
    }
    std::snprintf(buf, sizeof(buf),
                  "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%s\n",
                  face.fx, face.fy, face.nx, face.ny, p_face, cp, cf, rho_w, u_w, v_w,
                  mach_w, face.tag.c_str());
    out += buf;
  }
  return out;
}

// ---------------------------------------------------------------------------
// LU-SGS sweep (matrix-free, simplified Jacobians)
// ---------------------------------------------------------------------------
namespace {

// Matrix-free approximation of a flux Jacobian acting on dU_j.
// mode 0: physical (Euler) flux difference:  A_phys * dU_j
// mode 1: numerical (Rusanov/Roe) flux difference with fixed partner state.
inline Vec4 flux_diff(const LocalMesh& mesh, const std::vector<Vec4>& U, int i, int j, int f,
                      const Vec4& dUj, const CaseConfig& cfg, int mode) {
  if (dUj[0] == 0.0 && dUj[1] == 0.0 && dUj[2] == 0.0 && dUj[3] == 0.0) {
    return {0, 0, 0, 0};
  }
  const auto& face = mesh.faces[f];
  Vec4 Fn, F0;
  if (mode == 0) {
    Fn = euler_flux(U[j] + dUj, face.nx, face.ny, cfg.gamma) * face.area;
    F0 = euler_flux(U[j], face.nx, face.ny, cfg.gamma) * face.area;
  } else {
    Vec4 Ui = U[i];
    if (cfg.inviscid_flux == "roe") {
      Fn = roe_flux(Ui, U[j] + dUj, face.nx, face.ny, cfg.gamma) * face.area;
      F0 = roe_flux(Ui, U[j], face.nx, face.ny, cfg.gamma) * face.area;
    } else {
      Fn = rusanov_flux(Ui, U[j] + dUj, face.nx, face.ny, cfg.gamma,
                        cfg.rusanov_dissipation_scale) *
           face.area;
      F0 = rusanov_flux(Ui, U[j], face.nx, face.ny, cfg.gamma,
                        cfg.rusanov_dissipation_scale) *
           face.area;
    }
  }
  return Fn - F0;
}

inline double face_lambda(const LocalMesh& mesh, const std::vector<Vec4>& U, int i, int j,
                          int f, const CaseConfig& cfg) {
  const auto& face = mesh.faces[f];
  GasState gi = cons2prim(U[i], cfg.gamma);
  GasState gj = cons2prim(U[j], cfg.gamma);
  double uni = gi.u * face.nx + gi.v * face.ny;
  double unj = gj.u * face.nx + gj.v * face.ny;
  double a = 0.5 * (gi.a + gj.a);
  return (std::abs(0.5 * (uni + unj)) + a) * face.area;
}

}  // namespace

void lusgs_sweep(const LocalMesh& mesh, const std::vector<Vec4>& U, std::vector<Vec4>& dU,
                 const std::vector<Vec4>& res, const std::vector<double>& D,
                 const CaseConfig& cfg) {
  static int mode = -1;
  if (mode < 0) {
    const char* e = std::getenv("CFD_LUSGS_MODE");
    if (!e) mode = 3;  // default: symmetric GS with consistent Rusanov split
    else if (std::string(e) == "blazek_phys") mode = 0;
    else if (std::string(e) == "blazek_num") mode = 1;
    else if (std::string(e) == "rusanov_fwd") mode = 2;
    else if (std::string(e) == "rusanov_sym") mode = 3;
    else if (std::string(e) == "blazek_sym") mode = 4;
    else mode = 3;
  }
  static double omega = -1.0;
  if (omega < 0.0) {
    const char* e = std::getenv("CFD_LUSGS_OMEGA");
    omega = e ? std::atof(e) : 1.0;
  }

  // Forward sweep.
  for (int oi = 0; oi < mesh.n_owned; ++oi) {
    int i = mesh.sweep_order[oi];
    Vec4 rhs = res[i];
    for (int f : mesh.cell_faces[i]) {
      int j = mesh.faces[f].c0 == i ? mesh.faces[f].c1 : mesh.faces[f].c0;
      if (j < 0) continue;
      bool lower = mesh.is_ghost(j) || mesh.sweep_pos[j] < oi;
      if (mode == 2) lower = true;  // rusanov_fwd: all coupling in the forward pass
      if (!lower) continue;
      double s = mesh.faces[f].c0 == i ? 1.0 : -1.0;
      Vec4 dUj = dU[j];
      Vec4 A = flux_diff(mesh, U, i, j, f, dUj, cfg, mode == 1 ? 1 : 0);
      double lam = face_lambda(mesh, U, i, j, f, cfg);
      rhs += 0.5 * (s * A - lam * dUj);
    }
    {
      Vec4 du_raw = rhs * (-omega / D[i]);
      // Apply positivity check to the dU candidate before storing it.
      // This prevents non-physical dU values from contaminating the sweep.
      Vec4 U_new = U[i];
      for (int k = 0; k < 4; ++k) U_new[k] += du_raw[k];
      GasState gs_new = cons2prim(U_new, cfg.gamma);
      if (std::isfinite(gs_new.rho) && std::isfinite(gs_new.p) && gs_new.rho > 0.0 && gs_new.p > 0.0) {
        dU[i] = du_raw;
      } else {
        // Zero out the update if it would produce a non-physical state.
        // The cell will be updated by the next outer iteration instead.
        dU[i] = Vec4{0,0,0,0};
      }
    }
  }
  exchange_dU(mesh, dU);

  // Backward sweep.
  if (mode == 2) return;
  if (mode == 4) return;
  bool sym = (mode == 3);
  for (int oi = mesh.n_owned - 1; oi >= 0; --oi) {
    int i = mesh.sweep_order[oi];
    Vec4 rhs{0, 0, 0, 0};
    for (int f : mesh.cell_faces[i]) {
      int j = mesh.faces[f].c0 == i ? mesh.faces[f].c1 : mesh.faces[f].c0;
      if (j < 0) continue;
      if (!sym && (mesh.is_ghost(j) || mesh.sweep_pos[j] <= oi)) continue;
      double s = mesh.faces[f].c0 == i ? 1.0 : -1.0;
      Vec4 A = flux_diff(mesh, U, i, j, f, dU[j], cfg, mode == 1 ? 1 : 0);
      double lam = face_lambda(mesh, U, i, j, f, cfg);
      if (sym) {
        rhs += 0.5 * (s * A - lam * dU[j]);
      } else {
        rhs += 0.5 * (s * A + lam * dU[j]);
      }
    }
    {
      Vec4 du_raw = rhs * (omega / D[i]);
      Vec4 du_new;
      for (int k = 0; k < 4; ++k) du_new[k] = dU[i][k] - du_raw[k];
      Vec4 U_new = U[i];
      for (int k = 0; k < 4; ++k) U_new[k] += du_new[k];
      GasState gs_new = cons2prim(U_new, cfg.gamma);
      if (std::isfinite(gs_new.rho) && std::isfinite(gs_new.p) && gs_new.rho > 0.0 && gs_new.p > 0.0) {
        dU[i] = du_new;
      }
    }
  }
  exchange_dU(mesh, dU);
}

// ---------------------------------------------------------------------------
// Nonlinear symmetric Gauss-Seidel (point-implicit) relaxation.
// Each cell's local residual is recomputed with the latest neighbor states and
// the cell is updated in place: U_i <- U_i - R_i / D_i. The reconstruction is
// frozen (lagged) as in the LU-SGS defect-correction loop.
// ---------------------------------------------------------------------------
void sgs_sweep(const LocalMesh& mesh, std::vector<Vec4>& U,
               const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
               const CaseConfig& cfg, bool viscous, const std::vector<double>& D,
               const std::vector<Vec4>* extra) {
  const double rho_eps = 1e-4 * cfg.rho_inf;
  const double p_eps = 1e-4 * cfg.p_inf;
  const double mu = viscous ? cfg.mu() : 0.0;

  auto local_residual = [&](int i) -> Vec4 {
    Vec4 r{0, 0, 0, 0};
    for (int f : mesh.cell_faces[i]) {
      const auto& face = mesh.faces[f];
      if (face.c1 < 0) {
        r += boundary_face_flux(mesh, f, U, grads, lmt, cfg, viscous, rho_eps, p_eps, mu);
        continue;
      }
      int j = face.c0 == i ? face.c1 : face.c0;
      double s = face.c0 == i ? 1.0 : -1.0;
      Vec4 UL = limited_face_state(mesh, U, grads, lmt, i, f, cfg.gamma, rho_eps, p_eps);
      Vec4 UR = limited_face_state(mesh, U, grads, lmt, j, f, cfg.gamma, rho_eps, p_eps);
      Vec4 F;
      if (cfg.inviscid_flux == "roe") {
        F = roe_flux(UL, UR, face.nx, face.ny, cfg.gamma) * face.area;
      } else {
        F = rusanov_flux(UL, UR, face.nx, face.ny, cfg.gamma,
                         cfg.rusanov_dissipation_scale) *
            face.area;
      }
      if (viscous) {
        const auto& cell0 = mesh.cells[i];
        const auto& cell1 = mesh.cells[j];
        GasState g0 = cons2prim(U[i], cfg.gamma);
        GasState g1 = cons2prim(U[j], cfg.gamma);
        double dx = cell1.cx - cell0.cx;
        double dy = cell1.cy - cell0.cy;
        const PrimGrad& ga = grads[i];
        const PrimGrad& gb = grads[j];
        double T0 = g0.T, T1 = g1.T;
        double inv0 = 1.0 / g0.rho, inv1 = 1.0 / g1.rho;
        double gT0[2] = {(ga.gp[0] - T0 * ga.gr[0]) * inv0,
                         (ga.gp[1] - T0 * ga.gr[1]) * inv0};
        double gT1[2] = {(gb.gp[0] - T1 * gb.gr[0]) * inv1,
                         (gb.gp[1] - T1 * gb.gr[1]) * inv1};
        double gu_f[2], gv_f[2], gT_f[2];
        face_grad_avg(ga.gu, gb.gu, g0.u, g1.u, dx, dy, gu_f);
        face_grad_avg(ga.gv, gb.gv, g0.v, g1.v, dx, dy, gv_f);
        face_grad_avg(gT0, gT1, T0, T1, dx, dy, gT_f);
        Vec4 Fv = viscous_flux_face(g0, g1, gu_f, gu_f, gv_f, gv_f, gT_f, gT_f, face.nx,
                                    face.ny, face.area, cfg.gamma, mu, cfg.prandtl);
        F = F - Fv;
      }
      r += s * F;
    }
    if (extra) r += (*extra)[i];
    return r;
  };

  // Forward sweep.
  for (int oi = 0; oi < mesh.n_owned; ++oi) {
    int i = mesh.sweep_order[oi];
    Vec4 r = local_residual(i);
    Vec4 du = r * (-1.0 / D[i]);
    // Positivity-preserving line search: scale back the update if the new
    // state would have negative density or pressure.
    Vec4 U_new = U[i];
    for (int k = 0; k < 4; ++k) U_new[k] += du[k];
    GasState gs_new = cons2prim(U_new, cfg.gamma);
    double scale = 1.0;
    if (gs_new.rho <= 0.0) {
      double rho_old = U[i][0];
      if (du[0] < 0.0) scale = std::min(scale, 0.5 * rho_old / (-du[0]));
    }
    if (gs_new.p <= 0.0) {
      // Estimate the pressure change: p = (gamma-1) * rho * (E - 0.5*(u^2+v^2))
      // Simple approach: check if the pressure ratio is reasonable.
      double p_old = (cfg.gamma - 1.0) * (U[i][3] - 0.5 * (U[i][1]*U[i][1] + U[i][2]*U[i][2]) / U[i][0]);
      if (p_old > 0.0) {
        double dp = gs_new.p - p_old;
        if (dp <= -p_old) scale = std::min(scale, 0.5 * p_old / (-dp));
      }
    }
    if (scale < 1.0) scale = std::max(scale, 1e-6);
    for (int k = 0; k < 4; ++k) U[i][k] += du[k] * scale;
  }
  exchange_halo(mesh, U);
  // Backward sweep.
  for (int oi = mesh.n_owned - 1; oi >= 0; --oi) {
    int i = mesh.sweep_order[oi];
    Vec4 r = local_residual(i);
    Vec4 du = r * (-1.0 / D[i]);
    Vec4 U_new = U[i];
    for (int k = 0; k < 4; ++k) U_new[k] += du[k];
    GasState gs_new = cons2prim(U_new, cfg.gamma);
    if (std::isfinite(gs_new.rho) && std::isfinite(gs_new.p) && gs_new.rho > 0.0 && gs_new.p > 0.0)
      U[i] = U_new;
  }
  exchange_halo(mesh, U);
}
// ---------------------------------------------------------------------------
// Transient SGS sweep: adds the physical-time term to the residual using the
// CURRENT state (not the extra vector, which would be stale after updates).
// ---------------------------------------------------------------------------
void sgs_sweep_transient(const LocalMesh& mesh, std::vector<Vec4>& U,
                         const std::vector<Vec4>& Un, const std::vector<Vec4>& Unm1,
                         const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                         const CaseConfig& cfg, double dt, int ps, const std::vector<double>& D) {
  const double rho_eps = 1e-4 * cfg.rho_inf;
  const double p_eps = 1e-4 * cfg.p_inf;
  const double mu = cfg.mu();
  double a0 = (ps == 0) ? 1.0 : 1.5;
  double a1 = (ps == 0) ? -1.0 : -2.0;
  double a2 = (ps == 0) ? 0.0 : 0.5;
  double inv_dt = 1.0 / dt;

  auto local_residual = [&](int i) -> Vec4 {
    Vec4 r{0, 0, 0, 0};
    for (int f : mesh.cell_faces[i]) {
      const auto& face = mesh.faces[f];
      if (face.c1 < 0) {
        r += boundary_face_flux(mesh, f, U, grads, lmt, cfg, true, rho_eps, p_eps, mu);
        continue;
      }
      int j = face.c0 == i ? face.c1 : face.c0;
      double s = face.c0 == i ? 1.0 : -1.0;
      Vec4 UL = limited_face_state(mesh, U, grads, lmt, i, f, cfg.gamma, rho_eps, p_eps);
      Vec4 UR = limited_face_state(mesh, U, grads, lmt, j, f, cfg.gamma, rho_eps, p_eps);
      Vec4 F;
      if (cfg.inviscid_flux == "roe") {
        F = roe_flux(UL, UR, face.nx, face.ny, cfg.gamma) * face.area;
      } else {
        F = rusanov_flux(UL, UR, face.nx, face.ny, cfg.gamma,
                         cfg.rusanov_dissipation_scale) * face.area;
      }
      const auto& cell0 = mesh.cells[i];
      const auto& cell1 = mesh.cells[j];
      GasState g0 = cons2prim(U[i], cfg.gamma);
      GasState g1 = cons2prim(U[j], cfg.gamma);
      double dx = cell1.cx - cell0.cx;
      double dy = cell1.cy - cell0.cy;
      const PrimGrad& ga = grads[i];
      const PrimGrad& gb = grads[j];
      double T0 = g0.T, T1 = g1.T;
      double inv0 = 1.0 / g0.rho, inv1 = 1.0 / g1.rho;
      double gT0[2] = {(ga.gp[0] - T0 * ga.gr[0]) * inv0,
                       (ga.gp[1] - T0 * ga.gr[1]) * inv0};
      double gT1[2] = {(gb.gp[0] - T1 * gb.gr[0]) * inv1,
                       (gb.gp[1] - T1 * gb.gr[1]) * inv1};
      double gu_f[2], gv_f[2], gT_f[2];
      face_grad_avg(ga.gu, gb.gu, g0.u, g1.u, dx, dy, gu_f);
      face_grad_avg(ga.gv, gb.gv, g0.v, g1.v, dx, dy, gv_f);
      face_grad_avg(gT0, gT1, T0, T1, dx, dy, gT_f);
      Vec4 Fv = viscous_flux_face(g0, g1, gu_f, gu_f, gv_f, gv_f, gT_f, gT_f, face.nx,
                                  face.ny, face.area, cfg.gamma, mu, cfg.prandtl);
      F = F - Fv;
      r += s * F;
    }
    // Add the physical-time term using the current state of cell i.
    for (int k = 0; k < 4; ++k)
      r[k] += (a0 * U[i][k] + a1 * Un[i][k] + a2 * Unm1[i][k]) * inv_dt;
    return r;
  };

  // Forward sweep.
  for (int oi = 0; oi < mesh.n_owned; ++oi) {
    int i = mesh.sweep_order[oi];
    Vec4 r = local_residual(i);
    Vec4 du = r * (-1.0 / D[i]);
    // Positivity-preserving line search: scale back the update if the new
    // state would have negative density or pressure (same as sgs_sweep).
    Vec4 U_new = U[i];
    for (int k = 0; k < 4; ++k) U_new[k] += du[k];
    GasState gs_new = cons2prim(U_new, cfg.gamma);
    double scale = 1.0;
    if (gs_new.rho <= 0.0) {
      double rho_old = U[i][0];
      if (du[0] < 0.0) scale = std::min(scale, 0.5 * rho_old / (-du[0]));
    }
    if (gs_new.p <= 0.0) {
      double p_old = (cfg.gamma - 1.0) * (U[i][3] - 0.5 * (U[i][1]*U[i][1] + U[i][2]*U[i][2]) / U[i][0]);
      if (p_old > 0.0) {
        double dp = gs_new.p - p_old;
        if (dp <= -p_old) scale = std::min(scale, 0.5 * p_old / (-dp));
      }
    }
    if (scale < 1.0) scale = std::max(scale, 1e-6);
    for (int k = 0; k < 4; ++k) du[k] *= scale;
    U_new = U[i];
    for (int k = 0; k < 4; ++k) U_new[k] += du[k];
    gs_new = cons2prim(U_new, cfg.gamma);
    if (std::isfinite(gs_new.rho) && std::isfinite(gs_new.p) && gs_new.rho > 0.0 && gs_new.p > 0.0)
      U[i] = U_new;
  }
  exchange_halo(mesh, U);

  // Backward sweep.
  for (int oi = mesh.n_owned - 1; oi >= 0; --oi) {
    int i = mesh.sweep_order[oi];
    Vec4 r = local_residual(i);
    Vec4 du = r * (-1.0 / D[i]);
    Vec4 U_new = U[i];
    for (int k = 0; k < 4; ++k) U_new[k] += du[k];
    GasState gs_new = cons2prim(U_new, cfg.gamma);
    if (std::isfinite(gs_new.rho) && std::isfinite(gs_new.p) && gs_new.rho > 0.0 && gs_new.p > 0.0)
      U[i] = U_new;
  }
  exchange_halo(mesh, U);
}


// ---------------------------------------------------------------------------
// Forces on wall boundaries
// ---------------------------------------------------------------------------
void compute_forces(const LocalMesh& mesh, const std::vector<Vec4>& U,
                    const std::vector<PrimGrad>& grads, const std::vector<Limiters>& lmt,
                    const CaseConfig& cfg, bool viscous, double mu, double& cl, double& cd,
                    double& cmz, double& pressure_drag, double& viscous_drag,
                    double& pressure_lift, double& viscous_lift) {
  double fpx = 0.0, fpy = 0.0, fvx = 0.0, fvy = 0.0, mz = 0.0;
  const double rho_eps = 1e-4 * cfg.rho_inf;
  const double p_eps = 1e-4 * cfg.p_inf;
  for (int fid : mesh.wall_faces) {
    const auto& face = mesh.faces[fid];
    int c0 = face.c0;
    BcKind bc = (BcKind)face.bc;
    Vec4 UL = limited_face_state(mesh, U, grads, lmt, c0, fid, cfg.gamma, rho_eps, p_eps);
    GasState gsL = cons2prim(UL, cfg.gamma);

    // Pressure force on the body: F_p = p n A (n points outward from fluid, into the body).
    double p_face = gsL.p;  // wall pressure (pG = pL for slip and no-slip mirrors)
    double Fpx = p_face * face.nx * face.area;
    double Fpy = p_face * face.ny * face.area;
    fpx += Fpx;
    fpy += Fpy;

    if (viscous && bc == BcKind::NoSlipAdiabaticWall) {
      // Wall velocity gradients from the mirror-ghost treatment:
      //   grad(wall) = tangential(cell gradient) - (u_i / d) n
      // where d is the cell-centre to wall-face distance.
      const PrimGrad& g = grads[c0];
      double dx = face.fx - mesh.cells[c0].cx;
      double dy = face.fy - mesh.cells[c0].cy;
      double d = std::sqrt(dx * dx + dy * dy);
      if (d < kTiny) d = 1e-12;
      double u_cell = U[c0][1] / U[c0][0];
      double v_cell = U[c0][2] / U[c0][0];
      // Wall gradient using the corrected wall-normal gradient.
      // The wall-normal gradient at the wall is computed from the no-slip
      // condition: u_wall = 0. The cell-centered gradient gn is the gradient
      // at the cell center, and the wall-normal gradient at the wall is
      // approximated as -u_cell/d (linear profile).
      double gn = g.gu[0] * face.nx + g.gu[1] * face.ny;
      double ux_w = g.gu[0] - gn * face.nx - (u_cell / d) * face.nx;
      double uy_w = g.gu[1] - gn * face.ny - (u_cell / d) * face.ny;
      gn = g.gv[0] * face.nx + g.gv[1] * face.ny;
      double vx_w = g.gv[0] - gn * face.nx - (v_cell / d) * face.nx;
      double vy_w = g.gv[1] - gn * face.ny - (v_cell / d) * face.ny;

      double div = ux_w + vy_w;
      double tau_xx = 2.0 * mu * ux_w - (2.0 / 3.0) * mu * div;
      double tau_yy = 2.0 * mu * vy_w - (2.0 / 3.0) * mu * div;
      double tau_xy = mu * (uy_w + vx_w);
      double tnx = tau_xx * face.nx + tau_xy * face.ny;
      double tny = tau_xy * face.nx + tau_yy * face.ny;
      // Force on the body from viscosity: F_v = -tau . n A
      double Fvx = -tnx * face.area;
      double Fvy = -tny * face.area;
      fvx += Fvx;
      fvy += Fvy;
    }
    // Moment about the reference centre.
    double rx = face.fx - cfg.moment_cx;
    double ry = face.fy - cfg.moment_cy;
    mz += rx * (Fpy) - ry * (Fpx);
  }

  // Viscous moment contribution (tangential traction only).
  double mz_v = 0.0;
  if (viscous) {
    for (int fid : mesh.wall_faces) {
      const auto& face = mesh.faces[fid];
      if (face.bc != (int)BcKind::NoSlipAdiabaticWall) continue;
      int c0 = face.c0;
      const PrimGrad& g = grads[c0];
      double dx = face.fx - mesh.cells[c0].cx;
      double dy = face.fy - mesh.cells[c0].cy;
      double d = std::sqrt(dx * dx + dy * dy);
      if (d < kTiny) d = 1e-12;
      GasState gsL = cons2prim(U[c0], cfg.gamma);
      double u_cell = U[c0][1] / U[c0][0];
      double v_cell = U[c0][2] / U[c0][0];
      // Wall gradient using the corrected wall-normal gradient.
      // The wall-normal gradient at the wall is computed from the no-slip
      // condition: u_wall = 0. The cell-centered gradient gn is the gradient
      // at the cell center, and the wall-normal gradient at the wall is
      // approximated as -u_cell/d (linear profile).
      double gn = g.gu[0] * face.nx + g.gu[1] * face.ny;
      double ux_w = g.gu[0] - gn * face.nx - (u_cell / d) * face.nx;
      double uy_w = g.gu[1] - gn * face.ny - (u_cell / d) * face.ny;
      gn = g.gv[0] * face.nx + g.gv[1] * face.ny;
      double vx_w = g.gv[0] - gn * face.nx - (v_cell / d) * face.nx;
      double vy_w = g.gv[1] - gn * face.ny - (v_cell / d) * face.ny;
      double div = ux_w + vy_w;
      double tau_xx = 2.0 * mu * ux_w - (2.0 / 3.0) * mu * div;
      double tau_yy = 2.0 * mu * vy_w - (2.0 / 3.0) * mu * div;
      double tau_xy = mu * (uy_w + vx_w);
      double tnx = tau_xx * face.nx + tau_xy * face.ny;
      double tny = tau_xy * face.nx + tau_yy * face.ny;
      double Fvx = -tnx * face.area;
      double Fvy = -tny * face.area;
      double rx = face.fx - cfg.moment_cx;
      double ry = face.fy - cfg.moment_cy;
      mz_v += rx * Fvy - ry * Fvx;
    }
  }

  double q = cfg.q_inf();
  double inv = 1.0 / (q * cfg.ref_area);
  cd = (fpx + fvx) * inv;
  cl = (fpy + fvy) * inv;
  pressure_drag = fpx * inv;
  viscous_drag = fvx * inv;
  pressure_lift = fpy * inv;
  viscous_lift = fvy * inv;

  double lf[3] = {mz, mz_v, 0.0};
  double gf[3];
  MPI_Allreduce(lf, gf, 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  cmz = (gf[0] + gf[1]) * inv / cfg.ref_length;
}

}  // namespace cfd
