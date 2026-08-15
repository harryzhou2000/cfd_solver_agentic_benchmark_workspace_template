#include "cfd/fvm.hpp"

#include <mpi.h>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>

namespace cfd {

namespace {

Primitive operator+(Primitive a, const Primitive& b) {
  a.rho += b.rho;
  a.u += b.u;
  a.v += b.v;
  a.p += b.p;
  return a;
}

Primitive operator*(Real s, Primitive a) {
  a.rho *= s;
  a.u *= s;
  a.v *= s;
  a.p *= s;
  return a;
}

std::array<Real, 4> primitive_array(const Primitive& q) {
  return {q.rho, q.u, q.v, q.p};
}

Primitive primitive_from_array(const std::array<Real, 4>& a) {
  return {a[0], a[1], a[2], a[3]};
}

std::string bc_type(const CaseConfig& cfg, const std::string& tag) {
  const auto it = cfg.boundary_conditions.find(tag);
  if (it != cfg.boundary_conditions.end()) return it->second;
  if (tag == "UNMARKED" || tag.rfind("con-", 0) == 0) return "farfield";
  throw CfdError("mesh boundary tag '" + tag + "' is not present in case boundary_conditions");
}

Real env_real_local(const char* name, Real fallback) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return fallback;
  try {
    return std::stod(raw);
  } catch (...) {
    return fallback;
  }
}

Real reconstruction_scale() {
  static const Real scale =
      std::clamp(env_real_local("CFD_SOLVER_RECONSTRUCTION_SCALE", 1.0), 0.0, 1.0);
  return scale;
}

Conserved inviscid_normal_flux(const Primitive& q, const GasModel& gas, const Vec2& n) {
  const Real un = q.u * n.x + q.v * n.y;
  const Real E = q.p / ((gas.gamma - 1.0) * q.rho) + 0.5 * (q.u * q.u + q.v * q.v);
  return {q.rho * un, q.rho * q.u * un + q.p * n.x, q.rho * q.v * un + q.p * n.y,
          q.rho * (E + q.p / q.rho) * un};
}

Conserved rusanov_flux(const Primitive& ql, const Primitive& qr, const CaseConfig& cfg,
                       const Vec2& n) {
  const Conserved ul = primitive_to_conserved(ql, cfg.gas);
  const Conserved ur = primitive_to_conserved(qr, cfg.gas);
  const Conserved fl = inviscid_normal_flux(ql, cfg.gas, n);
  const Conserved fr = inviscid_normal_flux(qr, cfg.gas, n);
  const Real al = thermo_from_primitive(ql, cfg.gas).a;
  const Real ar = thermo_from_primitive(qr, cfg.gas).a;
  const Real unl = ql.u * n.x + ql.v * n.y;
  const Real unr = qr.u * n.x + qr.v * n.y;
  const Real dissipation_scale =
      std::max<Real>(0.0, env_real_local("CFD_SOLVER_RUSANOV_DISSIPATION_SCALE",
                                         cfg.run_control.rusanov_dissipation_scale));
  const Real s = dissipation_scale * std::max(std::abs(unl) + al, std::abs(unr) + ar);
  Conserved f{};
  for (int m = 0; m < 4; ++m) f[m] = 0.5 * (fl[m] + fr[m]) - 0.5 * s * (ur[m] - ul[m]);
  return f;
}

Primitive characteristic_farfield_state(const Primitive& ql, const Primitive& qinf,
                                        const GasModel& gas, const Vec2& n) {
  const Thermo tl = thermo_from_primitive(ql, gas);
  const Thermo ti = thermo_from_primitive(qinf, gas);
  const Real unl = ql.u * n.x + ql.v * n.y;
  const Real uni = qinf.u * n.x + qinf.v * n.y;
  if (unl >= tl.a) return ql;
  if (unl <= -tl.a) return qinf;

  const Vec2 t{-n.y, n.x};
  const Real utl = ql.u * t.x + ql.v * t.y;
  const Real uti = qinf.u * t.x + qinf.v * t.y;
  const Real rplus = unl + 2.0 * tl.a / (gas.gamma - 1.0);
  const Real rminus = uni - 2.0 * ti.a / (gas.gamma - 1.0);
  const Real un = 0.5 * (rplus + rminus);
  const Real a = std::max<Real>(1.0e-10, 0.25 * (gas.gamma - 1.0) * (rplus - rminus));
  const bool incoming = un < 0.0;
  const Real ut = incoming ? uti : utl;
  const Primitive& entropy_state = incoming ? qinf : ql;
  const Real entropy =
      std::max<Real>(entropy_state.p, 1.0e-12) /
      std::pow(std::max<Real>(entropy_state.rho, 1.0e-12), gas.gamma);
  const Real rho =
      std::pow(std::max<Real>(a * a / (gas.gamma * entropy), 1.0e-12),
               1.0 / (gas.gamma - 1.0));
  const Real p = entropy * std::pow(rho, gas.gamma);

  Primitive qb;
  qb.rho = rho;
  qb.u = un * n.x + ut * t.x;
  qb.v = un * n.y + ut * t.y;
  qb.p = p;
  return qb;
}

Primitive no_slip_wall_state(const Primitive& q) {
  Primitive w = q;
  w.u = 0.0;
  w.v = 0.0;
  return w;
}

std::vector<PrimitiveGrad> compute_gradients(const LocalMesh& mesh,
                                             const std::vector<Primitive>& prim) {
  std::vector<PrimitiveGrad> grads(mesh.cells.size());

  for (int i = 0; i < static_cast<int>(mesh.cells.size()); ++i) {
    Real a00 = 0.0;
    Real a01 = 0.0;
    Real a11 = 0.0;
    std::array<Real, 4> b0{0.0, 0.0, 0.0, 0.0};
    std::array<Real, 4> b1{0.0, 0.0, 0.0, 0.0};
    const std::array<Real, 4> qi = primitive_array(prim[static_cast<std::size_t>(i)]);
    for (int j : mesh.neighbor_cells[static_cast<std::size_t>(i)]) {
      const Vec2 d = mesh.cells[static_cast<std::size_t>(j)].center -
                     mesh.cells[static_cast<std::size_t>(i)].center;
      const Real r2 = std::max(dot(d, d), 1.0e-20);
      const Real w = 1.0 / r2;
      a00 += w * d.x * d.x;
      a01 += w * d.x * d.y;
      a11 += w * d.y * d.y;
      const std::array<Real, 4> qj = primitive_array(prim[static_cast<std::size_t>(j)]);
      for (int m = 0; m < 4; ++m) {
        const Real dq = qj[m] - qi[m];
        b0[m] += w * d.x * dq;
        b1[m] += w * d.y * dq;
      }
    }
    const Real det = a00 * a11 - a01 * a01;
    if (std::abs(det) < 1.0e-30) continue;
    for (int m = 0; m < 4; ++m) {
      grads[static_cast<std::size_t>(i)][static_cast<std::size_t>(m)].x =
          (a11 * b0[m] - a01 * b1[m]) / det;
      grads[static_cast<std::size_t>(i)][static_cast<std::size_t>(m)].y =
          (-a01 * b0[m] + a00 * b1[m]) / det;
    }
  }
  return grads;
}

std::vector<std::array<Real, 4>> compute_limiters(const LocalMesh& mesh,
                                                  const std::vector<Primitive>& prim,
                                                  const std::vector<PrimitiveGrad>& grads) {
  std::vector<std::array<Real, 4>> phi(mesh.cells.size(), {1.0, 1.0, 1.0, 1.0});
  for (int i = 0; i < static_cast<int>(mesh.cells.size()); ++i) {
    const std::array<Real, 4> qi = primitive_array(prim[static_cast<std::size_t>(i)]);
    std::array<Real, 4> qmin = qi;
    std::array<Real, 4> qmax = qi;
    for (int j : mesh.neighbor_cells[static_cast<std::size_t>(i)]) {
      const auto qj = primitive_array(prim[static_cast<std::size_t>(j)]);
      for (int m = 0; m < 4; ++m) {
        qmin[m] = std::min(qmin[m], qj[m]);
        qmax[m] = std::max(qmax[m], qj[m]);
      }
    }
    for (int iface : mesh.faces_by_cell[static_cast<std::size_t>(i)]) {
      const LocalFace& f = mesh.faces[static_cast<std::size_t>(iface)];
      const Vec2 dr = f.center - mesh.cells[static_cast<std::size_t>(i)].center;
      for (int m = 0; m < 4; ++m) {
        const Grad2& g = grads[static_cast<std::size_t>(i)][static_cast<std::size_t>(m)];
        const Real dq = g.x * dr.x + g.y * dr.y;
        Real lim = 1.0;
        if (dq > 1.0e-14) lim = std::min(1.0, (qmax[m] - qi[m]) / dq);
        if (dq < -1.0e-14) lim = std::min(1.0, (qmin[m] - qi[m]) / dq);
        phi[static_cast<std::size_t>(i)][static_cast<std::size_t>(m)] =
            std::min(phi[static_cast<std::size_t>(i)][static_cast<std::size_t>(m)],
                     std::max(0.0, lim));
      }
    }
  }
  return phi;
}

Primitive reconstruct(const LocalMesh& mesh, const std::vector<Primitive>& prim,
                      const std::vector<PrimitiveGrad>& grads,
                      const std::vector<std::array<Real, 4>>& phi, int cell, const Vec2& x) {
  std::array<Real, 4> q = primitive_array(prim[static_cast<std::size_t>(cell)]);
  const Vec2 dr = x - mesh.cells[static_cast<std::size_t>(cell)].center;
  const Real scale = reconstruction_scale();
  for (int m = 0; m < 4; ++m) {
    const Grad2& g = grads[static_cast<std::size_t>(cell)][static_cast<std::size_t>(m)];
    q[m] += scale * phi[static_cast<std::size_t>(cell)][static_cast<std::size_t>(m)] *
            (g.x * dr.x + g.y * dr.y);
  }
  q[0] = std::max(q[0], 1.0e-9);
  q[3] = std::max(q[3], 1.0e-9);
  return primitive_from_array(q);
}

Conserved viscous_flux_normal(const Primitive& q, const PrimitiveGrad& grad, const CaseConfig& cfg,
                              const Vec2& n) {
  Conserved fv{0.0, 0.0, 0.0, 0.0};
  const Real mu = viscosity(cfg);
  if (mu <= 0.0) return fv;

  const Real ux = grad[1].x;
  const Real uy = grad[1].y;
  const Real vx = grad[2].x;
  const Real vy = grad[2].y;
  const Real div = ux + vy;
  const Real tau_xx = 2.0 * mu * ux - (2.0 / 3.0) * mu * div;
  const Real tau_yy = 2.0 * mu * vy - (2.0 / 3.0) * mu * div;
  const Real tau_xy = mu * (uy + vx);

  const Real cp = cfg.gas.gamma * cfg.gas.R / (cfg.gas.gamma - 1.0);
  const Real k = mu * cp / cfg.gas.prandtl;
  const Real Tx = (grad[3].x * q.rho - q.p * grad[0].x) / (q.rho * q.rho * cfg.gas.R);
  const Real Ty = (grad[3].y * q.rho - q.p * grad[0].y) / (q.rho * q.rho * cfg.gas.R);
  const Real qx = -k * Tx;
  const Real qy = -k * Ty;

  fv[1] = tau_xx * n.x + tau_xy * n.y;
  fv[2] = tau_xy * n.x + tau_yy * n.y;
  fv[3] = (q.u * tau_xx + q.v * tau_xy - qx) * n.x +
          (q.u * tau_xy + q.v * tau_yy - qy) * n.y;
  return fv;
}

Conserved no_slip_wall_viscous_flux(const LocalMesh& mesh, const LocalFace& f, const Primitive& q,
                                    const CaseConfig& cfg) {
  Conserved fv{0.0, 0.0, 0.0, 0.0};
  const Real mu = viscosity(cfg);
  if (mu <= 0.0) return fv;
  const Vec2 t{-f.normal.y, f.normal.x};
  const Vec2 cc = mesh.cells[static_cast<std::size_t>(f.left)].center;
  const Real d = std::max(std::abs(dot(f.center - cc, f.normal)), 1.0e-8);
  const Real ut = q.u * t.x + q.v * t.y;
  const Real shear = mu * ut / d;
  fv[1] = -shear * t.x;
  fv[2] = -shear * t.y;
  fv[3] = 0.0;
  return fv;
}

}  // namespace

