#include "spatial.hpp"

#include <algorithm>
#include <cmath>
#include "physics.hpp"

namespace cfd {

void FlowState::allocate(const LocalMesh& m) {
  U.assign(m.n_cells, Vec4{});
  W.assign(m.n_cells, Vec4{});
  gradW.assign(m.n_cells, std::array<double, 8>{});
  phi.assign(m.n_cells, Vec4{1.0, 1.0, 1.0, 1.0});
  R.assign(m.n_owned, Vec4{});
  face_lambda.assign(m.n_faces, 0.0);
  lambda_sum.assign(m.n_owned, 0.0);
}

void Halo::exchange(const LocalMesh& m, double* data, int nvar) {
  const int nnbr = static_cast<int>(m.nbr_rank.size());
  if (nnbr == 0) return;
  const size_t nsend = static_cast<size_t>(m.send_cells.size()) * nvar;
  const size_t nrecv = static_cast<size_t>(m.recv_cells.size()) * nvar;
  if (send_buf.size() < nsend) send_buf.resize(nsend);
  if (recv_buf.size() < nrecv) recv_buf.resize(nrecv);
  send_reqs.assign(nnbr, MPI_REQUEST_NULL);
  recv_reqs.assign(nnbr, MPI_REQUEST_NULL);

  for (int k = 0; k < nnbr; ++k) {
    int n = (m.recv_start[k + 1] - m.recv_start[k]) * nvar;
    MPI_Irecv(recv_buf.data() + static_cast<size_t>(m.recv_start[k]) * nvar, n,
              MPI_DOUBLE, m.nbr_rank[k], 0, comm, &recv_reqs[k]);
  }
  for (int k = 0; k < nnbr; ++k) {
    double* dst = send_buf.data() + static_cast<size_t>(m.send_start[k]) * nvar;
    for (int c = m.send_start[k]; c < m.send_start[k + 1]; ++c) {
      const double* src = data + static_cast<size_t>(m.send_cells[c]) * nvar;
      std::copy(src, src + nvar, dst);
      dst += nvar;
    }
    int n = (m.send_start[k + 1] - m.send_start[k]) * nvar;
    MPI_Isend(send_buf.data() + static_cast<size_t>(m.send_start[k]) * nvar, n,
              MPI_DOUBLE, m.nbr_rank[k], 0, comm, &send_reqs[k]);
  }
  MPI_Waitall(nnbr, recv_reqs.data(), MPI_STATUSES_IGNORE);
  for (int k = 0; k < nnbr; ++k) {
    const double* src = recv_buf.data() + static_cast<size_t>(m.recv_start[k]) * nvar;
    for (int c = m.recv_start[k]; c < m.recv_start[k + 1]; ++c) {
      double* dst = data + static_cast<size_t>(m.recv_cells[c]) * nvar;
      std::copy(src, src + nvar, dst);
      src += nvar;
    }
  }
  MPI_Waitall(nnbr, send_reqs.data(), MPI_STATUSES_IGNORE);
}

Vec4 freestream_prim(const CaseFile& cs, double time, double pert_aoa_deg,
                     double pert_duration) {
  double aoa = cs.aoa_deg;
  if (pert_aoa_deg != 0.0 && pert_duration > 0.0 && time < pert_duration) {
    double w = 1.0 - time / pert_duration;
    aoa += pert_aoa_deg * w;  // linear taper of the perturbation angle
  }
  const double rad = aoa * M_PI / 180.0;
  Vec4 W;
  W[0] = cs.fs_rho;
  W[1] = cs.fs_vmag * std::cos(rad);
  W[2] = cs.fs_vmag * std::sin(rad);
  W[3] = cs.fs_p;
  return W;
}

Vec4 boundary_lsq_value(BCType bc, const Vec4& Wi, double nx, double ny,
                        const Vec4& Wfs) {
  switch (bc) {
    case BCType::Farfield:
      return Wfs;
    case BCType::SlipWall: {
      double un = Wi[1] * nx + Wi[2] * ny;
      return Vec4{Wi[0], Wi[1] - un * nx, Wi[2] - un * ny, Wi[3]};
    }
    case BCType::NoSlipAdiabaticWall:
      // Wall value: zero velocity; density/pressure from the interior cell
      // (implies zero normal gradient of p and T at the wall).
      return Vec4{Wi[0], 0.0, 0.0, Wi[3]};
  }
  return Wi;
}

void compute_primitives(const LocalMesh& m, const GasModel& gas, FlowState& s) {
  for (int i = 0; i < m.n_cells; ++i) s.W[i] = cons_to_prim(s.U[i], gas);
}

void compute_gradients(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                       const AssembleOpts& o) {
  const Vec4 Wfs = freestream_prim(cs, o.time, o.pert_aoa_deg, o.pert_duration);
  for (int i = 0; i < m.n_owned; ++i) {
    const Vec4& Wi = s.W[i];
    double b[4][2] = {{0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}};
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      Vec4 Wb;
      if (m.face_r[f] >= 0) {
        int nb = (m.face_l[f] == i) ? m.face_r[f] : m.face_l[f];
        Wb = s.W[nb];
      } else {
        BCType bc = cs.bc_map.at(m.bc_names[m.face_bc[f]]);
        Wb = boundary_lsq_value(bc, Wi, m.face_nx[f], m.face_ny[f], Wfs);
      }
      double w = m.cf_w[k], dx = m.cf_dx[k], dy = m.cf_dy[k];
      for (int v = 0; v < 4; ++v) {
        double d = Wb[v] - Wi[v];
        b[v][0] += w * d * dx;
        b[v][1] += w * d * dy;
      }
    }
    double m00 = m.lsq_m00[i], m01 = m.lsq_m01[i], m11 = m.lsq_m11[i];
    for (int v = 0; v < 4; ++v) {
      s.gradW[i][2 * v] = m00 * b[v][0] + m01 * b[v][1];
      s.gradW[i][2 * v + 1] = m01 * b[v][0] + m11 * b[v][1];
    }
  }
}

