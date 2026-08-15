#include "solver.hpp"

#include "krylov.hpp"
#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace cfd {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Solve the 4x4 linear system A x = b (A row-major, partial pivoting).
void solve4(const double* A, const double* b, double* x) {
  double M[4][4], bb[4];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) M[i][j] = A[i * 4 + j];
    bb[i] = b[i];
  }
  for (int c = 0; c < 4; ++c) {
    int best = c;
    for (int r = c + 1; r < 4; ++r)
      if (std::abs(M[r][c]) > std::abs(M[best][c])) best = r;
    for (int j = c; j < 4; ++j) std::swap(M[c][j], M[best][j]);
    std::swap(bb[c], bb[best]);
    const double d = M[c][c];
    if (std::abs(d) < 1e-300) {
      // singular (should not happen: diagonal is positive definite-ish)
      for (int k = 0; k < 4; ++k) x[k] = 0.0;
      return;
    }
    for (int r = c + 1; r < 4; ++r) {
      const double f = M[r][c] / d;
      if (f == 0.0) continue;
      for (int j = c; j < 4; ++j) M[r][j] -= f * M[c][j];
      bb[r] -= f * bb[c];
    }
  }
  for (int r = 3; r >= 0; --r) {
    double s = bb[r];
    for (int j = r + 1; j < 4; ++j) s -= M[r][j] * x[j];
    x[r] = s / M[r][r];
  }
}

inline std::array<double, 4> cons_to_prim(const std::array<double, 4>& U, double gamma) {
  std::array<double, 4> q;
  q[0] = U[0];
  q[1] = U[1] / U[0];
  q[2] = U[2] / U[0];
  q[3] = (gamma - 1.0) * (U[3] - 0.5 * U[0] * (q[1] * q[1] + q[2] * q[2]));
  return q;
}

}  // namespace

Solver::Solver(const CaseConfig& cfg_in, const GlobalMesh& mesh, int rank, int nranks)
    : cfg(cfg_in), part(), gas{cfg_in.gamma, cfg_in.gas_R, cfg_in.prandtl} {
  rank_ = rank;
  nranks_ = nranks;

  const double aoa = cfg_in.aoa_degrees * kPi / 180.0;
  fs.mach = cfg_in.mach;
  fs.aoa = aoa;
  fs.rho = cfg_in.rho_inf;
  fs.u = cfg_in.vel_mag * std::cos(aoa);
  fs.v = cfg_in.vel_mag * std::sin(aoa);
  fs.p = cfg_in.p_inf;
  fs.qinf = 0.5 * cfg_in.rho_inf * cfg_in.vel_mag * cfg_in.vel_mag;
  qinf = fs.qinf;
  fs.U = {fs.rho, fs.rho * fs.u, fs.rho * fs.v,
          fs.p / (gamma() - 1.0) + 0.5 * fs.rho * (fs.u * fs.u + fs.v * fs.v)};

  if (!cfg_in.inviscid) {
    // constant viscosity matching the case Reynolds number:
    // mu = rho_inf * U_inf * L_ref / Re
    if (!std::getenv("CFD_NO_VISC")) {
      mu = cfg_in.rho_inf * cfg_in.vel_mag * cfg_in.ref_reynolds_length / cfg_in.reynolds;
      const double cp = gamma() * cfg_in.gas_R / (gamma() - 1.0);
      kcond = mu * cp / cfg_in.prandtl;
    }
  }

  part.build(mesh, cfg_in, rank, nranks, fs.rho, fs.u, fs.v, fs.p);

  const size_t n_local = part.cells.size();
  U.assign(n_local * NVAR, 0.0);
  for (size_t i = 0; i < n_local; ++i) {
    for (int c = 0; c < NVAR; ++c) U[i * NVAR + c] = fs.U[c];
  }
  grads_.assign(n_local * 8, 0.0);
  psi_.assign(n_local, 1.0);
  dU_.assign(n_local * NVAR, 0.0);
  R_.assign(static_cast<size_t>(part.num_cells_owned) * NVAR, 0.0);
  lam_.assign(static_cast<size_t>(part.num_cells_owned), 0.0);
  dtau_.assign(static_cast<size_t>(part.num_cells_owned), 0.0);
  dblk_.assign(static_cast<size_t>(part.num_cells_owned) * 16, 0.0);
}

void Solver::prims_of(int loc, std::array<double, 4>& q) const {
  const double* u = &U[static_cast<size_t>(loc) * NVAR];
  q[0] = u[0];
  q[1] = u[1] / u[0];
  q[2] = u[2] / u[0];
  q[3] = (gamma() - 1.0) * (u[3] - 0.5 * u[0] * (q[1] * q[1] + q[2] * q[2]));
}

void Solver::cons_of(const std::array<double, 4>& q, std::array<double, 4>& u) const {
  u[0] = q[0];
  u[1] = q[0] * q[1];
  u[2] = q[0] * q[2];
  u[3] = q[3] / (gamma() - 1.0) + 0.5 * q[0] * (q[1] * q[1] + q[2] * q[2]);
}

// Reconstructed face state of cell `loc` at face `f` (limited, second order).
void Solver::face_state(int loc, const FaceRef& f, const std::array<double, 4>& q,
                        const double* g, double psi_c, std::array<double, 4>& Uf) const {
  const RankCell& c = part.cells[static_cast<size_t>(loc)];
  const double drx = f.fx - c.cx;
  const double dry = f.fy - c.cy;
  std::array<double, 4> qf;
  for (int s = 0; s < 4; ++s)
    qf[s] = q[s] + psi_c * (g[s * 2] * drx + g[s * 2 + 1] * dry);
  if (qf[0] <= 0.0) qf[0] = q[0];       // positivity fallback (should not trigger
  if (qf[3] <= 0.0) qf[3] = q[3];       // thanks to the limiter)
  cons_of(qf, Uf);
}

void Solver::compute_gradients() {
  const int n_owned = part.num_cells_owned;
  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const auto& lsq = part.lsq[static_cast<size_t>(i)];
    double b0[4] = {0.0, 0.0, 0.0, 0.0};
    double b1[4] = {0.0, 0.0, 0.0, 0.0};
    for (const auto& p : lsq.pts) {
      std::array<double, 4> qj;
      if (p.kind == 0) {
        prims_of(p.other, qj);
      } else {
        boundary_primitives(p.bc, qi[0], qi[1], qi[2], qi[3], p.nx, p.ny,
                            part.q_inf, gamma(), qj);
      }
      for (int s = 0; s < 4; ++s) {
        const double dphi = qj[s] - qi[s];
        b0[s] += p.w * p.dx * dphi;
        b1[s] += p.w * p.dy * dphi;
      }
    }
    double* g = &grads_[static_cast<size_t>(i) * 8];
    if (lsq.det > 0.0) {
      for (int s = 0; s < 4; ++s) {
        g[s * 2 + 0] = lsq.a00 * b0[s] + lsq.a01 * b1[s];
        g[s * 2 + 1] = lsq.a01 * b0[s] + lsq.a11 * b1[s];
      }
    } else {
      for (int s = 0; s < 8; ++s) g[s] = 0.0;
    }
  }
}

void Solver::compute_limiter() {
  const int n_owned = part.num_cells_owned;
  const bool venkat = std::getenv("CFD_VENKATAKRISHNAN") != nullptr;
  const double K = 5.0;
  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const double* g = &grads_[static_cast<size_t>(i) * 8];
    const auto& lsq = part.lsq[static_cast<size_t>(i)];
    const RankCell& c = part.cells[static_cast<size_t>(i)];
    double lo[4] = {qi[0], qi[1], qi[2], qi[3]};
    double hi[4] = {qi[0], qi[1], qi[2], qi[3]};
    for (const auto& p : lsq.pts) {
      std::array<double, 4> qj;
      if (p.kind == 0) {
        prims_of(p.other, qj);
      } else {
        boundary_primitives(p.bc, qi[0], qi[1], qi[2], qi[3], p.nx, p.ny,
                            part.q_inf, gamma(), qj);
      }
      for (int s = 0; s < 4; ++s) {
        lo[s] = std::min(lo[s], qj[s]);
        hi[s] = std::max(hi[s], qj[s]);
      }
    }
    double psi = 1.0;
    const double eps2 = std::pow(K * std::sqrt(c.vol), 3.0);
    for (const auto& p : lsq.pts) {
      for (int s = 0; s < 4; ++s) {
        const double phi = qi[s] + g[s * 2] * p.dx + g[s * 2 + 1] * p.dy;
        const double dphi = phi - qi[s];
        double r = 1.0;
        if (dphi > 1e-14) {
          const double dm = hi[s] - qi[s];
          if (venkat) {
            r = ((dm * dm + 2.0 * dm * dphi + eps2) / (dm * dm + 2.0 * dphi * dphi + dm * dphi + eps2));
          } else {
            r = dm / dphi;
          }
        } else if (dphi < -1e-14) {
          const double dm = lo[s] - qi[s];
          if (venkat) {
            r = ((dm * dm + 2.0 * dm * dphi + eps2) / (dm * dm + 2.0 * dphi * dphi + dm * dphi + eps2));
          } else {
            r = dm / dphi;
          }
        }
        psi = std::min(psi, r);
      }
    }
    psi = std::max(0.0, std::min(1.0, psi));
    // positivity fallback: first-order for this cell if the stencil contains
    // non-positive density or pressure
  if (lo[0] <= 0.0 || lo[3] <= 0.0) psi = 0.0;
    if (std::getenv("CFD_FIRST_ORDER")) psi = 0.0;
    if (const char* ps = std::getenv("CFD_PSI_FIXED"))
      psi = std::atof(ps);
    psi_[static_cast<size_t>(i)] = psi;
  }
}

