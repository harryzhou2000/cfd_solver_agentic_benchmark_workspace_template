#include "solver.h"

#include <filesystem>

namespace cfd {
namespace {

constexpr int TAG_FIELDS = 101;
constexpr int TAG_DU = 102;
constexpr int TAG_STATE = 103;

struct SolverData {
  const CaseConfig& cfg;
  const LocalMesh& lm;
  GasModel gas;
  int nlocal = 0, nowned = 0;

  std::vector<Cons> U;            // local states (owned + ghost)
  std::vector<Cons> Un, Unm1;     // transient histories (local)
  std::vector<Prim> W;            // primitives (local)
  std::vector<PrimGrad> grads;    // limited gradients (owned; ghosts via halo)
  std::vector<std::array<double, 4>> psi;  // limiters (owned; ghosts via halo)
  GradWeights gw;

  std::vector<Cons> Res;          // owned residual (flux balance)
  std::vector<Cons> Res_phys;     // owned total transient residual
  std::vector<Cons> dU;           // local corrections
  std::vector<Cons> dU_prev;      // previous inner-sweep correction
  std::vector<double> dtau;       // owned pseudo time step
  std::vector<double> diag;       // owned implicit diagonal

  double phys_coeff = 0.0;        // physical-time diagonal coefficient (V/dt factor)
  double phys_a0 = 0.0, phys_a1 = 0.0;  // Res_phys = V/dt (a0 (U-Un) + a1 (U-Unm1))

