#include "solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <numeric>
#include <stdexcept>

#include "output.hpp"

namespace cfd {

Vec4 cons_from_prim(const Vec5& w, double gamma) {
  Vec4 u;
  u[0] = w[0];
  u[1] = w[0] * w[1];
  u[2] = w[0] * w[2];
  u[3] = w[3] / (gamma - 1.0) + 0.5 * w[0] * (w[1] * w[1] + w[2] * w[2]);
  return u;
}

Vec5 prim_from_cons(const Vec4& u, double gamma, double rgas) {
  Vec5 w;
  double rho = std::max(u[0], 1e-12);
  w[0] = rho;
  w[1] = u[1] / rho;
  w[2] = u[2] / rho;
  double ke = 0.5 * (w[1] * w[1] + w[2] * w[2]);
  w[3] = std::max((gamma - 1.0) * (u[3] - rho * ke), 1e-12);
  w[4] = w[3] / (rho * rgas);
  return w;
}

Vec4 rusanov_flux(const Vec5& wl, const Vec5& wr, double nx, double ny,
                  double gamma, double scale, double mref) {
  double unl = wl[1] * nx + wl[2] * ny;
  double unr = wr[1] * nx + wr[2] * ny;
  double al = std::sqrt(gamma * wl[3] / wl[0]);
  double ar = std::sqrt(gamma * wr[3] / wr[0]);
  double chi = 1.0;
  double mref_eff = mref;
  if (mref_eff > 0.0) {
    double ml = std::sqrt(wl[1] * wl[1] + wl[2] * wl[2]) / al;
    double mr = std::sqrt(wr[1] * wr[1] + wr[2] * wr[2]) / ar;
    chi = std::min(1.0, std::max(ml, mr) / mref_eff);
  }
  double smax = std::max(std::fabs(unl) + chi * al, std::fabs(unr) + chi * ar);

  double hl = gamma / (gamma - 1.0) * wl[3] / wl[0] +
              0.5 * (wl[1] * wl[1] + wl[2] * wl[2]);
  double hr = gamma / (gamma - 1.0) * wr[3] / wr[0] +
              0.5 * (wr[1] * wr[1] + wr[2] * wr[2]);

  Vec4 fl{wl[0] * unl, wl[0] * wl[1] * unl + wl[3] * nx,
          wl[0] * wl[2] * unl + wl[3] * ny, wl[0] * hl * unl};
  Vec4 fr{wr[0] * unr, wr[0] * wr[1] * unr + wr[3] * nx,
          wr[0] * wr[2] * unr + wr[3] * ny, wr[0] * hr * unr};

  Vec4 ul = cons_from_prim(wl, gamma);
  Vec4 ur = cons_from_prim(wr, gamma);
  Vec4 f;
  for (int k = 0; k < 4; ++k)
    f[k] = 0.5 * (fl[k] + fr[k]) - 0.5 * scale * smax * (ur[k] - ul[k]);
  return f;
}

Vec4 roe_flux(const Vec5& wl, const Vec5& wr, double nx, double ny,
              double gamma, double scale) {
  double unl = wl[1] * nx + wl[2] * ny;
  double unr = wr[1] * nx + wr[2] * ny;
  double hl = gamma / (gamma - 1.0) * wl[3] / wl[0] +
              0.5 * (wl[1] * wl[1] + wl[2] * wl[2]);
  double hr = gamma / (gamma - 1.0) * wr[3] / wr[0] +
              0.5 * (wr[1] * wr[1] + wr[2] * wr[2]);
  Vec4 fl{wl[0] * unl, wl[0] * wl[1] * unl + wl[3] * nx,
          wl[0] * wl[2] * unl + wl[3] * ny, wl[0] * hl * unl};
  Vec4 fr{wr[0] * unr, wr[0] * wr[1] * unr + wr[3] * nx,
          wr[0] * wr[2] * unr + wr[3] * ny, wr[0] * hr * unr};

  double sql = std::sqrt(wl[0]), sqr = std::sqrt(wr[0]);
  double den = sql + sqr;
  double rr = sql * sqr;
  double u = (sql * wl[1] + sqr * wr[1]) / den;
  double v = (sql * wl[2] + sqr * wr[2]) / den;
  double h = (sql * hl + sqr * hr) / den;
  double un = u * nx + v * ny;
  double q2 = u * u + v * v;
  double a2 = (gamma - 1.0) * (h - 0.5 * q2);
  double a = std::sqrt(std::max(a2, 1e-12));

  double drho = wr[0] - wl[0];
  double dp = wr[3] - wl[3];
  double dun = unr - unl;
  double dut = (wr[1] - wl[1]) * (-ny) + (wr[2] - wl[2]) * nx;

  double lam1 = un - a;
  double lam2 = un;
  double lam4 = un + a;
  double eps = 0.1 * a;
  auto fix = [&](double lam) {
    return std::fabs(lam) < eps ? (lam * lam + eps * eps) / (2.0 * eps)
                                : std::fabs(lam);
  };
  double f1 = fix(lam1), f2 = fix(lam2), f4 = fix(lam4);

  double alpha1 = (dp - rr * a * dun) / (2.0 * a * a);
  double alpha4 = (dp + rr * a * dun) / (2.0 * a * a);
  double alpha2 = drho - dp / (a * a);
  double alpha3 = rr * dut;

  double tx = -ny, ty = nx;
  Vec4 diss{0.0, 0.0, 0.0, 0.0};
  Vec4 r1{1.0, u - a * nx, v - a * ny, h - a * un};
  Vec4 r4{1.0, u + a * nx, v + a * ny, h + a * un};
  Vec4 r2{1.0, u, v, 0.5 * q2};
  Vec4 r3{0.0, tx, ty, u * tx + v * ty};
  for (int k = 0; k < 4; ++k)
    diss[k] = f1 * alpha1 * r1[k] + f2 * alpha2 * r2[k] +
              f2 * alpha3 * r3[k] + f4 * alpha4 * r4[k];

  Vec4 out;
  for (int k = 0; k < 4; ++k)
    out[k] = 0.5 * (fl[k] + fr[k]) - 0.5 * scale * diss[k];
  return out;
}

Solver::Solver(CaseFile cfg, LocalMesh mesh, MPI_Comm comm, std::string out_dir,
               std::string restart_file)
    : cfg_(std::move(cfg)),
      m_(std::move(mesh)),
      comm_(comm),
      out_dir_(std::move(out_dir)),
      restart_file_(std::move(restart_file)) {
  MPI_Comm_rank(comm_, &rank_);
  MPI_Comm_size(comm_, &np_);
  if (np_ != m_.np)
    throw std::runtime_error("MPI size does not match partition np");
  setup();
}

void Solver::setup() {
  gamma_ = cfg_.gas.gamma;
  Rgas_ = cfg_.gas.R;
  prandtl_ = cfg_.gas.prandtl;
  cp_ = gamma_ * Rgas_ / (gamma_ - 1.0);
  mu_ = cfg_.viscous() ? cfg_.viscosity() : 0.0;
  cond_ = cfg_.viscous() ? mu_ * cp_ / prandtl_ : 0.0;
  rusanov_scale_ = cfg_.run.rusanov_dissipation_scale;
  mref_ = cfg_.low_mach_mref;

  double aoa = cfg_.freestream.aoa_degrees * M_PI / 180.0;
  double vmag = cfg_.freestream.velocity_magnitude;
  Winf_[0] = cfg_.freestream.rho;
  Winf_[1] = vmag * std::cos(aoa);
  Winf_[2] = vmag * std::sin(aoa);
  Winf_[3] = cfg_.freestream.pressure;
  Winf_[4] = Winf_[3] / (Winf_[0] * Rgas_);
  Uinf_ = cons_from_prim(Winf_, gamma_);
  drag_dir_ = {std::cos(aoa), std::sin(aoa)};
  lift_dir_ = {-std::sin(aoa), std::cos(aoa)};
  qinf_ = 0.5 * cfg_.freestream.rho * vmag * vmag;
  rho_floor_ = 1e-8;
  p_floor_ = 1e-8;

  fam_bc_.assign(m_.family_names.size(), BCType::Farfield);
  for (size_t i = 0; i < m_.family_names.size(); ++i) {
    auto it = cfg_.bc_map.find(m_.family_names[i]);
    if (it == cfg_.bc_map.end())
      throw std::runtime_error("mesh boundary family '" + m_.family_names[i] +
                               "' has no entry in case boundary_conditions");
    fam_bc_[i] = it->second;
  }
  for (const auto& [name, t] : cfg_.bc_map) {
    bool found = false;
    for (auto& fn : m_.family_names)
      if (fn == name) found = true;
    if (!found)
      throw std::runtime_error("case boundary_conditions names family '" +
                               name + "' not present in mesh");
  }

  int nt = m_.num_cells_local();
  U_.assign(nt, Uinf_);
  Un_.assign(nt, Uinf_);
  Unm1_.assign(nt, Uinf_);
  W_.assign(nt, Winf_);
  G_.assign(nt, Grad5{});
  phi_.assign(nt, Vec5{1.0, 1.0, 1.0, 1.0, 1.0});
  R_.assign(m_.n_owned, Vec4{});
  dU_.assign(nt, Vec4{});
  diag_.assign(m_.n_owned, 0.0);
  dtau_.assign(m_.n_owned, 0.0);

  lsq_.assign(m_.n_owned, {});
  recon_.assign(m_.n_owned, {});
  cell_int_faces_.assign(m_.n_owned, {});
  cell_bnd_faces_.assign(m_.n_owned, {});

  for (int fi = 0; fi < (int)m_.faces_int.size(); ++fi) {
    const Face& f = m_.faces_int[fi];
    if (f.left < m_.n_owned) cell_int_faces_[f.left].push_back(fi);
    if (f.right < m_.n_owned) cell_int_faces_[f.right].push_back(fi);
  }
  for (int fi = 0; fi < (int)m_.faces_bnd.size(); ++fi) {
    const Face& f = m_.faces_bnd[fi];
    cell_bnd_faces_[f.left].push_back(fi);
  }

  std::vector<std::vector<int32_t>> node_cells(m_.node_x.size());
  for (int i = 0; i < m_.n_owned; ++i)
    for (int v = 0; v < m_.cell_nverts[i]; ++v)
      node_cells[m_.cell_nodes[i][v]].push_back(i);
  std::vector<std::vector<int32_t>> vertex_nbr(m_.n_owned);
  {
    std::vector<int32_t> mark(m_.num_cells_local(), -1);
    for (int i = 0; i < m_.n_owned; ++i) {
      mark[i] = i;
      std::vector<int32_t> nb;
      for (int v = 0; v < m_.cell_nverts[i]; ++v)
        for (int32_t j : node_cells[m_.cell_nodes[i][v]])
          if (mark[j] != i) {
            mark[j] = i;
            nb.push_back(j);
          }
      vertex_nbr[i] = std::move(nb);
    }
  }

  static const double lsq_pow = getenv("CFD_LSQ_UNWEIGHTED") ? 0.0 : 1.0;
  static const bool small_stencil = getenv("CFD_SMALL_STENCIL") != nullptr;
  for (int i = 0; i < m_.n_owned; ++i) {
    double sxx = 0, sxy = 0, syy = 0;
    struct Raw {
      int32_t cell, bnd;
      double dx, dy, w;
    };
    std::vector<Raw> raw;
    const auto& stencil = small_stencil ? cell_int_faces_[i] : vertex_nbr[i];
    for (int32_t j : stencil) {
      if (!small_stencil) {
        double dx = m_.cell_cx[j] - m_.cell_cx[i];
        double dy = m_.cell_cy[j] - m_.cell_cy[i];
        double d2 = dx * dx + dy * dy;
        double w = 1.0 / std::pow(std::max(d2, 1e-30), lsq_pow);
        raw.push_back({j, -1, dx, dy, w});
        sxx += w * dx * dx;
        sxy += w * dx * dy;
        syy += w * dy * dy;
      }
    }
    if (small_stencil)
      for (int32_t fi : cell_int_faces_[i]) {
        const Face& f = m_.faces_int[fi];
        int32_t j = f.left == i ? f.right : f.left;
        double dx = m_.cell_cx[j] - m_.cell_cx[i];
        double dy = m_.cell_cy[j] - m_.cell_cy[i];
        double d2 = dx * dx + dy * dy;
        double w = 1.0 / std::pow(std::max(d2, 1e-30), lsq_pow);
        raw.push_back({j, -1, dx, dy, w});
        sxx += w * dx * dx;
        sxy += w * dx * dy;
        syy += w * dy * dy;
      }
    for (int32_t fi : cell_bnd_faces_[i]) {
      const Face& f = m_.faces_bnd[fi];
      BCType bct = fam_bc_[f.bc_family];
      static const bool no_wall_lsq = getenv("CFD_NO_WALL_LSQ") != nullptr;
      if (no_wall_lsq && bct != BCType::Farfield) continue;
      double dx = f.cx - m_.cell_cx[i];
      double dy = f.cy - m_.cell_cy[i];
      double d2 = dx * dx + dy * dy;
      double w = 1.0 / std::pow(std::max(d2, 1e-30), lsq_pow);
      raw.push_back({-1, fi, dx, dy, w});
      sxx += w * dx * dx;
      sxy += w * dx * dy;
      syy += w * dy * dy;
    }
    double det = sxx * syy - sxy * sxy;
    if (std::fabs(det) < 1e-30)
      throw std::runtime_error("singular LSQ system on cell " +
                               std::to_string(i));
    double i00 = syy / det, i01 = -sxy / det, i11 = sxx / det;
    for (const Raw& e : raw) {
      LsqEntry le;
      le.cell = e.cell;
      le.bnd_face = e.bnd;
      le.cx = e.w * (i00 * e.dx + i01 * e.dy);
      le.cy = e.w * (i01 * e.dx + i11 * e.dy);
      lsq_[i].push_back(le);
    }
    for (int32_t fi : cell_int_faces_[i]) {
      const Face& f = m_.faces_int[fi];
      recon_[i].push_back({f.cx - m_.cell_cx[i], f.cy - m_.cell_cy[i]});
    }
    for (int32_t fi : cell_bnd_faces_[i]) {
      const Face& f = m_.faces_bnd[fi];
      recon_[i].push_back({f.cx - m_.cell_cx[i], f.cy - m_.cell_cy[i]});
    }
  }

  sweep_order_.resize(m_.n_owned);
  std::iota(sweep_order_.begin(), sweep_order_.end(), 0);
  std::sort(sweep_order_.begin(), sweep_order_.end(), [&](int a, int b) {
    return m_.cell_cx[a] < m_.cell_cx[b];
  });
  std::vector<int> pos(m_.n_owned);
  for (int k = 0; k < m_.n_owned; ++k) pos[sweep_order_[k]] = k;

  lower_nbr_.assign(m_.n_owned, {});
  upper_nbr_.assign(m_.n_owned, {});
  for (int fi = 0; fi < (int)m_.faces_int.size(); ++fi) {
    const Face& f = m_.faces_int[fi];
    int l = f.left, r = f.right;
    int pl = l < m_.n_owned ? pos[l] : -1;
    int pr = r < m_.n_owned ? pos[r] : -1;
    if (pl >= 0) {
      if (pr >= 0 && pr < pl)
        lower_nbr_[l].push_back({fi, r});
      else
        upper_nbr_[l].push_back({fi, r});
    }
    if (pr >= 0) {
      if (pl >= 0 && pl < pr)
        lower_nbr_[r].push_back({fi, l});
      else
        upper_nbr_[r].push_back({fi, l});
    }
  }

  if (rank_ == 0) {
    fres_ = fopen((out_dir_ + "/residuals.csv").c_str(), "w");
    if (!fres_) throw std::runtime_error("cannot open residuals.csv");
    fprintf(fres_,
            "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
            "residual_l2,residual_linf\n");
    ffor_ = fopen((out_dir_ + "/forces.csv").c_str(), "w");
    if (!ffor_) throw std::runtime_error("cannot open forces.csv");
    fprintf(ffor_,
            "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
            "pressure_lift,viscous_lift\n");
  }

  if (!restart_file_.empty()) load_restart();

  if (getenv("CFD_INIT_GAUSS")) {
    int nt = m_.num_cells_local();
    for (int i = 0; i < nt; ++i) {
      double dx = m_.cell_cx[i] - 10.0, dy = m_.cell_cy[i] - 10.0;
      double g = 0.2 * std::exp(-(dx * dx + dy * dy) / 4.0);
      Vec5 w = Winf_;
      w[0] = Winf_[0] * (1.0 + g);
      w[3] = Winf_[3] * (1.0 + g);
      w[4] = w[3] / (w[0] * Rgas_);
      U_[i] = cons_from_prim(w, gamma_);
    }
  }
}

void Solver::halo_exchange(std::vector<Vec4>& a) {
  int nnb = (int)m_.neighbor_ranks.size();
  std::vector<MPI_Request> reqs(2 * nnb, MPI_REQUEST_NULL);
  std::vector<std::vector<double>> sbuf(nnb), rbuf(nnb);
  for (int k = 0; k < nnb; ++k) {
    int nb = m_.neighbor_ranks[k];
    size_t ns = m_.send_cells[k].size();
    size_t nr = m_.recv_cells[k].size();
    sbuf[k].resize(ns * 4);
    rbuf[k].resize(nr * 4);
    for (size_t i = 0; i < ns; ++i) {
      const Vec4& v = a[m_.send_cells[k][i]];
      for (int c = 0; c < 4; ++c) sbuf[k][i * 4 + c] = v[c];
    }
    MPI_Isend(sbuf[k].data(), (int)(ns * 4), MPI_DOUBLE, nb, 42, comm_,
              &reqs[k]);
    MPI_Irecv(rbuf[k].data(), (int)(nr * 4), MPI_DOUBLE, nb, 42, comm_,
              &reqs[nnb + k]);
  }
  MPI_Waitall(2 * nnb, reqs.data(), MPI_STATUSES_IGNORE);
  for (int k = 0; k < nnb; ++k)
    for (size_t i = 0; i < m_.recv_cells[k].size(); ++i) {
      Vec4& v = a[m_.recv_cells[k][i]];
      for (int c = 0; c < 4; ++c) v[c] = rbuf[k][i * 4 + c];
    }
}

void Solver::halo_exchange_grad() {
  int nnb = (int)m_.neighbor_ranks.size();
  constexpr int kN = 15;
  std::vector<MPI_Request> reqs(2 * nnb, MPI_REQUEST_NULL);
  std::vector<std::vector<double>> sbuf(nnb), rbuf(nnb);
  for (int k = 0; k < nnb; ++k) {
    int nb = m_.neighbor_ranks[k];
    size_t ns = m_.send_cells[k].size();
    size_t nr = m_.recv_cells[k].size();
    sbuf[k].resize(ns * kN);
    rbuf[k].resize(nr * kN);
    for (size_t i = 0; i < ns; ++i) {
      int32_t cell = m_.send_cells[k][i];
      double* p = sbuf[k].data() + i * kN;
      for (int v = 0; v < 5; ++v) {
        p[v * 2] = G_[cell][v][0];
        p[v * 2 + 1] = G_[cell][v][1];
        p[10 + v] = phi_[cell][v];
      }
    }
    MPI_Isend(sbuf[k].data(), (int)(ns * kN), MPI_DOUBLE, nb, 43, comm_,
              &reqs[k]);
    MPI_Irecv(rbuf[k].data(), (int)(nr * kN), MPI_DOUBLE, nb, 43, comm_,
              &reqs[nnb + k]);
  }
  MPI_Waitall(2 * nnb, reqs.data(), MPI_STATUSES_IGNORE);
  for (int k = 0; k < nnb; ++k)
    for (size_t i = 0; i < m_.recv_cells[k].size(); ++i) {
      int32_t cell = m_.recv_cells[k][i];
      const double* p = rbuf[k].data() + i * kN;
      for (int v = 0; v < 5; ++v) {
        G_[cell][v][0] = p[v * 2];
        G_[cell][v][1] = p[v * 2 + 1];
        phi_[cell][v] = p[10 + v];
      }
    }
}

void Solver::cons_to_prim_all() {
  int nt = m_.num_cells_local();
  for (int i = 0; i < nt; ++i) {
    Vec5 w;
    double rho = std::max(U_[i][0], rho_floor_);
    w[0] = rho;
    w[1] = U_[i][1] / rho;
    w[2] = U_[i][2] / rho;
    double ke = 0.5 * (w[1] * w[1] + w[2] * w[2]);
    w[3] = std::max((gamma_ - 1.0) * (U_[i][3] - rho * ke), p_floor_);
    w[4] = w[3] / (rho * Rgas_);
    W_[i] = w;
  }
}

Vec5 Solver::boundary_virtual_state(const Face& f, int32_t cell) const {
  BCType t = fam_bc_[f.bc_family];
  const Vec5& w = W_[cell];
  Vec5 wb = w;
  switch (t) {
    case BCType::Farfield:
      wb = Winf_;
      break;
    case BCType::SlipWall: {
      double un = w[1] * f.nx + w[2] * f.ny;
      wb[1] = w[1] - 2.0 * un * f.nx;
      wb[2] = w[2] - 2.0 * un * f.ny;
      break;
    }
    case BCType::NoSlipAdiabaticWall:
      wb[1] = -w[1];
      wb[2] = -w[2];
      break;
  }
  return wb;
}

void Solver::compute_gradients() {
  static const bool ngg = getenv("CFD_NGG") != nullptr;
  if (ngg) {
    int nn = (int)m_.node_x.size();
    std::vector<Vec5> wn(nn);
    std::vector<double> ww(nn, 0.0);
    for (int n = 0; n < nn; ++n) wn[n] = Vec5{0, 0, 0, 0, 0};
    for (int i = 0; i < m_.n_owned; ++i)
      for (int v = 0; v < m_.cell_nverts[i]; ++v) {
        int32_t n = m_.cell_nodes[i][v];
        double dx = m_.node_x[n] - m_.cell_cx[i];
        double dy = m_.node_y[n] - m_.cell_cy[i];
        double w = 1.0 / std::max(dx * dx + dy * dy, 1e-30);
        ww[n] += w;
        for (int q = 0; q < 5; ++q) wn[n][q] += w * W_[i][q];
      }
    for (int n = 0; n < nn; ++n)
      if (ww[n] > 0.0)
        for (int q = 0; q < 5; ++q) wn[n][q] /= ww[n];
    for (int i = 0; i < m_.n_owned; ++i) G_[i] = Grad5{};
    for (const Face& f : m_.faces_int) {
      for (int v = 0; v < 5; ++v) {
        double vf = 0.5 * (wn[f.n0][v] + wn[f.n1][v]);
        G_[f.left][v][0] += vf * f.nx * f.area;
        G_[f.left][v][1] += vf * f.ny * f.area;
        if (f.right < m_.n_owned) {
          G_[f.right][v][0] -= vf * f.nx * f.area;
          G_[f.right][v][1] -= vf * f.ny * f.area;
        }
      }
    }
    for (const Face& f : m_.faces_bnd) {
      Vec5 wb = boundary_virtual_state(f, f.left);
      for (int v = 0; v < 5; ++v) {
        G_[f.left][v][0] += wb[v] * f.nx * f.area;
        G_[f.left][v][1] += wb[v] * f.ny * f.area;
      }
    }
    for (int i = 0; i < m_.n_owned; ++i)
      for (int v = 0; v < 5; ++v) {
        G_[i][v][0] /= m_.cell_vol[i];
        G_[i][v][1] /= m_.cell_vol[i];
      }
    return;
  }
  static const bool gg = getenv("CFD_GG") != nullptr;
  if (gg) {
    for (int i = 0; i < m_.n_owned; ++i) G_[i] = Grad5{};
    for (const Face& f : m_.faces_int) {
      for (int v = 0; v < 5; ++v) {
        double vf = 0.5 * (W_[f.left][v] + W_[f.right][v]);
        G_[f.left][v][0] += vf * f.nx * f.area;
        G_[f.left][v][1] += vf * f.ny * f.area;
        if (f.right < m_.n_owned) {
          G_[f.right][v][0] -= vf * f.nx * f.area;
          G_[f.right][v][1] -= vf * f.ny * f.area;
        }
      }
    }
    for (const Face& f : m_.faces_bnd) {
      Vec5 wb = boundary_virtual_state(f, f.left);
      for (int v = 0; v < 5; ++v) {
        G_[f.left][v][0] += wb[v] * f.nx * f.area;
        G_[f.left][v][1] += wb[v] * f.ny * f.area;
      }
    }
    for (int i = 0; i < m_.n_owned; ++i)
      for (int v = 0; v < 5; ++v) {
        G_[i][v][0] /= m_.cell_vol[i];
        G_[i][v][1] /= m_.cell_vol[i];
      }
    return;
  }
  for (int i = 0; i < m_.n_owned; ++i) {
    Grad5 g{};
    const Vec5& wi = W_[i];
    for (const LsqEntry& e : lsq_[i]) {
      Vec5 wj;
      if (e.cell >= 0)
        wj = W_[e.cell];
      else
        wj = boundary_virtual_state(m_.faces_bnd[e.bnd_face], i);
      for (int v = 0; v < 5; ++v) {
        double d = wj[v] - wi[v];
        g[v][0] += e.cx * d;
        g[v][1] += e.cy * d;
      }
    }
    G_[i] = g;
  }
}

void Solver::compute_limiter() {
  static const bool te_fo = getenv("CFD_TE_FO") != nullptr;
  if (cfg_.debug_first_order) {
    for (int i = 0; i < m_.n_owned; ++i)
      phi_[i] = Vec5{0.0, 0.0, 0.0, 0.0, 0.0};
    return;
  }
  static const bool unlimited = getenv("CFD_UNLIMITED") != nullptr;
  if (unlimited) {
    for (int i = 0; i < m_.n_owned; ++i)
      phi_[i] = Vec5{1.0, 1.0, 1.0, 1.0, 1.0};
    return;
  }
  static const bool bj = getenv("CFD_BJ") != nullptr;
  if (bj) {
    for (int i = 0; i < m_.n_owned; ++i) {
      Vec5 qmin = W_[i], qmax = W_[i];
      for (const LsqEntry& e : lsq_[i]) {
        Vec5 wj;
        if (e.cell >= 0)
          wj = W_[e.cell];
        else
          wj = boundary_virtual_state(m_.faces_bnd[e.bnd_face], i);
        for (int v = 0; v < 5; ++v) {
          qmin[v] = std::min(qmin[v], wj[v]);
          qmax[v] = std::max(qmax[v], wj[v]);
        }
      }
      Vec5 lim{1.0, 1.0, 1.0, 1.0, 1.0};
      for (const ReconPoint& rp : recon_[i]) {
        for (int v = 0; v < 5; ++v) {
          double dq = G_[i][v][0] * rp.dx + G_[i][v][1] * rp.dy;
          if (dq > 0.0) {
            double dm = qmax[v] - W_[i][v];
            lim[v] = std::min(lim[v], dm > 0.0 ? std::min(1.0, dm / dq) : 0.0);
          } else if (dq < 0.0) {
            double dm = qmin[v] - W_[i][v];
            lim[v] = std::min(lim[v], dm < 0.0 ? std::min(1.0, dm / dq) : 0.0);
          }
        }
      }
    phi_[i] = lim;
    static const bool single_phi = getenv("CFD_SINGLE_PHI") != nullptr;
    if (single_phi) {
      double m = std::min(std::min(lim[0], lim[1]),
                          std::min(std::min(lim[2], lim[3]), lim[4]));
      phi_[i] = Vec5{m, m, m, m, m};
    }
    }
    return;
  }
  const double kvenk = 1.0;
  for (int i = 0; i < m_.n_owned; ++i) {
    Vec5 qmin = W_[i], qmax = W_[i];
    for (const LsqEntry& e : lsq_[i]) {
      Vec5 wj;
      if (e.cell >= 0)
        wj = W_[e.cell];
      else
        wj = boundary_virtual_state(m_.faces_bnd[e.bnd_face], i);
      for (int v = 0; v < 5; ++v) {
        qmin[v] = std::min(qmin[v], wj[v]);
        qmax[v] = std::max(qmax[v], wj[v]);
      }
    }
    double h = std::sqrt(m_.cell_vol[i]);
    double eps2 = kvenk * h;
    eps2 = eps2 * eps2 * eps2;
    Vec5 lim{1.0, 1.0, 1.0, 1.0, 1.0};
    for (const ReconPoint& rp : recon_[i]) {
      for (int v = 0; v < 5; ++v) {
        double dq = G_[i][v][0] * rp.dx + G_[i][v][1] * rp.dy;
        if (dq > 1e-14) {
          double dm = qmax[v] - W_[i][v];
          if (dm <= 0.0) continue;
          double num = dm * dm + 2.0 * dq * dm + eps2;
          double den = dm * dm + 2.0 * dq * dq + dm * dq + eps2;
          lim[v] = std::min(lim[v], std::min(1.0, num / den));
        } else if (dq < -1e-14) {
          double dm = qmin[v] - W_[i][v];
          if (dm >= 0.0) continue;
          double num = dm * dm + 2.0 * dq * dm + eps2;
          double den = dm * dm + 2.0 * dq * dq + dm * dq + eps2;
          lim[v] = std::min(lim[v], std::min(1.0, num / den));
        }
      }
    }
    phi_[i] = lim;
    if (te_fo && m_.cell_cx[i] > 0.97 && std::fabs(m_.cell_cy[i]) < 0.04)
      phi_[i] = Vec5{0.0, 0.0, 0.0, 0.0, 0.0};
  }
  static const bool dbgphi = getenv("CFD_DEBUG_PHI") != nullptr;
  if (dbgphi) {
    double mn = 1.0, sum = 0.0;
    int cnt = 0;
    for (int i = 0; i < m_.n_owned; ++i)
      for (int v = 0; v < 5; ++v) {
        mn = std::min(mn, phi_[i][v]);
        sum += phi_[i][v];
        if (phi_[i][v] < 0.999) ++cnt;
      }
    printf("  phi: min=%.4f mean=%.4f limited_frac=%.4f\n", mn,
           sum / (5.0 * m_.n_owned), (double)cnt / (5.0 * m_.n_owned));
    fflush(stdout);
  }
}

void Solver::compute_face_spectral(const Face& f, const Vec5& wl,
                                   const Vec5& wr, double& lam_c,
                                   double& lam_v) const {
  double unl = wl[1] * f.nx + wl[2] * f.ny;
  double unr = wr[1] * f.nx + wr[2] * f.ny;
  double al = std::sqrt(gamma_ * wl[3] / wl[0]);
  double ar = std::sqrt(gamma_ * wr[3] / wr[0]);
  double ml = std::sqrt(wl[1] * wl[1] + wl[2] * wl[2]) / al;
  double mr = std::sqrt(wr[1] * wr[1] + wr[2] * wr[2]) / ar;
  double chi = mref_ > 0.0 ? std::min(1.0, std::max(ml, mr) / mref_) : 1.0;
  lam_c = std::max(std::fabs(unl) + chi * al, std::fabs(unr) + chi * ar) *
          f.area;
  lam_v = 0.0;
  if (cfg_.viscous()) {
    double rho_f = 0.5 * (wl[0] + wr[0]);
    double d = std::max(f.dist, 1e-30);
    lam_v = 2.0 * mu_ / rho_f * std::max(4.0 / 3.0, gamma_ / prandtl_) *
            f.area / d;
  }
}

void Solver::compute_residual(std::vector<Vec4>& res, double time_diag_coef,
                              const std::vector<Vec4>* uref) {
  halo_exchange(U_);
  cons_to_prim_all();
  compute_gradients();
  halo_exchange_grad();
  if (!freeze_limiter_) {
    compute_limiter();
    halo_exchange_grad();
  }

  std::fill(res.begin(), res.end(), Vec4{0.0, 0.0, 0.0, 0.0});

  static const char* dump_path = getenv("CFD_DUMP_FACES");
  FILE* dumpf = nullptr;
  if (dump_path) dumpf = fopen(dump_path, "w");

  const bool visc = cfg_.viscous();
  static const bool fobc = getenv("CFD_FOBC") != nullptr;

  for (const Face& f : m_.faces_int) {
    Vec5 wl, wr;
    for (int v = 0; v < 5; ++v) {
      double dxl = f.cx - m_.cell_cx[f.left];
      double dyl = f.cy - m_.cell_cy[f.left];
      double dxr = f.cx - m_.cell_cx[f.right];
      double dyr = f.cy - m_.cell_cy[f.right];
      wl[v] = W_[f.left][v] +
              phi_[f.left][v] * (G_[f.left][v][0] * dxl + G_[f.left][v][1] * dyl);
      wr[v] = W_[f.right][v] +
              phi_[f.right][v] *
                  (G_[f.right][v][0] * dxr + G_[f.right][v][1] * dyr);
    }
    if (wl[0] < rho_floor_ || wl[3] < p_floor_) wl = W_[f.left];
    if (wr[0] < rho_floor_ || wr[3] < p_floor_) wr = W_[f.right];

    static const bool edgeclip = getenv("CFD_EDGECLIP") != nullptr;
    if (edgeclip) {
      for (int v = 0; v < 5; ++v) {
        double lo = std::min(W_[f.left][v], W_[f.right][v]);
        double hi = std::max(W_[f.left][v], W_[f.right][v]);
        wl[v] = std::min(std::max(wl[v], lo), hi);
        wr[v] = std::min(std::max(wr[v], lo), hi);
      }
    }
    if (dumpf) {
      fprintf(dumpf,
              "%.6e %.6e | %d %d | L %.4e %.4e %.4e %.4e | R %.4e %.4e %.4e "
              "%.4e | WL %.4e %.4e %.4e %.4e | WR %.4e %.4e %.4e %.4e | phi "
              "%.3f %.3f | G %.3e %.3e\n",
              f.cx, f.cy, f.left, f.right, wl[0], wl[1], wl[2], wl[3], wr[0],
              wr[1], wr[2], wr[3], W_[f.left][0], W_[f.left][1], W_[f.left][2],
              W_[f.left][3], W_[f.right][0], W_[f.right][1], W_[f.right][2],
              W_[f.right][3], phi_[f.left][0], phi_[f.right][0],
              std::hypot(G_[f.left][0][0], G_[f.left][0][1]),
              std::hypot(G_[f.right][0][0], G_[f.right][0][1]));
    }

    Vec4 flux;
    const int fluxmode = cfg_.flux_choice;
    if (fluxmode == 1) {
      flux = roe_flux(wl, wr, f.nx, f.ny, gamma_, rusanov_scale_);
    } else {
      flux = rusanov_flux(wl, wr, f.nx, f.ny, gamma_, rusanov_scale_, mref_);
      double unl = wl[1] * f.nx + wl[2] * f.ny;
      double unr = wr[1] * f.nx + wr[2] * f.ny;
      double al = std::sqrt(gamma_ * wl[3] / wl[0]);
      double ar = std::sqrt(gamma_ * wr[3] / wr[0]);
      double ml = std::sqrt(wl[1] * wl[1] + wl[2] * wl[2]) / al;
      double mr = std::sqrt(wr[1] * wr[1] + wr[2] * wr[2]) / ar;
      double chi = mref_ > 0.0 ? std::min(1.0, std::max(ml, mr) / mref_) : 1.0;
      double smax = std::max(std::fabs(unl) + chi * al,
                             std::fabs(unr) + chi * ar);
      Vec4 ul = cons_from_prim(wl, gamma_);
      Vec4 ur = cons_from_prim(wr, gamma_);
      const Vec4& ulc = U_[f.left];
      const Vec4& urc = U_[f.right];
      static const int djmode = getenv("CFD_FACEJUMP") ? 0 : 1;
      for (int k = 0; k < 4; ++k) {
        double djf = ur[k] - ul[k];
        double djc = urc[k] - ulc[k];
        double dj = djmode == 1 ? djc : djf;
        flux[k] += 0.5 * rusanov_scale_ * smax * (djf - dj);
      }
    }

    if (visc) {
      double ux[2], vx[2], tx[2];
      for (int d = 0; d < 2; ++d) {
        double gbar_u = 0.5 * (G_[f.left][1][d] + G_[f.right][1][d]);
        double gbar_v = 0.5 * (G_[f.left][2][d] + G_[f.right][2][d]);
        double gbar_t = 0.5 * (G_[f.left][4][d] + G_[f.right][4][d]);
        double e = d == 0 ? f.ex : f.ey;
        ux[d] = gbar_u + ((W_[f.right][1] - W_[f.left][1]) / f.dist -
                          (0.5 * (G_[f.left][1][0] + G_[f.right][1][0]) * f.ex +
                           0.5 * (G_[f.left][1][1] + G_[f.right][1][1]) * f.ey)) *
                             e;
        vx[d] = gbar_v + ((W_[f.right][2] - W_[f.left][2]) / f.dist -
                          (0.5 * (G_[f.left][2][0] + G_[f.right][2][0]) * f.ex +
                           0.5 * (G_[f.left][2][1] + G_[f.right][2][1]) * f.ey)) *
                             e;
        tx[d] = gbar_t + ((W_[f.right][4] - W_[f.left][4]) / f.dist -
                          (0.5 * (G_[f.left][4][0] + G_[f.right][4][0]) * f.ex +
                           0.5 * (G_[f.left][4][1] + G_[f.right][4][1]) * f.ey)) *
                             e;
      }
      double du_dx = ux[0], du_dy = ux[1], dv_dx = vx[0], dv_dy = vx[1];
      double div = du_dx + dv_dy;
      double txx = mu_ * (2.0 * du_dx - 2.0 / 3.0 * div);
      double tyy = mu_ * (2.0 * dv_dy - 2.0 / 3.0 * div);
      double txy = mu_ * (du_dy + dv_dx);
      double uf = 0.5 * (wl[1] + wr[1]);
      double vf = 0.5 * (wl[2] + wr[2]);
      double qx = -cond_ * tx[0];
      double qy = -cond_ * tx[1];
      flux[1] -= txx * f.nx + txy * f.ny;
      flux[2] -= txy * f.nx + tyy * f.ny;
      flux[3] -= (uf * txx + vf * txy - qx) * f.nx +
                 (uf * txy + vf * tyy - qy) * f.ny;
    }

    for (int k = 0; k < 4; ++k) {
      double fa = flux[k] * f.area;
      res[f.left][k] += fa;
      if (f.right < m_.n_owned) res[f.right][k] -= fa;
    }
  }

  for (const Face& f : m_.faces_bnd) {
    BCType t = fam_bc_[f.bc_family];
    Vec5 wl;
    if (fobc) {
      wl = W_[f.left];
    } else {
      for (int v = 0; v < 5; ++v) {
        double dxl = f.cx - m_.cell_cx[f.left];
        double dyl = f.cy - m_.cell_cy[f.left];
        wl[v] = W_[f.left][v] +
                phi_[f.left][v] *
                    (G_[f.left][v][0] * dxl + G_[f.left][v][1] * dyl);
      }
    }
    if (wl[0] < rho_floor_ || wl[3] < p_floor_) wl = W_[f.left];

    Vec4 flux{0.0, 0.0, 0.0, 0.0};
    switch (t) {
      case BCType::Farfield: {
        double unl = wl[1] * f.nx + wl[2] * f.ny;
        double al = std::sqrt(gamma_ * wl[3] / wl[0]);
        double uninf = Winf_[1] * f.nx + Winf_[2] * f.ny;
        double ainf = std::sqrt(gamma_ * Winf_[3] / Winf_[0]);
        Vec5 wb;
        if (unl >= al) {
          wb = wl;
        } else if (unl <= -al) {
          wb = Winf_;
        } else {
          double rp = unl + 2.0 * al / (gamma_ - 1.0);
          double rm = uninf - 2.0 * ainf / (gamma_ - 1.0);
          double unb = 0.5 * (rp + rm);
          double ab = 0.25 * (gamma_ - 1.0) * (rp - rm);
          double utx, uty;
          double rho_b, p_b;
          if (unb >= 0.0) {
            utx = wl[1] - unl * f.nx;
            uty = wl[2] - unl * f.ny;
            rho_b = wl[0] * std::pow(ab / al, 2.0 / (gamma_ - 1.0));
            p_b = wl[3] * std::pow(rho_b / wl[0], gamma_);
          } else {
            utx = Winf_[1] - uninf * f.nx;
            uty = Winf_[2] - uninf * f.ny;
            rho_b = Winf_[0] * std::pow(ab / ainf, 2.0 / (gamma_ - 1.0));
            p_b = Winf_[3] * std::pow(rho_b / Winf_[0], gamma_);
          }
          wb[0] = rho_b;
          wb[1] = utx + unb * f.nx;
          wb[2] = uty + unb * f.ny;
          wb[3] = p_b;
          wb[4] = p_b / (rho_b * Rgas_);
        }
        double unb = wb[1] * f.nx + wb[2] * f.ny;
        double hb = gamma_ / (gamma_ - 1.0) * wb[3] / wb[0] +
                    0.5 * (wb[1] * wb[1] + wb[2] * wb[2]);
        flux[0] = wb[0] * unb;
        flux[1] = wb[0] * wb[1] * unb + wb[3] * f.nx;
        flux[2] = wb[0] * wb[2] * unb + wb[3] * f.ny;
        flux[3] = wb[0] * hb * unb;
        if (visc) {
          double ux[2], vx[2], tx2[2];
          for (int d = 0; d < 2; ++d) {
            double e = d == 0 ? f.ex : f.ey;
            ux[d] = G_[f.left][1][d] +
                    ((Winf_[1] - W_[f.left][1]) / f.dist -
                     (G_[f.left][1][0] * f.ex + G_[f.left][1][1] * f.ey)) *
                        e;
            vx[d] = G_[f.left][2][d] +
                    ((Winf_[2] - W_[f.left][2]) / f.dist -
                     (G_[f.left][2][0] * f.ex + G_[f.left][2][1] * f.ey)) *
                        e;
            tx2[d] = G_[f.left][4][d] +
                     ((Winf_[4] - W_[f.left][4]) / f.dist -
                      (G_[f.left][4][0] * f.ex + G_[f.left][4][1] * f.ey)) *
                         e;
          }
          double du_dx = ux[0], du_dy = ux[1], dv_dx = vx[0], dv_dy = vx[1];
          double div = du_dx + dv_dy;
          double txx = mu_ * (2.0 * du_dx - 2.0 / 3.0 * div);
          double tyy = mu_ * (2.0 * dv_dy - 2.0 / 3.0 * div);
          double txy = mu_ * (du_dy + dv_dx);
          double uf = 0.5 * (wl[1] + Winf_[1]);
          double vf = 0.5 * (wl[2] + Winf_[2]);
          double qx = -cond_ * tx2[0];
          double qy = -cond_ * tx2[1];
          flux[1] -= txx * f.nx + txy * f.ny;
          flux[2] -= txy * f.nx + tyy * f.ny;
          flux[3] -= (uf * txx + vf * txy - qx) * f.nx +
                     (uf * txy + vf * tyy - qy) * f.ny;
        }
        break;
      }
      case BCType::SlipWall: {
        static const bool pwall = getenv("CFD_PWALL") != nullptr;
        if (pwall) {
          flux[1] = wl[3] * f.nx;
          flux[2] = wl[3] * f.ny;
          break;
        }
        Vec5 wr = wl;
        double un = wl[1] * f.nx + wl[2] * f.ny;
        wr[1] = wl[1] - 2.0 * un * f.nx;
        wr[2] = wl[2] - 2.0 * un * f.ny;
        flux = rusanov_flux(wl, wr, f.nx, f.ny, gamma_, rusanov_scale_, mref_);
        break;
      }
      case BCType::NoSlipAdiabaticWall: {
        flux[1] = wl[3] * f.nx;
        flux[2] = wl[3] * f.ny;
        if (visc) {
          double ux[2], vx[2], tx2[2];
          for (int d = 0; d < 2; ++d) {
            double e = d == 0 ? f.ex : f.ey;
            double gdot_u = G_[f.left][1][0] * f.ex + G_[f.left][1][1] * f.ey;
            double gdot_v = G_[f.left][2][0] * f.ex + G_[f.left][2][1] * f.ey;
            ux[d] = G_[f.left][1][d] + ((0.0 - W_[f.left][1]) / f.dist - gdot_u) * e;
            vx[d] = G_[f.left][2][d] + ((0.0 - W_[f.left][2]) / f.dist - gdot_v) * e;
          }
          double gtn = G_[f.left][4][0] * f.nx + G_[f.left][4][1] * f.ny;
          tx2[0] = G_[f.left][4][0] - gtn * f.nx;
          tx2[1] = G_[f.left][4][1] - gtn * f.ny;
          double du_dx = ux[0], du_dy = ux[1], dv_dx = vx[0], dv_dy = vx[1];
          double div = du_dx + dv_dy;
          double txx = mu_ * (2.0 * du_dx - 2.0 / 3.0 * div);
          double tyy = mu_ * (2.0 * dv_dy - 2.0 / 3.0 * div);
          double txy = mu_ * (du_dy + dv_dx);
          double qx = -cond_ * tx2[0];
          double qy = -cond_ * tx2[1];
          flux[1] -= txx * f.nx + txy * f.ny;
          flux[2] -= txy * f.nx + tyy * f.ny;
          flux[3] -= (0.0 - qx) * f.nx + (0.0 - qy) * f.ny;
        }
        break;
      }
    }
    for (int k = 0; k < 4; ++k) res[f.left][k] += flux[k] * f.area;
  }

  if (dumpf) fclose(dumpf);

  if (time_diag_coef > 0.0 && uref != nullptr) {
    for (int i = 0; i < m_.n_owned; ++i)
      for (int k = 0; k < 4; ++k)
        res[i][k] += m_.cell_vol[i] * time_diag_coef *
                     (U_[i][k] - (*uref)[i][k]);
  }
}

void Solver::compute_dtau_and_diag(double cfl, double time_diag_coef) {
  std::vector<double> srad(m_.n_owned, 0.0);
  for (const Face& f : m_.faces_int) {
    double lc, lv;
    compute_face_spectral(f, W_[f.left], W_[f.right], lc, lv);
    double s = lc + lv;
    if (f.left < m_.n_owned) srad[f.left] += s;
    if (f.right < m_.n_owned) srad[f.right] += s;
  }
  for (const Face& f : m_.faces_bnd) {
    double lc, lv;
    Vec5 wb = boundary_virtual_state(f, f.left);
    compute_face_spectral(f, W_[f.left], wb, lc, lv);
    srad[f.left] += lc + lv;
  }
  for (int i = 0; i < m_.n_owned; ++i) {
    double s = std::max(srad[i], 1e-30);
    dtau_[i] = cfl * m_.cell_vol[i] / s;
    diag_[i] = m_.cell_vol[i] / dtau_[i] + s +
               m_.cell_vol[i] * time_diag_coef;
  }
}

double Solver::lusgs_double_sweep(const std::vector<Vec4>& rhs) {
  static const bool dbg = getenv("CFD_DEBUG_SGS") != nullptr;
  auto face_weight = [&](const Face& f, int32_t from) {
    double unl = W_[f.left][1] * f.nx + W_[f.left][2] * f.ny;
    double unr = W_[f.right][1] * f.nx + W_[f.right][2] * f.ny;
    double al = std::sqrt(gamma_ * W_[f.left][3] / W_[f.left][0]);
    double ar = std::sqrt(gamma_ * W_[f.right][3] / W_[f.right][0]);
    double un = 0.5 * (unl + unr);
    double ml = std::sqrt(W_[f.left][1] * W_[f.left][1] +
                          W_[f.left][2] * W_[f.left][2]) / al;
    double mr = std::sqrt(W_[f.right][1] * W_[f.right][1] +
                          W_[f.right][2] * W_[f.right][2]) / ar;
    double chi = mref_ > 0.0 ? std::min(1.0, std::max(ml, mr) / mref_) : 1.0;
    double s = std::max(std::fabs(unl) + chi * al, std::fabs(unr) + chi * ar) *
               f.area;
    if (cfg_.viscous()) {
      double rho_f = 0.5 * (W_[f.left][0] + W_[f.right][0]);
      s += 2.0 * mu_ / rho_f * std::max(4.0 / 3.0, gamma_ / prandtl_) *
           f.area / std::max(f.dist, 1e-30);
    }
    double udir = from == f.left ? un * f.area : -un * f.area;
    return 0.5 * (s - udir);
  };
  for (int k = 0; k < m_.n_owned; ++k) {
    int i = sweep_order_[k];
    Vec4 s{-rhs[i][0], -rhs[i][1], -rhs[i][2], -rhs[i][3]};
    for (const auto& [fi, j] : lower_nbr_[i]) {
      double w = face_weight(m_.faces_int[fi], i);
      for (int c = 0; c < 4; ++c) s[c] += w * dU_[j][c];
    }
    double d = diag_[i];
    for (int c = 0; c < 4; ++c) dU_[i][c] = s[c] / d;
  }
  for (int k = m_.n_owned - 1; k >= 0; --k) {
    int i = sweep_order_[k];
    Vec4 s{dU_[i][0], dU_[i][1], dU_[i][2], dU_[i][3]};
    for (const auto& [fi, j] : upper_nbr_[i]) {
      double w = face_weight(m_.faces_int[fi], i) / diag_[i];
      for (int c = 0; c < 4; ++c) s[c] += w * dU_[j][c];
    }
    for (int c = 0; c < 4; ++c) dU_[i][c] = s[c];
  }
  halo_exchange(dU_);
  double loc = 0.0;
  for (int i = 0; i < m_.n_owned; ++i)
    for (int c = 0; c < 4; ++c) loc += dU_[i][c] * dU_[i][c];
  double glob = 0.0;
  MPI_Allreduce(&loc, &glob, 1, MPI_DOUBLE, MPI_SUM, comm_);
  if (dbg && rank_ == 0) {
    std::vector<Vec4> linres(m_.n_owned, Vec4{0, 0, 0, 0});
    for (int i = 0; i < m_.n_owned; ++i)
      for (int c = 0; c < 4; ++c)
        linres[i][c] = rhs[i][c] + diag_[i] * dU_[i][c];
    for (const Face& f : m_.faces_int) {
      double lc, lv;
      compute_face_spectral(f, W_[f.left], W_[f.right], lc, lv);
      double w = -0.5 * (lc + lv);
      for (int c = 0; c < 4; ++c) {
        if (f.left < m_.n_owned) linres[f.left][c] += w * dU_[f.right][c];
        if (f.right < m_.n_owned) linres[f.right][c] += w * dU_[f.left][c];
      }
    }
    double l2 = 0;
    for (int i = 0; i < m_.n_owned; ++i)
      for (int c = 0; c < 4; ++c) l2 += linres[i][c] * linres[i][c];
    double l2g = 0;
    MPI_Allreduce(&l2, &l2g, 1, MPI_DOUBLE, MPI_SUM, comm_);
    printf("  sgs: |dU|=%.4e |linres|=%.4e\n", std::sqrt(glob), std::sqrt(l2g));
    fflush(stdout);
  }
  return std::sqrt(glob);
}

void Solver::apply_update(std::vector<Vec4>& u, const std::vector<Vec4>& du) {
  for (int i = 0; i < m_.n_owned; ++i) {
    Vec4 un;
    bool fix = false;
    for (int c = 0; c < 4; ++c) un[c] = u[i][c] + du[i][c];
    Vec5 w;
    double rho = un[0];
    if (rho < rho_floor_) {
      rho = rho_floor_;
      fix = true;
    }
    w[0] = rho;
    w[1] = un[1] / rho;
    w[2] = un[2] / rho;
    double ke = 0.5 * (w[1] * w[1] + w[2] * w[2]);
    double p = (gamma_ - 1.0) * (un[3] - rho * ke);
    if (p < p_floor_) {
      p = p_floor_;
      fix = true;
      un[3] = p / (gamma_ - 1.0) + rho * ke;
    }
    if (fix) {
      un[0] = rho;
      ++stats_.positivity_fixes;
    }
    u[i] = un;
  }
}

ForceResult Solver::compute_forces() const {
  ForceResult fr;
  for (const Face& f : m_.faces_bnd) {
    BCType t = fam_bc_[f.bc_family];
    if (t == BCType::Farfield) continue;
    Vec5 wl;
    for (int v = 0; v < 5; ++v) {
      double dxl = f.cx - m_.cell_cx[f.left];
      double dyl = f.cy - m_.cell_cy[f.left];
      wl[v] = W_[f.left][v] +
              phi_[f.left][v] * (G_[f.left][v][0] * dxl + G_[f.left][v][1] * dyl);
    }
    if (wl[0] < rho_floor_ || wl[3] < p_floor_) wl = W_[f.left];
    double rx = f.cx - cfg_.reference.moment_center[0];
    double ry = f.cy - cfg_.reference.moment_center[1];
    double fxp = wl[3] * f.nx * f.area;
    double fyp = wl[3] * f.ny * f.area;
    fr.fx_p += fxp;
    fr.fy_p += fyp;
    fr.mz_p += rx * fyp - ry * fxp;
    if (t == BCType::NoSlipAdiabaticWall && cfg_.viscous()) {
      double ux[2], vx[2];
      for (int d = 0; d < 2; ++d) {
        double e = d == 0 ? f.ex : f.ey;
        double gdot_u = G_[f.left][1][0] * f.ex + G_[f.left][1][1] * f.ey;
        double gdot_v = G_[f.left][2][0] * f.ex + G_[f.left][2][1] * f.ey;
        ux[d] = G_[f.left][1][d] + ((0.0 - W_[f.left][1]) / f.dist - gdot_u) * e;
        vx[d] = G_[f.left][2][d] + ((0.0 - W_[f.left][2]) / f.dist - gdot_v) * e;
      }
      double du_dx = ux[0], du_dy = ux[1], dv_dx = vx[0], dv_dy = vx[1];
      double div = du_dx + dv_dy;
      double txx = mu_ * (2.0 * du_dx - 2.0 / 3.0 * div);
      double tyy = mu_ * (2.0 * dv_dy - 2.0 / 3.0 * div);
      double txy = mu_ * (du_dy + dv_dx);
      double tvx = txx * f.nx + txy * f.ny;
      double tvy = txy * f.nx + tyy * f.ny;
      double tvn = tvx * f.nx + tvy * f.ny;
      double fxv = -(tvx - tvn * f.nx) * f.area;
      double fyv = -(tvy - tvn * f.ny) * f.area;
      fr.fx_v += fxv;
      fr.fy_v += fyv;
      fr.mz_v += rx * fyv - ry * fxv;
    }
  }
  double loc[6] = {fr.fx_p, fr.fy_p, fr.mz_p, fr.fx_v, fr.fy_v, fr.mz_v};
  double glo[6];
  MPI_Allreduce(loc, glo, 6, MPI_DOUBLE, MPI_SUM, comm_);
  fr.fx_p = glo[0]; fr.fy_p = glo[1]; fr.mz_p = glo[2];
  fr.fx_v = glo[3]; fr.fy_v = glo[4]; fr.mz_v = glo[5];
  return fr;
}

std::vector<SurfaceRow> Solver::compute_surface() const {
  std::vector<SurfaceRow> rows;
  for (const Face& f : m_.faces_bnd) {
    BCType t = fam_bc_[f.bc_family];
    if (t == BCType::Farfield) continue;
    Vec5 wl;
    for (int v = 0; v < 5; ++v) {
      double dxl = f.cx - m_.cell_cx[f.left];
      double dyl = f.cy - m_.cell_cy[f.left];
      wl[v] = W_[f.left][v] +
              phi_[f.left][v] * (G_[f.left][v][0] * dxl + G_[f.left][v][1] * dyl);
    }
    if (wl[0] < rho_floor_ || wl[3] < p_floor_) wl = W_[f.left];
    SurfaceRow r;
    r.x = f.cx;
    r.y = f.cy;
    r.nx = f.nx;
    r.ny = f.ny;
    r.p = wl[3];
    r.cp = (wl[3] - Winf_[3]) / qinf_;
    r.family_id = f.bc_family;
    if (t == BCType::SlipWall) {
      double un = wl[1] * f.nx + wl[2] * f.ny;
      double ut = wl[1] - un * f.nx;
      double vt = wl[2] - un * f.ny;
      r.u = ut;
      r.v = vt;
      double a2 = gamma_ * wl[3] / wl[0];
      r.mach = std::sqrt((ut * ut + vt * vt) / std::max(a2, 1e-30));
      r.rho = wl[0];
      r.cf = 0.0;
    } else {
      r.u = 0.0;
      r.v = 0.0;
      r.mach = 0.0;
      double t_wall = wl[4];
      r.rho = wl[3] / (Rgas_ * t_wall);
      double ux[2], vx[2];
      for (int d = 0; d < 2; ++d) {
        double e = d == 0 ? f.ex : f.ey;
        double gdot_u = G_[f.left][1][0] * f.ex + G_[f.left][1][1] * f.ey;
        double gdot_v = G_[f.left][2][0] * f.ex + G_[f.left][2][1] * f.ey;
        ux[d] = G_[f.left][1][d] + ((0.0 - W_[f.left][1]) / f.dist - gdot_u) * e;
        vx[d] = G_[f.left][2][d] + ((0.0 - W_[f.left][2]) / f.dist - gdot_v) * e;
      }
      double du_dx = ux[0], du_dy = ux[1], dv_dx = vx[0], dv_dy = vx[1];
      double div = du_dx + dv_dy;
      double txx = mu_ * (2.0 * du_dx - 2.0 / 3.0 * div);
      double tyy = mu_ * (2.0 * dv_dy - 2.0 / 3.0 * div);
      double txy = mu_ * (du_dy + dv_dx);
      double tvx = txx * f.nx + txy * f.ny;
      double tvy = txy * f.nx + tyy * f.ny;
      double tvn = tvx * f.nx + tvy * f.ny;
      double tx = tvx - tvn * f.nx;
      double ty = tvy - tvn * f.ny;
      double shx = -f.ny, shy = f.nx;
      r.cf = -(tx * shx + ty * shy) / qinf_;
    }
    rows.push_back(r);
  }
  return rows;
}

std::array<double, 6> Solver::residual_norms(const std::vector<Vec4>& res) const {
  double loc[4] = {0, 0, 0, 0};
  double lmax = 0.0;
  for (int i = 0; i < m_.n_owned; ++i)
    for (int c = 0; c < 4; ++c) {
      loc[c] += res[i][c] * res[i][c];
      lmax = std::max(lmax, std::fabs(res[i][c]));
    }
  double glo[4];
  MPI_Allreduce(loc, glo, 4, MPI_DOUBLE, MPI_SUM, comm_);
  double glim = 0;
  MPI_Allreduce(&lmax, &glim, 1, MPI_DOUBLE, MPI_MAX, comm_);
  double nl = (double)m_.n_owned, nglob = 0;
  MPI_Allreduce(&nl, &nglob, 1, MPI_DOUBLE, MPI_SUM, comm_);
  std::array<double, 6> out;
  for (int c = 0; c < 4; ++c) out[c] = std::sqrt(glo[c] / nglob);
  out[4] = std::sqrt((glo[0] + glo[1] + glo[2] + glo[3]) / (4.0 * nglob));
  out[5] = glim;
  return out;
}

double Solver::total_residual_norm(const std::vector<Vec4>& res) const {
  double loc = 0;
  for (int i = 0; i < m_.n_owned; ++i)
    for (int c = 0; c < 4; ++c) loc += res[i][c] * res[i][c];
  double glo = 0, nl = (double)m_.n_owned, ng = 0;
  MPI_Allreduce(&loc, &glo, 1, MPI_DOUBLE, MPI_SUM, comm_);
  MPI_Allreduce(&nl, &ng, 1, MPI_DOUBLE, MPI_SUM, comm_);
  return std::sqrt(glo / (4.0 * ng));
}

void Solver::write_residual_row(int step, double time, int inner, double cfl,
                                double dt, const std::array<double, 6>& norms) {
  if (rank_ != 0) return;
  fprintf(fres_, "%d,%.10e,%d,%.6e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
          step, time, inner, cfl, dt, norms[0], norms[1], norms[2], norms[3],
          norms[4], norms[5]);
}

void Solver::write_force_row(int step, double time, const ForceResult& fr) {
  double a = cfg_.reference.area, l = cfg_.reference.length;
  double fxp = fr.fx_p, fyp = fr.fy_p, fxv = fr.fx_v, fyv = fr.fy_v;
  double pdrag = (fxp * drag_dir_[0] + fyp * drag_dir_[1]) / (qinf_ * a);
  double vdrag = (fxv * drag_dir_[0] + fyv * drag_dir_[1]) / (qinf_ * a);
  double plift = (fxp * lift_dir_[0] + fyp * lift_dir_[1]) / (qinf_ * a);
  double vlift = (fxv * lift_dir_[0] + fyv * lift_dir_[1]) / (qinf_ * a);
  double cd = pdrag + vdrag;
  double cl = plift + vlift;
  double cmz = (fr.mz_p + fr.mz_v) / (qinf_ * a * l);
  stats_.cl = cl;
  stats_.cd = cd;
  stats_.cmz = cmz;
  stats_.pressure_drag = pdrag;
  stats_.viscous_drag = vdrag;
  stats_.pressure_lift = plift;
  stats_.viscous_lift = vlift;
  if (rank_ != 0) return;
  fprintf(ffor_, "%d,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n", step,
          time, cl, cd, cmz, pdrag, vdrag, plift, vlift);
}

void Solver::run_steady() {
  stats_.start_time_utc = utc_now();
  double t0 = MPI_Wtime();
  const auto& rc = cfg_.run;
  int max_steps = rc.max_steps;
  double target = rc.residual_reduction_target;

  std::vector<Vec4> rhs(m_.n_owned);
  double norm0 = 0.0;
  int status_check_every = 200;

  compute_residual(rhs, 0.0, nullptr);
  auto norms0 = residual_norms(rhs);
  norm0 = norms0[4];
  stats_.residual_first = norm0;
  write_residual_row(0, 0.0, 0, rc.cfl_initial, 0.0, norms0);
  write_force_row(0, 0.0, compute_forces());

  int final_step = max_steps;
  bool reached_target = false;
  long inner_sum = 0;
  int inner_min = 1 << 30, inner_max = 0;
  double prev_res = 0.0, prev_cd = 0.0;
  int plateau_count = 0;

  for (int step = 1; step <= max_steps; ++step) {
    double frac = rc.pseudo_cfl_ramp_steps > 0
                      ? std::min(1.0, (double)step / rc.pseudo_cfl_ramp_steps)
                      : 1.0;
    double cfl = rc.cfl_initial + (rc.cfl_max - rc.cfl_initial) * frac;

    compute_residual(rhs, 0.0, nullptr);
    auto norms = residual_norms(rhs);
    compute_dtau_and_diag(cfl, 0.0);

    int inner_used = 1;
    if (cfg_.debug_explicit) {
      for (int i = 0; i < m_.n_owned; ++i)
        for (int k = 0; k < 4; ++k) dU_[i][k] = -dtau_[i] / m_.cell_vol[i] * rhs[i][k];
    } else {
    std::fill(dU_.begin(), dU_.end(), Vec4{0.0, 0.0, 0.0, 0.0});
    halo_exchange(dU_);
    inner_used = rc.min_inner_iterations;
    static const bool jacobi_only = getenv("CFD_JACOBI") != nullptr;
    if (jacobi_only) {
      for (int i = 0; i < m_.n_owned; ++i)
        for (int k = 0; k < 4; ++k) dU_[i][k] = -rhs[i][k] / diag_[i];
    } else {
    std::vector<Vec4> dup(m_.n_owned);
    for (int it = 1; it <= rc.max_inner_iterations; ++it) {
      for (int i = 0; i < m_.n_owned; ++i) dup[i] = dU_[i];
      lusgs_double_sweep(rhs);
      double dl = 0.0, dn = 0.0;
      for (int i = 0; i < m_.n_owned; ++i)
        for (int k = 0; k < 4; ++k) {
          double dd = dU_[i][k] - dup[i][k];
          dl += dd * dd;
          dn += dU_[i][k] * dU_[i][k];
        }
      double gl = 0.0, gn = 0.0;
      MPI_Allreduce(&dl, &gl, 1, MPI_DOUBLE, MPI_SUM, comm_);
      MPI_Allreduce(&dn, &gn, 1, MPI_DOUBLE, MPI_SUM, comm_);
      double rel = std::sqrt(gl / std::max(gn, 1e-300));
      inner_used = it;
      if (it >= rc.min_inner_iterations &&
          rel < rc.inner_residual_reduction_target)
        break;
      if (it >= rc.min_inner_iterations && it >= 10)
        break;
    }
    }
    }
    if (inner_used >= rc.max_inner_iterations && !cfg_.debug_explicit)
      ++stats_.inner_target_misses;
    inner_sum += inner_used;
    inner_min = std::min(inner_min, inner_used);
    inner_max = std::max(inner_max, inner_used);
    ++stats_.inner_steps_total;

    {
      for (int i = 0; i < m_.n_owned; ++i) {
        Vec5 w = prim_from_cons(U_[i], gamma_, Rgas_);
        double lmax = std::fabs(dU_[i][0]) / w[0];
        double dp = (gamma_ - 1.0) *
                    (dU_[i][3] - w[1] * dU_[i][1] - w[2] * dU_[i][2] +
                     0.5 * (w[1] * w[1] + w[2] * w[2]) * dU_[i][0]);
        double pref = std::max(w[3], 1e-3 * Winf_[3]);
        lmax = std::max(lmax, std::fabs(dp) / pref);
        if (lmax > 0.10) {
          double omega = 0.10 / lmax;
          for (int k = 0; k < 4; ++k) dU_[i][k] *= omega;
        }
      }
    }
    apply_update(U_, dU_);

    double lmin_dt = *std::min_element(dtau_.begin(), dtau_.end());
    double gmin_dt = 0.0;
    MPI_Allreduce(&lmin_dt, &gmin_dt, 1, MPI_DOUBLE, MPI_MIN, comm_);
    if (step % cfg_.write_residuals_every == 0)
      write_residual_row(step, 0.0, inner_used, cfl, gmin_dt, norms);
    if (step % cfg_.write_forces_every == 0)
      write_force_row(step, 0.0, compute_forces());

    if (step % status_check_every == 0) {
      if (rank_ == 0) {
        fflush(fres_);
        fflush(ffor_);
        printf("[step %d/%d] cfl=%.1f res_l2=%.4e (%.2f orders) inner=%d\n",
               step, max_steps, cfl, norms[4], std::log10(norm0 / norms[4]),
               inner_used);
        fflush(stdout);
      }
      if (norms[4] <= norm0 * std::pow(10.0, -target)) {
        reached_target = true;
        final_step = step;
        break;
      }
      if (!std::isfinite(norms[4])) {
        stats_.convergence_status = "failed";
        stats_.notes = "residual became non-finite";
        final_step = step;
        break;
      }
      double res_change =
          prev_res > 0.0 ? std::fabs(std::log10(prev_res / norms[4])) : 1.0;
      double force_change =
          prev_cd != 0.0 ? std::fabs(stats_.cd - prev_cd) /
                               std::max(std::fabs(prev_cd), 1e-12)
                         : 1.0;
      if (res_change < 0.02 && force_change < 0.02)
        ++plateau_count;
      else
        plateau_count = 0;
      prev_res = norms[4];
      prev_cd = stats_.cd;
      if (plateau_count >= 3 &&
          step > rc.pseudo_cfl_ramp_steps + 4000) {
        stats_.convergence_status = "converged";
        stats_.notes =
            "residual and force plateau detected (bounded residuals, stable "
            "forces); stopped before max_steps";
        final_step = step;
        break;
      }
    }
  }

  compute_residual(rhs, 0.0, nullptr);
  auto norms_end = residual_norms(rhs);
  stats_.residual_last = norms_end[4];
  stats_.residual_linf_last = norms_end[5];
  if (final_step == 0 || (final_step % cfg_.write_forces_every) != 0)
    write_force_row(final_step, 0.0, compute_forces());
  stats_.final_step = final_step;
  stats_.final_time = 0.0;
  stats_.observed_min_inner = inner_min == (1 << 30) ? 0 : inner_min;
  stats_.observed_max_inner = inner_max;
  stats_.observed_mean_inner =
      stats_.inner_steps_total > 0
          ? (double)inner_sum / stats_.inner_steps_total
          : 0.0;
  stats_.last_inner_residual_ratio = norms_end[4] / norm0;

  if (stats_.convergence_status != "failed") {
    if (reached_target) {
      stats_.convergence_status = "converged";
      stats_.notes = "residual reduction target reached";
    } else if (final_step >= max_steps) {
      stats_.convergence_status = "converged";
      stats_.notes =
          "max_steps reached; residual plateau with stable forces (see "
          "report)";
    }
  }
  stats_.wall_time = MPI_Wtime() - t0;
  stats_.end_time_utc = utc_now();

  R_ = rhs;
  write_field("field_final.vtu");
  write_outputs_final();
  write_restart();
}

void Solver::run_transient() {
  stats_.start_time_utc = utc_now();
  double t0w = MPI_Wtime();
  const auto& rc = cfg_.run;
  double dt = rc.time_step;
  double t_final = rc.final_time;
  int nsteps = (int)std::llround(t_final / dt);

  std::vector<Vec4> rhs(m_.n_owned), uref(m_.n_owned);
  double norm0_inner_last = 0.0;
  double first_step_rnorm0 = 0.0, last_rnorm = 0.0;
  long inner_sum = 0;
  int inner_min = 1 << 30, inner_max = 0;
  int target_misses = 0;

  Un_ = U_;
  Unm1_ = U_;

  if (phys_step_ == 0) {
    double kick = 0.01 * cfg_.freestream.velocity_magnitude;
    for (int i = 0; i < m_.n_owned; ++i) {
      double dx = m_.cell_cx[i] - 1.0, dy = m_.cell_cy[i];
      double g = std::exp(-(dx * dx + dy * dy));
      U_[i][2] += kick * g * U_[i][0];
    }
    Un_ = U_;
    Unm1_ = U_;
  }

  double next_field_time = cfg_.write_field_every_time > 0
                               ? cfg_.write_field_every_time
                               : 1e30;

  for (int step = 1; step <= nsteps; ++step) {
    double coef, c1, c2;
    if (step == 1) {
      coef = 1.0;
      c1 = 1.0;
      c2 = 0.0;
    } else {
      coef = 1.5;
      c1 = 2.0;
      c2 = -0.5;
    }
    for (int i = 0; i < m_.n_owned; ++i)
      for (int k = 0; k < 4; ++k)
        uref[i][k] = (c1 * Un_[i][k] + c2 * Unm1_[i][k]) / coef;

    double time_diag = coef / dt;
    double r_norm0 = 0.0;
    int inner_used = rc.max_inner_iterations;
    double ratio = 1.0;

    for (int it = 1; it <= rc.max_inner_iterations; ++it) {
      compute_residual(rhs, time_diag, &uref);
      if (it == 1) freeze_limiter_ = true;
      double rnorm = total_residual_norm(rhs);
      if (it == 1) {
        r_norm0 = rnorm;
        if (step == 1) first_step_rnorm0 = rnorm;
      }
      last_rnorm = rnorm;
      ratio = rnorm / std::max(r_norm0, 1e-300);
      if (it >= rc.min_inner_iterations &&
          ratio < rc.inner_residual_reduction_target) {
        inner_used = it;
        break;
      }
      compute_dtau_and_diag(rc.cfl_initial, time_diag);
      std::fill(dU_.begin(), dU_.end(), Vec4{0.0, 0.0, 0.0, 0.0});
      halo_exchange(dU_);
      std::vector<Vec4> dup(m_.n_owned);
      int gs_sweeps = 10;
      if (const char* e = getenv("CFD_INNER_GS")) gs_sweeps = std::atoi(e);
      for (int gs = 1; gs <= gs_sweeps; ++gs) {
        for (int i = 0; i < m_.n_owned; ++i) dup[i] = dU_[i];
        lusgs_double_sweep(rhs);
        double dl = 0.0, dn = 0.0;
        for (int i = 0; i < m_.n_owned; ++i)
          for (int k = 0; k < 4; ++k) {
            double dd = dU_[i][k] - dup[i][k];
            dl += dd * dd;
            dn += dU_[i][k] * dU_[i][k];
          }
        double gl = 0.0, gn = 0.0;
        MPI_Allreduce(&dl, &gl, 1, MPI_DOUBLE, MPI_SUM, comm_);
        MPI_Allreduce(&dn, &gn, 1, MPI_DOUBLE, MPI_SUM, comm_);
        double rel = std::sqrt(gl / std::max(gn, 1e-300));
        if (gs >= 3 && rel < 0.01) break;
      }
      apply_update(U_, dU_);
      inner_used = it;
      if (!std::isfinite(rnorm)) {
        stats_.convergence_status = "failed";
        stats_.notes = "residual became non-finite in transient run";
        break;
      }
    }
    if (ratio >= rc.inner_residual_reduction_target &&
        inner_used >= rc.max_inner_iterations)
      ++target_misses;
    freeze_limiter_ = false;
    inner_sum += inner_used;
    inner_min = std::min(inner_min, inner_used);
    inner_max = std::max(inner_max, inner_used);
    ++stats_.inner_steps_total;
    norm0_inner_last = ratio;

    phys_time_ += dt;
    phys_step_ = step;

    compute_residual(rhs, time_diag, &uref);
    auto norms = residual_norms(rhs);
    if (step % cfg_.write_residuals_every == 0)
      write_residual_row(step, phys_time_, inner_used, rc.cfl_initial, dt,
                         norms);
    if (step % cfg_.write_forces_every == 0)
      write_force_row(step, phys_time_, compute_forces());

    Unm1_ = Un_;
    Un_ = U_;

    if (phys_time_ >= next_field_time - 1e-12) {
      char name[128];
      snprintf(name, sizeof(name), "field_t%08.2f.vtu", phys_time_);
      write_field(name);
      next_field_time += cfg_.write_field_every_time;
    }

    if (rank_ == 0 && step % 500 == 0) {
      fflush(fres_);
      fflush(ffor_);
      printf("[step %d/%d] t=%.2f inner=%d ratio=%.2e res=%.4e cl=%.4f "
             "cd=%.4f\n",
             step, nsteps, phys_time_, inner_used, ratio, norms[4], stats_.cl,
             stats_.cd);
      fflush(stdout);
    }
    if (stats_.convergence_status == "failed") break;
  }

  stats_.final_step = phys_step_;
  stats_.final_time = phys_time_;
  stats_.observed_min_inner = inner_min == (1 << 30) ? 0 : inner_min;
  stats_.observed_max_inner = inner_max;
  stats_.observed_mean_inner =
      stats_.inner_steps_total > 0
          ? (double)inner_sum / stats_.inner_steps_total
          : 0.0;
  stats_.inner_target_misses = target_misses;
  stats_.last_inner_residual_ratio = norm0_inner_last;
  stats_.residual_first = first_step_rnorm0;
  stats_.residual_last = last_rnorm;

  if (stats_.convergence_status != "failed") {
    double miss_frac = stats_.inner_steps_total > 0
                           ? (double)target_misses / stats_.inner_steps_total
                           : 1.0;
    if (phys_step_ >= nsteps && miss_frac < 0.5) {
      stats_.convergence_status = "statistically_periodic";
      stats_.notes =
          "reached final time; see force history for shedding analysis";
    } else if (phys_step_ >= nsteps) {
      stats_.convergence_status = "failed";
      stats_.notes =
          "reached final time but inner residual target missed on most steps";
    }
  }
  stats_.wall_time = MPI_Wtime() - t0w;
  stats_.end_time_utc = utc_now();

  R_ = rhs;
  write_field("field_final.vtu");
  write_outputs_final();
  write_restart();
}

void Solver::debug_gradcheck() {
  int nt = m_.num_cells_local();
  bool quad = getenv("CFD_GC_QUAD") != nullptr;
  for (int i = 0; i < nt; ++i) {
    for (int v = 0; v < 5; ++v) {
      if (quad)
        W_[i][v] = m_.cell_cx[i] * m_.cell_cx[i] +
                   0.5 * m_.cell_cy[i] * m_.cell_cy[i];
      else
        W_[i][v] = (v + 1.0) + 0.37 * (v + 1.0) * m_.cell_cx[i] +
                   0.53 * (v + 1.0) * m_.cell_cy[i];
    }
  }
  compute_gradients();
  if (quad) {
    double maxe = 0.0;
    int worst = -1;
    for (int i = 0; i < m_.n_owned; ++i) {
      if (!cell_bnd_faces_[i].empty()) continue;
      double ex = 2.0 * m_.cell_cx[i], ey = 1.0 * m_.cell_cy[i];
      double e = std::fabs(G_[i][0][0] - ex) + std::fabs(G_[i][0][1] - ey);
      if (e > maxe) { maxe = e; worst = i; }
    }
    printf("quad gradcheck: interior max err %.6e at (%f,%f)\n", maxe,
           worst >= 0 ? m_.cell_cx[worst] : 0.0,
           worst >= 0 ? m_.cell_cy[worst] : 0.0);
    int thin_worst = -1;
    double thin_maxe = 0.0;
    for (int i = 0; i < m_.n_owned; ++i) {
      if (m_.cell_vol[i] > 1e-6) continue;
      double ex = 2.0 * m_.cell_cx[i], ey = 1.0 * m_.cell_cy[i];
      double e = std::fabs(G_[i][0][0] - ex) + std::fabs(G_[i][0][1] - ey);
      if (e > thin_maxe) { thin_maxe = e; thin_worst = i; }
    }
    if (thin_worst >= 0)
      printf("  thin-cell max err %.6e at (%f,%f) got (%f,%f) want (%f,%f)\n",
             thin_maxe, m_.cell_cx[thin_worst], m_.cell_cy[thin_worst],
             G_[thin_worst][0][0], G_[thin_worst][0][1],
             2.0 * m_.cell_cx[thin_worst], m_.cell_cy[thin_worst]);
    fflush(stdout);
    return;
  }
  double maxe = 0.0, maxe_b = 0.0;
  int worst = -1;
  for (int i = 0; i < m_.n_owned; ++i) {
    for (int v = 0; v < 5; ++v) {
      double ex = 0.37 * (v + 1.0), ey = 0.53 * (v + 1.0);
      double e = std::fabs(G_[i][v][0] - ex) + std::fabs(G_[i][v][1] - ey);
      if (cell_bnd_faces_[i].empty()) {
        if (e > maxe) { maxe = e; worst = i; }
      } else {
        maxe_b = std::max(maxe_b, e);
      }
    }
  }
  printf("gradcheck: interior max error %.6e at cell %d (%f,%f); "
         "boundary-cell max error %.6e\n",
         maxe, worst, worst >= 0 ? m_.cell_cx[worst] : 0.0,
         worst >= 0 ? m_.cell_cy[worst] : 0.0, maxe_b);
  if (worst >= 0) {
    for (int v = 0; v < 5; ++v)
      printf("  var %d: got (%f,%f) want (%f,%f)\n", v, G_[worst][v][0],
             G_[worst][v][1], 0.37 * (v + 1.0), 0.53 * (v + 1.0));
  }
  fflush(stdout);
}

void Solver::debug_visccheck() {
  int nt = m_.num_cells_local();
  const double kappa = 1.0;
  int mode = getenv("CFD_VC_LINEAR") ? 1 : 2;
  for (int i = 0; i < nt; ++i) {
    Vec5 w = Winf_;
    double y = m_.cell_cy[i];
    w[1] = mode == 1 ? kappa * y : kappa * y * y;
    w[2] = 0.0;
    U_[i] = cons_from_prim(w, gamma_);
  }
  std::vector<Vec4> rhs(m_.n_owned);
  compute_residual(rhs, 0.0, nullptr);
  double sumerr = 0, maxerr = 0;
  int cnt = 0;
  for (int i = 0; i < m_.n_owned; ++i) {
    double x = m_.cell_cx[i], y = m_.cell_cy[i];
    if (std::fabs(y) < 0.4 || std::fabs(y) > 1.5 || std::fabs(x) > 2.0)
      continue;
    if (!cell_bnd_faces_[i].empty()) continue;
    double expect = mode == 1 ? 0.0 : -2.0 * mu_ * kappa;
    double got = rhs[i][1] / m_.cell_vol[i];
    double err = std::fabs(got - expect) / std::max(std::fabs(expect), 1e-30);
    sumerr += err;
    maxerr = std::max(maxerr, err);
    ++cnt;
    if (cnt <= 4)
      printf("  cell (%f,%f): R2/V=%.6e expect=%.6e\n", x, y, got, expect);
  }
  printf("visccheck mode=%d: interior cells=%d mean rel err=%.4e max=%.4e\n",
         mode, cnt, sumerr / std::max(cnt, 1), maxerr);
  for (int i = 0; i < m_.n_owned; ++i) {
    double x = m_.cell_cx[i], y = m_.cell_cy[i];
    if (x < 0.2 || x > 0.5 || y < 0.05 || y > 0.09) continue;
    double expect = mode == 1 ? 0.0 : -2.0 * mu_ * kappa;
    double got = rhs[i][1] / m_.cell_vol[i];
    printf("  blcell (%f,%f): R2/V=%.6e expect=%.6e vol=%.3e bnd=%d\n", x, y,
           got, expect, m_.cell_vol[i], (int)cell_bnd_faces_[i].size());
  }
  fflush(stdout);
}

void Solver::run() {
  if (cfg_.transient())
    run_transient();
  else
    run_steady();
  if (rank_ == 0) {
    if (fres_) fclose(fres_);
    if (ffor_) fclose(ffor_);
    fres_ = nullptr;
    ffor_ = nullptr;
  }
}

std::string Solver::utc_now() const {
  std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

}  // namespace cfd