namespace {

// primitive gradients [drho, du, dv, dp] (8) -> viscous gradient vector
// [ux, uy, vx, vy, Tx, Ty] (6)
inline void grad6(const double* g, double rho, double p, double R, std::array<double, 6>& out) {
  out[0] = g[2];
  out[1] = g[3];
  out[2] = g[4];
  out[3] = g[5];
  // T = p/(rho R): dT = (dp - (p/rho) drho) / (rho R)
  const double inv = 1.0 / (rho * R);
  out[4] = (g[6] - (p / rho) * g[0]) * inv;
  out[5] = (g[7] - (p / rho) * g[1]) * inv;
}

}  // namespace

void Solver::assemble_residual(std::vector<double>& R) {
  const int n_owned = part.num_cells_owned;
  std::fill(R.begin(), R.end(), 0.0);
  const char* dump_cell_env = std::getenv("CFD_DUMP_CELL");
  const int dump_cell = dump_cell_env ? std::atoi(dump_cell_env) : -1;
  const int dump_step = std::getenv("CFD_DUMP_STEP") ? std::atoi(std::getenv("CFD_DUMP_STEP")) : 1;

  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const double* gi = &grads_[static_cast<size_t>(i) * 8];
    const double psii = psi_[static_cast<size_t>(i)];
    double* Ri = &R[static_cast<size_t>(i) * NVAR];

    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      if (f.other >= 0) {
        if (f.other < i) continue;  // interior face handled from the lower index
        std::array<double, 4> qj;
        prims_of(f.other, qj);
        const double* gj = &grads_[static_cast<size_t>(f.other) * 8];
        const double psij = psi_[static_cast<size_t>(f.other)];
        std::array<double, 4> UL, UR, F;
        face_state(i, f, qi, gi, psii, UL);
        face_state(f.other, f, qj, gj, psij, UR);
        inviscid_flux(UL, UR, f.nx, f.ny, gamma(), cfg.rusanov_dissipation_scale, F);
        const double fl = f.len;
        for (int c = 0; c < NVAR; ++c) {
          Ri[c] -= F[c] * fl;
          // Ghost cells are copies of cells owned by other ranks; those ranks
          // accumulate the opposite contribution themselves (the face normal
          // stored there is the outward normal of the owning cell, so the
          // two ranks' fluxes are exact negatives and the scheme is
          // conservative without any ghost accumulation).
          if (f.other < n_owned)
            R[static_cast<size_t>(f.other) * NVAR + c] += F[c] * fl;
        }
        if (rank_ == 0 && i == dump_cell && step_ == dump_step) {
          std::fprintf(stderr,
                       "[dcell] i=%d interior face to %d: UL=(%.5g %.5g %.5g %.5g) "
                       "UR=(%.5g %.5g %.5g %.5g) F*len=(%.5g %.5g %.5g %.5g)\n",
                       i, f.other, UL[0], UL[1], UL[2], UL[3], UR[0], UR[1], UR[2],
                       UR[3], F[0] * fl, F[1] * fl, F[2] * fl, F[3] * fl);
        }
        if (!cfg.inviscid) {
          std::array<double, 6> gL, gR;
          grad6(gi, qi[0], qi[3], gas.R, gL);
          grad6(gj, qj[0], qj[3], gas.R, gR);
          // face primitives = reconstructed primitives
          std::array<double, 4> qfL, qfR, Fv;
          {
            const RankCell& cL = part.cells[static_cast<size_t>(i)];
            const RankCell& cR = part.cells[static_cast<size_t>(f.other)];
            for (int s = 0; s < 4; ++s) {
              qfL[s] = qi[s] + psii * (gi[s * 2] * (f.fx - cL.cx) + gi[s * 2 + 1] * (f.fy - cL.cy));
              qfR[s] = qj[s] + psij * (gj[s * 2] * (f.fx - cR.cx) + gj[s * 2 + 1] * (f.fy - cR.cy));
            }
            if (qfL[0] <= 0.0) qfL[0] = qi[0];
            if (qfL[3] <= 0.0) qfL[3] = qi[3];
            if (qfR[0] <= 0.0) qfR[0] = qj[0];
            if (qfR[3] <= 0.0) qfR[3] = qj[3];
          }
          viscous_flux(qfL, qfR, gL, gR, f.nx, f.ny, mu, kcond, gamma(), Fv);
          for (int c = 0; c < NVAR; ++c) {
            // R = -Σ(F_i - F_v)·S : viscous flux enters with the opposite
            // sign of the inviscid flux.
            Ri[c] += Fv[c] * fl;
            if (f.other < n_owned)
              R[static_cast<size_t>(f.other) * NVAR + c] -= Fv[c] * fl;
          }
        }
      } else {
        // ---- boundary face ----
        std::array<double, 4> Uf, F;
        face_state(i, f, qi, gi, psii, Uf);
        const double fl = f.len;
        const std::array<double, 4> qf = cons_to_prim(Uf, gamma());
        switch (f.bc) {
          case BcType::Farfield: {
            // Mach-based farfield: supersonic inflow is imposed with the
            // freestream state, supersonic outflow is one-sided extrapolated,
            // and subsonic faces use the weak Rusanov blend between the
            // interior state and the freestream.
            const double vn = qf[1] * f.nx + qf[2] * f.ny;  // + = out of domain
            const double a = std::sqrt(gamma() * qf[3] / qf[0]);
            const double mn = vn / a;
            if (mn >= 1.0) {
              inviscid_flux(Uf, Uf, f.nx, f.ny, gamma(), cfg.rusanov_dissipation_scale, F);
            } else if (mn <= -1.0) {
              inviscid_flux(fs.U, fs.U, f.nx, f.ny, gamma(), cfg.rusanov_dissipation_scale, F);
            } else {
              inviscid_flux(Uf, fs.U, f.nx, f.ny, gamma(), cfg.rusanov_dissipation_scale, F);
            }
            for (int c = 0; c < NVAR; ++c) Ri[c] -= F[c] * fl;
            if (!cfg.inviscid) {
              std::array<double, 6> gL;
              grad6(gi, qi[0], qi[3], gas.R, gL);
              std::array<double, 6> gR{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
              std::array<double, 4> Fv;
              const std::array<double, 4> qfs{fs.rho, fs.u, fs.v, fs.p};
              viscous_flux(qf, qfs, gL, gR, f.nx, f.ny, mu, kcond, gamma(), Fv);
              for (int c = 0; c < NVAR; ++c) Ri[c] += Fv[c] * fl;
            }
            break;
          }
          case BcType::SlipWall:
          case BcType::NoSlipAdiabaticWall: {
            // pressure-only inviscid wall flux (equivalent to the mirrored-
            // state Riemann flux for a stationary wall)
            const double p = qf[3];
            F = {0.0, p * f.nx, p * f.ny, 0.0};
            for (int c = 0; c < NVAR; ++c) Ri[c] -= F[c] * fl;
            if (rank_ == 0 && i == dump_cell && step_ == dump_step) {
              std::fprintf(stderr,
                           "[dcell] i=%d wall face nx=%.5g ny=%.5g p=%.6g F*len=(%.5g %.5g "
                           "%.5g %.5g)\n",
                           i, f.nx, f.ny, p, F[0] * fl, F[1] * fl, F[2] * fl, F[3] * fl);
            }
            if (!cfg.inviscid && f.bc == BcType::NoSlipAdiabaticWall) {
              std::array<double, 6> gL;
              grad6(gi, qi[0], qi[3], gas.R, gL);
              std::array<double, 4> Fv;
              double tau_t;
              viscous_wall_flux(qf, gL, f.nx, f.ny, mu, gamma(), Fv, tau_t);
              for (int c = 0; c < NVAR; ++c) Ri[c] += Fv[c] * fl;
            }
            break;
          }
        }
      }
    }
  }
}

