#include "timestep.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include "physics.hpp"

namespace cfd {

void InnerStats::add(int iters, bool hit_target, double ratio) {
  if (observed_min < 0 || iters < observed_min) observed_min = iters;
  if (iters > observed_max) observed_max = iters;
  observed_mean += iters;
  ++total_steps;
  if (!hit_target) ++target_misses;
  last_ratio = ratio;
}

void InnerStats::finalize() {
  if (total_steps > 0) {
    observed_mean /= total_steps;
    converged_fraction = 1.0 - static_cast<double>(target_misses) / total_steps;
  }
}

void SolverContext::init() {
  st.allocate(mesh);
  Vec4 Wfs{cs->fs_rho, cs->fs.u, cs->fs.v, cs->fs_p};
  Vec4 Ufs = prim_to_cons(Wfs, cs->gas);
  for (int i = 0; i < mesh.n_cells; ++i) st.U[i] = Ufs;
  compute_primitives(mesh, cs->gas, st);
  U_n.assign(mesh.n_owned, Ufs);
  U_nm1.assign(mesh.n_owned, Ufs);
  halo.comm = comm;
}

void SolverContext::eval_residual(const AssembleOpts& o, ForceSums& forces_local) {
  // Halo sync of U (4 doubles/cell), then gradients/limiter, then a combined
  // gradient+limiter halo sync (12 doubles/cell) before assembly.
  halo.exchange(mesh, &st.U[0][0], 4);
  compute_primitives(mesh, cs->gas, st);
  compute_gradients(mesh, *cs, st, o);
  compute_limiter(mesh, *cs, st, o);
  const int nvar = 12;
  size_t n = static_cast<size_t>(mesh.n_cells) * nvar;
  if (pack_buf.size() < n) pack_buf.resize(n);
  for (int i = 0; i < mesh.n_cells; ++i) {
    for (int v = 0; v < 8; ++v) pack_buf[i * nvar + v] = st.gradW[i][v];
    for (int v = 0; v < 4; ++v) pack_buf[i * nvar + 8 + v] = st.phi[i][v];
  }
  halo.exchange(mesh, pack_buf.data(), nvar);
  for (int i = mesh.n_owned; i < mesh.n_cells; ++i) {
    for (int v = 0; v < 8; ++v) st.gradW[i][v] = pack_buf[i * nvar + v];
    for (int v = 0; v < 4; ++v) st.phi[i][v] = pack_buf[i * nvar + 8 + v];
  }
  assemble_residual(mesh, *cs, st, o, forces_local);
}

namespace {

ResidualNorms norms_of(MPI_Comm comm, const LocalMesh& mesh,
                       const std::vector<double>& lambda_sum,
                       const std::vector<Vec4>& Rv, bool divide_by_vol) {
  double ss[4] = {0, 0, 0, 0};
  double linf = 0.0, dtmin = 0.0;
  bool first = true;
  for (int i = 0; i < mesh.n_owned; ++i) {
    double iv = divide_by_vol ? 1.0 / mesh.vol[i] : 1.0;
    for (int k = 0; k < 4; ++k) {
      double r = Rv[i][k] * iv;
      ss[k] += r * r;
      linf = std::max(linf, std::fabs(r));
    }
    if (lambda_sum[i] > 0.0) {
      double d = mesh.vol[i] / lambda_sum[i];
      if (first || d < dtmin) { dtmin = d; first = false; }
    }
  }
  double gss[4], glinf = 0.0, gdtmin = 0.0;
  MPI_Allreduce(ss, gss, 4, MPI_DOUBLE, MPI_SUM, comm);
  MPI_Allreduce(&linf, &glinf, 1, MPI_DOUBLE, MPI_MAX, comm);
  MPI_Allreduce(&dtmin, &gdtmin, 1, MPI_DOUBLE, MPI_MIN, comm);
  long no = mesh.n_owned, n_global = 0;
  MPI_Allreduce(&no, &n_global, 1, MPI_LONG, MPI_SUM, comm);
  ResidualNorms out;
  double tot = 0.0;
  for (int k = 0; k < 4; ++k) {
    out.per_eq[k] = std::sqrt(gss[k] / static_cast<double>(n_global));
    tot += gss[k];
  }
  out.l2 = std::sqrt(tot / (4.0 * static_cast<double>(n_global)));
  out.linf = glinf;
  out.dt_min = gdtmin;
  return out;
}

}  // namespace

ResidualNorms SolverContext::global_norms() const {
  return norms_of(comm, mesh, st.lambda_sum, st.R, true);
}

ResidualNorms SolverContext::global_norms_F(const std::vector<Vec4>& F) const {
  return norms_of(comm, mesh, st.lambda_sum, F, true);
}

ForceCoeffs SolverContext::reduce_forces(const ForceSums& l) const {
  double loc[6] = {l.fx_p, l.fy_p, l.fx_v, l.fy_v, l.mz_p, l.mz_v};
  double g[6];
  MPI_Allreduce(loc, g, 6, MPI_DOUBLE, MPI_SUM, comm);
  const double aoa = cs->aoa_deg * M_PI / 180.0;
  const double dx = std::cos(aoa), dy = std::sin(aoa);   // drag direction
  const double lx = -dy, ly = dx;                        // lift direction
  const double qA = cs->fs.q_dyn * cs->ref.area;
  const double qAL = qA * cs->ref.length;
  ForceCoeffs f;
  f.pressure_drag = (g[0] * dx + g[1] * dy) / qA;
  f.viscous_drag = (g[2] * dx + g[3] * dy) / qA;
  f.pressure_lift = (g[0] * lx + g[1] * ly) / qA;
  f.viscous_lift = (g[2] * lx + g[3] * ly) / qA;
  f.cd = f.pressure_drag + f.viscous_drag;
  f.cl = f.pressure_lift + f.viscous_lift;
  f.pressure_moment = g[4] / qAL;
  f.viscous_moment = g[5] / qAL;
  f.cmz = f.pressure_moment + f.viscous_moment;
  return f;
}

double SolverContext::local_dt(int i, double cfl) const {
  return cfl * mesh.vol[i] / std::max(st.lambda_sum[i], 1e-30);
}

namespace {

// Applies the approximate (frozen) Jacobian to x: out = D x + sum_j B_ij x_j.
// D is the scalar diagonal per cell; B_ij = 0.5*(A_j*n_ij*area - lambda*I).
void apply_approx_jacobian(SolverContext& ctx, const std::vector<double>& diag,
                           const std::vector<Vec4>& x, std::vector<Vec4>& out) {
  const LocalMesh& m = ctx.mesh;
  const GasModel& gas = ctx.cs->gas;
  for (int i = 0; i < m.n_owned; ++i) {
    Vec4 r;
    for (int v = 0; v < 4; ++v) r[v] = diag[i] * x[i][v];
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      if (m.face_r[f] < 0) continue;
      int j = (m.face_l[f] == i) ? m.face_r[f] : m.face_l[f];
      double nx = m.face_nx[f], ny = m.face_ny[f];
      if (m.face_r[f] == i) { nx = -nx; ny = -ny; }
      double lam = ctx.st.face_lambda[f];
      Vec4 aj = flux_jac_times(ctx.st.W[j], nx, ny, x[j], gas);
      for (int v = 0; v < 4; ++v)
        r[v] += 0.5 * (m.face_area[f] * aj[v] - lam * x[j][v]);
    }
    out[i] = r;
  }
}

double global_rms(MPI_Comm comm, int n_owned, const std::vector<Vec4>& v) {
  double ss = 0.0;
  for (int i = 0; i < n_owned; ++i)
    for (int k = 0; k < 4; ++k) ss += v[i][k] * v[i][k];
  double g = 0.0;
  MPI_Allreduce(&ss, &g, 1, MPI_DOUBLE, MPI_SUM, comm);
  long no = n_owned, ng = 0;
  MPI_Allreduce(&no, &ng, 1, MPI_LONG, MPI_SUM, comm);
  return std::sqrt(g / (4.0 * std::max<long>(1, ng)));
}

// One symmetric Gauss--Seidel sweep pair for (D + B) dU = rhs with
// simplified Jacobians. Ghost dU values are synchronized between sweeps.
void sgs_sweep_pair(SolverContext& ctx, const std::vector<double>& diag,
                    const std::vector<Vec4>& rhs, std::vector<Vec4>& dU,
                    bool jacobi_term) {
  const LocalMesh& m = ctx.mesh;
  const FlowState& st = ctx.st;
  const GasModel& gas = ctx.cs->gas;

  // Forward sweep (owned cells ascending; ghosts contribute lagged values).
  for (int i = 0; i < m.n_owned; ++i) {
    Vec4 r = rhs[i];
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      if (m.face_r[f] < 0) continue;  // boundary faces contribute to D only
      int j = (m.face_l[f] == i) ? m.face_r[f] : m.face_l[f];
      bool lower = (j < m.n_owned && j < i) || (j >= m.n_owned);
      if (!lower) continue;
      double nx = m.face_nx[f], ny = m.face_ny[f];
      if (m.face_r[f] == i) { nx = -nx; ny = -ny; }  // normal from i to j
      double lam = st.face_lambda[f];
      Vec4 b;
      if (jacobi_term) {
        Vec4 aj = flux_jac_times(st.W[j], nx, ny, dU[j], gas);
        for (int v = 0; v < 4; ++v)
          b[v] = 0.5 * (m.face_area[f] * aj[v] - lam * dU[j][v]);
      } else {
        for (int v = 0; v < 4; ++v) b[v] = -0.5 * lam * dU[j][v];
      }
      isub4(r, b);
    }
    for (int v = 0; v < 4; ++v) dU[i][v] = r[v] / diag[i];
  }
  ctx.halo.exchange(m, &dU[0][0], 4);