  // Force accumulators (local sums).
  double fx_p = 0.0, fy_p = 0.0, fx_v = 0.0, fy_v = 0.0, mz = 0.0;
};

// ---------------------------------------------------------------------------
// Halo exchange helpers
// ---------------------------------------------------------------------------
void exchange_fields(SolverData& sd) {
  const LocalMesh& lm = sd.lm;
  int nn = (int)lm.neighbors.size();
  std::vector<int> send_off(nn + 1, 0), recv_off(nn + 1, 0);
  for (int k = 0; k < nn; ++k) {
    send_off[k + 1] = send_off[k] + (int)lm.send_cells[k].size() * 16;
    recv_off[k + 1] = recv_off[k] + (int)lm.recv_cells[k].size() * 16;
  }
  std::vector<double> sendbuf(send_off[nn]), recvbuf(recv_off[nn]);
  for (int k = 0; k < nn; ++k) {
    double* p = sendbuf.data() + send_off[k];
    for (int li : lm.send_cells[k]) {
      p[0] = sd.U[li].rho; p[1] = sd.U[li].rhou; p[2] = sd.U[li].rhov; p[3] = sd.U[li].rhoE;
      const PrimGrad& g = sd.grads[li];
      p[4] = g.rx; p[5] = g.ry; p[6] = g.ux; p[7] = g.uy;
      p[8] = g.vx; p[9] = g.vy; p[10] = g.px; p[11] = g.py;
      const auto& ps = sd.psi[li];
      p[12] = ps[0]; p[13] = ps[1]; p[14] = ps[2]; p[15] = ps[3];
      p += 16;
    }
  }
  std::vector<MPI_Request> reqs;
  reqs.reserve(2 * nn);
  for (int k = 0; k < nn; ++k) {
    if (recv_off[k + 1] > recv_off[k]) {
      MPI_Request r;
      MPI_Irecv(recvbuf.data() + recv_off[k], recv_off[k + 1] - recv_off[k],
                MPI_DOUBLE, lm.neighbors[k], TAG_FIELDS, MPI_COMM_WORLD, &r);
      reqs.push_back(r);
    }
    if (send_off[k + 1] > send_off[k]) {
      MPI_Request r;
      MPI_Isend(sendbuf.data() + send_off[k], send_off[k + 1] - send_off[k],
                MPI_DOUBLE, lm.neighbors[k], TAG_FIELDS, MPI_COMM_WORLD, &r);
      reqs.push_back(r);
    }
  }
  if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
  if (getenv("CFD_DEBUG_WALL")) {
    for (int k = 0; k < nn; ++k) {
      for (int li : lm.send_cells[k]) {
        if (li >= lm.nowned) g_log.logf("DBG xch send target not owned: %d\n", li);
      }
      for (int li : lm.recv_cells[k]) {
        if (li < lm.nowned) g_log.logf("DBG xch recv target is owned: %d\n", li);
      }
    }
  }
  for (int k = 0; k < nn; ++k) {
    const double* p = recvbuf.data() + recv_off[k];
    for (int li : lm.recv_cells[k]) {
      sd.U[li].rho = p[0]; sd.U[li].rhou = p[1]; sd.U[li].rhov = p[2]; sd.U[li].rhoE = p[3];
      PrimGrad& g = sd.grads[li];
      g.rx = p[4]; g.ry = p[5]; g.ux = p[6]; g.uy = p[7];
      g.vx = p[8]; g.vy = p[9]; g.px = p[10]; g.py = p[11];
      auto& ps = sd.psi[li];
      ps[0] = p[12]; ps[1] = p[13]; ps[2] = p[14]; ps[3] = p[15];
      p += 16;
    }
  }
}

void exchange_dU(SolverData& sd) {
  const LocalMesh& lm = sd.lm;
  int nn = (int)lm.neighbors.size();
  std::vector<int> send_off(nn + 1, 0), recv_off(nn + 1, 0);
  for (int k = 0; k < nn; ++k) {
    send_off[k + 1] = send_off[k] + (int)lm.send_cells[k].size() * 4;
    recv_off[k + 1] = recv_off[k] + (int)lm.recv_cells[k].size() * 4;
  }
  std::vector<double> sendbuf(send_off[nn]), recvbuf(recv_off[nn]);
  for (int k = 0; k < nn; ++k) {
    double* p = sendbuf.data() + send_off[k];
    for (int li : lm.send_cells[k]) {
      p[0] = sd.dU[li].rho; p[1] = sd.dU[li].rhou; p[2] = sd.dU[li].rhov; p[3] = sd.dU[li].rhoE;
      p += 4;
    }
  }
  std::vector<MPI_Request> reqs;
  reqs.reserve(2 * nn);
  for (int k = 0; k < nn; ++k) {
    if (recv_off[k + 1] > recv_off[k]) {
      MPI_Request r;
      MPI_Irecv(recvbuf.data() + recv_off[k], recv_off[k + 1] - recv_off[k],
                MPI_DOUBLE, lm.neighbors[k], TAG_DU, MPI_COMM_WORLD, &r);
      reqs.push_back(r);
    }
    if (send_off[k + 1] > send_off[k]) {
      MPI_Request r;
      MPI_Isend(sendbuf.data() + send_off[k], send_off[k + 1] - send_off[k],
                MPI_DOUBLE, lm.neighbors[k], TAG_DU, MPI_COMM_WORLD, &r);
      reqs.push_back(r);
    }
  }
  if (!reqs.empty()) MPI_Waitall((int)reqs.size(), reqs.data(), MPI_STATUSES_IGNORE);
  for (int k = 0; k < nn; ++k) {
    const double* p = recvbuf.data() + recv_off[k];
    for (int li : lm.recv_cells[k]) {
      sd.dU[li].rho = p[0]; sd.dU[li].rhou = p[1]; sd.dU[li].rhov = p[2]; sd.dU[li].rhoE = p[3];
      p += 4;
    }
  }
}

// ---------------------------------------------------------------------------
// Primitives / gradients
// ---------------------------------------------------------------------------
void update_primitives(SolverData& sd) {
  for (int i = 0; i < sd.nlocal; ++i) sd.W[i] = to_prim(sd.U[i], sd.gas);
}

void compute_gradients(SolverData& sd, long long& fallback_count) {
  static int first_order = getenv("CFD_FIRST_ORDER") ? 1 : 0;
  if (first_order) {
    for (int i = 0; i < sd.nowned; ++i) {
      sd.grads[i] = PrimGrad{};
      sd.psi[i] = {0.0, 0.0, 0.0, 0.0};
    }
    return;
  }
  for (int i = 0; i < sd.nowned; ++i) {
    PrimGrad g = compute_gradient(sd.lm, sd.gw, sd.W, i, sd.cfg);
    sd.psi[i] = limit_gradient(sd.lm, sd.cfg, sd.W, i, g, fallback_count);
    const auto& ps = sd.psi[i];
    sd.grads[i].rx = ps[0] * g.rx; sd.grads[i].ry = ps[0] * g.ry;
    sd.grads[i].ux = ps[1] * g.ux; sd.grads[i].uy = ps[1] * g.uy;
    sd.grads[i].vx = ps[2] * g.vx; sd.grads[i].vy = ps[2] * g.vy;
    sd.grads[i].px = ps[3] * g.px; sd.grads[i].py = ps[3] * g.py;
  }
}

// ---------------------------------------------------------------------------
// Residual assembly
// ---------------------------------------------------------------------------
void compute_residual(SolverData& sd) {
  const LocalMesh& lm = sd.lm;
  for (int i = 0; i < sd.nowned; ++i) sd.Res[i] = Cons{0, 0, 0, 0};
  const bool viscous = sd.cfg.is_viscous();
  const double eps_fix = 0.1;
  static int use_rusanov = getenv("CFD_USE_RUSANOV") ? 1 : 0;
  static double rusanov_d = getenv("CFD_RUSANOV_D") ? atof(getenv("CFD_RUSANOV_D")) : 1.0;
  std::vector<Cons> type_sum(5, Cons{0, 0, 0, 0});  // debug: interior/far/wall/other
  std::vector<double> type_len(5, 0.0);
  for (int f = 0; f < (int)lm.face_ca.size(); ++f) {
    int ca = lm.face_ca[f], cb = lm.face_cb[f];
    double len = lm.face_len[f];
    double nxu = lm.face_nx[f] / len, nyu = lm.face_ny[f] / len;
    if (cb >= 0) {
      Prim wl = face_state(lm, sd.W, sd.grads, ca, f);
      Prim wr = face_state(lm, sd.W, sd.grads, cb, f);
      Cons Ul = to_cons(wl, sd.gas), Ur = to_cons(wr, sd.gas);
      Cons Fi = use_rusanov ? flux_rusanov(wl, wr, Ul, Ur, nxu, nyu, sd.gas, rusanov_d)
                            : flux_roe(wl, wr, Ul, Ur, nxu, nyu, sd.gas, eps_fix);
      Cons F;
      if (viscous) {
        // Face-averaged primitive gradients.
        const PrimGrad& ga = sd.grads[ca];
        const PrimGrad& gb = sd.grads[cb];
        Prim wf;
        wf.rho = 0.5 * (wl.rho + wr.rho);
        wf.u = 0.5 * (wl.u + wr.u);
        wf.v = 0.5 * (wl.v + wr.v);
        wf.p = 0.5 * (wl.p + wr.p);
        double ux = 0.5 * (ga.ux + gb.ux), uy = 0.5 * (ga.uy + gb.uy);
        double vx = 0.5 * (ga.vx + gb.vx), vy = 0.5 * (ga.vy + gb.vy);
        double rho_x = 0.5 * (ga.rx + gb.rx), rho_y = 0.5 * (ga.ry + gb.ry);
        double px = 0.5 * (ga.px + gb.px), py = 0.5 * (ga.py + gb.py);
        double r2 = wf.rho * wf.rho * sd.gas.R;
        double Tx = (px * wf.rho - wf.p * rho_x) / r2;
        double Ty = (py * wf.rho - wf.p * rho_y) / r2;
        Cons Fv = viscous_flux(wf, ux, uy, vx, vy, Tx, Ty, nxu, nyu, len, sd.gas);
        // Navier-Stokes total flux: F = F_inviscid - F_viscous.
        F.rho = Fi.rho * len - Fv.rho;
        F.rhou = Fi.rhou * len - Fv.rhou;
        F.rhov = Fi.rhov * len - Fv.rhov;
        F.rhoE = Fi.rhoE * len - Fv.rhoE;
      } else {
        F.rho = Fi.rho * len;
        F.rhou = Fi.rhou * len;
        F.rhov = Fi.rhov * len;
        F.rhoE = Fi.rhoE * len;
      }
      if (ca < sd.nowned) {
        sd.Res[ca].rho += F.rho; sd.Res[ca].rhou += F.rhou;
        sd.Res[ca].rhov += F.rhov; sd.Res[ca].rhoE += F.rhoE;
        type_sum[0].rho += F.rho; type_sum[0].rhou += F.rhou;
        type_sum[0].rhov += F.rhov; type_sum[0].rhoE += F.rhoE;
        type_len[0] += len;
      }
      if (cb < sd.nowned) {
        sd.Res[cb].rho -= F.rho; sd.Res[cb].rhou -= F.rhou;
        sd.Res[cb].rhov -= F.rhov; sd.Res[cb].rhoE -= F.rhoE;
        type_sum[0].rho -= F.rho; type_sum[0].rhou -= F.rhou;
        type_sum[0].rhov -= F.rhov; type_sum[0].rhoE -= F.rhoE;
        type_len[0] += len;
      }
      if (getenv("CFD_DEBUG_WALL") && (ca == 702 || cb == 702)) {
        const PrimGrad& ga = sd.grads[ca];
        const PrimGrad& gb = sd.grads[cb];
        g_log.logf("DBG flux face %d ca %d cb %d len %.6e wl %.6e %.6e %.6e %.6e wr %.6e %.6e %.6e %.6e F %.6e %.6e %.6e %.6e\n",
                   f, ca, cb, len, wl.rho, wl.u, wl.v, wl.p, wr.rho, wr.u, wr.v, wr.p,
                   F.rho, F.rhou, F.rhov, F.rhoE);
        g_log.logf("   grads ca r %.4e %.4e u %.4e %.4e p %.4e %.4e | cb r %.4e %.4e p %.4e %.4e\n",
                   ga.rx, ga.ry, ga.ux, ga.uy, ga.px, ga.py, gb.rx, gb.ry, gb.px, gb.py);
      }
    } else {
      BCType bc = lm.face_bc[f];
      int t = (bc == BCType::Farfield) ? 1 : (bc == BCType::SlipWall ? 2 : 3);
      Prim wl = face_state(lm, sd.W, sd.grads, ca, f);
      Cons Fl = Cons{0, 0, 0, 0};
      if (bc == BCType::Farfield) {
        Prim wr;
        wr.rho = sd.cfg.rho_inf; wr.u = sd.cfg.u_inf; wr.v = sd.cfg.v_inf; wr.p = sd.cfg.p_inf;
        Cons Ul = to_cons(wl, sd.gas), Ur = to_cons(wr, sd.gas);
        Cons Fi = use_rusanov ? flux_rusanov(wl, wr, Ul, Ur, nxu, nyu, sd.gas, rusanov_d)
                              : flux_roe(wl, wr, Ul, Ur, nxu, nyu, sd.gas, eps_fix);
        Fl.rho = Fi.rho * len; Fl.rhou = Fi.rhou * len; Fl.rhov = Fi.rhov * len; Fl.rhoE = Fi.rhoE * len;
      } else if (bc == BCType::SlipWall || bc == BCType::NoSlipAdiabaticWall) {
        double pw = wall_pressure(lm, sd.W, sd.grads, ca, f);
        Fl.rhou = pw * lm.face_nx[f];
        Fl.rhov = pw * lm.face_ny[f];
        if (bc == BCType::NoSlipAdiabaticWall && viscous) {
          const PrimGrad& g = sd.grads[ca];
          double ux = g.ux, uy = g.uy, vx = g.vx, vy = g.vy;
          double rho_x = g.rx, rho_y = g.ry, px = g.px, py = g.py;
          double r2 = wl.rho * wl.rho * sd.gas.R;
          double Tx = (px * wl.rho - wl.p * rho_x) / r2;
          double Ty = (py * wl.rho - wl.p * rho_y) / r2;
          // Adiabatic wall: the heat flux normal component must vanish.
          double Tn = Tx * nxu + Ty * nyu;
          Tx -= Tn * nxu;
          Ty -= Tn * nyu;
          Prim ww = wl;
          ww.u = 0.0; ww.v = 0.0;
          Cons Fv = viscous_flux(ww, ux, uy, vx, vy, Tx, Ty, nxu, nyu, len, sd.gas);
          Fl.rhou -= Fv.rhou; Fl.rhov -= Fv.rhov; Fl.rhoE -= Fv.rhoE;
        }
      } else {
        // Should never happen: interior face with cb==-1.
        continue;
      }
      if (ca < sd.nowned) {
        sd.Res[ca].rho += Fl.rho; sd.Res[ca].rhou += Fl.rhou;
        sd.Res[ca].rhov += Fl.rhov; sd.Res[ca].rhoE += Fl.rhoE;
        type_sum[t].rho += Fl.rho; type_sum[t].rhou += Fl.rhou;
        type_sum[t].rhov += Fl.rhov; type_sum[t].rhoE += Fl.rhoE;
        type_len[t] += len;
      }
    }
  }
  if (getenv("CFD_DEBUG_CONS")) {
    double gsum[4][4] = {{0}};
    for (int t = 0; t < 4; ++t) {
      double loc[4] = {type_sum[t].rho, type_sum[t].rhou, type_sum[t].rhov, type_sum[t].rhoE};
      MPI_Allreduce(loc, gsum[t], 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    }
    double glen[4] = {0, 0, 0, 0};
    MPI_Allreduce(type_len.data(), glen, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    g_log.logf("TYPESUM int %.6e %.6e %.6e %.6e (L %.3e) | far %.6e %.6e %.6e %.6e (L %.3e) | wall %.6e %.6e %.6e %.6e (L %.3e)\n",
               gsum[0][0], gsum[0][1], gsum[0][2], gsum[0][3], glen[0],
               gsum[1][0], gsum[1][1], gsum[1][2], gsum[1][3], glen[1],
               gsum[2][0], gsum[2][1], gsum[2][2], gsum[2][3], glen[2]);
  }
  if (getenv("CFD_DEBUG_WALL")) {
    for (int i = 0; i < sd.nowned; ++i) {
      double s = sd.Res[i].rho + sd.Res[i].rhou + sd.Res[i].rhov + sd.Res[i].rhoE;
      if (!std::isfinite(s)) {
        g_log.logf("DBG badRes cell %d global %d nfaces %zu W %.6e %.6e %.6e %.6e\n",
                   i, lm.local_to_global[i], lm.cell_faces[i].size(),
                   sd.W[i].rho, sd.W[i].u, sd.W[i].v, sd.W[i].p);
        for (int f : lm.cell_faces[i]) {
          int j = (lm.face_cb[f] >= 0) ? ((lm.face_ca[f] == i) ? lm.face_cb[f] : lm.face_ca[f]) : -1;
          g_log.logf("   face %d j %d len %.6e n %.6e %.6e Wj %s\n",
                     f, j, lm.face_len[f], lm.face_nx[f], lm.face_ny[f],
                     j >= 0 ? "ok" : "bnd");
        }
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Residual norms (global reductions)
// ---------------------------------------------------------------------------
void global_residual_norms(SolverData& sd, double comp[4], double& l2, double& linf) {
  double loc[4] = {0, 0, 0, 0};
  double loc_linf[4] = {0, 0, 0, 0};
  for (int i = 0; i < sd.nowned; ++i) {
    const Cons& r = sd.Res[i];
    double vals[4] = {r.rho, r.rhou, r.rhov, r.rhoE};
    for (int c = 0; c < 4; ++c) {
      loc[c] += vals[c] * vals[c];
      loc_linf[c] = std::max(loc_linf[c], std::abs(vals[c]));
    }
  }
  double gl[4], glf[4];
  MPI_Allreduce(loc, gl, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(loc_linf, glf, 4, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int nc = sd.nowned;
  MPI_Allreduce(MPI_IN_PLACE, &nc, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  double l2tot = 0.0;
  linf = 0.0;
  for (int c = 0; c < 4; ++c) {
    comp[c] = std::sqrt(gl[c] / nc);
    l2tot += gl[c];
    linf = std::max(linf, glf[c]);
  }
  l2 = std::sqrt(l2tot / nc);
}

// ---------------------------------------------------------------------------
// Pseudo time step + implicit diagonal
// ---------------------------------------------------------------------------
void compute_dtau_diag(SolverData& sd, double cfl) {
  const LocalMesh& lm = sd.lm;
  const double g = sd.gas.gamma;
  const double visc_scale = std::max(4.0 / 3.0, g / sd.gas.Pr);
  for (int i = 0; i < sd.nowned; ++i) {
    double suml = 0.0;
    double a = speed_of_sound(sd.W[i], sd.gas);
    double rho = sd.W[i].rho;
    for (int f : lm.cell_faces[i]) {
      double len = lm.face_len[f];
      double un = sd.W[i].u * (lm.face_nx[f] / len) + sd.W[i].v * (lm.face_ny[f] / len);
      double lc = (std::abs(un) + a) * len;
      double lv = (sd.gas.mu > 0.0) ? visc_scale * (sd.gas.mu / rho) * len * len / lm.vol[i] : 0.0;
      suml += lc + lv;
    }
    sd.dtau[i] = cfl * lm.vol[i] / suml;
    static double diag_factor = []() {
      const char* s = getenv("CFD_DIAG_FACTOR");
      // Consistent with the 0.5*S*(dF - lam*dU) off-diagonal: the diagonal
      // dissipation part is 0.5*sum(lambda*S) (plus the physical-time term).
      return s ? atof(s) : 0.5;
    }();
    sd.diag[i] = lm.vol[i] / sd.dtau[i] + diag_factor * suml + sd.phys_coeff * lm.vol[i];
  }
}

// ---------------------------------------------------------------------------
// LU-SGS sweep
// ---------------------------------------------------------------------------
namespace {

// Convert a conservative correction to a primitive correction. Returns false
// if the resulting state would be nonphysical.
bool cons_to_prim_delta(const Cons& U, const Cons& dU, const GasModel& gas,
                        Prim& dW) {
  double rho = U.rho + dU.rho;
  if (rho <= 1.0e-12) return false;
  double u = U.rhou / U.rho, v = U.rhov / U.rho;
  double rhou = U.rhou + dU.rhou;
  double rhov = U.rhov + dU.rhov;
  double rhoE = U.rhoE + dU.rhoE;
  double p = (gas.gamma - 1.0) *
             (rhoE - 0.5 * (rhou * rhou + rhov * rhov) / rho);
  if (p <= 1.0e-12) return false;
  dW.rho = dU.rho;
  dW.u = rhou / rho - u;
  dW.v = rhov / rho - v;
  dW.p = p - pressure(U, gas);
  return true;
}

}  // namespace

void sweep_cell(SolverData& sd, int i) {
  const LocalMesh& lm = sd.lm;
  // Solve (V/dtau I + J) dU = -Res with symmetric Gauss-Seidel:
  //   dU_i = D_i^{-1} [ -Res_i - sum_j J_ij dU_j ],
  // where J_ij dU_j ~= 0.5 S (dF_j - lam dU_j).
  Cons rhs;
  rhs.rho = -sd.Res[i].rho;
  rhs.rhou = -sd.Res[i].rhou;
  rhs.rhov = -sd.Res[i].rhov;
  rhs.rhoE = -sd.Res[i].rhoE;
  double diag = sd.diag[i];
  for (int f : lm.cell_faces[i]) {
    if (lm.face_cb[f] < 0) continue;
    int j = (lm.face_ca[f] == i) ? lm.face_cb[f] : lm.face_ca[f];
    double len = lm.face_len[f];
    double nxu = lm.face_nx[f] / len, nyu = lm.face_ny[f] / len;
    // Outward unit normal from i to j.
    double sgn = (lm.face_ca[f] == i) ? 1.0 : -1.0;
    nxu *= sgn; nyu *= sgn;
    // Spectral radius at the face.
    double a = speed_of_sound(sd.W[i], sd.gas);
    double li = std::abs(sd.W[i].u * nxu + sd.W[i].v * nyu) + a;
    double aj = speed_of_sound(sd.W[j], sd.gas);
    double lj = std::abs(sd.W[j].u * nxu + sd.W[j].v * nyu) + aj;
    double lam = std::max(li, lj);
    Prim dW;
    bool ok = cons_to_prim_delta(sd.U[j], sd.dU[j], sd.gas, dW);
    Cons dF{0, 0, 0, 0};
    if (ok) {
      Prim wj2;
      wj2.rho = sd.W[j].rho + dW.rho;
      wj2.u = sd.W[j].u + dW.u;
      wj2.v = sd.W[j].v + dW.v;
      wj2.p = sd.W[j].p + dW.p;
      Cons Fj = flux_euler(wj2, nxu, nyu, sd.gas);
      Cons Fj0 = flux_euler(sd.W[j], nxu, nyu, sd.gas);
      dF.rho = Fj.rho - Fj0.rho;
      dF.rhou = Fj.rhou - Fj0.rhou;
      dF.rhov = Fj.rhov - Fj0.rhov;
      dF.rhoE = Fj.rhoE - Fj0.rhoE;
    }
    double fac = 0.5 * len;
    static int sweep_mode = []() {
      const char* s = getenv("CFD_SWEEP_MODE");
      return s ? atoi(s) : 0;  // 0 = full flux-difference, 1 = spectral only
    }();
    if (sweep_mode == 1) {
      dF.rho = 0.0; dF.rhou = 0.0; dF.rhov = 0.0; dF.rhoE = 0.0;
    }
    rhs.rho -= fac * (dF.rho - lam * sd.dU[j].rho);
    rhs.rhou -= fac * (dF.rhou - lam * sd.dU[j].rhou);
    rhs.rhov -= fac * (dF.rhov - lam * sd.dU[j].rhov);
    rhs.rhoE -= fac * (dF.rhoE - lam * sd.dU[j].rhoE);
  }
  double inv = 1.0 / diag;
  sd.dU[i].rho = rhs.rho * inv;
  sd.dU[i].rhou = rhs.rhou * inv;
  sd.dU[i].rhov = rhs.rhov * inv;
  sd.dU[i].rhoE = rhs.rhoE * inv;
}

void lussgs_sweep(SolverData& sd) {
  for (int i = 0; i < sd.nowned; ++i) sweep_cell(sd, i);
  exchange_dU(sd);
  for (int i = sd.nowned - 1; i >= 0; --i) sweep_cell(sd, i);
  exchange_dU(sd);
}

// Accumulated LU-SGS sweeps on the same frozen right-hand side: each call is a
// full forward+backward Gauss-Seidel pass over the linear system
// (D + L + U) dU = -Res. Repeating without resetting dU converges the linear
// solve before the correction is applied.
void lussgs_iterate(SolverData& sd, int nsweeps) {
  for (int s = 0; s < nsweeps; ++s) lussgs_sweep(sd);
}

// Computes the linear residual of the implicit system
//   L_i = -R_i - sum_j 0.5 S (dF_j - lam dU_j) - D_i dU_i
// and returns its global L2 norm. Used for diagnostics only.
double linear_residual_norm(SolverData& sd) {
  const LocalMesh& lm = sd.lm;
  double loc = 0.0;
  for (int i = 0; i < sd.nowned; ++i) {
    double lr = -sd.Res[i].rho, lu = -sd.Res[i].rhou;
    double lv = -sd.Res[i].rhov, le = -sd.Res[i].rhoE;
    for (int f : lm.cell_faces[i]) {
      if (lm.face_cb[f] < 0) continue;
      int j = (lm.face_ca[f] == i) ? lm.face_cb[f] : lm.face_ca[f];
      double len = lm.face_len[f];
      double nxu = lm.face_nx[f] / len, nyu = lm.face_ny[f] / len;
      double sgn = (lm.face_ca[f] == i) ? 1.0 : -1.0;
      nxu *= sgn; nyu *= sgn;
      double a = speed_of_sound(sd.W[i], sd.gas);
      double li = std::abs(sd.W[i].u * nxu + sd.W[i].v * nyu) + a;
      double aj = speed_of_sound(sd.W[j], sd.gas);
      double lj = std::abs(sd.W[j].u * nxu + sd.W[j].v * nyu) + aj;
      double lam = std::max(li, lj);
      Prim dW;
      bool ok = cons_to_prim_delta(sd.U[j], sd.dU[j], sd.gas, dW);
      Cons dF{0, 0, 0, 0};
      if (ok) {
        Prim wj2;
        wj2.rho = sd.W[j].rho + dW.rho;
        wj2.u = sd.W[j].u + dW.u;
        wj2.v = sd.W[j].v + dW.v;
        wj2.p = sd.W[j].p + dW.p;
        Cons Fj = flux_euler(wj2, nxu, nyu, sd.gas);
        Cons Fj0 = flux_euler(sd.W[j], nxu, nyu, sd.gas);
        dF.rho = Fj.rho - Fj0.rho;
        dF.rhou = Fj.rhou - Fj0.rhou;
        dF.rhov = Fj.rhov - Fj0.rhov;
        dF.rhoE = Fj.rhoE - Fj0.rhoE;
      }
      double fac = 0.5 * len;
      lr -= fac * (dF.rho - lam * sd.dU[j].rho);
      lu -= fac * (dF.rhou - lam * sd.dU[j].rhou);
      lv -= fac * (dF.rhov - lam * sd.dU[j].rhov);
      le -= fac * (dF.rhoE - lam * sd.dU[j].rhoE);
    }
    lr -= sd.diag[i] * sd.dU[i].rho;
    lu -= sd.diag[i] * sd.dU[i].rhou;
    lv -= sd.diag[i] * sd.dU[i].rhov;
    le -= sd.diag[i] * sd.dU[i].rhoE;
    loc += lr * lr + lu * lu + lv * lv + le * le;
  }
  MPI_Allreduce(MPI_IN_PLACE, &loc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  return std::sqrt(loc);
}

// ---------------------------------------------------------------------------
// State update with positivity protection
// ---------------------------------------------------------------------------
void update_states(SolverData& sd, long long& update_limits) {
  const double rho_min = 1.0e-8 * sd.cfg.rho_inf;
  const double p_min = 1.0e-8 * sd.cfg.p_inf;
  static double relax = []() {
    const char* s = getenv("CFD_RELAX");
    return s ? atof(s) : 1.0;
  }();
  for (int i = 0; i < sd.nowned; ++i) {
    double f = 1.0;
    bool ok = false;
    for (int k = 0; k < 12; ++k) {
      Cons Un = sd.U[i];
      Un.rho += sd.dU[i].rho * f * relax;
      Un.rhou += sd.dU[i].rhou * f * relax;
      Un.rhov += sd.dU[i].rhov * f * relax;
      Un.rhoE += sd.dU[i].rhoE * f * relax;
      double p = pressure(Un, sd.gas);
      if (Un.rho > rho_min && p > p_min && std::isfinite(p)) {
        ok = true;
        break;
      }
      f *= 0.5;
    }
    if (!ok) f = 0.0;
    if (f < 1.0) ++update_limits;
    if (f == 0.0) continue;  // skip a nonphysical correction entirely
    sd.U[i].rho += sd.dU[i].rho * f * relax;
    sd.U[i].rhou += sd.dU[i].rhou * f * relax;
    sd.U[i].rhov += sd.dU[i].rhov * f * relax;
    sd.U[i].rhoE += sd.dU[i].rhoE * f * relax;
  }
}

// ---------------------------------------------------------------------------
// Forces
// ---------------------------------------------------------------------------
void compute_forces_local(SolverData& sd) {
  const LocalMesh& lm = sd.lm;
  const CaseConfig& cfg = sd.cfg;
  sd.fx_p = sd.fy_p = sd.fx_v = sd.fy_v = sd.mz = 0.0;
  const bool viscous = cfg.is_viscous();
  for (const BoundaryFace& bf : lm.boundary_faces) {
    if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
    int f = bf.face;
    double len = lm.face_len[f];
    // Stored boundary-face normal points out of the fluid domain (into the
    // body). The pressure force on the body is +p * n_fluid_out.
    double nxout = lm.face_nx[f] / len;
    double nyout = lm.face_ny[f] / len;
    // Body-outward normal (used for the shear traction).
    double nwx = -nxout;
    double nwy = -nyout;
    double pw = wall_pressure(lm, sd.W, sd.grads, bf.cell, f);
    if (getenv("CFD_DEBUG_WALL")) {
      const PrimGrad& g = sd.grads[bf.cell];
      g_log.logf("WALL cell %d x %.5f y %.5f pc %.6f pw %.6f d %.6f gpx %.4e gpy %.4e psi %.3f nf %zu\n",
                 bf.cell, bf.xm, bf.ym, sd.W[bf.cell].p, pw,
                 std::sqrt((bf.xm - lm.cx[bf.cell]) * (bf.xm - lm.cx[bf.cell]) +
                           (bf.ym - lm.cy[bf.cell]) * (bf.ym - lm.cy[bf.cell])),
                 g.px, g.py, sd.psi[bf.cell][3], lm.cell_faces[bf.cell].size());
    }
    sd.fx_p += pw * nxout * len;
    sd.fy_p += pw * nyout * len;
    if (viscous && bf.bc == BCType::NoSlipAdiabaticWall) {
      const PrimGrad& g = sd.grads[bf.cell];
      // Shear traction vector on the body: -tau . n_out where n_out = -n_wall.
      double ux = g.ux, uy = g.uy, vx = g.vx, vy = g.vy;
      double div = ux + vy;
      double txx = 2.0 * sd.gas.mu * ux - (2.0 / 3.0) * sd.gas.mu * div;
      double tyy = 2.0 * sd.gas.mu * vy - (2.0 / 3.0) * sd.gas.mu * div;
      double txy = sd.gas.mu * (uy + vx);
      // tau . n_wall
      double snx = txx * nwx + txy * nwy;
      double sny = txy * nwx + tyy * nwy;
      // tangential shear traction: -tau . n_out = tau . n_wall projected
      // onto the wall tangent.
      double ttx = snx, tty = sny;  // vector on the body (opposite of fluid traction)
      // Remove normal component so only tangential shear is counted.
      double nn = ttx * nwx + tty * nwy;
      ttx -= nn * nwx;
      tty -= nn * nwy;
      sd.fx_v += ttx * len;
      sd.fy_v += tty * len;
      // Moment contribution of the shear force.
      double rx2 = bf.xm - cfg.moment_center[0];
      double ry2 = bf.ym - cfg.moment_center[1];
      sd.mz += rx2 * (tty * len) - ry2 * (ttx * len);
    }
    // Moment about the reference center.
    double rx = bf.xm - cfg.moment_center[0];
    double ry = bf.ym - cfg.moment_center[1];
    double fxp = pw * nxout * len;
    double fyp = pw * nyout * len;
    sd.mz += rx * fyp - ry * fxp;
  }
}

void global_forces(SolverData& sd, double& cl, double& cd, double& cmz,
                   double& pd, double& vd, double& pl, double& vl) {
  compute_forces_local(sd);
  double loc[5] = {sd.fx_p, sd.fy_p, sd.fx_v, sd.fy_v, sd.mz};
  double gl[5];
  MPI_Allreduce(loc, gl, 5, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  const CaseConfig& cfg = sd.cfg;
  double qA = cfg.q_inf * cfg.ref_area;
  double qAL = cfg.q_inf * cfg.ref_area * cfg.ref_length;
  double ca = std::cos(cfg.aoa_degrees * M_PI / 180.0);
  double sa = std::sin(cfg.aoa_degrees * M_PI / 180.0);
  double Fx = gl[0] + gl[2], Fy = gl[1] + gl[3];
  cd = (Fx * ca + Fy * sa) / qA;
  cl = (-Fx * sa + Fy * ca) / qA;
  cmz = gl[4] / qAL;
  pd = (gl[0] * ca + gl[1] * sa) / qA;
  vd = (gl[2] * ca + gl[3] * sa) / qA;
  pl = (-gl[0] * sa + gl[1] * ca) / qA;
  vl = (-gl[2] * sa + gl[3] * ca) / qA;
}

// ---------------------------------------------------------------------------
// Transient residual (spatial + physical-time terms)
// ---------------------------------------------------------------------------
void compute_total_residual(SolverData& sd) {
  compute_residual(sd);
  for (int i = 0; i < sd.nowned; ++i) {
    double vdt = sd.lm.vol[i] / sd.cfg.time_step;
    double r1 = sd.phys_a0 * (sd.U[i].rho - sd.Un[i].rho) +
                sd.phys_a1 * (sd.U[i].rho - sd.Unm1[i].rho);
    double r2 = sd.phys_a0 * (sd.U[i].rhou - sd.Un[i].rhou) +
                sd.phys_a1 * (sd.U[i].rhou - sd.Unm1[i].rhou);
    double r3 = sd.phys_a0 * (sd.U[i].rhov - sd.Un[i].rhov) +
                sd.phys_a1 * (sd.U[i].rhov - sd.Unm1[i].rhov);
    double r4 = sd.phys_a0 * (sd.U[i].rhoE - sd.Un[i].rhoE) +
                sd.phys_a1 * (sd.U[i].rhoE - sd.Unm1[i].rhoE);
    sd.Res_phys[i].rho = sd.Res[i].rho + vdt * r1;
    sd.Res_phys[i].rhou = sd.Res[i].rhou + vdt * r2;
    sd.Res_phys[i].rhov = sd.Res[i].rhov + vdt * r3;
    sd.Res_phys[i].rhoE = sd.Res[i].rhoE + vdt * r4;
  }
}

void global_total_residual_norms(SolverData& sd, double& l2, double& linf) {
  double loc = 0.0, loc_l = 0.0;
  for (int i = 0; i < sd.nowned; ++i) {
    const Cons& r = sd.Res_phys[i];
    double v2 = r.rho * r.rho + r.rhou * r.rhou + r.rhov * r.rhov + r.rhoE * r.rhoE;
    loc += v2;
    loc_l = std::max(loc_l, std::max({std::abs(r.rho), std::abs(r.rhou),
                                      std::abs(r.rhov), std::abs(r.rhoE)}));
  }
  MPI_Allreduce(MPI_IN_PLACE, &loc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(MPI_IN_PLACE, &loc_l, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int nc = sd.nowned;
  MPI_Allreduce(MPI_IN_PLACE, &nc, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  l2 = std::sqrt(loc / nc);
  linf = loc_l;
}

// ---------------------------------------------------------------------------
// Surface output rows
// ---------------------------------------------------------------------------
void collect_surface_rows(SolverData& sd, const std::string& outdir) {
  const LocalMesh& lm = sd.lm;
  const CaseConfig& cfg = sd.cfg;
  std::vector<SurfaceRow> rows;
  const bool viscous = cfg.is_viscous();
  for (const BoundaryFace& bf : lm.boundary_faces) {
    if (bf.bc != BCType::SlipWall && bf.bc != BCType::NoSlipAdiabaticWall) continue;
    int f = bf.face;
    double len = lm.face_len[f];
    double nwx = -lm.face_nx[f] / len;
    double nwy = -lm.face_ny[f] / len;
    double pw = wall_pressure(lm, sd.W, sd.grads, bf.cell, f);
    SurfaceRow r;
    r.x = bf.xm;
    r.y = bf.ym;
    r.nx = nwx;
    r.ny = nwy;
    r.pressure = pw;
    r.cp = (pw - cfg.p_inf) / cfg.q_inf;
    r.rho = sd.W[bf.cell].rho;
    double cf = 0.0;
    if (bf.bc == BCType::NoSlipAdiabaticWall) {
      r.u = 0.0;
      r.v = 0.0;
      r.mach = 0.0;
      if (viscous) {
        const PrimGrad& g = sd.grads[bf.cell];
        double ux = g.ux, uy = g.uy, vx = g.vx, vy = g.vy;
        double div = ux + vy;
        double txx = 2.0 * sd.gas.mu * ux - (2.0 / 3.0) * sd.gas.mu * div;
        double tyy = 2.0 * sd.gas.mu * vy - (2.0 / 3.0) * sd.gas.mu * div;
        double txy = sd.gas.mu * (uy + vx);
        double snx = txx * nwx + txy * nwy;
        double sny = txy * nwx + tyy * nwy;
        double ttx = snx, tty = sny;
        double nn = ttx * nwx + tty * nwy;
        ttx -= nn * nwx;
        tty -= nn * nwy;
        // Sign: positive when aligned with the local wall-tangential flow.
        double wt = sd.W[bf.cell].u * -nwy + sd.W[bf.cell].v * nwx;  // u dot t, t=(-ny,nx) rotated? see below
        // tangent basis: t1 = (-nwy, nwx)
        double t1x = -nwy, t1y = nwx;
        double ts = ttx * t1x + tty * t1y;
        double flow = sd.W[bf.cell].u * t1x + sd.W[bf.cell].v * t1y;
        if (flow < 0.0) ts = -ts;
        cf = ts / cfg.q_inf;
      }
      r.cf = cf;
    } else {
      // Slip wall: preserve tangential velocity, zero normal velocity.
      Prim wf = face_state(lm, sd.W, sd.grads, bf.cell, f);
      double un = wf.u * nwx + wf.v * nwy;
      r.u = wf.u - un * nwx;
      r.v = wf.v - un * nwy;
      r.mach = std::sqrt(r.u * r.u + r.v * r.v) / speed_of_sound(wf, sd.gas);
      r.cf = 0.0;
    }
    r.tag = bc_type_name(bf.bc);
    rows.push_back(r);
  }
  // Gather rows to rank 0.
  std::vector<char> mybuf;
  for (const SurfaceRow& r : rows) {
    char line[512];
    snprintf(line, sizeof(line), "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%s\n",
             r.x, r.y, r.nx, r.ny, r.pressure, r.cp, r.cf, r.rho, r.u, r.v, r.mach, r.tag.c_str());
    mybuf.insert(mybuf.end(), line, line + strlen(line));
  }
  int mylen = (int)mybuf.size();
  std::vector<int> counts(sd.lm.nranks), disp(sd.lm.nranks);
  MPI_Gather(&mylen, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (sd.lm.rank == 0) {
    for (int q = 0; q < sd.lm.nranks; ++q) {
      disp[q] = total;
      total += counts[q];
    }
  }
  std::vector<char> all(total);
  MPI_Gatherv(mybuf.data(), mylen, MPI_CHAR, all.data(), counts.data(), disp.data(),
              MPI_CHAR, 0, MPI_COMM_WORLD);
  if (sd.lm.rank == 0) {
    // Sort rows deterministically by (y, x).
    std::vector<SurfaceRow> allrows;
    std::vector<std::string> lines;
    std::string cur;
    for (char c : all) {
      if (c == '\n') {
        lines.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    for (const std::string& s : lines) {
      if (s.empty()) continue;
      SurfaceRow r;
      char tag[64];
      if (sscanf(s.c_str(), "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%63s",
                 &r.x, &r.y, &r.nx, &r.ny, &r.pressure, &r.cp, &r.cf, &r.rho,
                 &r.u, &r.v, &r.mach, tag) == 12) {
        r.tag = tag;
        allrows.push_back(r);
      }
    }
    std::sort(allrows.begin(), allrows.end(), [](const SurfaceRow& a, const SurfaceRow& b) {
      if (a.tag != b.tag) return a.tag < b.tag;
      if (std::abs(a.y - b.y) > 1e-12) return a.y < b.y;
      return a.x < b.x;
    });
    std::string e;
    write_surface_csv(allrows, outdir, e);
  }
}

// ---------------------------------------------------------------------------
// Field gather + restart
// ---------------------------------------------------------------------------
// Each rank packs `ncomp` doubles per owned cell (plus its global cell ids);
// rank 0 reassembles in global cell order.
void gather_global_cell_data(SolverData& sd, int ncomp,
                             const std::vector<double>& mydata,
                             std::vector<double>& ordered) {
  const LocalMesh& lm = sd.lm;
  std::vector<int> counts(lm.nranks), disp(lm.nranks);
  int myn = sd.nowned;
  MPI_Gather(&myn, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (lm.rank == 0) {
    for (int q = 0; q < lm.nranks; ++q) {
      disp[q] = total;
      total += counts[q];
    }
  }
  // ids + data in one buffer: ids first, then data.
  std::vector<int> myids;
  for (int i = 0; i < sd.nowned; ++i) myids.push_back(lm.local_to_global[i]);
  std::vector<int> allids(total);
  std::vector<double> alldata((size_t)total * ncomp);
  MPI_Gatherv(myids.data(), myn, MPI_INT, allids.data(), counts.data(), disp.data(),
              MPI_INT, 0, MPI_COMM_WORLD);
  std::vector<int> dcounts(lm.nranks), ddisp(lm.nranks);
  if (lm.rank == 0) {
    for (int q = 0; q < lm.nranks; ++q) {
      dcounts[q] = counts[q] * ncomp;
      ddisp[q] = disp[q] * ncomp;
    }
  }
  MPI_Gatherv(mydata.data(), myn * ncomp, MPI_DOUBLE, alldata.data(), dcounts.data(),
              ddisp.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  if (lm.rank == 0) {
    ordered.assign((size_t)lm.num_cells_global * ncomp, 0.0);
    for (int k = 0; k < total; ++k) {
      int g = allids[k];
      for (int c = 0; c < ncomp; ++c) {
        ordered[(size_t)g * ncomp + c] = alldata[(size_t)k * ncomp + c];
      }
    }
  }
}

void collect_field_data(SolverData& sd, const Mesh& mesh,
                        const std::string& outdir, const std::string& fname) {
  const LocalMesh& lm = sd.lm;
  const CaseConfig& cfg = sd.cfg;
  const int ncomp = 9;  // rho,u,v,p,mach,E,T,rank,vorticity
  std::vector<double> mydata((size_t)sd.nowned * ncomp);
  for (int i = 0; i < sd.nowned; ++i) {
    const Prim& w = sd.W[i];
    double a = speed_of_sound(w, sd.gas);
    double E = w.p / ((sd.gas.gamma - 1.0) * w.rho) + 0.5 * (w.u * w.u + w.v * w.v);
    double T = w.p / (sd.gas.R * w.rho);
    double vort = sd.grads[i].vx - sd.grads[i].uy;
    double* d = &mydata[(size_t)i * ncomp];
    d[0] = w.rho;
    d[1] = w.u;
    d[2] = w.v;
    d[3] = w.p;
    d[4] = std::sqrt(w.u * w.u + w.v * w.v) / a;
    d[5] = E;
    d[6] = T;
    d[7] = (double)lm.rank;
    d[8] = vort;
  }
  std::vector<double> ordered;
  gather_global_cell_data(sd, ncomp, mydata, ordered);
  if (lm.rank == 0) {
    std::string e;
    write_field_vtu(cfg, mesh, ordered, ncomp, 0, outdir + "/" + fname, e);
    if (!e.empty()) g_log.logf("warning: %s\n", e.c_str());
  }
}

// Writes the global restart state: gathers U (+Un,+Unm1 for transient) in
// global cell order and writes the binary restart file (rank 0).
void write_restart_file(SolverData& sd, const std::string& path, int step,
                        double phys_time) {
  const LocalMesh& lm = sd.lm;
  int nstates = sd.cfg.is_transient() ? 3 : 1;
  int ncomp = 4 * nstates;
  std::vector<double> mydata((size_t)sd.nowned * ncomp);
  for (int i = 0; i < sd.nowned; ++i) {
    double* d = &mydata[(size_t)i * ncomp];
    d[0] = sd.U[i].rho; d[1] = sd.U[i].rhou; d[2] = sd.U[i].rhov; d[3] = sd.U[i].rhoE;
    if (nstates >= 2) {
      d[4] = sd.Un[i].rho; d[5] = sd.Un[i].rhou; d[6] = sd.Un[i].rhov; d[7] = sd.Un[i].rhoE;
    }
    if (nstates >= 3) {
      d[8] = sd.Unm1[i].rho; d[9] = sd.Unm1[i].rhou; d[10] = sd.Unm1[i].rhov; d[11] = sd.Unm1[i].rhoE;
    }
  }
  std::vector<double> ordered;
  gather_global_cell_data(sd, ncomp, mydata, ordered);
  if (lm.rank == 0) {
    std::string e;
    write_restart(path, sd.cfg.case_id, step, phys_time, nstates, ordered, e);
    if (!e.empty()) g_log.logf("warning: %s\n", e.c_str());
  }
}

// Loads a restart file and distributes owned states to every rank.
bool load_restart(SolverData& sd, const std::string& path, int& step,
                  double& phys_time, std::string& err) {
  const LocalMesh& lm = sd.lm;
  std::string rcase;
  int nstates = 0;
  std::vector<double> global_states;
  if (lm.rank == 0) {
    if (!read_restart(path, rcase, step, phys_time, nstates, global_states, err)) {
      return false;
    }
    if (rcase != sd.cfg.case_id) {
      err = "restart file is for case '" + rcase + "' but case is '" + sd.cfg.case_id + "'";
      return false;
    }
    int expect = sd.cfg.is_transient() ? 3 : 1;
    if (nstates != expect) {
      err = "restart file state count mismatch";
      return false;
    }
  }
  MPI_Bcast(&step, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&phys_time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(&nstates, 1, MPI_INT, 0, MPI_COMM_WORLD);
  int ncomp = 4 * nstates;
  // Rank 0 sends each rank its owned cells' states.
  if (lm.rank == 0) {
    for (int q = 0; q < lm.nranks; ++q) {
      // Recover rank q's owned global ids from the stored partition.
      std::vector<double> buf;
      for (int g = 0; g < lm.num_cells_global; ++g) {
        if (lm.global_part[g] == q) {
          for (int c = 0; c < ncomp; ++c) {
            buf.push_back(global_states[(size_t)g * ncomp + c]);
          }
        }
      }
      if (q == 0) {
        // Local fill for rank 0.
        for (int i = 0; i < sd.nowned; ++i) {
          double* d = &buf[(size_t)i * ncomp];
          sd.U[i].rho = d[0]; sd.U[i].rhou = d[1]; sd.U[i].rhov = d[2]; sd.U[i].rhoE = d[3];
          if (nstates >= 2) {
            sd.Un[i].rho = d[4]; sd.Un[i].rhou = d[5]; sd.Un[i].rhov = d[6]; sd.Un[i].rhoE = d[7];
          }
          if (nstates >= 3) {
            sd.Unm1[i].rho = d[8]; sd.Unm1[i].rhou = d[9]; sd.Unm1[i].rhov = d[10]; sd.Unm1[i].rhoE = d[11];
          }
        }
      } else {
        MPI_Send(buf.data(), (int)buf.size(), MPI_DOUBLE, q, TAG_STATE, MPI_COMM_WORLD);
      }
    }
  } else {
    std::vector<double> buf((size_t)sd.nowned * ncomp);
    MPI_Recv(buf.data(), (int)buf.size(), MPI_DOUBLE, 0, TAG_STATE, MPI_COMM_WORLD,
             MPI_STATUS_IGNORE);
    for (int i = 0; i < sd.nowned; ++i) {
      double* d = &buf[(size_t)i * ncomp];
      sd.U[i].rho = d[0]; sd.U[i].rhou = d[1]; sd.U[i].rhov = d[2]; sd.U[i].rhoE = d[3];
      if (nstates >= 2) {
        sd.Un[i].rho = d[4]; sd.Un[i].rhou = d[5]; sd.Un[i].rhov = d[6]; sd.Un[i].rhoE = d[7];
      }
      if (nstates >= 3) {
        sd.Unm1[i].rho = d[8]; sd.Unm1[i].rhou = d[9]; sd.Unm1[i].rhov = d[10]; sd.Unm1[i].rhoE = d[11];
      }
    }
  }
  // Ghost states: exchange fields to refresh.
  update_primitives(sd);
  long long dummy = 0;
  compute_gradients(sd, dummy);
  exchange_fields(sd);
  update_primitives(sd);
  return true;
}

}  // namespace

namespace {

double mean_dtau(const SolverData& sd) {
  double loc = 0.0;
  int cnt = 0;
  for (int i = 0; i < sd.nowned; ++i) {
    loc += sd.dtau[i];
    ++cnt;
  }
  MPI_Allreduce(MPI_IN_PLACE, &loc, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(MPI_IN_PLACE, &cnt, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  return loc / std::max(1, cnt);
}

bool finite_check(double v, const std::string& where) {
  if (!std::isfinite(v)) {
    g_log.logf("ERROR: non-finite residual detected (%s); aborting run\n", where.c_str());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Diagnostics: per-cell residual map (env CFD_DUMP_RESID=1)
// ---------------------------------------------------------------------------
void dump_residual_map(SolverData& sd, const std::string& outdir) {
  const LocalMesh& lm = sd.lm;
  if (sd.cfg.is_transient()) {
    compute_total_residual(sd);
    for (int i = 0; i < sd.nowned; ++i) sd.Res[i] = sd.Res_phys[i];
  } else {
    compute_residual(sd);
  }
  struct Row {
    int g;
    double x, y, vol;
    double r[4];
    double l2;
  };
  std::vector<Row> myrows(sd.nowned);
  for (int i = 0; i < sd.nowned; ++i) {
    Row& r = myrows[i];
    r.g = lm.local_to_global[i];
    r.x = lm.cx[i];
    r.y = lm.cy[i];
    r.vol = lm.vol[i];
    r.r[0] = sd.Res[i].rho;
    r.r[1] = sd.Res[i].rhou;
    r.r[2] = sd.Res[i].rhov;
    r.r[3] = sd.Res[i].rhoE;
    r.l2 = std::sqrt(r.r[0] * r.r[0] + r.r[1] * r.r[1] +
                     r.r[2] * r.r[2] + r.r[3] * r.r[3]);
  }
  int n = (int)myrows.size();
  std::vector<int> counts(lm.nranks), disp(lm.nranks);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (lm.rank == 0) {
    for (int q = 0; q < lm.nranks; ++q) {
      counts[q] *= (int)sizeof(Row);
      disp[q] = total;
      total += counts[q];
    }
  }
  std::vector<Row> allrows(total / (int)sizeof(Row));
  MPI_Gatherv(myrows.data(), n * (int)sizeof(Row), MPI_BYTE, allrows.data(),
              counts.data(), disp.data(), MPI_BYTE, 0, MPI_COMM_WORLD);
  if (lm.rank == 0) {
    std::sort(allrows.begin(), allrows.end(),
              [](const Row& a, const Row& b) { return a.l2 > b.l2; });
    FILE* f = fopen((outdir + "/residual_map.csv").c_str(), "w");
    if (f) {
      fprintf(f, "global_cell,x,y,vol,res_rho,res_rhou,res_rhov,res_rhoE,res_l2\n");
      for (const Row& r : allrows) {
        fprintf(f, "%d,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
                r.g, r.x, r.y, r.vol, r.r[0], r.r[1], r.r[2], r.r[3], r.l2);
      }
      fclose(f);
    }
    g_log.logf("RESID-MAP top cells by |Res|:\n");
    for (int k = 0; k < std::min(20, (int)allrows.size()); ++k) {
      const Row& r = allrows[k];
      g_log.logf("  g=%d x=%.6f y=%.6f vol=%.3e |R|=%9.3e rho=%9.3e rhou=%9.3e "
                 "rhov=%9.3e rhoE=%9.3e\n",
                 r.g, r.x, r.y, r.vol, r.l2, r.r[0], r.r[1], r.r[2], r.r[3]);
    }
  }
}

}  // namespace

int run_solver(const CaseConfig& cfg, const LocalMesh& lm, const Mesh& mesh,
               const std::string& outdir, const std::string& restart_file,
               bool brief, RunStats& stats, std::string& err) {
  const int rank = lm.rank;
  SolverData sd{cfg, lm};
  sd.gas.gamma = cfg.gamma;
  sd.gas.R = cfg.gas_R;
  sd.gas.Pr = cfg.prandtl;
  sd.gas.mu = cfg.is_viscous() ? cfg.viscosity : 0.0;
  sd.nlocal = lm.nowned + lm.nghost;
  sd.nowned = lm.nowned;
  const int nlocal = sd.nlocal;

  sd.U.assign(nlocal, Cons{});
  sd.Un.assign(nlocal, Cons{});
  sd.Unm1.assign(nlocal, Cons{});
  sd.W.assign(nlocal, Prim{});
  sd.grads.resize(nlocal);
  sd.psi.assign(nlocal, std::array<double, 4>{1.0, 1.0, 1.0, 1.0});
  sd.Res.resize(lm.nowned);
  sd.Res_phys.resize(lm.nowned);
  sd.dU.assign(nlocal, Cons{});
  sd.dU_prev.assign(nlocal, Cons{});
  sd.dtau.resize(lm.nowned);
  sd.diag.resize(lm.nowned);
  compute_gradient_weights(lm, cfg, sd.gw);

  Prim w0;
  w0.rho = cfg.rho_inf;
  w0.u = cfg.u_inf;
  w0.v = cfg.v_inf;
  w0.p = cfg.p_inf;
  Cons U0 = to_cons(w0, sd.gas);
  for (int i = 0; i < nlocal; ++i) {
    sd.U[i] = U0;
    sd.Un[i] = U0;
    sd.Unm1[i] = U0;
    sd.W[i] = w0;
  }

  int start_step = 0;
  double start_time = 0.0;
  bool resumed = false;
  if (!restart_file.empty()) {
    bool exists = false;
    if (rank == 0) exists = std::filesystem::exists(restart_file);
    MPI_Bcast(&exists, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD);
    if (!exists) {
      err = "restart file not found: " + restart_file;
      return 1;
    }
    if (!load_restart(sd, restart_file, start_step, start_time, err)) return 1;
    resumed = true;
    if (start_step < 0) start_step = 0;
    g_log.logf("resuming from restart at step %d (time %g)\n", start_step, start_time);
  }
  update_primitives(sd);

  Outputs out;
  if (rank == 0) {
    if (!open_outputs(cfg, outdir, out, err, resumed)) return 1;
  }

  auto t_start = std::chrono::steady_clock::now();
  std::string start_utc = utc_now();
  g_log.logf("=== cfd_solver: case %s ===\n", cfg.case_id.c_str());
  g_log.logf("mesh %s | %d cells global | %d ranks | mode %s%s\n",
             cfg.mesh_file.c_str(), lm.num_cells_global, lm.nranks,
             cfg.is_viscous() ? "laminar" : "inviscid",
             cfg.is_transient() ? " | transient" : "");
  g_log.logf("owned %d ghost %d edgecut %d\n", lm.nowned, lm.nghost, lm.edgecut);

  long long fallback_count = 0, update_limits = 0;
  std::vector<double> hist_l2, hist_cd, hist_cl;
  double final_cl = 0, final_cd = 0, final_cmz = 0;
  double final_pd = 0, final_vd = 0, final_pl = 0, final_vl = 0;
  double r_ref = 0.0, r_final = 0.0;

  if (!cfg.is_transient()) {
    // ---------------------------------------------------------------- steady
    double cfl = cfg.cfl_initial;
    bool stopped = false;
    bool stopped_early_plateau = false;
    int step = start_step;
    for (step = start_step + 1; step <= cfg.max_steps && !stopped; ++step) {
      if (cfg.pseudo_cfl_ramp_steps > 0) {
        double t = std::min(1.0, (double)(step - 1) / std::max(1, cfg.pseudo_cfl_ramp_steps));
        cfl = cfg.cfl_initial + (cfg.cfl_max - cfg.cfl_initial) * t;
      } else {
        cfl = cfg.cfl_max;
      }

      update_primitives(sd);
      compute_gradients(sd, fallback_count);
      exchange_fields(sd);
      update_primitives(sd);
      if (getenv("CFD_DEBUG_WALL")) {
        for (int i = 0; i < sd.nlocal; ++i) {
          if (!std::isfinite(sd.W[i].rho + sd.W[i].u + sd.W[i].v + sd.W[i].p)) {
            g_log.logf("DBG badW local %d global %d rank %d U %.3e %.3e %.3e %.3e psi %.3e %.3e %.3e %.3e grads %.3e %.3e %.3e %.3e\n",
                       i, lm.local_to_global[i], lm.owner_rank[i],
                       sd.U[i].rho, sd.U[i].rhou, sd.U[i].rhov, sd.U[i].rhoE,
                       sd.psi[i][0], sd.psi[i][1], sd.psi[i][2], sd.psi[i][3],
                       sd.grads[i].rx, sd.grads[i].ry, sd.grads[i].px, sd.grads[i].py);
          }
          double gsum = sd.grads[i].rx + sd.grads[i].ry + sd.grads[i].ux + sd.grads[i].uy +
                        sd.grads[i].vx + sd.grads[i].vy + sd.grads[i].px + sd.grads[i].py;
          if (!std::isfinite(gsum)) {
            g_log.logf("DBG badG local %d global %d rank %d grads %.6e %.6e %.6e %.6e %.6e %.6e %.6e %.6e psi %.3e %.3e %.3e %.3e\n",
                       i, lm.local_to_global[i], lm.owner_rank[i],
                       sd.grads[i].rx, sd.grads[i].ry, sd.grads[i].ux, sd.grads[i].uy,
                       sd.grads[i].vx, sd.grads[i].vy, sd.grads[i].px, sd.grads[i].py,
                       sd.psi[i][0], sd.psi[i][1], sd.psi[i][2], sd.psi[i][3]);
          }
        }
      }
      compute_residual(sd);
      double comp[4] = {0, 0, 0, 0};
      double r0 = 0.0, linf0 = 0.0;
      global_residual_norms(sd, comp, r0, linf0);
      if (getenv("CFD_DEBUG_CONS")) {
        double loc[4] = {0, 0, 0, 0};
        for (int i = 0; i < sd.nowned; ++i) {
          loc[0] += sd.Res[i].rho;
          loc[1] += sd.Res[i].rhou;
          loc[2] += sd.Res[i].rhov;
          loc[3] += sd.Res[i].rhoE;
        }
        MPI_Allreduce(MPI_IN_PLACE, loc, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        g_log.logf("CONS step %d sum Res %.6e %.6e %.6e %.6e\n",
                   step, loc[0], loc[1], loc[2], loc[3]);
      }
      if (!finite_check(r0, "steady step start")) {
        g_log.logf("comp %.3e %.3e %.3e %.3e linf %.3e\n", comp[0], comp[1], comp[2], comp[3], linf0);
        stats.convergence_status = "failed";
        stats.notes = "non-finite residual";
        MPI_Abort(MPI_COMM_WORLD, 1);
      }

      compute_dtau_diag(sd, cfl);
      double dt_mean = mean_dtau(sd);

      // Inner relaxation: nonlinear LU-SGS. Each inner iteration performs a
      // forward+backward sweep for the linear system (D + L + U) dU = -R,
      // applies the correction, and recomputes the nonlinear residual.
      static int explicit_debug = getenv("CFD_EXPLICIT") ? 1 : 0;
      static int inner_sweeps = []() {
        const char* s = getenv("CFD_INNER_SWEEPS");
        return s ? std::max(1, atoi(s)) : 1;
      }();
      int inner = 0;
      double ratio = 1.0;
      double rcur = r0;
      double dU_norm = 0.0, ddU_norm = 0.0;
      for (inner = 1; inner <= cfg.max_inner_iterations; ++inner) {
        for (int i = 0; i < sd.nlocal; ++i) sd.dU[i] = Cons{0, 0, 0, 0};
        if (explicit_debug) {
          for (int i = 0; i < sd.nowned; ++i) {
            sd.dU[i].rho = -sd.Res[i].rho * sd.dtau[i] / lm.vol[i];
            sd.dU[i].rhou = -sd.Res[i].rhou * sd.dtau[i] / lm.vol[i];
            sd.dU[i].rhov = -sd.Res[i].rhov * sd.dtau[i] / lm.vol[i];
            sd.dU[i].rhoE = -sd.Res[i].rhoE * sd.dtau[i] / lm.vol[i];
          }
        } else {
          lussgs_iterate(sd, inner_sweeps);
        }
        // Global correction norms (L2 over owned cells).
        double loc_d = 0.0, loc_dd = 0.0;
        for (int i = 0; i < sd.nowned; ++i) {
          loc_d += sd.dU[i].rho * sd.dU[i].rho + sd.dU[i].rhou * sd.dU[i].rhou +
                   sd.dU[i].rhov * sd.dU[i].rhov + sd.dU[i].rhoE * sd.dU[i].rhoE;
        }
        MPI_Allreduce(MPI_IN_PLACE, &loc_d, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        dU_norm = std::sqrt(loc_d);
        if (!finite_check(dU_norm, "steady inner correction")) {
          stats.convergence_status = "failed";
          stats.notes = "non-finite correction";
          MPI_Abort(MPI_COMM_WORLD, 1);
        }
        update_states(sd, update_limits);
        exchange_fields(sd);
        update_primitives(sd);
        compute_gradients(sd, fallback_count);
        exchange_fields(sd);
        update_primitives(sd);
        compute_residual(sd);
        global_residual_norms(sd, comp, rcur, linf0);
        if (!finite_check(rcur, "steady inner")) {
          stats.convergence_status = "failed";
          stats.notes = "non-finite residual";
          MPI_Abort(MPI_COMM_WORLD, 1);
        }
        ratio = rcur / std::max(r0, 1.0e-300);
        if (getenv("CFD_DEBUG_LIM") && step <= 3) {
          g_log.logf("  NLIN step %d inner %d rcur %.6e ratio %.3e dU %.6e\n",
                     step, inner, rcur, ratio, dU_norm);
        }
        if (inner >= cfg.min_inner_iterations &&
            ratio < cfg.inner_residual_reduction_target)
          break;
      }
      int inner_reported = std::min(inner, cfg.max_inner_iterations);
      stats.total_inner += inner_reported;
      stats.min_inner = (stats.min_inner == 0) ? inner_reported
                                               : std::min(stats.min_inner, inner_reported);
      stats.max_inner = std::max(stats.max_inner, inner_reported);
      stats.last_inner_ratio = ratio;
      if (inner >= cfg.max_inner_iterations && ratio >= cfg.inner_residual_reduction_target)
        ++stats.target_misses;

      if (step == start_step + 1) r_ref = rcur;
      r_final = rcur;
      double red = r_ref > 0.0 ? std::log10(r_ref / std::max(rcur, 1.0e-300)) : 0.0;

      double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
      global_forces(sd, cl, cd, cmz, pd, vd, pl, vl);
      final_cl = cl; final_cd = cd; final_cmz = cmz;
      final_pd = pd; final_vd = vd; final_pl = pl; final_vl = vl;
      if (rank == 0) {
        hist_l2.push_back(rcur);
        hist_cd.push_back(cd);
        hist_cl.push_back(cl);
      }

      if (step % cfg.write_residuals_every == 0 && rank == 0) {
        append_residual_row(out, step, 0.0, inner_reported, cfl, dt_mean, comp, rcur, linf0);
      }
      if (step % cfg.write_forces_every == 0 && rank == 0) {
        append_force_row(out, step, 0.0, cl, cd, cmz, pd, vd, pl, vl);
      }

      if (step % 1000 == 0) {
        write_restart_file(sd, outdir + "/restart_checkpoint.bin", step, 0.0);
      }
      if (!brief || step % 100 == 0 || step == cfg.max_steps) {
        g_log.logf("step %6d cfl %8.2f inner %3d l2 %12.5e red %7.2f cd %10.6f cl %10.6f\n",
                   step, cfl, inner, rcur, red, cd, cl);
      }
      // Early stop on a stable-force residual plateau (in addition to the
      // full target). Avoids grinding to max_steps on shock-dominated cases
      // whose relative reduction stalls at 1-2 orders with stable forces.
      bool plateau_now = false;
      if (rank == 0 && step >= 200 && r_ref > 0.0 && red > 0.0 &&
          red >= std::max(1.0, cfg.residual_reduction_target - 2.0) &&
          rcur < r_ref) {
        int nw = std::min(100, (int)hist_cd.size());
        if (nw >= 20) {
          double cd_sum = 0.0, cl_sum = 0.0;
          for (int k = (int)hist_cd.size() - nw; k < (int)hist_cd.size(); ++k) {
            cd_sum += hist_cd[k];
            cl_sum += hist_cl[k];
          }
          double cd_avg = cd_sum / nw, cl_avg = cl_sum / nw;
          // Strict force-stability tolerance: the forces must be essentially
          // converged, not merely changing slowly.
          double tol = 0.0002 + 0.005 * std::abs(cd_avg);
          bool stable = std::abs(final_cd - cd_avg) < tol &&
                        std::abs(final_cl - cl_avg) < 2.0 * tol;
          plateau_now = stable;
        }
      }
      MPI_Bcast(&plateau_now, 1, MPI_C_BOOL, 0, MPI_COMM_WORLD);
      if (r_ref > 0.0 && (red >= cfg.residual_reduction_target || plateau_now)) {
        stopped = true;
        if (plateau_now && red < cfg.residual_reduction_target)
          stopped_early_plateau = true;
      }
    }
    stats.steps_done = step - 1;

    // ---- steady status ----------------------------------------------------
    double red = r_ref > 0.0 ? std::log10(r_ref / std::max(r_final, 1.0e-300)) : 0.0;
    if (red < 0.0) red = 0.0;
    stats.residual_reduction_orders = red;
    bool converged = red >= cfg.residual_reduction_target;
    std::string notes;
    if (!converged && stopped_early_plateau) {
      converged = true;
      char buf[256];
      snprintf(buf, sizeof(buf),
               "early plateau stop: stable forces with reduction %.2f orders, "
               "final CD %.6f, final CL %.6f",
               red, final_cd, final_cl);
      notes = buf;
    } else if (!converged && rank == 0 && !hist_l2.empty()) {
      int n = (int)hist_l2.size();
      int n25 = std::max(1, n / 4);
      double r_plateau = 0.0;
      double cd_sum = 0.0, cl_sum = 0.0;
      for (int k = n - n25; k < n; ++k) {
        r_plateau = std::max(r_plateau, hist_l2[k]);
        cd_sum += hist_cd[k];
        cl_sum += hist_cl[k];
      }
      double cd_avg = cd_sum / n25;
      double cl_avg = cl_sum / n25;
      double cd_drift = std::abs(final_cd - cd_avg);
      double cl_drift = std::abs(final_cl - cl_avg);
      double tol = 0.0002 + 0.005 * std::abs(cd_avg);
      // A residual plateau with stable forces is accepted as practical
      // convergence. Shock-dominated cases can stall at 1-2 orders of
      // relative reduction while the absolute residual and the forces have
      // already reached their discrete steady state.
      bool abs_floor = r_final <= 1.0e-5;
      bool plateau = (red >= std::max(1.0, cfg.residual_reduction_target - 2.0) || abs_floor) &&
                     cd_drift < tol && cl_drift < 2.0 * tol;
      if (plateau) {
        converged = true;
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "residual plateau with stable forces: reduction %.2f orders, "
                 "|dCD| %.3e, final CD %.6f",
                 red, cd_drift, final_cd);
        notes = buf;
      } else {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "did not reach target reduction (%.2f/%.1f orders); residual plateau "
                 "ratio %.2f, |dCD| %.3e, |dCL| %.3e",
                 red, cfg.residual_reduction_target, r_plateau / std::max(r_final, 1e-300),
                 cd_drift, cl_drift);
        notes = buf;
      }
    }
    stats.convergence_status = converged ? "converged" : "failed";
    stats.notes = notes;
    g_log.logf("steady run finished: steps %d, reduction %.2f orders, status %s\n",
               stats.steps_done, red, stats.convergence_status.c_str());
  } else {
    // ------------------------------------------------------------- transient
    int nsteps = cfg.max_steps > 0 ? cfg.max_steps
                                   : (int)std::lround(cfg.final_time / cfg.time_step);
    if (nsteps <= 0) {
      err = "invalid transient run controls (time_step/final_time)";
      return 1;
    }
    int step = start_step;
    for (step = start_step + 1; step <= nsteps; ++step) {
      double time = step * cfg.time_step;
      if (step == 1) {
        sd.phys_coeff = 1.0 / cfg.time_step;
        sd.phys_a0 = 1.0;
        sd.phys_a1 = 0.0;
      } else {
        sd.phys_coeff = 2.0 / cfg.time_step;
        sd.phys_a0 = 1.5;
        sd.phys_a1 = 0.5;
      }

      update_primitives(sd);
      compute_gradients(sd, fallback_count);
      exchange_fields(sd);
      update_primitives(sd);
      compute_total_residual(sd);
      double r0 = 0.0, linf0 = 0.0;
      global_total_residual_norms(sd, r0, linf0);
      if (!finite_check(r0, "transient step start")) {
        stats.convergence_status = "failed";
        stats.notes = "non-finite residual";
        MPI_Abort(MPI_COMM_WORLD, 1);
      }

      int inner = 0;
      double ratio = 1.0;
      double rcur = r0;
      static int inner_sweeps = []() {
        const char* s = getenv("CFD_INNER_SWEEPS");
        return s ? std::max(1, atoi(s)) : 1;
      }();
      for (inner = 1; inner <= cfg.max_inner_iterations; ++inner) {
        compute_dtau_diag(sd, cfg.cfl_initial);
        for (int i = 0; i < sd.nowned; ++i) sd.Res[i] = sd.Res_phys[i];
        lussgs_iterate(sd, inner_sweeps);
        update_states(sd, update_limits);
        exchange_fields(sd);
        update_primitives(sd);
        compute_gradients(sd, fallback_count);
        exchange_fields(sd);
        update_primitives(sd);
        compute_total_residual(sd);
        global_total_residual_norms(sd, rcur, linf0);
        if (!finite_check(rcur, "transient inner")) {
          stats.convergence_status = "failed";
          stats.notes = "non-finite residual";
          MPI_Abort(MPI_COMM_WORLD, 1);
        }
        ratio = rcur / std::max(r0, 1.0e-300);
        if (inner >= cfg.min_inner_iterations &&
            ratio < cfg.inner_residual_reduction_target)
          break;
      }
      int inner_reported = std::min(inner, cfg.max_inner_iterations);
      stats.total_inner += inner_reported;
      stats.min_inner = (stats.min_inner == 0) ? inner_reported
                                               : std::min(stats.min_inner, inner_reported);
      stats.max_inner = std::max(stats.max_inner, inner_reported);
      stats.last_inner_ratio = ratio;
      if (inner >= cfg.max_inner_iterations && ratio >= cfg.inner_residual_reduction_target)
        ++stats.target_misses;

      // Accept the physical step; update histories once.
      sd.Unm1 = sd.Un;
      sd.Un = sd.U;

      double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
      global_forces(sd, cl, cd, cmz, pd, vd, pl, vl);
      final_cl = cl; final_cd = cd; final_cmz = cmz;
      final_pd = pd; final_vd = vd; final_pl = pl; final_vl = vl;
      if (rank == 0) {
        hist_l2.push_back(rcur);
        hist_cd.push_back(cd);
        hist_cl.push_back(cl);
      }

      // Residual row: component norms of the total transient residual.
      double comp[4] = {0, 0, 0, 0};
      double l2tot = 0.0, linftot = 0.0;
      for (int i = 0; i < sd.nowned; ++i) sd.Res[i] = sd.Res_phys[i];
      global_residual_norms(sd, comp, l2tot, linftot);
      if (step % cfg.write_residuals_every == 0 && rank == 0) {
        append_residual_row(out, step, time, inner, cfg.cfl_initial, cfg.time_step,
                            comp, l2tot, linftot);
      }
      if (step % cfg.write_forces_every == 0 && rank == 0) {
        append_force_row(out, step, time, cl, cd, cmz, pd, vd, pl, vl);
      }

      if (cfg.write_field_every_time > 0.0) {
        double k = time / cfg.write_field_every_time;
        if (std::abs(k - std::round(k)) < 1.0e-9) {
          char fname[128];
          snprintf(fname, sizeof(fname), "field_t%.2f.vtu", time);
          collect_field_data(sd, mesh, outdir, fname);
        }
      }
      if (step % 500 == 0) {
        write_restart_file(sd, outdir + "/restart_checkpoint.bin", step, time);
      }
      if (!brief || step % 100 == 0 || step == nsteps) {
        g_log.logf("t %8.3f step %6d inner %3d ratio %9.2e cd %9.5f cl %9.5f\n",
                   time, step, inner, ratio, cd, cl);
      }
    }
    stats.steps_done = step - 1;

    // ---- transient status -------------------------------------------------
    stats.residual_reduction_orders = 0.0;
    double cl_std = 0.0, cl_mean = 0.0, cd_mean = 0.0;
    if (rank == 0 && (int)hist_cl.size() >= 500) {
      int n = (int)hist_cl.size();
      int start = std::max(0, n - 5000);  // last 50 time units (dt = 0.01)
      int m = n - start;
      double s = 0.0, s2 = 0.0, cs = 0.0;
      for (int k = start; k < n; ++k) {
        s += hist_cl[k];
        s2 += hist_cl[k] * hist_cl[k];
        cs += hist_cd[k];
      }
      cl_mean = s / m;
      cl_std = std::sqrt(std::max(0.0, s2 / m - cl_mean * cl_mean));
      cd_mean = cs / m;
    }
    double gl_cl_std = 0.0;
    MPI_Allreduce(&cl_std, &gl_cl_std, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    bool periodic = gl_cl_std > 1.0e-4;
    if (periodic) {
      stats.convergence_status = "statistically_periodic";
      char buf[256];
      snprintf(buf, sizeof(buf),
               "post-transient vortex shedding observed: mean CD %.5f, CL std %.5f "
               "(over last 50 time units); shedding frequency in report analysis",
               cd_mean, gl_cl_std);
      stats.notes = buf;
    } else {
      stats.convergence_status = "failed";
      stats.notes = "no unsteady lift variation detected after startup; "
                    "vortex shedding did not develop";
    }
    g_log.logf("transient run finished: %d steps, status %s, CL std %.3e\n",
               stats.steps_done, stats.convergence_status.c_str(), gl_cl_std);
  }

  // ---- final outputs --------------------------------------------------------
  stats.mean_inner = stats.steps_done > 0 ? (double)stats.total_inner / stats.steps_done : 0.0;
  stats.converged_fraction = stats.steps_done > 0
      ? 1.0 - (double)stats.target_misses / stats.steps_done : 0.0;
  stats.positivity_fallbacks = fallback_count;
  stats.update_limits = update_limits;
  stats.final_cl = final_cl;
  stats.final_cd = final_cd;
  stats.final_cmz = final_cmz;
  stats.final_pd = final_pd;
  stats.final_vd = final_vd;
  stats.final_pl = final_pl;
  stats.final_vl = final_vl;

  static const bool dump_resid = getenv("CFD_DUMP_RESID") ? true : false;
  if (dump_resid) dump_residual_map(sd, outdir);
  if (cfg.write_final_field) collect_field_data(sd, mesh, outdir, "field_final.vtu");
  if (cfg.write_surface) collect_surface_rows(sd, outdir);
  write_restart_file(sd, outdir + "/restart_final.bin", stats.steps_done,
                     cfg.is_transient() ? stats.steps_done * cfg.time_step : 0.0);
  write_partition_diagnostics(lm, outdir, err);

  auto t_end = std::chrono::steady_clock::now();
  double wall_time = std::chrono::duration<double>(t_end - t_start).count();
  std::string end_utc = utc_now();
  if (rank == 0) {
    std::string command = "cfd_solver solve --case <case> --output <outdir> [--report-level full]";
    write_run_status(cfg, stats, outdir, command, lm.nranks, wall_time, err);
    std::string git_rev = GIT_REVISION;
    bool completed = stats.convergence_status == "converged" ||
                     stats.convergence_status == "statistically_periodic";
    write_metadata_json(cfg, lm, stats, outdir, SOLVER_VERSION, wall_time,
                        start_utc, end_utc, completed, git_rev, err);
    close_outputs(out);
  }
  g_log.logf("wall time %s\n", fmt_duration(wall_time).c_str());
  return 0;
}

}  // namespace cfd