std::vector<Conserved> initialize_state(const LocalMesh& mesh, const CaseConfig& cfg, int rank) {
  std::vector<Conserved> state(mesh.cells.size());
  Primitive qinf = freestream_primitive(cfg);
  std::vector<Vec2> no_slip_wall_points;
  const bool use_laminar_wall_damping =
      cfg.mode == PhysicsMode::Laminar &&
      env_real_local("CFD_SOLVER_LAMINAR_WALL_DAMPING", 1.0) > 0.0;
  if (use_laminar_wall_damping) {
    for (const LocalFace& f : mesh.faces) {
      if (f.right >= 0) continue;
      const auto it = cfg.boundary_conditions.find(f.tag);
      if (it != cfg.boundary_conditions.end() && it->second == "no_slip_adiabatic_wall") {
        no_slip_wall_points.push_back(f.center);
      }
    }
  }
  const Real wall_layer =
      use_laminar_wall_damping
          ? std::max(0.02 * cfg.reference.length,
                     2.0 * cfg.reference.length / std::sqrt(std::max(cfg.reynolds, 1.0)))
          : 0.0;
  for (int i = 0; i < static_cast<int>(mesh.cells.size()); ++i) {
    Primitive q = qinf;
    if (!no_slip_wall_points.empty()) {
      Real dmin = std::numeric_limits<Real>::infinity();
      const Vec2 c = mesh.cells[static_cast<std::size_t>(i)].center;
      for (const Vec2& w : no_slip_wall_points) dmin = std::min(dmin, norm(c - w));
      const Real damping = std::tanh(dmin / std::max(wall_layer, 1.0e-12));
      q.u *= damping;
      q.v *= damping;
    }
    if (cfg.run_control.type == RunType::Transient) {
      const Vec2 c = mesh.cells[static_cast<std::size_t>(i)].center;
      const Real amp =
          std::max<Real>(0.0, env_real_local("CFD_SOLVER_TRANSIENT_PERTURBATION_AMPLITUDE", 1.0e-3));
      q.v += amp * std::sin(7.0 * c.x + 3.0 * c.y + 0.37 * rank) *
             std::exp(-0.02 * (c.x * c.x + c.y * c.y));
      const Real wake_amp =
          env_real_local("CFD_SOLVER_TRANSIENT_WAKE_PERTURBATION_AMPLITUDE", 0.0);
      if (wake_amp != 0.0 && cfg.mode == PhysicsMode::Laminar) {
        const Real x0 = env_real_local("CFD_SOLVER_TRANSIENT_WAKE_X0", 0.35);
        const Real width = std::max<Real>(
            1.0e-6, env_real_local("CFD_SOLVER_TRANSIENT_WAKE_WIDTH", 0.75));
        const Real stream_decay = std::max<Real>(
          1.0e-6, env_real_local("CFD_SOLVER_TRANSIENT_WAKE_STREAM_DECAY", 12.0));
        const Real wave_number =
            env_real_local("CFD_SOLVER_TRANSIENT_WAKE_WAVENUMBER", 2.4);
        const bool odd_mode =
            env_real_local("CFD_SOLVER_TRANSIENT_WAKE_ODD_MODE", 0.0) > 0.0;
        if (c.x > x0) {
          const Real x = c.x - x0;
          const Real lateral = std::exp(-(c.y * c.y) / (width * width));
          const Real stream = std::exp(-x / stream_decay);
          const Real mode = std::sin(wave_number * x);
          const Real varicose = std::cos(wave_number * x);
          const Real shape = odd_mode ? c.y / width : 1.0;
          q.v += wake_amp * mode * shape * lateral * stream;
          q.u -= 0.5 * std::abs(wake_amp) * varicose * std::abs(shape) * lateral * stream;
        }
      }
    }
    state[static_cast<std::size_t>(i)] = primitive_to_conserved(q, cfg.gas);
  }
  return state;
}