  // Backward sweep (owned cells descending; owned j > i already updated).
  for (int i = m.n_owned - 1; i >= 0; --i) {
    Vec4 r;
    for (int v = 0; v < 4; ++v) r[v] = diag[i] * dU[i][v];
    for (int k = m.cell_face_start[i]; k < m.cell_face_start[i + 1]; ++k) {
      int f = m.cell_face_list[k];
      if (m.face_r[f] < 0) continue;
      int j = (m.face_l[f] == i) ? m.face_r[f] : m.face_l[f];
      if (j >= m.n_owned || j <= i) continue;  // only owned upper neighbors
      double nx = m.face_nx[f], ny = m.face_ny[f];
      if (m.face_r[f] == i) { nx = -nx; ny = -ny; }
      double lam = st.face_lambda[f];
      Vec4 b;
      if (jacobi_term) {
        Vec4 aj = flux_jac_times(st.W[j], nx, ny, dU[j], gas);
        for (int v = 0; v < 4; ++v)
          b[v] = 0.5 * (m.face_area[f] * aj[v] - lam * dU[j][v]);
      } else {
        for (int v = 0; v < 4; ++v) b[v] = -0.5 * lam * dU[j][v];
      }
      isub4(r, b);
    }
    for (int v = 0; v < 4; ++v) dU[i][v] = r[v] / diag[i];
  }
  ctx.halo.exchange(m, &dU[0][0], 4);
}