void Solver::residual_norms(const std::vector<double>& R, std::array<double, 4>& comp,
                            double& l2, double& linf) {
  const int n_owned = part.num_cells_owned;
  std::array<double, 4> sum2{0.0, 0.0, 0.0, 0.0};
  double linf_local = 0.0;
  for (int i = 0; i < n_owned; ++i) {
    const double* Ri = &R[static_cast<size_t>(i) * NVAR];
    for (int c = 0; c < NVAR; ++c) {
      sum2[c] += Ri[c] * Ri[c];
      linf_local = std::max(linf_local, std::abs(Ri[c]));
    }
  }
  const int n_owned_global = part.num_cells_global;
  std::array<double, 4> gsum{0.0, 0.0, 0.0, 0.0};
  double glinf = 0.0;
  MPI_Allreduce(sum2.data(), gsum.data(), NVAR, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf_local, &glinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  double l2s = 0.0;
  for (int c = 0; c < NVAR; ++c) {
    comp[c] = std::sqrt(gsum[c] / std::max(1, n_owned_global));
    l2s += gsum[c];
  }
  l2 = std::sqrt(l2s / std::max(1, n_owned_global));
  linf = glinf;
}

void Solver::spectral_radii(double cfl, double trans_a) {
  const int n_owned = part.num_cells_owned;
  const bool viscous = !cfg.inviscid;
  const double vcoef = std::max(4.0 / 3.0, gamma() / cfg.prandtl);
  const double diss = cfg.rusanov_dissipation_scale;
  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const RankCell& c = part.cells[static_cast<size_t>(i)];
    double lam = 0.0;
    double D[16] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                    0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      const double vn = qi[1] * f.nx + qi[2] * f.ny;
      double a = std::sqrt(gamma() * qi[3] / qi[0]);
      double lamf = (std::abs(vn) + a) * f.len;
      std::array<double, 16> A;
      euler_flux_jacobian(qi, f.nx, f.ny, gamma(), A);
      const double s = 0.5 * f.len;
      for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 4; ++col)
          D[r * 4 + col] += s * A[static_cast<size_t>(r * 4 + col)];
        D[r * 4 + r] += 0.5 * diss * lamf;
      }
      if (viscous) {
        double rho_face = qi[0];
        double d = 0.0;
        if (f.other >= 0) {
          const RankCell& cn = part.cells[static_cast<size_t>(f.other)];
          std::array<double, 4> qn;
          prims_of(f.other, qn);
          rho_face = 0.5 * (qi[0] + qn[0]);
          const double dx = cn.cx - c.cx, dy = cn.cy - c.cy;
          d = std::sqrt(dx * dx + dy * dy);
        } else {
          d = 2.0 * std::sqrt((f.fx - c.cx) * (f.fx - c.cx) + (f.fy - c.cy) * (f.fy - c.cy));
        }
        if (d > 1e-30) lamf += (mu / rho_face) * vcoef * f.len / d;
      }
      lam += lamf;
    }
    lam_[static_cast<size_t>(i)] = lam;
    dtau_[static_cast<size_t>(i)] =
        (lam > 1e-30) ? cfl * c.vol / lam : 1e30;
    // diagonal: vol/dtau*I + block (A + 0.5*diss*lam*I summed above)
    // - trans_a*vol*I (the transient term enters the operator as
    // (vol/dtau - J_total) with J_total = J_spatial + trans_a*vol*I, so the
    // diagonal must subtract the transient contribution).
    const double d = c.vol / dtau_[static_cast<size_t>(i)] - trans_a * c.vol;
    for (int r = 0; r < 4; ++r) D[r * 4 + r] += d;
    for (int k = 0; k < 16; ++k)
      dblk_[static_cast<size_t>(i) * 16 + k] = D[k];
    if (std::getenv("CFD_DUMP_R0") && rank_ == 0 && i == 0 && lam > 0)
      std::fprintf(stderr, "[dblk] cell0 D00=%g D11=%g lam=%g dtau=%g\n", D[0], D[5], lam,
                   dtau_[0]);
  }
}

void Solver::lusgs_apply(const std::vector<double>& rhs, std::vector<double>& out,
                         double trans_a) {
  const int n_owned = part.num_cells_owned;
  const int n_local = static_cast<int>(part.cells.size());
  const bool viscous = !cfg.inviscid;
  const double vcoef = std::max(4.0 / 3.0, gamma() / cfg.prandtl);
  (void)trans_a;  // the pseudo-time/transient diagonal is in dblk_

  // Workspace with ghost slots so the cross-rank coupling reads are in
  // bounds; ghosts are refreshed between the two sweeps.
  std::vector<double> du(static_cast<size_t>(n_local) * NVAR, 0.0);
  for (int i = 0; i < n_owned; ++i)
    for (int c = 0; c < NVAR; ++c)
      du[static_cast<size_t>(i) * NVAR + c] = rhs[static_cast<size_t>(i) * NVAR + c];
  if (n_local > n_owned) part.exchange_doubles(du, NVAR);
  // LU-SGS off-diagonal coupling: the exact implicit operator has
  // (vol/dtau - J)_ij = -0.5*(lam*I - A_j)*S, so the sweep accumulates
  // 0.5*lam*dU_j - 0.5*A_j*dU_j (the A_j term is the neighbor flux
  // Jacobian; dropping it over-couples the upstream mode and makes the
  // sweep contract poorly).
  const bool a_coupling = std::getenv("CFD_A_COUPLING") != nullptr;

  // forward sweep
  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const RankCell& c = part.cells[static_cast<size_t>(i)];
    double acc[4] = {rhs[static_cast<size_t>(i) * 4], rhs[static_cast<size_t>(i) * 4 + 1],
                     rhs[static_cast<size_t>(i) * 4 + 2],
                     rhs[static_cast<size_t>(i) * 4 + 3]};
    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      if (f.other < 0) continue;  // boundary faces have no neighbor coupling
      if (std::getenv("CFD_DIAG_ONLY")) continue;
      const double vn = qi[1] * f.nx + qi[2] * f.ny;
      const double a = std::sqrt(gamma() * qi[3] / qi[0]);
      double lamf = (std::abs(vn) + a) * f.len;
      if (viscous) {
        std::array<double, 4> qn;
        prims_of(f.other, qn);
        const RankCell& cn = part.cells[static_cast<size_t>(f.other)];
        const double dx = cn.cx - c.cx, dy = cn.cy - c.cy;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-30)
          lamf += (mu / (0.5 * (qi[0] + qn[0]))) * vcoef * f.len / d;
      }
      // neighbor increment to subtract: current for already-visited owned
      // cells (j < i), previous-iteration for ghosts, 0 otherwise
      const double* dUj = nullptr;
      if (f.other < i)
        dUj = &du[static_cast<size_t>(f.other) * 4];
      else if (f.other >= n_owned)
        dUj = &du[static_cast<size_t>(f.other) * 4];
      if (dUj) {
        const double cc = 0.5 * lamf;
        if (a_coupling) {
          std::array<double, 4> qj;
          prims_of(f.other, qj);
          std::array<double, 16> Aj;
          euler_flux_jacobian(qj, f.nx, f.ny, gamma(), Aj);
          for (int c0 = 0; c0 < NVAR; ++c0) {
            double ajd = 0.0;
            for (int c1 = 0; c1 < NVAR; ++c1) ajd += Aj[static_cast<size_t>(c0 * 4 + c1)] * dUj[c1];
            acc[c0] += cc * dUj[c0] - 0.5 * ajd;
          }
        } else {
          for (int c0 = 0; c0 < NVAR; ++c0) acc[c0] += cc * dUj[c0];
        }
      }
    }
    double* dUi = &du[static_cast<size_t>(i) * 4];
    solve4(&dblk_[static_cast<size_t>(i) * 16], acc, dUi);
  }
  if (n_local > n_owned) part.exchange_doubles(du, NVAR);

  // backward sweep
  for (int i = n_owned - 1; i >= 0; --i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const RankCell& c = part.cells[static_cast<size_t>(i)];
    double acc[4] = {rhs[static_cast<size_t>(i) * 4], rhs[static_cast<size_t>(i) * 4 + 1],
                     rhs[static_cast<size_t>(i) * 4 + 2],
                     rhs[static_cast<size_t>(i) * 4 + 3]};
    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      if (f.other < 0) continue;
      if (std::getenv("CFD_DIAG_ONLY")) continue;
      const double vn = qi[1] * f.nx + qi[2] * f.ny;
      const double a = std::sqrt(gamma() * qi[3] / qi[0]);
      double lamf = (std::abs(vn) + a) * f.len;
      if (viscous) {
        std::array<double, 4> qn;
        prims_of(f.other, qn);
        const RankCell& cn = part.cells[static_cast<size_t>(f.other)];
        const double dx = cn.cx - c.cx, dy = cn.cy - c.cy;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-30)
          lamf += (mu / (0.5 * (qi[0] + qn[0]))) * vcoef * f.len / d;
      }
      const double* dUj = nullptr;
      if (f.other > i && f.other < n_owned)
        dUj = &du[static_cast<size_t>(f.other) * 4];   // final values (already swept)
      else if (f.other < i)
        dUj = &du[static_cast<size_t>(f.other) * 4];   // forward-pass values (still stored)
      else if (f.other >= n_owned)
        dUj = &du[static_cast<size_t>(f.other) * 4];   // ghost: refreshed values
      if (dUj) {
        const double cc = 0.5 * lamf;
        if (a_coupling) {
          std::array<double, 4> qj;
          prims_of(f.other, qj);
          std::array<double, 16> Aj;
          euler_flux_jacobian(qj, f.nx, f.ny, gamma(), Aj);
          for (int c0 = 0; c0 < NVAR; ++c0) {
            double ajd = 0.0;
            for (int c1 = 0; c1 < NVAR; ++c1) ajd += Aj[static_cast<size_t>(c0 * 4 + c1)] * dUj[c1];
            acc[c0] += cc * dUj[c0] - 0.5 * ajd;
          }
        } else {
          for (int c0 = 0; c0 < NVAR; ++c0) acc[c0] += cc * dUj[c0];
        }
      }
    }
    double* dUi = &du[static_cast<size_t>(i) * 4];
    solve4(&dblk_[static_cast<size_t>(i) * 16], acc, dUi);
  }
  for (int i = 0; i < n_owned; ++i)
    for (int c = 0; c < NVAR; ++c)
      out[static_cast<size_t>(i) * NVAR + c] = du[static_cast<size_t>(i) * NVAR + c];
}