void compute_limiter(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                     const AssembleOpts& o) {
  if (o.limiter == 0) {
    for (int i = 0; i < m.n_owned; ++i)
      s.phi[i] = Vec4{0.0, 0.0, 0.0, 0.0};  // first-order fallback
    return;
  }
  const Vec4 Wfs = freestream_prim(cs, o.time, o.pert_aoa_deg, o.pert_duration);
  for (int i = 0; i < m.n_owned; ++i) {
    const Vec4& Wi = s.W[i];
    const auto& gi = s.gradW[i];
    Vec4 mn = Wi, mx = Wi;
    // Stencil extrema (neighbors + boundary virtual values).
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      Vec4 Wb;
      if (m.face_r[f] >= 0) {
        int nb = (m.face_l[f] == i) ? m.face_r[f] : m.face_l[f];
        Wb = s.W[nb];
      } else {
        BCType bc = cs.bc_map.at(m.bc_names[m.face_bc[f]]);
        Wb = boundary_lsq_value(bc, Wi, m.face_nx[f], m.face_ny[f], Wfs);
      }
      for (int v = 0; v < 4; ++v) {
        mn[v] = std::min(mn[v], Wb[v]);
        mx[v] = std::max(mx[v], Wb[v]);
      }
    }
    const double h = std::sqrt(m.vol[i]);
    const double eps2 = std::pow(o.venkat_k * h, 3.0);
    Vec4 phi{1.0, 1.0, 1.0, 1.0};
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      double dx = m.face_cx[f] - m.cx[i];
      double dy = m.face_cy[f] - m.cy[i];
      for (int v = 0; v < 4; ++v) {
        double d2 = gi[2 * v] * dx + gi[2 * v + 1] * dy;
        double d1 = (d2 > 0.0) ? (mx[v] - Wi[v]) : (mn[v] - Wi[v]);
        double pf = 1.0;
        if (std::fabs(d2) > 1e-14 * (std::fabs(Wi[v]) + 1.0)) {
          if (o.limiter == 1) {
            // Barth--Jespersen
            pf = std::min(1.0, d1 / d2);
          } else {
            // Venkatakrishnan-type smooth limiter
            double num = d1 * d1 + 2.0 * eps2 + 2.0 * d1 * d2;
            double den = d1 * d1 + 2.0 * d2 * d2 + d1 * d2 + eps2;
            pf = (den > 0.0) ? num / den : 1.0;
          }
          pf = std::max(0.0, std::min(1.0, pf));
          phi[v] = std::min(phi[v], pf);
        }
      }
    }
    s.phi[i] = phi;
  }
}