// Positivity-safeguarded conservative update.
void apply_update(SolverContext& ctx, const std::vector<Vec4>& dU) {
  const GasModel& gas = ctx.cs->gas;
  for (int i = 0; i < ctx.mesh.n_owned; ++i) {
    Vec4 Un = add4(ctx.st.U[i], dU[i]);
    Vec4 W = cons_to_prim(Un, gas);
    double scale = 1.0;
    int tries = 0;
    while ((Un[0] <= RHO_FLOOR ||
            (gas.gamma - 1.0) * (Un[3] - 0.5 * (Un[1] * Un[1] + Un[2] * Un[2]) /
                                                  std::max(Un[0], RHO_FLOOR)) <=
                P_FLOOR) &&
           tries < 30) {
      scale *= 0.5;
      ++tries;
      Un = add4(ctx.st.U[i], scale4(dU[i], scale));
      W = cons_to_prim(Un, gas);
    }
    if (tries >= 30) continue;  // skip the update for this cell
    ctx.st.U[i] = Un;
    (void)W;
  }
}

}  // namespace

RunResult run_steady(SolverContext& ctx, OutputContext& out, long start_step) {
  const CaseFile& cs = *ctx.cs;
  const RunControl& rc = cs.run;
  RunResult res;
  res.n_physical_steps = 0;

  AssembleOpts o;
  o.viscous = cs.fs.viscous;
  o.inviscid_flux = ctx.cfg.inviscid_flux;
  o.rusanov_scale = rc.rusanov_dissipation_scale;
  o.limiter = ctx.cfg.limiter;
  o.venkat_k = ctx.cfg.venkat_k;
  o.time = 0.0;
  o.pert_aoa_deg = 0.0;
  o.pert_duration = 0.0;

  std::vector<Vec4> dU(ctx.mesh.n_cells, Vec4{});
  std::vector<Vec4> rhs(ctx.mesh.n_owned, Vec4{});
  std::vector<double> diag(ctx.mesh.n_owned, 0.0);

  ForceSums fl;
  ctx.eval_residual(o, fl);
  ResidualNorms norms = ctx.global_norms();
  ForceCoeffs fc = ctx.reduce_forces(fl);
  double base_l2 = norms.l2;
  if (base_l2 <= 0.0 || !std::isfinite(base_l2)) base_l2 = 1e-30;

  std::deque<double> recent_cd, recent_l2;
  long step = start_step;
  auto log_row = [&](long s, int inner_used, double cfl) {
    write_residual_row(out, s, 0.0, inner_used, cfl, norms.dt_min, norms.per_eq,
                       norms.l2, norms.linf);
    write_force_row(out, s, 0.0, fc.cl, fc.cd, fc.cmz, fc.pressure_drag,
                    fc.viscous_drag, fc.pressure_lift, fc.viscous_lift);
  };
  log_row(step, 0, rc.cfl_initial);
  recent_cd.push_back(fc.cd);
  recent_l2.push_back(norms.l2);

  bool failed = false, converged = false;
  int inner_used_last = 0;
  long last_logged = start_step;
  for (step = start_step + 1; step <= start_step + rc.max_steps; ++step) {
    const double ramp_frac =
        std::min(1.0, static_cast<double>(step - start_step - 1) /
                          std::max(1, rc.pseudo_cfl_ramp_steps));
    const double cfl = rc.cfl_initial *
                       std::pow(rc.cfl_max / rc.cfl_initial, ramp_frac);
    // Frozen-Jacobian inner solve of (D + B) dU = -R by defect correction:
    // each inner iteration applies one symmetric Gauss--Seidel sweep pair to
    // the *true* linear residual rA = R + (D+B) dU, so the iteration converges
    // to the exact solution of the approximate linear system.
    for (int i = 0; i < ctx.mesh.n_owned; ++i) {
      double dt = ctx.local_dt(i, cfl);
      diag[i] = ctx.mesh.vol[i] / dt + 0.5 * ctx.st.lambda_sum[i];
    }
    for (auto& d : dU) d = Vec4{};
    ctx.halo.exchange(ctx.mesh, &dU[0][0], 4);  // zero ghost dU
    std::vector<Vec4> rA(ctx.mesh.n_owned);     // linear residual
    std::vector<Vec4> corr(ctx.mesh.n_cells, Vec4{});  // sweep correction
    apply_approx_jacobian(ctx, diag, dU, rA);
    for (int i = 0; i < ctx.mesh.n_owned; ++i) iadd4(rA[i], ctx.st.R[i]);
    const double r0 = std::max(global_rms(ctx.comm, ctx.mesh.n_owned, rA), 1e-300);
    int inner = 0;
    double ratio = 1.0;
    for (inner = 1; inner <= rc.max_inner_iterations; ++inner) {
      for (int i = 0; i < ctx.mesh.n_owned; ++i) rhs[i] = scale4(rA[i], -1.0);
      for (auto& d : corr) d = Vec4{};
      sgs_sweep_pair(ctx, diag, rhs, corr, true);
      for (int i = 0; i < ctx.mesh.n_owned; ++i) iadd4(dU[i], corr[i]);
      // Refresh the true linear residual.
      apply_approx_jacobian(ctx, diag, dU, rA);
      for (int i = 0; i < ctx.mesh.n_owned; ++i) iadd4(rA[i], ctx.st.R[i]);
      double rg = global_rms(ctx.comm, ctx.mesh.n_owned, rA);
      ratio = rg / r0;
      if (getenv("CFD_DEBUG_INNER") && ctx.rank == 0 && step <= 3 &&
          (inner <= 5 || inner % 10 == 0)) {
        std::printf("    step %ld inner %d ratio %.4e\n", step, inner, ratio);
      }
      if (inner >= rc.min_inner_iterations &&
          ratio <= rc.inner_residual_reduction_target)
        break;
    }
    if (inner > rc.max_inner_iterations) inner = rc.max_inner_iterations;
    inner_used_last = inner;
    res.inner.add(inner, ratio <= rc.inner_residual_reduction_target, ratio);

    apply_update(ctx, dU);
    ctx.eval_residual(o, fl);
    norms = ctx.global_norms();
    fc = ctx.reduce_forces(fl);
    log_row(step, inner_used_last, cfl);
    last_logged = step;
    recent_cd.push_back(fc.cd);
    recent_l2.push_back(norms.l2);
    if (recent_cd.size() > 2000) { recent_cd.pop_front(); recent_l2.pop_front(); }

    if (ctx.st.non_finite || !std::isfinite(norms.l2)) {
      failed = true;
      out.log("ERROR: non-finite residual detected; marking case failed");
      break;
    }
    double orders = std::log10(base_l2 / std::max(norms.l2, 1e-300));
    if (step % 500 == 0) {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "step %ld  cfl %.1f  resL2 %.3e  (-%.2f orders)  cd %.5f  cl %.5f  inner %d",
                    step, cfl, norms.l2, orders, fc.cd, fc.cl, inner_used_last);
      out.log(buf);
    }
    if (orders >= rc.residual_reduction_target) {
      converged = true;
      break;
    }
  }

  res.final_step = last_logged;
  res.final_time = 0.0;
  res.last_forces = fc;
  res.residual_reduction = std::log10(base_l2 / std::max(norms.l2, 1e-300));
  res.inner.finalize();

  if (failed) {
    res.status = "failed";
  } else if (converged) {
    res.status = "converged";
  } else {
    // Plateau check: residual down >= 2 orders and force drift tiny.
    bool plateau = res.residual_reduction >= 2.0;
    if (plateau && recent_cd.size() > 100) {
      size_t n = recent_cd.size();
      size_t m = n / 5;
      double c0 = 0.0, c1 = 0.0;
      for (size_t i = 0; i < m; ++i) c0 += recent_cd[i];
      for (size_t i = n - m; i < n; ++i) c1 += recent_cd[i];
      c0 /= m; c1 /= m;
      double drift = std::fabs(c1 - c0) / std::max(1e-12, std::fabs(c1));
      plateau = drift < 1e-3;
    }
    res.status = plateau ? "converged" : "failed";
  }
  return res;
}