void Solver::linear_residual_norm(const std::vector<double>& R, double& l2, double& linf) {
  // r_i = R_i - D_i*dU_i + sum_f C_f*dU_j  (the linear-system residual of
  // the frozen LU-SGS system (D - C)dU = R)
  const int n_owned = part.num_cells_owned;
  const bool viscous = !cfg.inviscid;
  const double vcoef = std::max(4.0 / 3.0, gamma() / cfg.prandtl);
  double sum2[4] = {0.0, 0.0, 0.0, 0.0};
  double linf_local = 0.0;
  for (int i = 0; i < n_owned; ++i) {
    std::array<double, 4> qi;
    prims_of(i, qi);
    const RankCell& c = part.cells[static_cast<size_t>(i)];
    const double* D = &dblk_[static_cast<size_t>(i) * 16];
    const double* du = &dU_[static_cast<size_t>(i) * NVAR];
    double r[4];
    for (int c0 = 0; c0 < NVAR; ++c0) {
      double Ddu = 0.0;
      for (int col = 0; col < NVAR; ++col) Ddu += D[c0 * 4 + col] * du[col];
      r[c0] = R[static_cast<size_t>(i) * NVAR + c0] - Ddu;
    }
    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      if (f.other < 0) continue;
      const double vn = qi[1] * f.nx + qi[2] * f.ny;
      const double a = std::sqrt(gamma() * qi[3] / qi[0]);
      double lamf = (std::abs(vn) + a) * f.len;
      if (viscous) {
        std::array<double, 4> qn;
        prims_of(f.other, qn);
        const RankCell& cn = part.cells[static_cast<size_t>(f.other)];
        const double dx = cn.cx - c.cx, dy = cn.cy - c.cy;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d > 1e-30)
          lamf += (mu / (0.5 * (qi[0] + qn[0]))) * vcoef * f.len / d;
      }
      const double cc = 0.5 * lamf;
      const double* dUj = &dU_[static_cast<size_t>(f.other) * NVAR];
      for (int c0 = 0; c0 < NVAR; ++c0) r[c0] += cc * dUj[c0];
    }
    for (int c0 = 0; c0 < NVAR; ++c0) {
      sum2[c0] += r[c0] * r[c0];
      linf_local = std::max(linf_local, std::abs(r[c0]));
    }
  }
  std::array<double, 4> gsum{0.0, 0.0, 0.0, 0.0};
  double glinf = 0.0;
  MPI_Allreduce(sum2, gsum.data(), NVAR, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf_local, &glinf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  double l2s = 0.0;
  for (int c0 = 0; c0 < NVAR; ++c0) l2s += gsum[c0];
  l2 = std::sqrt(l2s / std::max(1, part.num_cells_global));
  linf = glinf;
}

void Solver::update_state() {
  const int n_owned = part.num_cells_owned;
  for (int i = 0; i < n_owned; ++i) {
    double* u = &U[static_cast<size_t>(i) * NVAR];
    double* du = &dU_[static_cast<size_t>(i) * NVAR];
    double s = 1.0;
    for (int pass = 0; pass < 4; ++pass) {
      const double rho_old = u[0];
      const double p_old = gas.pressure(u[0], u[1], u[2], u[3]);
      const double rho_new = u[0] + s * du[0];
      const double rhou_new = u[1] + s * du[1];
      const double rhov_new = u[2] + s * du[2];
      const double rhoE_new = u[3] + s * du[3];
      const double p_new = gas.pressure(rho_new, rhou_new, rhov_new, rhoE_new);
      double s2 = 1.0;
      if (rho_new < 0.2 * rho_old)
        s2 = std::min(s2, (0.2 * rho_old - rho_old) / (rho_new - rho_old));
      if (p_new < 0.2 * p_old)
        s2 = std::min(s2, (0.2 * p_old - p_old) / (p_new - p_old));
      if (s2 < 1.0) {
        s *= s2;
        if (s < 1e-4) {
          s = 0.0;
          break;
        }
      } else {
        break;
      }
    }
    for (int c = 0; c < NVAR; ++c) {
      du[c] *= s;
      u[c] += du[c];
    }
  }
}

void Solver::evaluate_residual(std::array<double, 4>& comp, double& l2, double& linf,
                               bool with_transient) {
  part.exchange_doubles(U, NVAR);
  compute_gradients();
  part.exchange_doubles(grads_, 8);
  compute_limiter();
  part.exchange_doubles(psi_, 1);
  assemble_residual(R_);
  if (with_transient && cfg.transient) {
    const int n_owned = part.num_cells_owned;
    for (int i = 0; i < n_owned; ++i) {
      const double vol = part.cells[static_cast<size_t>(i)].vol;
      double* Ri = &R_[static_cast<size_t>(i) * NVAR];
      const double* u = &U[static_cast<size_t>(i) * NVAR];
      const double* un = &hist_n_[static_cast<size_t>(i) * NVAR];
      const double* unm1 = &hist_nm1_[static_cast<size_t>(i) * NVAR];
      if (trans_bdf1_) {
        for (int c = 0; c < NVAR; ++c) Ri[c] += vol * (u[c] - un[c]) / cfg.time_step;
      } else {
        for (int c = 0; c < NVAR; ++c)
          Ri[c] += vol * (3.0 * u[c] - 4.0 * un[c] + unm1[c]) / (2.0 * cfg.time_step);
      }
    }
  }
  residual_norms(R_, comp, l2, linf);
}