namespace {

// Reconstruct the primitive state of cell i at face f with limiting and a
// positivity fallback (falls back to the cell value if rho or p go
// non-positive at the face).
inline Vec4 reconstruct_at_face(const LocalMesh& m, const FlowState& s, int i, int f) {
  const Vec4& Wi = s.W[i];
  if (i >= m.n_owned) return Wi;  // ghosts carry owner-limited grad/phi arrays
  const auto& g = s.gradW[i];
  const Vec4& ph = s.phi[i];
  double dx = m.face_cx[f] - m.cx[i];
  double dy = m.face_cy[f] - m.cy[i];
  Vec4 Wf;
  for (int v = 0; v < 4; ++v)
    Wf[v] = Wi[v] + ph[v] * (g[2 * v] * dx + g[2 * v + 1] * dy);
  if (Wf[0] < RHO_FLOOR || Wf[3] < P_FLOOR) return Wi;  // positivity fallback
  return Wf;
}

// Reconstruction for a ghost cell: use the owner-provided gradient and
// limiter (exchanged through the halo).
inline Vec4 reconstruct_ghost(const LocalMesh& m, const FlowState& s, int i, int f) {
  const Vec4& Wi = s.W[i];
  const auto& g = s.gradW[i];
  const Vec4& ph = s.phi[i];
  double dx = m.face_cx[f] - m.cx[i];
  double dy = m.face_cy[f] - m.cy[i];
  Vec4 Wf;
  for (int v = 0; v < 4; ++v)
    Wf[v] = Wi[v] + ph[v] * (g[2 * v] * dx + g[2 * v + 1] * dy);
  if (Wf[0] < RHO_FLOOR || Wf[3] < P_FLOOR) return Wi;
  return Wf;
}

inline Vec4 reconstruct_any(const LocalMesh& m, const FlowState& s, int i, int f) {
  return (i < m.n_owned) ? reconstruct_at_face(m, s, i, f)
                         : reconstruct_ghost(m, s, i, f);
}

// Characteristic farfield boundary state from interior state WL and
// freestream Wfs (outward unit normal n).
Vec4 farfield_state(const Vec4& WL, const Vec4& Wfs, double nx, double ny,
                    const GasModel& gas) {
  const double gm1 = gas.gamma - 1.0;
  double unL = WL[1] * nx + WL[2] * ny;
  double aL = sound_speed(WL[0], WL[3], gas);
  double unF = Wfs[1] * nx + Wfs[2] * ny;
  double aF = sound_speed(Wfs[0], Wfs[3], gas);
  if (unL <= -aL) return Wfs;  // supersonic inflow
  if (unL >= aL) return WL;    // supersonic outflow
  double tx = -ny, ty = nx;
  double utL = WL[1] * tx + WL[2] * ty;
  double utF = Wfs[1] * tx + Wfs[2] * ty;
  double Rp = unL + 2.0 * aL / gm1;
  double Rm = unF - 2.0 * aF / gm1;
  double unb = 0.5 * (Rp + Rm);
  double ab = 0.25 * gm1 * (Rp - Rm);
  ab = std::max(ab, 1e-8);
  double rhob, pb, utb;
  if (unb < 0.0) {
    // subsonic inflow: entropy and tangential velocity from freestream
    rhob = Wfs[0] * std::pow(ab * ab / (aF * aF), 1.0 / gm1);
    pb = Wfs[3] * std::pow(rhob / Wfs[0], gas.gamma);
    utb = utF;
  } else {
    // subsonic outflow: entropy and tangential velocity from interior
    rhob = WL[0] * std::pow(ab * ab / (aL * aL), 1.0 / gm1);
    pb = WL[3] * std::pow(rhob / WL[0], gas.gamma);
    utb = utL;
  }
  return Vec4{rhob, unb * nx + utb * tx, unb * ny + utb * ty, pb};
}

}  // namespace