RunResult run_transient(SolverContext& ctx, OutputContext& out, long start_step,
                        double start_time) {
  const CaseFile& cs = *ctx.cs;
  const RunControl& rc = cs.run;
  RunResult res;

  AssembleOpts o;
  o.viscous = cs.fs.viscous;
  o.inviscid_flux = ctx.cfg.inviscid_flux;
  o.rusanov_scale = rc.rusanov_dissipation_scale;
  o.limiter = ctx.cfg.limiter;
  o.venkat_k = ctx.cfg.venkat_k;
  o.pert_aoa_deg = ctx.cfg.pert_aoa_deg;
  o.pert_duration = ctx.cfg.pert_duration;

  const double dt = rc.time_step;
  const double tf = rc.final_time;
  const long nsteps = static_cast<long>(std::llround((tf - start_time) / dt));
  const bool has_history = start_step > 0;  // restart with valid BDF2 history

  std::vector<Vec4> dU(ctx.mesh.n_cells, Vec4{});
  std::vector<Vec4> rhs(ctx.mesh.n_owned, Vec4{});
  std::vector<Vec4> F(ctx.mesh.n_owned, Vec4{});
  std::vector<double> diag(ctx.mesh.n_owned, 0.0);

  // History arrays (owned cells): U_n, U_nm1 already sized in init().
  for (int i = 0; i < ctx.mesh.n_owned; ++i) {
    ctx.U_n[i] = ctx.st.U[i];
    ctx.U_nm1[i] = ctx.st.U[i];
  }

  ForceSums fl;
  double last_field_time = start_time;
  std::deque<double> late_cl;  // lift history for periodicity assessment

  auto compute_F = [&](double c0, double c1, double c2) {
    for (int i = 0; i < ctx.mesh.n_owned; ++i)
      for (int v = 0; v < 4; ++v)
        F[i][v] = ctx.mesh.vol[i] * (c0 * ctx.st.U[i][v] + c1 * ctx.U_n[i][v] +
                                     c2 * ctx.U_nm1[i][v]) +
                  ctx.st.R[i][v];
  };

  // Log the initial state row.
  {
    o.time = start_time;
    ctx.eval_residual(o, fl);
    ResidualNorms norms = ctx.global_norms();
    ForceCoeffs fc0 = ctx.reduce_forces(fl);
    write_residual_row(out, start_step, start_time, 0, rc.cfl_initial, dt,
                       norms.per_eq, norms.l2, norms.linf);
    write_force_row(out, start_step, start_time, fc0.cl, fc0.cd, fc0.cmz,
                    fc0.pressure_drag, fc0.viscous_drag, fc0.pressure_lift,
                    fc0.viscous_lift);
  }

  bool failed = false;
  long step = start_step;
  ForceCoeffs fc;
  ResidualNorms fnorms;
  for (long n = 1; n <= nsteps; ++n) {
    step = start_step + n;
    const double t = start_time + n * dt;
    o.time = t;
    const bool bdf1 = (n == 1) && !has_history;
    const double c0 = bdf1 ? 1.0 / dt : 1.5 / dt;
    const double c1 = bdf1 ? -1.0 / dt : -2.0 / dt;
    const double c2 = bdf1 ? 0.0 : 0.5 / dt;

    // Predictor: linear extrapolation for BDF2, copy for the first step.
    for (int i = 0; i < ctx.mesh.n_owned; ++i) {
      Vec4 pred = ctx.U_n[i];
      if (!bdf1) pred = add4(ctx.U_n[i], sub4(ctx.U_n[i], ctx.U_nm1[i]));
      Vec4 Wp = cons_to_prim(pred, cs.gas);
      if (pred[0] <= RHO_FLOOR ||
          Wp[3] <= P_FLOOR * 10)  // extrapolation can overshoot; fall back
        pred = ctx.U_n[i];
      ctx.st.U[i] = pred;
    }

    // Initial inner residual at the predictor.
    ctx.eval_residual(o, fl);
    compute_F(c0, c1, c2);
    ResidualNorms n0 = ctx.global_norms_F(F);
    const double ref = std::max(n0.l2, 1e-30);

    int inner = 0;
    double ratio = 1.0;
    bool hit = false;
    for (inner = 1; inner <= rc.max_inner_iterations; ++inner) {
      for (int i = 0; i < ctx.mesh.n_owned; ++i) {
        double dtl = ctx.local_dt(i, rc.cfl_initial);  // CFL fixed near 1.0
        diag[i] = ctx.mesh.vol[i] / dtl + ctx.mesh.vol[i] * c0 +
                  0.5 * ctx.st.lambda_sum[i];
        rhs[i] = scale4(F[i], -1.0);
      }
      for (auto& d : dU) d = Vec4{};
      ctx.halo.exchange(ctx.mesh, &dU[0][0], 4);
      for (int sw = 0; sw < ctx.cfg.transient_sweeps_per_inner; ++sw)
        sgs_sweep_pair(ctx, diag, rhs, dU, true);
      apply_update(ctx, dU);
      ctx.eval_residual(o, fl);
      compute_F(c0, c1, c2);
      fnorms = ctx.global_norms_F(F);
      ratio = fnorms.l2 / ref;
      if (inner >= rc.min_inner_iterations &&
          ratio <= rc.inner_residual_reduction_target) {
        hit = true;
        break;
      }
    }
    if (inner > rc.max_inner_iterations) inner = rc.max_inner_iterations;
    hit = hit || (inner >= rc.min_inner_iterations &&
                  ratio <= rc.inner_residual_reduction_target);
    res.inner.add(inner, hit, ratio);

    // Accept: update physical-time histories (frozen during inner loop).
    for (int i = 0; i < ctx.mesh.n_owned; ++i) {
      ctx.U_nm1[i] = ctx.U_n[i];
      ctx.U_n[i] = ctx.st.U[i];
    }

    fc = ctx.reduce_forces(fl);
    write_residual_row(out, step, t, inner, rc.cfl_initial, dt, fnorms.per_eq,
                       fnorms.l2, fnorms.linf);
    write_force_row(out, step, t, fc.cl, fc.cd, fc.cmz, fc.pressure_drag,
                    fc.viscous_drag, fc.pressure_lift, fc.viscous_lift);
    late_cl.push_back(fc.cl);
    if (late_cl.size() > 6000) late_cl.pop_front();

    if (ctx.st.non_finite || !std::isfinite(fnorms.l2)) {
      failed = true;
      out.log("ERROR: non-finite transient residual; marking case failed");
      break;
    }
    if (n % 500 == 0) {
      char buf[256];
      std::snprintf(buf, sizeof(buf),
                    "step %ld  t %.2f  inner %d  ratio %.2e  cl %.4f  cd %.4f",
                    step, t, inner, ratio, fc.cl, fc.cd);
      out.log(buf);
    }
    // Intermediate field snapshots.
    if (cs.outputs.write_field_every_time > 0.0 &&
        t - last_field_time >= cs.outputs.write_field_every_time - 1e-12) {
      char fname[128];
      std::snprintf(fname, sizeof(fname), "field_t%08.2f.vtu", t);
      write_field_vtu(out, ctx.mesh, cs, ctx.st, fname);
      last_field_time = t;
    }
    // Rolling checkpoint for crash safety.
    if (n % 2000 == 0)
      write_restart(out, ctx.mesh, "restart_checkpoint.bin", step, t, ctx.st.U,
                    ctx.U_n, ctx.U_nm1, 3);
  }

  res.final_step = step;
  res.final_time = start_time + (step - start_step) * dt;
  res.last_forces = fc;
  res.inner.finalize();
  res.n_physical_steps = static_cast<int>(step - start_step);

  // Statistical periodicity: nonzero lift oscillation over the late window.
  bool periodic = false;
  if (!failed && late_cl.size() > 100) {
    size_t n = late_cl.size();
    size_t m = n / 2;
    double mn = 1e300, mx = -1e300;
    for (size_t i = m; i < n; ++i) {
      mn = std::min(mn, late_cl[i]);
      mx = std::max(mx, late_cl[i]);
    }
    periodic = (mx - mn) > 1e-3;
  }
  res.residual_reduction = res.inner.last_ratio > 0.0
                               ? -std::log10(std::max(res.inner.last_ratio, 1e-300))
                               : 0.0;
  if (failed) {
    res.status = "failed";
  } else if (periodic && res.inner.converged_fraction >= 0.95) {
    res.status = "statistically_periodic";
  } else if (periodic) {
    // Shedding present but inner target missed too often: honest failure.
    res.status = "failed";
    out.log("WARNING: shedding detected but inner convergence target missed "
            "too frequently");
  } else {
    res.status = "failed";
    out.log("WARNING: no significant lift oscillation in late window; "
            "vortex shedding not established");
  }
  return res;
}

}  // namespace cfd