void exchange_halos(std::vector<Conserved>& state, const HaloPlan& halo, MPI_Comm comm) {
  std::map<int, std::vector<Real>> recv_buffers;
  std::map<int, std::vector<Real>> send_buffers;
  std::vector<MPI_Request> requests;

  for (const auto& [rank, indices] : halo.recv_ghost_local_indices) {
    if (indices.empty()) continue;
    auto& buf = recv_buffers[rank];
    buf.assign(indices.size() * 4, 0.0);
    MPI_Request req{};
    MPI_Irecv(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE, rank, 177, comm, &req);
    requests.push_back(req);
  }
  for (const auto& [rank, indices] : halo.send_owned_local_indices) {
    if (indices.empty()) continue;
    auto& buf = send_buffers[rank];
    buf.reserve(indices.size() * 4);
    for (int idx : indices) {
      const Conserved& u = state[static_cast<std::size_t>(idx)];
      for (Real v : u) buf.push_back(v);
    }
    MPI_Request req{};
    MPI_Isend(buf.data(), static_cast<int>(buf.size()), MPI_DOUBLE, rank, 177, comm, &req);
    requests.push_back(req);
  }
  if (!requests.empty()) {
    MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE);
  }
  for (const auto& [rank, indices] : halo.recv_ghost_local_indices) {
    const auto bit = recv_buffers.find(rank);
    if (bit == recv_buffers.end()) continue;
    const auto& buf = bit->second;
    for (std::size_t k = 0; k < indices.size(); ++k) {
      Conserved& u = state[static_cast<std::size_t>(indices[k])];
      for (int m = 0; m < 4; ++m) u[m] = buf[k * 4 + static_cast<std::size_t>(m)];
    }
  }
}