void assemble_residual(const LocalMesh& m, const CaseFile& cs, FlowState& s,
                       const AssembleOpts& o, ForceSums& fs) {
  fs.reset();
  s.non_finite = false;
  const GasModel& gas = cs.gas;
  const Vec4 Wfs = freestream_prim(cs, o.time, o.pert_aoa_deg, o.pert_duration);
  for (int i = 0; i < m.n_owned; ++i) {
    s.R[i] = Vec4{0.0, 0.0, 0.0, 0.0};
    if (o.compute_dt) s.lambda_sum[i] = 0.0;
  }

  const double mu = cs.fs.mu;
  const double kc = cs.fs.k_cond;

  for (int f = 0; f < m.n_faces; ++f) {
    const int L = m.face_l[f];
    const int R = m.face_r[f];
    const double nx = m.face_nx[f], ny = m.face_ny[f], A = m.face_area[f];

    double lam_face;
    if (R >= 0) {
      // ---------------- internal face ----------------
      Vec4 WL = reconstruct_any(m, s, L, f);
      Vec4 WR = reconstruct_any(m, s, R, f);
      Vec4 F = (o.inviscid_flux == 0)
                   ? roe_flux(WL, WR, nx, ny, gas)
                   : rusanov_flux(WL, WR, nx, ny, gas, o.rusanov_scale);

      double un = 0.5 * ((WL[1] + WR[1]) * nx + (WL[2] + WR[2]) * ny);
      double af = 0.5 * (sound_speed(WL[0], WL[3], gas) +
                         sound_speed(WR[0], WR[3], gas));
      lam_face = (std::fabs(un) + af) * A;

      if (o.viscous) {
        // Corrected-average face gradients of u, v, T. The finite-difference
        // correction along the cell-to-cell direction MUST use cell-center
        // values (s.W), not the reconstructed face states: WL and WR are both
        // evaluated at the same face centroid, so (WR - WL)/d would vanish
        // for smooth fields and destroy the normal-derivative coupling.
        double dx = m.cx[R] - m.cx[L];
        double dy = m.cy[R] - m.cy[L];
        double d = std::max(std::hypot(dx, dy), 1e-30);
        double ex = dx / d, ey = dy / d;
        double rhoL = WL[0], rhoR = WR[0];
        double TL = s.W[L][3] / (s.W[L][0] * gas.R);
        double TR = s.W[R][3] / (s.W[R][0] * gas.R);
        const auto& gL = s.gradW[L];
        const auto& gR = s.gradW[R];
        auto face_grad = [&](int comp, double vL, double vR) -> std::pair<double, double> {
          double gx = 0.5 * (gL[2 * comp] + gR[2 * comp]);
          double gy = 0.5 * (gL[2 * comp + 1] + gR[2 * comp + 1]);
          double gn = (vR - vL) / d;
          double corr = gn - (gx * ex + gy * ey);
          return {gx + corr * ex, gy + corr * ey};
        };
        auto gu = face_grad(1, s.W[L][1], s.W[R][1]);
        auto gv = face_grad(2, s.W[L][2], s.W[R][2]);
        // T gradient from primitive gradients at each cell, then corrected.
        auto cell_Tgrad = [&](int c, double rho, double T) {
          double gx = (s.gradW[c][6] - T * s.gradW[c][0]) / (rho * gas.R);
          double gy = (s.gradW[c][7] - T * s.gradW[c][1]) / (rho * gas.R);
          return std::pair<double, double>{gx, gy};
        };
        auto gTL = cell_Tgrad(L, rhoL, TL);
        auto gTR = cell_Tgrad(R, rhoR, TR);
        double Tgx = 0.5 * (gTL.first + gTR.first);
        double Tgy = 0.5 * (gTL.second + gTR.second);
        {
          double gn = (TR - TL) / d;  // cell-center temperatures
          double corr = gn - (Tgx * ex + Tgy * ey);
          Tgx += corr * ex;
          Tgy += corr * ey;
        }
        double uf = 0.5 * (WL[1] + WR[1]);
        double vf = 0.5 * (WL[2] + WR[2]);
        Vec4 Fv = viscous_flux_phys(uf, vf, gu.first, gu.second, gv.first, gv.second,
                                    Tgx, Tgy, mu, kc, nx, ny);
        for (int k = 0; k < 4; ++k) F[k] -= Fv[k];
        // Viscous spectral radius contribution.
        double rho_f = 0.5 * (rhoL + rhoR);
        lam_face += 2.0 * mu / std::max(rho_f, RHO_FLOOR) * A / d;
      }

      for (int k = 0; k < 4; ++k) {
        s.R[L][k] += F[k] * A;
        if (R < m.n_owned) s.R[R][k] -= F[k] * A;
      }
      if (o.compute_dt) {
        s.lambda_sum[L] += lam_face;
        if (R < m.n_owned) s.lambda_sum[R] += lam_face;
      }
    } else {
      // ---------------- boundary face ----------------
      BCType bc = cs.bc_map.at(m.bc_names[m.face_bc[f]]);
      Vec4 WL = reconstruct_at_face(m, s, L, f);
      Vec4 F{0.0, 0.0, 0.0, 0.0};
      double unL = WL[1] * nx + WL[2] * ny;
      double aL = sound_speed(WL[0], WL[3], gas);
      lam_face = (std::fabs(unL) + aL) * A;
      double pw = WL[3];  // reconstructed wall/farfield pressure (left state)

      if (bc == BCType::Farfield) {
        Vec4 Wb = farfield_state(WL, Wfs, nx, ny, gas);
        F = (o.inviscid_flux == 0)
                ? roe_flux(WL, Wb, nx, ny, gas)
                : rusanov_flux(WL, Wb, nx, ny, gas, o.rusanov_scale);
      } else {
        // Wall faces: inviscid flux is the consistent pressure-only flux
        // (identical to the mirror-state Riemann flux at a stationary wall).
        F[1] = pw * nx;
        F[2] = pw * ny;
        if (bc == BCType::NoSlipAdiabaticWall && o.viscous) {
          // Mirrored ghost: u_g = -u_L, T_g = T_L. Corrected face gradient
          // uses the normal difference to the reflected ghost center.
          double dxc = m.face_cx[f] - m.cx[L];
          double dyc = m.face_cy[f] - m.cy[L];
          double dist = std::max(std::hypot(dxc, dyc), 1e-30);
          double ex = dxc / dist, ey = dyc / dist;
          double d = 2.0 * dist;
          double rhoL = WL[0];
          const auto& gL = s.gradW[L];
          // Mirror correction uses CELL-CENTER velocity: u_ghost = -u_cell at
          // the reflected ghost center (distance 2*dist). Using the
          // face-reconstructed WL here would double-count the extrapolation
          // and drive the wall shear toward zero at second order.
          auto wall_grad = [&](int comp, double vL) -> std::pair<double, double> {
            double gx = gL[2 * comp], gy = gL[2 * comp + 1];
            double gn = (-vL - vL) / d;  // (v_ghost - v_cell) / d, v_ghost = -v_cell
            double corr = gn - (gx * ex + gy * ey);
            return {gx + corr * ex, gy + corr * ey};
          };
          auto gu = wall_grad(1, s.W[L][1]);
          auto gv = wall_grad(2, s.W[L][2]);
          // Adiabatic: normal temperature gradient is exactly zero.
          double Tgx = 0.0, Tgy = 0.0;
          Vec4 Fv = viscous_flux_phys(0.0, 0.0, gu.first, gu.second, gv.first,
                                      gv.second, Tgx, Tgy, mu, kc, nx, ny);
          for (int k = 0; k < 4; ++k) F[k] -= Fv[k];
          lam_face += 2.0 * mu / std::max(rhoL, RHO_FLOOR) * A / dist;

          // Forces on the body: with the face normal n pointing out of the
          // fluid domain (into the wall), the fluid-to-body force is
          //   F = integral p n dA  -  integral tau . n dA.
          // The skin-friction part uses only the tangential shear traction.
          double div = gu.first + gv.second;
          double txx = 2.0 * mu * gu.first - 2.0 / 3.0 * mu * div;
          double tyy = 2.0 * mu * gv.second - 2.0 / 3.0 * mu * div;
          double txy = mu * (gu.second + gv.first);
          double tx = txx * nx + txy * ny;
          double ty = txy * nx + tyy * ny;
          double tn = tx * nx + ty * ny;
          double ttx = tx - tn * nx, tty = ty - tn * ny;  // tangential traction
          fs.fx_p += pw * nx * A;
          fs.fy_p += pw * ny * A;
          fs.fx_v -= ttx * A;
          fs.fy_v -= tty * A;
          double rx = m.face_cx[f] - cs.ref.moment_center[0];
          double ry = m.face_cy[f] - cs.ref.moment_center[1];
          fs.mz_p += (rx * (pw * ny) - ry * (pw * nx)) * A;
          fs.mz_v -= (rx * tty - ry * ttx) * A;
        } else if (bc == BCType::SlipWall) {
          fs.fx_p += pw * nx * A;
          fs.fy_p += pw * ny * A;
          double rx = m.face_cx[f] - cs.ref.moment_center[0];
          double ry = m.face_cy[f] - cs.ref.moment_center[1];
          fs.mz_p += (rx * (pw * ny) - ry * (pw * nx)) * A;
        }
      }

      for (int k = 0; k < 4; ++k) s.R[L][k] += F[k] * A;
      if (o.compute_dt) s.lambda_sum[L] += lam_face;
    }
    if (o.compute_dt) s.face_lambda[f] = lam_face;
  }

  // Finite check on owned residuals (cheap; catches divergent states early).
  for (int i = 0; i < m.n_owned; ++i)
    for (int k = 0; k < 4; ++k)
      if (!std::isfinite(s.R[i][k])) s.non_finite = true;
}