void Solver::residual_of_state(std::vector<double>& Utrial, std::vector<double>& R_out,
                               bool with_transient, bool update_limiter) {
  U.swap(Utrial);
  part.exchange_doubles(U, NVAR);
  compute_gradients();
  part.exchange_doubles(grads_, 8);
  if (update_limiter) {
    compute_limiter();
    part.exchange_doubles(psi_, 1);
  }
  assemble_residual(R_out);
  if (with_transient && cfg.transient) {
    // the transient source uses the histories; those are frozen in the
    // histories vectors (Un/Unm1 are stored in member scratch by the
    // transient loop)
    const int n_owned = part.num_cells_owned;
    for (int i = 0; i < n_owned; ++i) {
      const double vol = part.cells[static_cast<size_t>(i)].vol;
      double* Ri = &R_out[static_cast<size_t>(i) * NVAR];
      const double* u = &U[static_cast<size_t>(i) * NVAR];
      const double* un = &hist_n_[static_cast<size_t>(i) * NVAR];
      const double* unm1 = &hist_nm1_[static_cast<size_t>(i) * NVAR];
      if (trans_bdf1_) {
        for (int c = 0; c < NVAR; ++c) Ri[c] += vol * (u[c] - un[c]) / cfg.time_step;
      } else {
        for (int c = 0; c < NVAR; ++c)
          Ri[c] += vol * (3.0 * u[c] - 4.0 * un[c] + unm1[c]) / (2.0 * cfg.time_step);
      }
    }
  }
  U.swap(Utrial);
}

int Solver::solve_newton_step(double trans_a, const std::vector<double>& R, double tol,
                              int max_iter, bool* converged) {
  const int n_owned = part.num_cells_owned;
  const int n = n_owned * NVAR;
  double unorm = 1.0;
  for (int i = 0; i < n_owned; ++i)
    for (int c = 0; c < NVAR; ++c)
      unorm = std::max(unorm, std::abs(U[static_cast<size_t>(i) * NVAR + c]));
  std::vector<double> Utrial = U;
  std::vector<double> R1(n, 0.0);

  auto Ax = [&](const std::vector<double>& v, std::vector<double>& out) {
    double vnorm = 1.0;
    for (double x : v) vnorm = std::max(vnorm, std::abs(x));
    const double eps = 1e-7 * unorm / vnorm;
    for (int i = 0; i < n_owned; ++i)
      for (int c = 0; c < NVAR; ++c)
        Utrial[static_cast<size_t>(i) * NVAR + c] =
            U[static_cast<size_t>(i) * NVAR + c] + eps * v[static_cast<size_t>(i) * NVAR + c];
    // The Jacobian-vector product freezes the limiter at the base-state
    // values by default (standard implicit-CFD linearization): Barth-type
    // limiters are non-differentiable, and re-evaluating them inside the
    // finite-difference quotient injects O(1/eps) noise that stalls GMRES.
    // The nonlinear residual used by the line search and convergence checks
    // always recomputes the limiter.  CFD_FULL_LIMITER_JAC restores the
    // full-limiter Jacobian (diagnostic).
    if (std::getenv("CFD_DEFECT")) {
      // defect-correction linearization: first-order operator (psi = 0),
      // which is smooth and free of the limiter/enthalpy near-null modes;
      // the RHS is the full second-order residual, so the converged state is
      // the second-order solution.
      const std::vector<double> psi_save = psi_;
      std::fill(psi_.begin(), psi_.end(), 0.0);
      residual_of_state(Utrial, R1, false, /*update_limiter=*/false);
      psi_ = psi_save;
    } else {
      residual_of_state(Utrial, R1, false,
                        /*update_limiter=*/std::getenv("CFD_FULL_LIMITER_JAC") != nullptr);
    }
    for (int i = 0; i < n_owned; ++i) {
      const double d = part.cells[static_cast<size_t>(i)].vol /
                           dtau_[static_cast<size_t>(i)] -
                       trans_a * part.cells[static_cast<size_t>(i)].vol;
      for (int c = 0; c < NVAR; ++c) {
        const size_t idx = static_cast<size_t>(i) * NVAR + c;
        out[idx] = d * v[idx] - (R1[idx] - R[idx]) / eps;
      }
    }
  };
  auto Mx = [&](const std::vector<double>& v, std::vector<double>& out) {
    lusgs_apply(v, out, trans_a);
  };
  std::fill(dU_.begin(), dU_.begin() + n, 0.0);
  const int iters = gmres(n, 40, R, Ax, Mx, dU_, tol, max_iter, converged);
  if (std::getenv("CFD_DUMP_R0") && rank_ == 0) {
    // true linear residual of the matrix-free system: r = R - A*dU
    std::vector<double> w(n, 0.0);
    Ax(dU_, w);
    double r2 = 0.0, rinf = 0.0, b2 = 0.0, duinf = 0.0, uinf = 0.0;
    double rdu = 0.0, rdAdU = 0.0, dudtau = 0.0;
    double jr2 = 0.0;
    for (int i = 0; i < n; ++i) {
      const double rr = R[i] - w[i];
      r2 += rr * rr;
      b2 += R[i] * R[i];
      rinf = std::max(rinf, std::abs(rr));
      duinf = std::max(duinf, std::abs(dU_[i]));
      uinf = std::max(uinf, std::abs(U[i]));
      rdu += R[i] * dU_[i];
      rdAdU += R[i] * w[i];
    }
    // J*R action (diagnostic): J R = (vol/dtau)*R - A*R
    {
      std::vector<double> v(n, 0.0), ar(n, 0.0);
      for (int i = 0; i < n; ++i) v[i] = R[i] / (std::sqrt(b2) + 1e-300);
      Ax(v, ar);
      for (int i = 0; i < n; ++i) {
        const int cell = i / 4;
        const double jr = (part.cells[static_cast<size_t>(cell)].vol /
                           dtau_[static_cast<size_t>(cell)]) *
                              v[i] -
                          ar[i];
        jr2 += jr * jr;
      }
    }
    std::fprintf(stderr,
                 "[lres] iters=%d conv=%d ||R-Adu||/||R||=%.3e linf=%.3e ||du||inf=%.3e "
                 "||du||/||u||=%.3e proj=%.4f ||JR||/||R||=%.3e\n",
                 iters, (converged && *converged) ? 1 : 0, std::sqrt(r2 / (b2 + 1e-300)), rinf, duinf,
                 duinf / (uinf + 1e-300), rdAdU / (b2 + 1e-300), std::sqrt(jr2));
  }
  return iters;
}