ResidualResult compute_residual(const LocalMesh& mesh, const CaseConfig& cfg,
                                const std::vector<Conserved>& state, MPI_Comm comm,
                                bool second_order) {
  ResidualResult rr;
  rr.residual.assign(state.size(), {0.0, 0.0, 0.0, 0.0});
  rr.spectral_radius.assign(state.size(), 1.0e-12);

  std::vector<Primitive> prim(state.size());
  for (int i = 0; i < static_cast<int>(state.size()); ++i) {
    prim[static_cast<std::size_t>(i)] = conserved_to_primitive(state[static_cast<std::size_t>(i)],
                                                               cfg.gas);
  }
  std::vector<PrimitiveGrad> grads(mesh.cells.size());
  std::vector<std::array<Real, 4>> phi(mesh.cells.size(), {1.0, 1.0, 1.0, 1.0});
  if (second_order) {
    grads = compute_gradients(mesh, prim);
    phi = compute_limiters(mesh, prim, grads);
  }
  const Primitive qinf = freestream_primitive(cfg);
  const Real mu = viscosity(cfg);

  for (const LocalFace& f : mesh.faces) {
    const bool left_owned = mesh.cells[static_cast<std::size_t>(f.left)].owned;
    const Primitive ql =
        (second_order && left_owned) ? reconstruct(mesh, prim, grads, phi, f.left, f.center)
                                    : prim[static_cast<std::size_t>(f.left)];
    Conserved flux{};
    Real spec = 0.0;
    if (f.right >= 0) {
      const bool right_owned = mesh.cells[static_cast<std::size_t>(f.right)].owned;
      const Primitive qr =
          (second_order && right_owned) ? reconstruct(mesh, prim, grads, phi, f.right, f.center)
                                       : prim[static_cast<std::size_t>(f.right)];
      flux = rusanov_flux(ql, qr, cfg, f.normal);
      Primitive qavg = 0.5 * (ql + qr);
      PrimitiveGrad gavg{};
      for (int m = 0; m < 4; ++m) {
        const Grad2& gl = grads[static_cast<std::size_t>(f.left)][static_cast<std::size_t>(m)];
        const Grad2& gr = grads[static_cast<std::size_t>(f.right)][static_cast<std::size_t>(m)];
        if (left_owned && right_owned) {
          gavg[static_cast<std::size_t>(m)].x = 0.5 * (gl.x + gr.x);
          gavg[static_cast<std::size_t>(m)].y = 0.5 * (gl.y + gr.y);
        } else if (left_owned) {
          gavg[static_cast<std::size_t>(m)] = gl;
        } else if (right_owned) {
          gavg[static_cast<std::size_t>(m)] = gr;
        }
      }
      const Conserved vf = viscous_flux_normal(qavg, gavg, cfg, f.normal);
      for (int m = 0; m < 4; ++m) flux[m] -= vf[m];
      const Real al = thermo_from_primitive(ql, cfg.gas).a;
      const Real ar = thermo_from_primitive(qr, cfg.gas).a;
      spec = std::max(std::abs(ql.u * f.normal.x + ql.v * f.normal.y) + al,
                      std::abs(qr.u * f.normal.x + qr.v * f.normal.y) + ar);
    } else {
      const std::string bc = bc_type(cfg, f.tag);
      if (bc == "farfield") {
        const Primitive qfar = characteristic_farfield_state(ql, qinf, cfg.gas, f.normal);
        flux = rusanov_flux(ql, qfar, cfg, f.normal);
        spec = std::abs(ql.u * f.normal.x + ql.v * f.normal.y) +
               thermo_from_primitive(ql, cfg.gas).a;
      } else if (bc == "slip_wall") {
        flux = {0.0, ql.p * f.normal.x, ql.p * f.normal.y, 0.0};
        spec = thermo_from_primitive(ql, cfg.gas).a;
      } else if (bc == "no_slip_adiabatic_wall") {
        flux = {0.0, ql.p * f.normal.x, ql.p * f.normal.y, 0.0};
        const Conserved vf = no_slip_wall_viscous_flux(mesh, f, ql, cfg);
        for (int m = 0; m < 4; ++m) flux[m] -= vf[m];
        spec = thermo_from_primitive(ql, cfg.gas).a + std::sqrt(ql.u * ql.u + ql.v * ql.v);
      } else {
        throw CfdError("unsupported boundary condition type: " + bc);
      }
    }

    for (int m = 0; m < 4; ++m) rr.residual[static_cast<std::size_t>(f.left)][m] +=
        flux[m] * f.length;
    rr.spectral_radius[static_cast<std::size_t>(f.left)] +=
        (spec + 4.0 * mu / std::max(mesh.cells[static_cast<std::size_t>(f.left)].area, 1.0e-12)) *
        f.length;
    if (f.right >= 0) {
      for (int m = 0; m < 4; ++m) rr.residual[static_cast<std::size_t>(f.right)][m] -=
          flux[m] * f.length;
      rr.spectral_radius[static_cast<std::size_t>(f.right)] +=
          (spec + 4.0 * mu /
                      std::max(mesh.cells[static_cast<std::size_t>(f.right)].area, 1.0e-12)) *
          f.length;
    }
  }

  Real local_sum[5]{0.0, 0.0, 0.0, 0.0, 0.0};
  Real local_linf = 0.0;
  Real local_area = 0.0;
  for (int idx : mesh.owned_local_indices) {
    const Real area = std::max(mesh.cells[static_cast<std::size_t>(idx)].area, 1.0e-14);
    const Real inv_area = 1.0 / area;
    Real cell_norm_sq = 0.0;
    for (int m = 0; m < 4; ++m) {
      const Real r = rr.residual[static_cast<std::size_t>(idx)][m] * inv_area;
      local_sum[m] += area * r * r;
      cell_norm_sq += r * r;
      local_linf = std::max(local_linf, std::abs(r));
    }
    local_sum[4] += area * cell_norm_sq;
    local_area += area;
  }
  Real global_sum[5]{};
  Real global_linf = 0.0;
  Real global_area = 0.0;
  MPI_Allreduce(local_sum, global_sum, 5, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(&local_linf, &global_linf, 1, MPI_DOUBLE, MPI_MAX, comm);
  MPI_Allreduce(&local_area, &global_area, 1, MPI_DOUBLE, MPI_SUM, comm);
  const Real denom = std::max<Real>(1.0e-300, global_area);
  rr.rho_l2 = std::sqrt(global_sum[0] / denom);
  rr.rhou_l2 = std::sqrt(global_sum[1] / denom);
  rr.rhov_l2 = std::sqrt(global_sum[2] / denom);
  rr.rhoE_l2 = std::sqrt(global_sum[3] / denom);
  rr.l2 = std::sqrt(global_sum[4] / denom);
  rr.linf = global_linf;
  return rr;
}

Conserved compute_cell_residual_first_order(const LocalMesh& mesh, const CaseConfig& cfg,
                                            const std::vector<Conserved>& state, int cell) {
  return compute_cell_residual_first_order_with_state(
      mesh, cfg, state, cell, state[static_cast<std::size_t>(cell)]);
}

Conserved compute_cell_residual_first_order_with_state(const LocalMesh& mesh, const CaseConfig& cfg,
                                                       const std::vector<Conserved>& state,
                                                       int cell,
                                                       const Conserved& cell_state) {
  Conserved residual{0.0, 0.0, 0.0, 0.0};
  const Primitive qcell = conserved_to_primitive(cell_state, cfg.gas);
  const Primitive qinf = freestream_primitive(cfg);

  for (int iface : mesh.faces_by_cell[static_cast<std::size_t>(cell)]) {
    const LocalFace& f = mesh.faces[static_cast<std::size_t>(iface)];
    Conserved flux{};
    Real sign = 1.0;
    if (f.left == cell) {
      if (f.right >= 0) {
        const Primitive qr =
            conserved_to_primitive(state[static_cast<std::size_t>(f.right)], cfg.gas);
        flux = rusanov_flux(qcell, qr, cfg, f.normal);
      } else {
        const std::string bc = bc_type(cfg, f.tag);
        if (bc == "farfield") {
          const Primitive qfar = characteristic_farfield_state(qcell, qinf, cfg.gas, f.normal);
          flux = rusanov_flux(qcell, qfar, cfg, f.normal);
        } else if (bc == "slip_wall") {
          flux = {0.0, qcell.p * f.normal.x, qcell.p * f.normal.y, 0.0};
        } else if (bc == "no_slip_adiabatic_wall") {
          flux = {0.0, qcell.p * f.normal.x, qcell.p * f.normal.y, 0.0};
          const Conserved vf = no_slip_wall_viscous_flux(mesh, f, qcell, cfg);
          for (int m = 0; m < 4; ++m) flux[m] -= vf[m];
        } else {
          throw CfdError("unsupported boundary condition type: " + bc);
        }
      }
      sign = 1.0;
    } else if (f.right == cell) {
      const Primitive ql =
          conserved_to_primitive(state[static_cast<std::size_t>(f.left)], cfg.gas);
      flux = rusanov_flux(ql, qcell, cfg, f.normal);
      sign = -1.0;
    } else {
      continue;
    }
    for (int m = 0; m < 4; ++m) residual[m] += sign * flux[m] * f.length;
  }
  return residual;
}

ForceResult compute_forces(const LocalMesh& mesh, const CaseConfig& cfg,
                           const std::vector<Conserved>& state, MPI_Comm comm) {
  ForceResult fsum;
  const Real qref = std::max(dynamic_pressure(cfg) * cfg.reference.area, 1.0e-14);
  const Real mu = viscosity(cfg);

  for (const LocalFace& f : mesh.faces) {
    if (f.right >= 0) continue;
    const std::string bc = bc_type(cfg, f.tag);
    if (bc != "slip_wall" && bc != "no_slip_adiabatic_wall") continue;
    const Primitive q = conserved_to_primitive(state[static_cast<std::size_t>(f.left)], cfg.gas);
    const Real pgauge = q.p - cfg.freestream.pressure;
    const Real fxp = pgauge * f.normal.x * f.length;
    const Real fyp = pgauge * f.normal.y * f.length;
    fsum.pressure_drag += fxp / qref;
    fsum.pressure_lift += fyp / qref;
    const Vec2 r = f.center - cfg.reference.moment_center;
    fsum.cmz += (r.x * fyp - r.y * fxp) / (qref * std::max(cfg.reference.length, 1.0e-14));

    if (bc == "no_slip_adiabatic_wall" && mu > 0.0) {
      const Vec2 t{-f.normal.y, f.normal.x};
      const Vec2 cc = mesh.cells[static_cast<std::size_t>(f.left)].center;
      const Real d = std::max(std::abs(dot(f.center - cc, f.normal)), 1.0e-8);
      const Real ut = q.u * t.x + q.v * t.y;
      const Real shear = mu * ut / d;
      const Real fxv = shear * t.x * f.length;
      const Real fyv = shear * t.y * f.length;
      fsum.viscous_drag += fxv / qref;
      fsum.viscous_lift += fyv / qref;
      fsum.cmz += (r.x * fyv - r.y * fxv) / (qref * std::max(cfg.reference.length, 1.0e-14));
    }
  }

  Real local[5]{fsum.pressure_drag, fsum.viscous_drag, fsum.pressure_lift, fsum.viscous_lift,
                fsum.cmz};
  Real global[5]{};
  MPI_Allreduce(local, global, 5, MPI_DOUBLE, MPI_SUM, comm);
  fsum.pressure_drag = global[0];
  fsum.viscous_drag = global[1];
  fsum.pressure_lift = global[2];
  fsum.viscous_lift = global[3];
  fsum.cmz = global[4];
  fsum.cd = fsum.pressure_drag + fsum.viscous_drag;
  fsum.cl = fsum.pressure_lift + fsum.viscous_lift;
  if (cfg.mode == PhysicsMode::Inviscid) {
    fsum.viscous_drag = 0.0;
    fsum.viscous_lift = 0.0;
    fsum.cd = fsum.pressure_drag;
    fsum.cl = fsum.pressure_lift;
  }
  return fsum;
}

std::vector<std::string> make_surface_rows(const LocalMesh& mesh, const CaseConfig& cfg,
                                           const std::vector<Conserved>& state) {
  std::vector<std::string> rows;
  const Real qref = std::max(dynamic_pressure(cfg), 1.0e-14);
  const Real mu = viscosity(cfg);
  for (const LocalFace& f : mesh.faces) {
    if (f.right >= 0) continue;
    const std::string bc = bc_type(cfg, f.tag);
    if (bc != "slip_wall" && bc != "no_slip_adiabatic_wall") continue;
    Primitive q = conserved_to_primitive(state[static_cast<std::size_t>(f.left)], cfg.gas);
    Primitive qb = q;
    Real cf = 0.0;
    if (bc == "no_slip_adiabatic_wall") {
      qb = no_slip_wall_state(q);
      const Vec2 t{-f.normal.y, f.normal.x};
      const Vec2 cc = mesh.cells[static_cast<std::size_t>(f.left)].center;
      const Real d = std::max(std::abs(dot(f.center - cc, f.normal)), 1.0e-8);
      const Real ut = q.u * t.x + q.v * t.y;
      cf = mu * ut / d / qref;
    } else if (bc == "slip_wall") {
      const Real un = q.u * f.normal.x + q.v * f.normal.y;
      qb.u = q.u - un * f.normal.x;
      qb.v = q.v - un * f.normal.y;
      cf = 0.0;
    }
    const Real cp = (q.p - cfg.freestream.pressure) / qref;
    const Real mach = thermo_from_primitive(qb, cfg.gas).mach;
    std::ostringstream line;
    line << std::setprecision(16) << f.center.x << ',' << f.center.y << ',' << f.normal.x << ','
         << f.normal.y << ',' << q.p << ',' << cp << ',' << cf << ',' << qb.rho << ',' << qb.u
         << ',' << qb.v << ',' << mach << ',' << f.tag;
    rows.push_back(line.str());
  }
  return rows;
}

}  // namespace cfd