std::vector<SurfaceRow> compute_surface_rows(const LocalMesh& m, const CaseFile& cs,
                                             FlowState& s, const AssembleOpts& o) {
  std::vector<SurfaceRow> rows;
  const GasModel& gas = cs.gas;
  const double mu = cs.fs.mu;
  for (int f = 0; f < m.n_faces; ++f) {
    if (m.face_bc[f] < 0) continue;
    BCType bc = cs.bc_map.at(m.bc_names[m.face_bc[f]]);
    if (bc == BCType::Farfield) continue;  // surface.csv covers walls
    const int L = m.face_l[f];
    const double nx = m.face_nx[f], ny = m.face_ny[f];
    Vec4 WL = reconstruct_at_face(m, s, L, f);
    SurfaceRow row;
    row.x = m.face_cx[f];
    row.y = m.face_cy[f];
    row.nx = nx;
    row.ny = ny;
    row.pressure = WL[3];
    row.cp = (WL[3] - cs.fs_p) / cs.fs.q_dyn;
    row.family = m.face_bc[f];
    double aL = sound_speed(WL[0], WL[3], gas);
    if (bc == BCType::NoSlipAdiabaticWall) {
      // Boundary-condition values at a no-slip wall: velocity is zero.
      row.rho = WL[0];
      row.u = 0.0;
      row.v = 0.0;
      row.mach = 0.0;
      if (o.viscous) {
        double dxc = m.face_cx[f] - m.cx[L];
        double dyc = m.face_cy[f] - m.cy[L];
        double dist = std::max(std::hypot(dxc, dyc), 1e-30);
        double ex = dxc / dist, ey = dyc / dist;
        double d = 2.0 * dist;
        const auto& gL = s.gradW[L];
        auto wall_grad = [&](int comp, double vL) {
          double gx = gL[2 * comp], gy = gL[2 * comp + 1];
          double gn = (-vL - vL) / d;
          double corr = gn - (gx * ex + gy * ey);
          return std::pair<double, double>{gx + corr * ex, gy + corr * ey};
        };
        // Mirror uses CELL-CENTER velocity (see assemble_residual): the
        // face-reconstructed WL is ~0 at the wall and would zero the
        // normal-derivative correction, underreporting cf massively.
        auto gu = wall_grad(1, s.W[L][1]);
        auto gv = wall_grad(2, s.W[L][2]);
        double div = gu.first + gv.second;
        double txx = 2.0 * mu * gu.first - 2.0 / 3.0 * mu * div;
        double tyy = 2.0 * mu * gv.second - 2.0 / 3.0 * mu * div;
        double txy = mu * (gu.second + gv.first);
        double tx = txx * nx + txy * ny;
        double ty = txy * nx + tyy * ny;
        double tn = tx * nx + ty * ny;
        double ttx = tx - tn * nx, tty = ty - tn * ny;
        // Signed cf along the face tangential direction.
        double t_hat_x = -ny, t_hat_y = nx;
        row.cf = -(ttx * t_hat_x + tty * t_hat_y) / cs.fs.q_dyn;
      } else {
        row.cf = 0.0;
      }
    } else {
      // Slip wall: zero normal velocity, tangential velocity preserved.
      double un = WL[1] * nx + WL[2] * ny;
      double ub = WL[1] - un * nx;
      double vb = WL[2] - un * ny;
      row.rho = WL[0];
      row.u = ub;
      row.v = vb;
      row.mach = std::hypot(ub, vb) / aL;
      row.cf = 0.0;
    }
    rows.push_back(row);
  }
  return rows;
}

}  // namespace cfd