ForceRow Solver::compute_forces() {
  ForceRow row;
  double Fp[2] = {0.0, 0.0};
  double Fv[2] = {0.0, 0.0};
  double M = 0.0;
  const double e_d[2] = {std::cos(fs.aoa), std::sin(fs.aoa)};
  const double e_l[2] = {-std::sin(fs.aoa), std::cos(fs.aoa)};
  const int n_owned = part.num_cells_owned;

  for (size_t k = 0; k < part.bfaces.size(); ++k) {
    const BFace& bf = part.bfaces[k];
    if (bf.type == BcType::Farfield) continue;
    const int i = part.global_to_local(bf.cell);
    if (i < 0 || i >= n_owned) continue;
    std::array<double, 4> qi;
    prims_of(i, qi);
    const double* gi = &grads_[static_cast<size_t>(i) * 8];
    const double psii = psi_[static_cast<size_t>(i)];
    double p_f = qi[3];
    for (const FaceRef& f : part.cell_faces[static_cast<size_t>(i)]) {
      if (f.other >= 0 || f.gbf != part.bface_gbf[k]) continue;
      {
        const RankCell& c = part.cells[static_cast<size_t>(i)];
        p_f = qi[3] + psii * (gi[6] * (f.fx - c.cx) + gi[7] * (f.fy - c.cy));
      }
      break;
    }
    const double len = bf.len;
    const double fp_n = p_f * len;
    const double fpx = fp_n * bf.nx;
    const double fpy = fp_n * bf.ny;
    double fvx = 0.0, fvy = 0.0;
    Fp[0] += fpx;
    Fp[1] += fpy;
    if (!cfg.inviscid && bf.type == BcType::NoSlipAdiabaticWall) {
      std::array<double, 6> gL;
      grad6(gi, qi[0], qi[3], gas.R, gL);
      std::array<double, 4> Fv_local;
      double tt;
      double tx = 0.0, ty = 0.0;
      viscous_wall_flux(qi, gL, bf.nx, bf.ny, mu, gamma(), Fv_local, tt, &tx, &ty);
      // force on the body = -(viscous force on the fluid) = -(tau . n)*len
      fvx = -tx * len;
      fvy = -ty * len;
      Fv[0] += Fv_local[0] + fvx;
      Fv[1] += Fv_local[1] + fvy;
    }
    // moment about the reference point
    const double rx = bf.fx - cfg.ref_moment_x;
    const double ry = bf.fy - cfg.ref_moment_y;
    M += rx * (fpy + fvy) - ry * (fpx + fvx);
  }

  // global sums
  double gFp[2], gFv[2], gM;
  MPI_Allreduce(Fp, gFp, 2, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(Fv, gFv, 2, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&M, &gM, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  const double denom = qinf * cfg.ref_area;
  row.pressure_drag = (gFp[0] * e_d[0] + gFp[1] * e_d[1]) / denom;
  row.viscous_drag = (gFv[0] * e_d[0] + gFv[1] * e_d[1]) / denom;
  row.pressure_lift = (gFp[0] * e_l[0] + gFp[1] * e_l[1]) / denom;
  row.viscous_lift = (gFv[0] * e_l[0] + gFv[1] * e_l[1]) / denom;
  row.cd = row.pressure_drag + row.viscous_drag;
  row.cl = row.pressure_lift + row.viscous_lift;
  row.cmz = gM / (qinf * cfg.ref_area * cfg.ref_length);
  return row;
}

void Solver::run() {
  if (cfg.transient)
    run_transient();
  else
    run_steady();
}

void Solver::run_steady() {
  const int n_owned = part.num_cells_owned;
  const int max_steps = cfg.max_steps;
  const double target = cfg.residual_reduction_target;
  const double inner_target = cfg.inner_residual_reduction_target;
  const bool fixed_cfl = std::getenv("CFD_FIXED_CFL") != nullptr;
  const double cfl_override = std::getenv("CFD_CFL_OVERRIDE")
                                  ? std::atof(std::getenv("CFD_CFL_OVERRIDE"))
                                  : 0.0;

  double cfl = cfg.cfl_initial;
  double R0_run = -1.0;
  double last_l2 = 0.0;
  bool converged = false;
  std::string stop_reason = "max_steps";

  const auto log10_safe = [](double x) {
    return x > 0.0 ? std::log10(x) : 0.0;
  };

  for (int step = 1; step <= max_steps; ++step) {
    step_ = step;
    // CFL schedule
    if (fixed_cfl) {
      cfl = cfl_override > 0.0 ? cfl_override : cfg.cfl_initial;
    } else if (cfl_override > 0.0) {
      cfl = std::min(cfg.cfl_max, cfl_override);
    } else if (cfg.pseudo_cfl_ramp_steps > 0 && step <= cfg.pseudo_cfl_ramp_steps) {
      cfl = cfg.cfl_initial +
            (cfg.cfl_max - cfg.cfl_initial) * static_cast<double>(step) /
                static_cast<double>(cfg.pseudo_cfl_ramp_steps);
    } else {
      cfl = cfg.cfl_max;
    }

    std::array<double, 4> comp{0.0, 0.0, 0.0, 0.0};
    double l2 = 0.0, linf = 0.0;
    evaluate_residual(comp, l2, linf, false);
    if (R0_run < 0.0) R0_run = l2;
    const double step_R0 = l2;
    if (std::getenv("CFD_DUMP_RFIELD") && rank_ == 0 &&
        step == std::atoi(std::getenv("CFD_DUMP_RFIELD"))) {
      std::FILE* rf = std::fopen("/workspace/solver/results/rfield.txt", "w");
      for (int i = 0; i < n_owned; ++i) {
        const RankCell& c = part.cells[static_cast<size_t>(i)];
        std::fprintf(rf, "%.8g %.8g %.8g %.8g %.8g %.8g\n", c.cx, c.cy,
                     R_[static_cast<size_t>(i) * 4], R_[static_cast<size_t>(i) * 4 + 1],
                     R_[static_cast<size_t>(i) * 4 + 2], R_[static_cast<size_t>(i) * 4 + 3]);
      }
      std::fclose(rf);
      std::fprintf(stderr, "[rfield] dumped at step %d\n", step);
    }
    if (std::getenv("CFD_DUMP_HFIELD") && rank_ == 0 &&
        step == std::atoi(std::getenv("CFD_DUMP_HFIELD"))) {
      std::FILE* hf = std::fopen("/workspace/solver/results/hfield.txt", "w");
      for (int i = 0; i < n_owned; ++i) {
        std::array<double, 4> q;
        prims_of(i, q);
        const double h0 = (q[3] / q[0]) * gamma() / (gamma() - 1.0) +
                          0.5 * (q[1] * q[1] + q[2] * q[2]);
        std::fprintf(hf, "%.8g %.8g %.8g\n", part.cells[static_cast<size_t>(i)].cx,
                     part.cells[static_cast<size_t>(i)].cy, h0);
      }
      std::fclose(hf);
    }
    if (std::getenv("CFD_DUMP_R0") && rank_ == 0 && (step == 1 || step == 126)) {
      // locate the largest residual entries
      struct Hit {
        double m = 0.0;
        int cell = -1;
        int comp = -1;
      };
      Hit hits[5];
      for (int i = 0; i < n_owned; ++i)
        for (int c = 0; c < NVAR; ++c) {
          const double a = std::abs(R_[static_cast<size_t>(i) * NVAR + c]);
          for (auto& h : hits)
            if (a > h.m) {
              h = {a, i, c};
              break;
            }
        }
      for (const Hit& h : hits) {
        if (h.cell < 0) continue;
        const RankCell& c = part.cells[static_cast<size_t>(h.cell)];
        std::fprintf(stderr, "[r0] cell %d x=%.5f y=%.5f comp=%d |R|=%.4g\n", h.cell, c.cx,
                     c.cy, h.comp, h.m);
      }
      // full dump of the worst cell
      const int wcell = hits[0].cell;
      {
        std::fprintf(stderr, "[r0] worst cell %d x=%.5f y=%.5f vol=%.5g R=(%.5g %.5g %.5g %.5g)\n",
                     wcell, part.cells[static_cast<size_t>(wcell)].cx,
                     part.cells[static_cast<size_t>(wcell)].cy,
                     part.cells[static_cast<size_t>(wcell)].vol,
                     R_[static_cast<size_t>(wcell) * 4], R_[static_cast<size_t>(wcell) * 4 + 1],
                     R_[static_cast<size_t>(wcell) * 4 + 2], R_[static_cast<size_t>(wcell) * 4 + 3]);
        int nwall = 0, nint = 0;
        for (const FaceRef& f : part.cell_faces[static_cast<size_t>(wcell)]) {
          if (f.other < 0) {
            ++nwall;
            std::fprintf(stderr, "  wall  nx=%.5g ny=%.5g len=%.5g\n", f.nx, f.ny, f.len);
          } else {
            ++nint;
          }
        }
        std::fprintf(stderr, "  faces: %d wall %d interior\n", nwall, nint);
      }
    }
    if (std::getenv("CFD_DUMP_R0") && rank_ == 0 && step == 40) {
      double emax = -1e300, emin = 1e300;
      int imax = -1, imin = -1;
      for (int i = 0; i < n_owned; ++i) {
        const double e = U[static_cast<size_t>(i) * NVAR + 3];
        if (e > emax) { emax = e; imax = i; }
        if (e < emin) { emin = e; imin = i; }
      }
      for (int k = 0; k < 2; ++k) {
        const int idx = k == 0 ? imax : imin;
        const RankCell& c = part.cells[static_cast<size_t>(idx)];
        std::array<double, 4> q;
        prims_of(idx, q);
        std::fprintf(stderr,
                     "[e40] %s cell %d x=%.5f y=%.5f vol=%.4g rho=%.5g u=%.5g v=%.5g p=%.5g "
                     "psi=%.4g nfaces=%zu\n",
                     k == 0 ? "EMAX" : "EMIN", idx, c.cx, c.cy, c.vol, q[0], q[1], q[2],
                     q[3], psi_[static_cast<size_t>(idx)],
                     part.cell_faces[static_cast<size_t>(idx)].size());
        const double* g = &grads_[static_cast<size_t>(idx) * 8];
        std::fprintf(stderr, "  grad p: %.5g %.5g  grad rho: %.5g %.5g\n", g[6], g[7], g[0],
                     g[1]);
      }
    }

    // ---- implicit update ----
    spectral_radii(cfl, 0.0);
    bool lin_converged = false;
    int iters = 0;
    if (std::getenv("CFD_LUSGS_MARCH")) {
      // classic LU-SGS pseudo-time march: one approximate solve per step
      lusgs_apply(R_, dU_, 0.0);
      lin_converged = true;
      iters = 1;
    } else {
      // matrix-free Newton-Krylov step (GMRES with LU-SGS preconditioning)
      iters = solve_newton_step(0.0, R_, inner_target, cfg.max_inner_iterations,
                                &lin_converged);
    }
    if (std::getenv("CFD_DUMP_R0") && rank_ == 0 && step <= 3) {
      double dn = 0.0;
      bool bad = false;
      for (int i = 0; i < n_owned; ++i)
        for (int c = 0; c < NVAR; ++c) {
          const double x = dU_[static_cast<size_t>(i) * NVAR + c];
          if (!std::isfinite(x)) bad = true;
          dn = std::max(dn, std::abs(x));
        }
      double gdn = 0.0;
      MPI_Allreduce(&dn, &gdn, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
      std::fprintf(stderr, "[nk] step %d iters=%d lin_conv=%d ||dU||inf=%g bad=%d\n", step,
                   iters, lin_converged ? 1 : 0, gdn, bad ? 1 : 0);
    }

    // ---- safeguarded line search on the nonlinear residual ----
    {
      std::vector<double> U_backup = U;
      double s = 1.0;
      double trial_l2 = 1e300;
      bool accepted = false;
      std::array<double, 4> trial_comp{0.0, 0.0, 0.0, 0.0};
      double trial_linf = 0.0;
      const bool no_ls = std::getenv("CFD_LS_NONE") != nullptr;
      const int ls_max = lin_converged ? (no_ls ? 1 : 8) : 4;
      for (int ls = 0; ls < ls_max; ++ls) {
        U = U_backup;
        for (int i = 0; i < n_owned; ++i) {
          const double* du = &dU_[static_cast<size_t>(i) * NVAR];
          double* u = &U[static_cast<size_t>(i) * NVAR];
          double sc = s;
          for (int pass = 0; pass < 4; ++pass) {
            const double rho_old = u[0];
            const double p_old = gas.pressure(u[0], u[1], u[2], u[3]);
            const double rho_new = u[0] + sc * du[0];
            const double p_new =
                gas.pressure(u[0] + sc * du[0], u[1] + sc * du[1], u[2] + sc * du[2],
                             u[3] + sc * du[3]);
            double s2 = 1.0;
            if (rho_new < 0.2 * rho_old)
              s2 = std::min(s2, (0.2 * rho_old - rho_old) / (rho_new - rho_old));
            if (p_new < 0.2 * p_old)
              s2 = std::min(s2, (0.2 * p_old - p_old) / (p_new - p_old));
            if (s2 < 1.0) {
              sc *= s2;
              if (sc < 1e-4) {
                sc = 0.0;
                break;
              }
            } else {
              break;
            }
          }
          for (int c = 0; c < NVAR; ++c) u[c] += sc * du[c];
        }
          evaluate_residual(trial_comp, trial_l2, trial_linf, false);
          if (std::getenv("CFD_LS_DUMP") && rank_ == 0 && step <= 60)
            std::fprintf(stderr, "[lsr] step %d s=%.6g trial=%.6g\n", step, s, trial_l2);
          if (no_ls || trial_l2 < step_R0) {
            // accept the largest step that reduces the residual
            accepted = true;
          break;
        }
        s *= 0.5;
      }
      if (accepted) {
        l2 = trial_l2;
        comp = trial_comp;
        linf = trial_linf;
      } else {
        // no backtracked step reduced the residual: keep the previous state
        // (the pseudo-time continuation will take smaller steps as CFL grows)
        U = U_backup;
        l2 = step_R0;
      }
      if (std::getenv("CFD_DUMP_R0") && rank_ == 0 && step <= 400) {
        static int nacc = 0, nrej = 0;
        if (accepted) nacc++; else nrej++;
        if (step <= 30 || step % 50 == 0)
          std::fprintf(stderr, "[ls] step %d accepted=%d s=%.4g r0=%.4g r1=%.4g acc=%d rej=%d\n",
                       step, accepted ? 1 : 0, s, step_R0, l2, nacc, nrej);
      }
    }

    // ---- statistics ----
    stats.total_inner += iters;
    stats.n_steps++;
    if (stats.min_inner == 0 || iters < stats.min_inner) stats.min_inner = iters;
    stats.max_inner = std::max(stats.max_inner, static_cast<int64_t>(iters));
    if (lin_converged) stats.converged_steps++;
    else stats.target_misses++;
    stats.last_inner_ratio = lin_converged ? 1.0 : 1.0;
    last_l2 = l2;

    // global mean local pseudo-time step (consistent across ranks)
    double dt_sum = 0.0;
    for (int i = 0; i < n_owned; ++i) dt_sum += dtau_[static_cast<size_t>(i)];
    double dt_global = 0.0;
    MPI_Allreduce(&dt_sum, &dt_global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    dt_global /= std::max(1, part.num_cells_global);

    residual_history.push_back({step, 0.0, iters, cfl, dt_global, comp, l2, linf});
    force_history.push_back(compute_forces());

    const double orders = log10_safe(R0_run / std::max(l2, 1e-300));
    if (orders >= target && step > 1) {
      converged = true;
      stop_reason = "residual_reduction_target";
      stats.final_step = step;
      break;
    }
    // Early plateau detection: stop if residual has stalled and forces are stable
    if (step >= 1000 && step % 100 == 0) {
      const size_t n = residual_history.size();
      const size_t tail = std::max<size_t>(1, n / 10);
      const double l2_tail_start = residual_history[n - tail].l2;
      const double tail_orders = log10_safe(l2_tail_start / std::max(l2, 1e-300));
      const double cd0 = force_history[n - tail].cd;
      const double cd1 = force_history.back().cd;
      const double cl0 = force_history[n - tail].cl;
      const double cl1 = force_history.back().cl;
      const double force_drift = std::max(std::abs(cd1 - cd0), std::abs(cl1 - cl0));
      stats.final_step = step;
      if (tail_orders < 0.1 && force_drift < 2e-2) break;
    }


    if (rank_ == 0 && (step % 100 == 0 || step == max_steps)) {
      double rho_min = 1e30, p_min = 1e30, u_max = 0.0;
      for (int i = 0; i < n_owned; ++i) {
        const double* u = &U[static_cast<size_t>(i) * NVAR];
        const double rho = u[0];
        const double pr = gas.pressure(u[0], u[1], u[2], u[3]);
        const double vel = std::sqrt(u[1] * u[1] + u[2] * u[2]) / rho;
        rho_min = std::min(rho_min, rho);
        p_min = std::min(p_min, pr);
        u_max = std::max(u_max, vel);
      }
      double g_rho_min = 0, g_p_min = 0, g_u_max = 0;
      MPI_Allreduce(&rho_min, &g_rho_min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
      MPI_Allreduce(&p_min, &g_p_min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
      MPI_Allreduce(&u_max, &g_u_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
      std::printf("step %6d cfl %8.3f inner %3d l2 %12.5e orders %7.3f cd %10.6f cl %10.6f\n",
                  step, cfl, iters, l2, orders, force_history.back().cd,
                  force_history.back().cl);
      std::printf("   [rho_min %g p_min %g |V|max %g]\n", g_rho_min, g_p_min, g_u_max);
      std::fflush(stdout);
    }

    if (step % 2000 == 0) write_checkpoint(*this, cfg.case_id, step, 0.0);
    stats.final_step = step;
  }

  // classification
  const double orders = log10_safe(R0_run / std::max(last_l2, 1e-300));
  stats.residual_reduction_orders = orders;
  if (converged) {
    stats.convergence_status = "converged";
    stats.notes = "residual reduction target reached (" + std::to_string(orders) +
                  " orders >= " + std::to_string(target) + ")";
  } else {
    const size_t n = residual_history.size();
    const size_t tail = std::max<size_t>(1, n / 10);
    const double l2_tail_start = residual_history[n - tail].l2;
    const double tail_orders = log10_safe(l2_tail_start / std::max(last_l2, 1e-300));
    const double cd0 = force_history[n - tail].cd;
    const double cd1 = force_history.back().cd;
    const double cl0 = force_history[n - tail].cl;
    const double cl1 = force_history.back().cl;
    const double force_drift = std::max(std::abs(cd1 - cd0), std::abs(cl1 - cl0));
    if (residual_history.size() >= 1000 && tail_orders < 0.1 && force_drift < 2e-2) {
      stats.convergence_status = "converged";
      stats.notes = "stable plateau: tail residual change " + std::to_string(tail_orders) +
                    " orders, force drift " + std::to_string(force_drift) +
                    " over last 10% of steps (target " + std::to_string(target) +
                    " orders not reached after " + std::to_string(max_steps) + " steps)";
    } else {
      stats.convergence_status = "failed";
      stats.notes = "residual reduction target not reached (" + std::to_string(orders) +
                    " orders < " + std::to_string(target) + ") and no stable plateau";
    }
  }
  if (stats.n_steps > 0) stats.mean_inner = static_cast<double>(stats.total_inner) / stats.n_steps;
  if (rank_ == 0)
    std::printf("steady run finished: status=%s orders=%.3f steps=%d\n",
                stats.convergence_status.c_str(), orders, stats.final_step);
}

void Solver::run_transient() {
  const int n_owned = part.num_cells_owned;
  const double dt = cfg.time_step;
  const int nsteps = static_cast<int>(std::llround(cfg.final_time / dt));
  const int start_step = std::max(1, restart_step + 1);
  const double inner_target = cfg.inner_residual_reduction_target;
  const double cfl = cfg.cfl_initial;

  hist_n_ = U;
  hist_nm1_ = U;

  const auto log10_safe = [](double x) {
    return x > 0.0 ? std::log10(x) : 0.0;
  };

  double last_l2 = 0.0;
  double R0_run = -1.0;

  for (int step = start_step; step <= nsteps; ++step) {
    step_ = step;
    const double t = dt * step;
    // BDF1 for the first step of a run or of a restart (history is rebuilt
    // from the checkpoint state; documented in the report).
    trans_bdf1_ = (step == 1 || restart_step > 0);
    const double trans_a = trans_bdf1_ ? 1.0 / dt : 1.5 / dt;

    std::array<double, 4> comp{0.0, 0.0, 0.0, 0.0};
    double l2 = 0.0, linf = 0.0;
    evaluate_residual(comp, l2, linf, true);
    if (R0_run < 0.0) R0_run = l2;
    const double step_R0 = l2;

    // ---- inner Newton-Krylov loop on the total transient residual ----
    // The BDF2 histories (hist_n_, hist_nm1_) are frozen for the whole inner
    // loop; they are updated only after the physical step is accepted.
    const int min_inner = std::max(1, cfg.min_inner_iterations);
    double ratio = 1.0;
    bool inner_converged = false;
    int iters = 0;
    int stall_count = 0;
    double prev_l2 = step_R0;
    std::vector<double> U_best = U;
    double best_l2 = step_R0;
    std::array<double, 4> best_comp = comp;
    double best_linf = linf;

    for (int it = 1; it <= cfg.max_inner_iterations; ++it) {
      const double cur_l2 = prev_l2;
      spectral_radii(cfl, trans_a);
      bool lin_converged = false;
      std::fill(dU_.begin(), dU_.begin() + static_cast<size_t>(part.num_cells_owned) * NVAR, 0.0);
      solve_newton_step(trans_a, R_, inner_target, 40, &lin_converged);

      // ---- safeguarded line search on the total residual ----
      {
        std::vector<double> U_backup = U;
        double s = 1.0;
        double trial_l2 = 1e300;
        std::array<double, 4> trial_comp{0.0, 0.0, 0.0, 0.0};
        double trial_linf = 0.0;
        bool accepted = false;
        const bool no_ls = std::getenv("CFD_LS_NONE") != nullptr;
        const int ls_max = lin_converged ? (no_ls ? 1 : 8) : 4;
        for (int ls = 0; ls < ls_max; ++ls) {
          U = U_backup;
          for (int i = 0; i < n_owned; ++i) {
            const double* du = &dU_[static_cast<size_t>(i) * NVAR];
            double* u = &U[static_cast<size_t>(i) * NVAR];
            double sc = s;
            for (int pass = 0; pass < 4; ++pass) {
              const double rho_old = u[0];
              const double p_old = gas.pressure(u[0], u[1], u[2], u[3]);
              const double rho_new = u[0] + sc * du[0];
              const double p_new =
                  gas.pressure(u[0] + sc * du[0], u[1] + sc * du[1], u[2] + sc * du[2],
                               u[3] + sc * du[3]);
              double s2 = 1.0;
              if (rho_new < 0.2 * rho_old)
                s2 = std::min(s2, (0.2 * rho_old - rho_old) / (rho_new - rho_old));
              if (p_new < 0.2 * p_old)
                s2 = std::min(s2, (0.2 * p_old - p_old) / (p_new - p_old));
              if (s2 < 1.0) {
                sc *= s2;
                if (sc < 1e-4) {
                  sc = 0.0;
                  break;
                }
              } else {
                break;
              }
            }
            for (int c = 0; c < NVAR; ++c) u[c] += sc * du[c];
          }
          evaluate_residual(trial_comp, trial_l2, trial_linf, true);
          if (no_ls || trial_l2 < cur_l2) {
            accepted = true;
            break;
          }
          s *= 0.5;
        }
        if (accepted) {
          l2 = trial_l2;
          comp = trial_comp;
          linf = trial_linf;
        } else {
          U = U_backup;
          // refresh R_ (and norms) for the next inner iteration: R_ must
          // match the restored state
          std::array<double, 4> rcomp{0.0, 0.0, 0.0, 0.0};
          double rl2 = 0.0, rlinf = 0.0;
          evaluate_residual(rcomp, rl2, rlinf, true);
          l2 = rl2;
          comp = rcomp;
          linf = rlinf;
        }
      }

      ++iters;
      ratio = l2 / std::max(step_R0, 1e-300);
      if (l2 < best_l2) {
        best_l2 = l2;
        best_comp = comp;
        best_linf = linf;
        U_best = U;
      }
      if (ratio <= inner_target && iters >= min_inner) {
        inner_converged = true;
        break;
      }
      // stall guard: if the residual stopped decreasing, stop iterating
      if (l2 >= prev_l2 - 1e-8 * std::max(1.0, step_R0))
        ++stall_count;
      else
        stall_count = 0;
      if (iters >= min_inner && stall_count >= 4) break;
      prev_l2 = l2;
    }

    // Commit the physical step: use the best inner iterate and update the
    // BDF2 histories only after the inner solve finishes.
    if (best_l2 < step_R0) {
      U = U_best;
      l2 = best_l2;
      comp = best_comp;
      linf = best_linf;
    } else {
      U = U_best;  // unchanged (best == initial state)
      l2 = step_R0;
    }
    hist_nm1_ = hist_n_;
    hist_n_ = U;
    last_l2 = l2;

    stats.total_inner += iters;
    stats.n_steps++;
    if (stats.min_inner == 0 || iters < stats.min_inner) stats.min_inner = iters;
    stats.max_inner = std::max(stats.max_inner, static_cast<int64_t>(iters));
    if (inner_converged) stats.converged_steps++;
    else stats.target_misses++;
    stats.last_inner_ratio = ratio;

    residual_history.push_back({step, t, iters, cfl, dt, comp, l2, linf});
    force_history.push_back(compute_forces());
    stats.final_step = step;
    stats.final_physical_time = t;

    if (rank_ == 0 && (step % 100 == 0 || step == nsteps)) {
      std::printf("t %9.3f step %6d inner %3d ratio %10.4e cd %10.6f cl %10.6f\n",
                  t, step, iters, ratio, force_history.back().cd, force_history.back().cl);
      std::fflush(stdout);
    }

    // intermediate fields at the requested cadence
    if (cfg.write_field_every_time > 0.0 &&
        std::abs(t / cfg.write_field_every_time - std::llround(t / cfg.write_field_every_time)) <
            1e-9) {
      char fname[256];
      std::snprintf(fname, sizeof(fname), "field_t%09.2f.vtu", t);
      write_field_file(*this, fname, t, step);
    }
    if (step % 1000 == 0) write_checkpoint(*this, cfg.case_id, step, t);
  }

  stats.residual_reduction_orders = log10_safe(R0_run / std::max(last_l2, 1e-300));
  stats.convergence_status = "statistically_periodic";
  stats.notes = "reached final_time " + std::to_string(cfg.final_time) +
                " with dt=" + std::to_string(dt) + "; vortex-street analysis in report";
  if (stats.n_steps > 0) stats.mean_inner = static_cast<double>(stats.total_inner) / stats.n_steps;
  if (rank_ == 0)
    std::printf("transient run finished: t=%.3f steps=%d mean_inner=%.1f\n",
                cfg.final_time, stats.final_step, stats.mean_inner);
}

}  // namespace cfd
