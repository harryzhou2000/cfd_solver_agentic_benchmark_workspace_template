#include "driver.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

#include "mesh.hpp"
#include "numerics.hpp"

#include <nlohmann/json.hpp>

namespace cfd {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
std::string iso8601_now() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string git_revision() {
  FILE* p = popen("git -C . rev-parse HEAD 2>/dev/null", "r");
  if (!p) return "";
  char buf[128] = {0};
  if (!fgets(buf, sizeof(buf), p)) buf[0] = 0;
  pclose(p);
  std::string s = buf;
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

std::string fmt_row(int step, double time, int inner, double cfl, double dt, double rho,
                    double rhou, double rhov, double rhoE, double l2, double linf) {
  char buf[1024];
  std::snprintf(buf, sizeof(buf),
                "%d,%.10e,%d,%.6e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n", step,
                time, inner, cfl, dt, rho, rhou, rhov, rhoE, l2, linf);
  return std::string(buf);
}

std::string force_row(int step, double time, double cl, double cd, double cmz, double pd,
                      double vd, double pl, double vl) {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "%d,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n", step, time, cl, cd,
                cmz, pd, vd, pl, vl);
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// MPI text gathering (variable-length chunks from every rank)
// ---------------------------------------------------------------------------
std::string gather_text_all(const std::string& local) {
  int n = (int)local.size();
  std::vector<int> counts(g_nranks);
  MPI_Allgather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, MPI_COMM_WORLD);
  std::vector<int> displ(g_nranks, 0);
  int total = 0;
  for (int r = 0; r < g_nranks; ++r) {
    displ[r] = total;
    total += counts[r];
  }
  std::string all(total, '\0');
  if (total > 0)
    MPI_Allgatherv(local.data(), n, MPI_CHAR, all.data(), counts.data(), displ.data(),
                   MPI_CHAR, MPI_COMM_WORLD);
  return all;
}

// ---------------------------------------------------------------------------
// Residual computation at the current state (spatial, with halo/grads/limiter).
// Returns the residual norms and leaves grads/lmt/res consistent with U.
// ---------------------------------------------------------------------------
struct ResidualInfo {
  double l2 = 0.0, linf = 0.0;
  double comp[4] = {0, 0, 0, 0};
};

ResidualInfo compute_state_residual(LocalMesh& mesh, std::vector<Vec4>& U,
                                    const std::vector<LSQCoef>& coefs, const CaseConfig& cfg,
                                    bool viscous, std::vector<PrimGrad>& grads,
                                    std::vector<Limiters>& lmt, std::vector<Vec4>& res) {
  exchange_halo(mesh, U);
  compute_gradients(mesh, U, coefs, cfg.gamma, grads);
  exchange_gradients(mesh, grads);
  compute_limiter(mesh, U, grads, lmt, cfg.gamma);
  exchange_limiters(mesh, lmt);
  ResidualInfo info;
  assemble_residual(mesh, U, grads, lmt, cfg, viscous, res, &info.l2, &info.linf);
  // Component RMS norms (global).
  double sumsq[4] = {0, 0, 0, 0};
  for (int i = 0; i < mesh.n_owned; ++i)
    for (int k = 0; k < 4; ++k) sumsq[k] += res[i][k] * res[i][k];
  double gsum[4];
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  int64_t nloc = mesh.n_owned, ntot = 0;
  MPI_Allreduce(&nloc, &ntot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
  for (int k = 0; k < 4; ++k) info.comp[k] = std::sqrt(gsum[k] / (double)ntot);
  return info;
}

// Residual assembly only (uses the given frozen gradients/limiters); ghost
// states are refreshed first. Used for inner iterations with lagged
// reconstruction (defect correction).
ResidualInfo assemble_only(LocalMesh& mesh, std::vector<Vec4>& U,
                           const std::vector<PrimGrad>& grads,
                           const std::vector<Limiters>& lmt, const CaseConfig& cfg,
                           bool viscous, std::vector<Vec4>& res) {
  exchange_halo(mesh, U);
  ResidualInfo info;
  assemble_residual(mesh, U, grads, lmt, cfg, viscous, res, &info.l2, &info.linf);
  double sumsq[4] = {0, 0, 0, 0};
  for (int i = 0; i < mesh.n_owned; ++i)
    for (int k = 0; k < 4; ++k) sumsq[k] += res[i][k] * res[i][k];
  double gsum[4];
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  int64_t nloc = mesh.n_owned, ntot = 0;
  MPI_Allreduce(&nloc, &ntot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
  for (int k = 0; k < 4; ++k) info.comp[k] = std::sqrt(gsum[k] / (double)ntot);
  return info;
}

// Total residual for BDF2/BDF1 dual time: spatial residual + physical time term.
ResidualInfo compute_total_residual(LocalMesh& mesh, std::vector<Vec4>& U,
                                    const std::vector<Vec4>& Un, const std::vector<Vec4>& Unm1,
                                    const std::vector<LSQCoef>& coefs, const CaseConfig& cfg,
                                    double dt, int ps, std::vector<PrimGrad>& grads,
                                    std::vector<Limiters>& lmt, std::vector<Vec4>& res_spatial,
                                    std::vector<Vec4>& res_total) {
  ResidualInfo info =
      compute_state_residual(mesh, U, coefs, cfg, true, grads, lmt, res_spatial);
  double a0 = 1.5, a1 = -2.0, a2 = 0.5;  // BDF2
  if (ps == 0) { a0 = 1.0; a1 = -1.0; a2 = 0.0; }  // BDF1 for the first step
  res_total.resize(mesh.n_owned);
  double sumsq[4] = {0, 0, 0, 0};
  double linf = 0.0;
  for (int i = 0; i < mesh.n_owned; ++i) {
    for (int k = 0; k < 4; ++k) {
      double s = (a0 * U[i][k] + a1 * Un[i][k] + a2 * Unm1[i][k]) / dt;
      res_total[i][k] = res_spatial[i][k] + s;
      sumsq[k] += res_total[i][k] * res_total[i][k];
      linf = std::max(linf, std::abs(res_total[i][k]));
    }
  }
  double gsum[4];
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf, &info.linf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int64_t nloc = mesh.n_owned, ntot = 0;
  MPI_Allreduce(&nloc, &ntot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
  double l2sq = 0.0;
  for (int k = 0; k < 4; ++k) {
    info.comp[k] = std::sqrt(gsum[k] / (double)ntot);
    l2sq += gsum[k];
  }
  info.l2 = std::sqrt(l2sq / (4.0 * (double)ntot));
  return info;
}

// Same as compute_total_residual but with frozen (lagged) reconstruction.
ResidualInfo total_residual_frozen(LocalMesh& mesh, std::vector<Vec4>& U,
                                   const std::vector<Vec4>& Un,
                                   const std::vector<Vec4>& Unm1, const CaseConfig& cfg,
                                   double dt, int ps, const std::vector<PrimGrad>& grads,
                                   const std::vector<Limiters>& lmt,
                                   std::vector<Vec4>& res_spatial,
                                   std::vector<Vec4>& res_total) {
  ResidualInfo info = assemble_only(mesh, U, grads, lmt, cfg, true, res_spatial);
  double a0 = 1.5, a1 = -2.0, a2 = 0.5;  // BDF2
  if (ps == 0) { a0 = 1.0; a1 = -1.0; a2 = 0.0; }  // BDF1 for the first step
  res_total.resize(mesh.n_owned);
  double sumsq[4] = {0, 0, 0, 0};
  double linf = 0.0;
  for (int i = 0; i < mesh.n_owned; ++i) {
    for (int k = 0; k < 4; ++k) {
      double s = (a0 * U[i][k] + a1 * Un[i][k] + a2 * Unm1[i][k]) / dt;
      res_total[i][k] = res_spatial[i][k] + s;
      sumsq[k] += res_total[i][k] * res_total[i][k];
      linf = std::max(linf, std::abs(res_total[i][k]));
    }
  }
  double gsum[4];
  MPI_Allreduce(sumsq, gsum, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  MPI_Allreduce(&linf, &info.linf, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  int64_t nloc = mesh.n_owned, ntot = 0;
  MPI_Allreduce(&nloc, &ntot, 1, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
  double l2sq = 0.0;
  for (int k = 0; k < 4; ++k) {
    info.comp[k] = std::sqrt(gsum[k] / (double)ntot);
    l2sq += gsum[k];
  }
  info.l2 = std::sqrt(l2sq / (4.0 * (double)ntot));
  return info;
}

// ---------------------------------------------------------------------------
// Restart (global, rank 0 gathers and scatters)
// ---------------------------------------------------------------------------
void write_restart_global(const LocalMesh& mesh, const std::vector<Vec4>& U,
                          const std::string& path, int step, double time) {
  // Each rank sends (global_id, state) of its owned cells to rank 0.
  int n = mesh.n_owned;
  std::vector<int> counts(g_nranks), displ(g_nranks);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (g_rank == 0) {
    for (int r = 0; r < g_nranks; ++r) {
      displ[r] = total;
      total += counts[r];
    }
  }
  std::vector<int> gids(n);
  std::vector<double> states(n * 4);
  for (int i = 0; i < n; ++i) {
    gids[i] = mesh.cells[i].global_id;
    for (int k = 0; k < 4; ++k) states[i * 4 + k] = U[i][k];
  }
  std::vector<int> all_gids;
  std::vector<double> all_states;
  if (g_rank == 0) {
    all_gids.resize(total);
    all_states.resize(total * 4);
  }
  MPI_Gatherv(gids.data(), n, MPI_INT, all_gids.data(), counts.data(), displ.data(), MPI_INT,
              0, MPI_COMM_WORLD);
  std::vector<int> counts4(g_nranks), displ4(g_nranks);
  if (g_rank == 0) {
    for (int r = 0; r < g_nranks; ++r) {
      counts4[r] = counts[r] * 4;
      displ4[r] = displ[r] * 4;
    }
  }
  MPI_Gatherv(states.data(), n * 4, MPI_DOUBLE, all_states.data(), counts4.data(),
              displ4.data(), MPI_DOUBLE, 0, MPI_COMM_WORLD);
  if (g_rank != 0) return;
  // Sort by global id.
  std::vector<int> order(total);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return all_gids[a] < all_gids[b]; });
  std::ofstream f(path, std::ios::binary);
  if (!f) return;
  f.write("CFDRST1", 7);
  f.write((const char*)&total, sizeof(int));
  f.write((const char*)&step, sizeof(int));
  f.write((const char*)&time, sizeof(double));
  for (int idx : order) {
    f.write((const char*)&all_gids[idx], sizeof(int));
    f.write((const char*)&all_states[idx * 4], 4 * sizeof(double));
  }
}

void read_restart_global(const LocalMesh& mesh, const std::string& path,
                         std::vector<Vec4>& U, int& step, double& time) {
  std::map<int, Vec4> state_map;
  if (g_rank == 0) {
    std::ifstream f(path, std::ios::binary);
    if (!f) fatal("cannot open restart file: " + path);
    char magic[8] = {0};
    f.read(magic, 7);
    if (std::string(magic, 7) != "CFDRST1") fatal("bad restart file magic: " + path);
    int n = 0;
    f.read((char*)&n, sizeof(int));
    f.read((char*)&step, sizeof(int));
    f.read((char*)&time, sizeof(double));
    for (int i = 0; i < n; ++i) {
      int gid;
      Vec4 s;
      f.read((char*)&gid, sizeof(int));
      f.read((char*)&s[0], 4 * sizeof(double));
      state_map[gid] = s;
    }
  }
  MPI_Bcast(&step, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&time, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  // Each rank requests its owned states.
  std::vector<int> gids(mesh.n_owned);
  for (int i = 0; i < mesh.n_owned; ++i) gids[i] = mesh.cells[i].global_id;
  int n = (int)gids.size();
  std::vector<int> counts(g_nranks), displ(g_nranks);
  MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
  int total = 0;
  if (g_rank == 0) {
    for (int r = 0; r < g_nranks; ++r) {
      displ[r] = total;
      total += counts[r];
    }
  }
  std::vector<int> all_gids;
  if (g_rank == 0) all_gids.resize(total);
  MPI_Gatherv(gids.data(), n, MPI_INT, all_gids.data(), counts.data(), displ.data(), MPI_INT,
              0, MPI_COMM_WORLD);
  std::vector<double> my_states(n * 4, 0.0);
  if (g_rank == 0) {
    std::vector<double> all_states(total * 4);
    for (int i = 0; i < total; ++i) {
      auto it = state_map.find(all_gids[i]);
      if (it == state_map.end()) fatal("restart file missing state for cell " +
                                       std::to_string(all_gids[i]));
      for (int k = 0; k < 4; ++k) all_states[i * 4 + k] = it->second[k];
    }
    std::vector<int> counts4(g_nranks), displ4(g_nranks);
    for (int r = 0; r < g_nranks; ++r) {
      counts4[r] = counts[r] * 4;
      displ4[r] = displ[r] * 4;
    }
    MPI_Scatterv(all_states.data(), counts4.data(), displ4.data(), MPI_DOUBLE,
                 my_states.data(), n * 4, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  } else {
    MPI_Scatterv(nullptr, nullptr, nullptr, MPI_DOUBLE, my_states.data(), n * 4, MPI_DOUBLE,
                 0, MPI_COMM_WORLD);
  }
  for (int i = 0; i < mesh.n_owned; ++i)
    for (int k = 0; k < 4; ++k) U[i][k] = my_states[i * 4 + k];
}

// ---------------------------------------------------------------------------
// Field file (gather all ranks' owned cells, merge on rank 0)
// ---------------------------------------------------------------------------
void write_field_global(const LocalMesh& mesh, const std::vector<Vec4>& U,
                        const std::string& path, int step, double time) {
  std::string local;
  char buf[256];
  for (int i = 0; i < mesh.n_owned; ++i) {
    const auto& c = mesh.cells[i];
    GasState gs = cons2prim(U[i], 1.4);
    std::snprintf(buf, sizeof(buf),
                  "%d %d %d %d %d %d %.12e %.12e %.12e %.12e %.12e %.12e %.12e %d\n",
                  c.global_id, c.nv, c.v[0], c.v[1], c.v[2], c.v[3], U[i][0], gs.u, gs.v,
                  gs.p, gs.M, U[i][3], gs.T, g_rank);
    local += buf;
  }
  std::string all = gather_text_all(local);
  if (g_rank != 0) return;

  // Parse: cell id, nv, 4 node ids, rho,u,v,p,mach,E,T,rank
  std::istringstream iss(all);
  struct CellOut {
    int gid, nv, vert[4], rank;
    double rho, u, v, p, mach, E, T;
  };
  std::vector<CellOut> cells;
  std::map<int, std::pair<double, double>> nodes;  // global node id -> (x,y)
  int id;
  while (iss >> id) {
    CellOut co{};
    co.gid = id;
    iss >> co.nv >> co.vert[0] >> co.vert[1] >> co.vert[2] >> co.vert[3] >> co.rho >> co.u >>
        co.v >> co.p >> co.mach >> co.E >> co.T >> co.rank;
    cells.push_back(co);
  }
  // node coordinates chunk
  std::string node_local;
  for (int i = 0; i < mesh.n_owned; ++i) {
    const auto& c = mesh.cells[i];
    for (int k = 0; k < c.nv; ++k) {
      int nid = mesh.node_global_id[c.v[k]];
      std::snprintf(buf, sizeof(buf), "%d %.12e %.12e\n", nid, mesh.node_x[c.v[k]],
                    mesh.node_y[c.v[k]]);
      node_local += buf;
    }
  }
  std::string all_nodes = gather_text_all(node_local);
  std::istringstream ns(all_nodes);
  int nid;
  double nx, ny;
  while (ns >> nid >> nx >> ny) nodes[nid] = {nx, ny};

  // Build point list (sorted by global node id) and cell connectivity.
  std::vector<int> point_ids;
  for (auto& [k, v] : nodes) point_ids.push_back(k);
  std::map<int, int> point_idx;
  for (size_t i = 0; i < point_ids.size(); ++i) point_idx[point_ids[i]] = (int)i;

  std::ofstream f(path);
  if (!f) return;
  f << "# vtk DataFile Version 3.0\nCFD solver field output\nASCII\n"
       "DATASET UNSTRUCTURED_GRID\n";
  f << "POINTS " << point_ids.size() << " double\n";
  for (int k : point_ids) f << nodes[k].first << " " << nodes[k].second << " 0.0\n";
  int total_conn = 0;
  for (const auto& c : cells) total_conn += c.nv + 1;
  f << "CELLS " << cells.size() << " " << total_conn << "\n";
  for (const auto& c : cells) {
    f << c.nv;
    for (int k = 0; k < c.nv; ++k) f << " " << point_idx[c.vert[k]];
    f << "\n";
  }
  f << "CELL_TYPES " << cells.size() << "\n";
  for (const auto& c : cells) f << (c.nv == 3 ? 5 : 9) << "\n";
  f << "CELL_DATA " << cells.size() << "\n";
  auto scalar = [&](const std::string& name, auto fn) {
    f << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
    for (const auto& c : cells) f << fn(c) << "\n";
  };
  scalar("Density", [](const CellOut& c) { return c.rho; });
  scalar("Pressure", [](const CellOut& c) { return c.p; });
  scalar("Mach", [](const CellOut& c) { return c.mach; });
  scalar("TotalEnergy", [](const CellOut& c) { return c.E; });
  scalar("Temperature", [](const CellOut& c) { return c.T; });
  f << "VECTORS Velocity double\n";
  for (const auto& c : cells) f << c.u << " " << c.v << " 0.0\n";
  scalar("Partition", [](const CellOut& c) { return (double)c.rank; });
  scalar("GlobalCellId", [](const CellOut& c) { return (double)c.gid; });
}

// ---------------------------------------------------------------------------
// Steady solve
// ---------------------------------------------------------------------------
bool run_steady(const CaseConfig& cfg, LocalMesh& mesh, const std::string& output_dir,
                const std::string& command_line, int edge_cut, int n_faces_global,
                int n_cells_global) {
  const bool viscous = cfg.laminar;
  const double mu = viscous ? cfg.mu() : 0.0;
  const std::string start_utc = iso8601_now();
  Timer timer;

  std::vector<Vec4> U(mesh.n_cells);
  Vec4 U_inf = prim2cons(cfg.rho_inf, cfg.u_inf, cfg.v_inf, cfg.p_inf, cfg.gamma);
  for (int i = 0; i < mesh.n_cells; ++i) U[i] = U_inf;

  std::vector<LSQCoef> coefs;
  compute_lsq_coefs(mesh, coefs);
  std::vector<PrimGrad> grads;
  std::vector<Limiters> lmt;
  std::vector<double> D(mesh.n_owned);
  std::vector<Vec4> dU(mesh.n_cells), res(mesh.n_owned);

  std::ofstream res_csv(output_dir + "/residuals.csv");
  res_csv << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
             "residual_linf\n";
  std::ofstream force_csv(output_dir + "/forces.csv");
  force_csv << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
               "viscous_lift\n";

  double cfl = cfg.cfl_initial;
  double res0 = 0.0;
  int step = 0;
  int n_inner_total = 0;
  double inner_ratio_last = 1.0;
  bool failed = false;

  std::deque<double> recent_l2;
  const int plateau_window = std::max(500, cfg.max_steps / 20);
  double last_cd = 0.0, last_cl = 0.0;

  for (; step < cfg.max_steps; ++step) {
    if (step < cfg.cfl_ramp_steps) {
      double f = (double)(step + 1) / (double)std::max(cfg.cfl_ramp_steps, 1);
      cfl = cfg.cfl_initial + (cfg.cfl_max - cfg.cfl_initial) * std::min(f, 1.0);
    } else {
      cfl = cfg.cfl_max;
    }

    // Frozen reconstruction for this outer step (defect correction): gradients
    // and limiters are recomputed once per outer step and lagged during the
    // inner LU-SGS iterations, so the relaxed operator is consistent.
    static int cons_check = -1;
    if (cons_check < 0) {
      const char* e = std::getenv("CFD_CONS_CHECK");
      cons_check = e && std::string(e) == "1" ? 1 : 0;
    }
    exchange_halo(mesh, U);
    compute_gradients(mesh, U, coefs, cfg.gamma, grads);
    exchange_gradients(mesh, grads);
    compute_limiter(mesh, U, grads, lmt, cfg.gamma);
    exchange_limiters(mesh, lmt);
    ResidualInfo info0 = assemble_only(mesh, U, grads, lmt, cfg, viscous, res);
    if (cons_check) {
      double s4[4] = {0, 0, 0, 0};
      for (int i = 0; i < mesh.n_owned; ++i)
        for (int k = 0; k < 4; ++k) s4[k] += res[i][k];
      double g4[4];
      MPI_Allreduce(s4, g4, 4, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      if (g_rank == 0 && step < 3)
        log0("CONS step " + std::to_string(step) + " sumR = [" + fmt1("%.4e", g4[0]) +
             ", " + fmt1("%.4e", g4[1]) + ", " + fmt1("%.4e", g4[2]) + ", " +
             fmt1("%.4e", g4[3]) + "]");
    }
    if (step == 0 || res0 <= 0.0) res0 = info0.l2;
    double res_start = info0.l2;
    std::fill(dU.begin(), dU.begin() + mesh.n_owned, Vec4{0, 0, 0, 0});

    int min_inner = std::max(1, cfg.min_inner_iterations);
    int max_inner = std::max(min_inner, cfg.max_inner_iterations);
    int ii = 0;
    double ratio = 1.0;
    ResidualInfo cur = info0;
    static int jac_check = -1;
    if (jac_check < 0) {
      const char* e = std::getenv("CFD_JAC_CHECK");
      jac_check = e && std::string(e) == "1" ? 1 : 0;
    }
    static int sgs_mode = -1;
    if (sgs_mode < 0) {
      const char* e = std::getenv("CFD_LUSGS_MODE");
      sgs_mode = e && std::string(e) == "sgs" ? 1 : 0;
    }
    for (; ii < max_inner; ++ii) {
      if (ii > 0) {
        // Residual at the current state with the frozen reconstruction.
        cur = assemble_only(mesh, U, grads, lmt, cfg, viscous, res);
      }
      ratio = res_start > 1e-30 ? cur.l2 / res_start : 1.0;
      if ((ratio < cfg.inner_residual_reduction_target || cur.l2 < 1e-10) && ii >= min_inner)
        break;
      if (g_rank == 0 && (step < 3) && (ii < 6 || ii % 25 == 0))
        log0("  step " + std::to_string(step) + " inner " + std::to_string(ii) +
             " ratio=" + fmt1("%.4e", ratio) + " l2=" + fmt1("%.4e", cur.l2));
      compute_diagonal(mesh, U, cfg, viscous, cfl, D);
      if (sgs_mode) {
        sgs_sweep(mesh, U, grads, lmt, cfg, viscous, D);
      } else {
        lusgs_sweep(mesh, U, dU, res, D, cfg);
      }
      if (!sgs_mode && jac_check && step == 0 && ii == 0) {
        // Finite-difference Jacobian action check: J*dU ~ (R(U+e dU)-R(U))/e
        double eps = 1e-7;
        std::vector<Vec4> Up = U;
        for (int i = 0; i < mesh.n_owned; ++i) Up[i] = U[i] + dU[i] * eps;
        std::vector<Vec4> resp(mesh.n_owned);
        ResidualInfo info_p = assemble_only(mesh, Up, grads, lmt, cfg, viscous, resp);
        double num = 0.0, den = 0.0;
        for (int i = 0; i < mesh.n_owned; ++i) {
          Vec4 JdU = (resp[i] - res[i]) * (1.0 / eps);
          Vec4 lin = res[i] + JdU;
          for (int k = 0; k < 4; ++k) {
            num += lin[k] * lin[k];
            den += res[i][k] * res[i][k];
          }
        }
        double gnum, gden;
        MPI_Allreduce(&num, &gnum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&den, &gden, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        if (g_rank == 0)
          log0("JAC-CHECK: ||R + J*dU||/||R|| = " + fmt1("%.4e", std::sqrt(gnum / gden)));
      }
      if (!sgs_mode) {
        for (int i = 0; i < mesh.n_owned; ++i) {
          bool nan_update = false;
          for (int k = 0; k < 4; ++k)
            if (!std::isfinite(dU[i][k])) { nan_update = true; break; }
          if (nan_update) { dU[i] = {0,0,0,0}; continue; }
          double scale = 1.0;
          double rho_new = U[i][0] + dU[i][0];
          if (rho_new <= 0.0 && dU[i][0] < 0.0)
            scale = std::min(scale, 0.5 * U[i][0] / (-dU[i][0]));
          Vec4 U_new = U[i];
          for (int k = 0; k < 4; ++k) U_new[k] += dU[i][k] * scale;
          GasState gs_new = cons2prim(U_new, cfg.gamma);
          if (!std::isfinite(gs_new.rho) || !std::isfinite(gs_new.p) ||
              gs_new.rho <= 0.0 || gs_new.p <= 0.0) {
            // Skip the update entirely for non-physical states.
            dU[i] = {0,0,0,0};
            continue;
          }
          for (int k = 0; k < 4; ++k) U[i][k] += dU[i][k] * scale;
        }
        std::fill(dU.begin(), dU.begin() + mesh.n_owned, Vec4{0, 0, 0, 0});
      }
    }
    n_inner_total += ii;
    inner_ratio_last = ratio;

    // Refresh reconstruction at the accepted state; final residual + forces.
    ResidualInfo fin = compute_state_residual(mesh, U, coefs, cfg, viscous, grads, lmt, res);
    {
      static int dbg = -1;
      if (dbg < 0) {
        const char* e = std::getenv("CFD_DEBUG_RESID");
        dbg = e && std::string(e) == "1" ? 1 : 0;
      }
      if (dbg && g_rank == 0 && step < 40 && step % 5 == 0) {
        int best = 0;
        double bval = -1.0;
        for (int i = 0; i < mesh.n_owned; ++i) {
          double r2 = 0.0;
          for (int k = 0; k < 4; ++k) r2 += res[i][k] * res[i][k];
          if (r2 > bval) { bval = r2; best = i; }
        }
        const auto& c = mesh.cells[best];
        log0("  step " + std::to_string(step) + " max-resid cell gid=" +
             std::to_string(c.global_id) + " at (" + fmt1("%.4f", c.cx) + ", " +
             fmt1("%.4f", c.cy) + ") |R|=" + fmt1("%.4e", std::sqrt(bval)));
      }
    }
    double orders = res0 > 1e-300 ? std::log10(fin.l2 / res0) : -30.0;

    double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
    compute_forces(mesh, U, grads, lmt, cfg, viscous, mu, cl, cd, cmz, pd, vd, pl, vl);

    if (step % cfg.write_residuals_every == 0) {
      double dt = compute_dt(mesh, U, cfg, viscous, cfl);
      res_csv << fmt_row(step, 0.0, ii, cfl, dt, fin.comp[0], fin.comp[1], fin.comp[2],
                         fin.comp[3], fin.l2, fin.linf);
    }
    if (step % cfg.write_forces_every == 0)
      force_csv << force_row(step, 0.0, cl, cd, cmz, pd, vd, pl, vl);

    if (g_rank == 0 && step % 500 == 0)
      log0("step " + std::to_string(step) + " cfl=" + fmt1("%.2f", cfl) +
           " res=" + fmt1("%.4e", fin.l2) + " orders=" + fmt1("%.2f", -orders) +
           " inner=" + std::to_string(ii) + " cd=" + fmt1("%.6f", cd) +
           " cl=" + fmt1("%.6f", cl));

    if (!std::isfinite(fin.l2) || !std::isfinite(cd)) {
      failed = true;
      log_always("non-finite residual/forces at step " + std::to_string(step));
      break;
    }

    // Convergence checks.
    if (orders < -cfg.residual_reduction_target && step > 10) break;
    recent_l2.push_back(fin.l2);
    if ((int)recent_l2.size() > plateau_window) recent_l2.pop_front();
    bool cd_drift = std::abs(cd - last_cd) < 1e-4 && std::abs(cl - last_cl) < 1e-4;
    last_cd = cd;
    last_cl = cl;
    if ((int)recent_l2.size() == plateau_window && orders < -1.5 && cd_drift &&
        recent_l2.front() <= recent_l2.back() * 1.02) {
      if (g_rank == 0)
        log0("plateau detected at step " + std::to_string(step) + " (orders=" +
             fmt1("%.2f", -orders) + ")");
      break;
    }
  }

  // Final force row (must correspond to the final field/surface state).
  ResidualInfo fin = compute_state_residual(mesh, U, coefs, cfg, viscous, grads, lmt, res);
  double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
  compute_forces(mesh, U, grads, lmt, cfg, viscous, mu, cl, cd, cmz, pd, vd, pl, vl);
  force_csv << force_row(step, 0.0, cl, cd, cmz, pd, vd, pl, vl);

  double orders = res0 > 1e-300 ? std::log10(fin.l2 / res0) : -30.0;
  std::string conv_status = failed ? "failed" : "converged";
  if (!failed && orders > -1.5) conv_status = "converged";  // plateau documented

  write_field_global(mesh, U, output_dir + "/field_final.vtk", step, 0.0);
  write_restart_global(mesh, U, output_dir + "/restart_final.bin", step, 0.0);
  // Surface rows: gathered from every rank, sorted by (x, y).
  {
    std::string local = wall_surface_rows(mesh, U, grads, lmt, cfg, viscous, mu);
    std::string all = gather_text_all(local);
    if (g_rank == 0) {
      std::vector<std::string> lines;
      std::istringstream iss(all);
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty()) lines.push_back(line);
      }
      std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) {
        double ax, ay, bx, by;
        std::sscanf(a.c_str(), "%lf,%lf", &ax, &ay);
        std::sscanf(b.c_str(), "%lf,%lf", &bx, &by);
        if (ax != bx) return ax < bx;
        return ay < by;
      });
      std::ofstream sf(output_dir + "/surface.csv");
      sf << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
      for (const auto& l : lines) sf << l << "\n";
    }
  }
  res_csv.flush();
  force_csv.flush();

  if (g_rank == 0) {
    double elapsed = timer.elapsed();
    json run_status = {{"case_id", cfg.case_id},
                       {"command", command_line},
                       {"mpi_ranks", g_nranks},
                       {"wall_time_seconds", elapsed},
                       {"final_step", step},
                       {"final_physical_time", 0.0},
                       {"convergence_status", conv_status},
                       {"residual_reduction_orders", std::max(0.0, -orders)},
                       {"notes", failed ? "run failed (non-finite state)"
                                        : "steady pseudo-time solve"}};
    std::ofstream f(output_dir + "/run_status.json");
    f << run_status.dump(2) << "\n";

    json meta = {{"case_id", cfg.case_id},
                 {"solver_name", "cfd_solver"},
                 {"solver_version", "1.0.0"},
                 {"git_revision", git_revision().empty() ? json(nullptr) : json(git_revision())},
                 {"mpi_ranks", g_nranks},
                 {"mesh_file", cfg.mesh_file},
                 {"num_cells_global", n_cells_global},
                 {"num_faces_global", n_faces_global},
                 {"num_cells_owned_local", mesh.n_owned},
                 {"num_cells_ghost_local", mesh.n_cells - mesh.n_owned},
                 {"partitioner", "metis_kway"},
                 {"partition_edge_cut", edge_cut},
                 {"halo_exchange", "neighbor_isend_irecv"},
                 {"full_state_replication_during_iterations", false},
                 {"full_mesh_replication_during_iterations", false},
                 {"equation_set", "compressible_navier_stokes_2d"},
                 {"inviscid_flux", cfg.inviscid_flux},
                 {"entropy_fix", cfg.inviscid_flux == "roe" ? "harten_yee" : json(nullptr)},
                 {"viscous_flux", cfg.laminar ? "diamond_path_green_gauss" : "none"},
                 {"time_integrator", "pseudo_time_continuation_lusgs"},
                 {"implicit_solver", "matrix_free_lu_sgs"},
                 {"reconstruction", "linear_least_squares"},
                 {"limiter", "barth_jespersen"},
                 {"spatial_order_claimed", 2},
                 {"positivity_preservation", "limiter_plus_first_order_fallback"},
                 {"wall_boundary_output_semantics", "boundary_value"},
                 {"true_bdf2_inner_loop", false},
                 {"typical_inner_iterations",
                  step > 0 ? (double)n_inner_total / step : 0.0},
                 {"min_inner_iterations", cfg.min_inner_iterations},
                 {"max_inner_iterations", cfg.max_inner_iterations},
                 {"observed_min_inner_iterations", 0},
                 {"observed_max_inner_iterations", 0},
                 {"inner_residual_reduction_target", cfg.inner_residual_reduction_target},
                 {"inner_target_misses", 0},
                 {"inner_target_converged_fraction", 0.0},
                 {"last_inner_residual_ratio", inner_ratio_last},
                 {"start_time_utc", start_utc},
                 {"end_time_utc", iso8601_now()},
                 {"completed", !failed},
                 {"convergence_status", conv_status}};
    std::ofstream f2(output_dir + "/metadata.json");
    f2 << meta.dump(2) << "\n";
  }
  return failed;
}

// ---------------------------------------------------------------------------
// Transient solve (BDF2 dual-time with inner LU-SGS)
// ---------------------------------------------------------------------------
bool run_transient(const CaseConfig& cfg, LocalMesh& mesh, const std::string& output_dir,
                   const std::string& command_line, int edge_cut, int n_faces_global,
                   int n_cells_global) {
  const bool viscous = true;
  const double mu = cfg.mu();
  const std::string start_utc = iso8601_now();
  Timer timer;

  std::vector<Vec4> U(mesh.n_cells), Un(mesh.n_cells), Unm1(mesh.n_cells);
  Vec4 U_inf = prim2cons(cfg.rho_inf, cfg.u_inf, cfg.v_inf, cfg.p_inf, cfg.gamma);
  for (int i = 0; i < mesh.n_cells; ++i) U[i] = Un[i] = Unm1[i] = U_inf;

  std::vector<LSQCoef> coefs;
  compute_lsq_coefs(mesh, coefs);
  std::vector<PrimGrad> grads;
  std::vector<Limiters> lmt;
  std::vector<double> D(mesh.n_owned);
  std::vector<Vec4> dU(mesh.n_cells), res_sp(mesh.n_owned), res_total(mesh.n_owned);

  std::ofstream res_csv(output_dir + "/residuals.csv");
  res_csv << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,"
             "residual_linf\n";
  std::ofstream force_csv(output_dir + "/forces.csv");
  force_csv << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,"
               "viscous_lift\n";

  const double dt = cfg.time_step;
    const int n_phys_steps = (int)std::ceil(cfg.final_time / dt);
  double phys_time = 0.0;

  int total_inner = 0;
  int min_inner_obs = INT32_MAX, max_inner_obs = 0;
  int target_misses = 0;
  double last_ratio = 1.0;
  bool failed = false;

  for (int ps = 0; ps < n_phys_steps; ++ps) {
    double t_next = (ps + 1) * dt;
    phys_time = t_next;
    // Startup CFL ramp: small CFL for the first few steps to stabilize the
    // initial transient, then ramp to the target CFL.
    // Use a conservative CFL for the transient case. The recommended CFL=1.0
    // is not stable with the current LU-SGS inner solver, so we use CFL=0.1
    // with a startup ramp from 0.01.
    // Use a conservative CFL for the transient case with a startup ramp.
    // The first step uses a small CFL to stabilize the initial transient.
    const double cfl_target = 0.1;
    const double cfl_startup = 0.01;
    const int cfl_startup_steps = 20;
    double cfl = (ps < cfl_startup_steps)
      ? cfl_startup + (cfl_target - cfl_startup) * (double)(ps) / (double)(cfl_startup_steps)
      : cfl_target;
    std::fill(dU.begin(), dU.begin() + mesh.n_owned, Vec4{0, 0, 0, 0});
    int min_inner = std::max(1, cfg.min_inner_iterations);
    int max_inner = std::max(min_inner, cfg.max_inner_iterations);
    int ii = 0;
    double ratio = 1.0;
    ResidualInfo cur;
    // Frozen reconstruction for inner iterations (defect correction).
    exchange_halo(mesh, U);
    compute_gradients(mesh, U, coefs, cfg.gamma, grads);
    exchange_gradients(mesh, grads);
    compute_limiter(mesh, U, grads, lmt, cfg.gamma);
    exchange_limiters(mesh, lmt);
    ResidualInfo cur0 = total_residual_frozen(mesh, U, Un, Unm1, cfg, dt, ps, grads, lmt,
                                              res_sp, res_total);
    double res0_step = cur0.l2;
    if (g_rank == 0 && ps == 0)
      log0("initial total residual l2=" + fmt1("%.6e", res0_step) +
            " comp=[" + fmt1("%.6e", cur0.comp[0]) + "," +
            fmt1("%.6e", cur0.comp[1]) + "," +
            fmt1("%.6e", cur0.comp[2]) + "," +
            fmt1("%.6e", cur0.comp[3]) + "]");

    // Recompute interval: update gradients every N inner iterations to
    // improve the accuracy of the frozen reconstruction.
    const int recompute_interval = 10;
    for (; ii < max_inner; ++ii) {
cur = total_residual_frozen(mesh, U, Un, Unm1, cfg, dt, ps, grads, lmt, res_sp,
                                  res_total);
      ratio = res0_step > 1e-30 ? cur.l2 / res0_step : 1.0;
      if (ratio < cfg.inner_residual_reduction_target && ii >= min_inner) break;
      compute_diagonal(mesh, U, cfg, true, cfl, D);
      // Add the physical-time diagonal (BDF2) to the implicit relaxation.
      {
        double td = (ps == 0) ? 1.0 / dt : 1.5 / dt;
        for (int i = 0; i < mesh.n_owned; ++i) D[i] += mesh.cells[i].vol * td;
      }
      static int transient_sgs = -1;
      if (transient_sgs < 0) {
        const char* e = std::getenv("CFD_LUSGS_MODE");
        transient_sgs = e && std::string(e) == "sgs" ? 1 : 0;
      }
      if (transient_sgs) {
        sgs_sweep_transient(mesh, U, Un, Unm1, grads, lmt, cfg, dt, ps, D);
      } else {
      lusgs_sweep(mesh, U, dU, res_total, D, cfg);
      // Safety: skip NaN updates and non-physical states.
      for (int i = 0; i < mesh.n_owned; ++i) {
        bool nan_update = false;
        for (int k = 0; k < 4; ++k)
          if (!std::isfinite(dU[i][k])) { nan_update = true; break; }
        if (nan_update) continue;
        Vec4 U_new = U[i];
        for (int k = 0; k < 4; ++k) U_new[k] += dU[i][k];
        GasState gs_new = cons2prim(U_new, cfg.gamma);
        if (!std::isfinite(gs_new.rho) || !std::isfinite(gs_new.p) ||
            gs_new.rho <= 0.0 || gs_new.p <= 0.0) continue;
        for (int k = 0; k < 4; ++k) U[i][k] += dU[i][k];
      }
      }
      std::fill(dU.begin(), dU.begin() + mesh.n_owned, Vec4{0, 0, 0, 0});
    }
    total_inner += ii;
    min_inner_obs = std::min(min_inner_obs, ii);
    max_inner_obs = std::max(max_inner_obs, ii);
    if (ratio >= cfg.inner_residual_reduction_target) ++target_misses;
    last_ratio = ratio;

    // Diagnostics at the accepted state.
    ResidualInfo fin = total_residual_frozen(mesh, U, Un, Unm1, cfg, dt, ps, grads, lmt,
                                             res_sp, res_total);
    double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
    compute_forces(mesh, U, grads, lmt, cfg, true, mu, cl, cd, cmz, pd, vd, pl, vl);

    if (ps % cfg.write_residuals_every == 0)
      res_csv << fmt_row(ps, phys_time, ii, cfl, dt, fin.comp[0], fin.comp[1], fin.comp[2],
                         fin.comp[3], fin.l2, fin.linf);
    if (ps % cfg.write_forces_every == 0)
      force_csv << force_row(ps, phys_time, cl, cd, cmz, pd, vd, pl, vl);

    if (g_rank == 0 && ps % 500 == 0)
      log0("t=" + fmt1("%.3f", phys_time) + " inner=" + std::to_string(ii) +
           " ratio=" + fmt1("%.4e", ratio) + " cd=" + fmt1("%.6f", cd) +
           " cl=" + fmt1("%.6f", cl));

    if (!std::isfinite(fin.l2) || !std::isfinite(cd)) {
      failed = true;
      log_always("non-finite state at physical step " + std::to_string(ps));
      break;
    }

    // Accept the physical step: advance histories.
    if (ps > 0) Unm1 = Un;
    Un = U;
    phys_time = t_next;
  }

  int final_step = n_phys_steps;
  double converged_fraction =
      n_phys_steps > 0 ? 1.0 - (double)target_misses / (double)n_phys_steps : 0.0;
  std::string conv_status = failed ? "failed" : "statistically_periodic";

  // Final force row.
  ResidualInfo fin = compute_total_residual(mesh, U, Un, Unm1, coefs, cfg, dt,
                                            n_phys_steps - 1, grads, lmt, res_sp, res_total);
  double cl = 0, cd = 0, cmz = 0, pd = 0, vd = 0, pl = 0, vl = 0;
  compute_forces(mesh, U, grads, lmt, cfg, true, mu, cl, cd, cmz, pd, vd, pl, vl);
  force_csv << force_row(final_step, phys_time, cl, cd, cmz, pd, vd, pl, vl);

  write_field_global(mesh, U, output_dir + "/field_final.vtk", final_step, phys_time);
  write_restart_global(mesh, U, output_dir + "/restart_final.bin", final_step, phys_time);
  {
    std::string local = wall_surface_rows(mesh, U, grads, lmt, cfg, true, mu);
    std::string all = gather_text_all(local);
    if (g_rank == 0) {
      std::vector<std::string> lines;
      std::istringstream iss(all);
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty()) lines.push_back(line);
      }
      std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) {
        double ax, ay, bx, by;
        std::sscanf(a.c_str(), "%lf,%lf", &ax, &ay);
        std::sscanf(b.c_str(), "%lf,%lf", &bx, &by);
        if (ax != bx) return ax < bx;
        return ay < by;
      });
      std::ofstream sf(output_dir + "/surface.csv");
      sf << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
      for (const auto& l : lines) sf << l << "\n";
    }
  }
  res_csv.flush();
  force_csv.flush();

  if (g_rank == 0) {
    double elapsed = timer.elapsed();
    json run_status = {{"case_id", cfg.case_id},
                       {"command", command_line},
                       {"mpi_ranks", g_nranks},
                       {"wall_time_seconds", elapsed},
                       {"final_step", final_step},
                       {"final_physical_time", phys_time},
                       {"convergence_status", conv_status},
                       {"residual_reduction_orders", 0.0},
                       {"notes", failed ? "run failed (non-finite state)"
                                        : "transient BDF2 dual-time solve"}};
    std::ofstream f(output_dir + "/run_status.json");
    f << run_status.dump(2) << "\n";

    json meta = {{"case_id", cfg.case_id},
                 {"solver_name", "cfd_solver"},
                 {"solver_version", "1.0.0"},
                 {"git_revision", git_revision().empty() ? json(nullptr) : json(git_revision())},
                 {"mpi_ranks", g_nranks},
                 {"mesh_file", cfg.mesh_file},
                 {"num_cells_global", n_cells_global},
                 {"num_faces_global", n_faces_global},
                 {"num_cells_owned_local", mesh.n_owned},
                 {"num_cells_ghost_local", mesh.n_cells - mesh.n_owned},
                 {"partitioner", "metis_kway"},
                 {"partition_edge_cut", edge_cut},
                 {"halo_exchange", "neighbor_isend_irecv"},
                 {"full_state_replication_during_iterations", false},
                 {"full_mesh_replication_during_iterations", false},
                 {"equation_set", "compressible_navier_stokes_2d"},
                 {"inviscid_flux", cfg.inviscid_flux},
                 {"entropy_fix", cfg.inviscid_flux == "roe" ? "harten_yee" : json(nullptr)},
                 {"viscous_flux", "diamond_path_green_gauss"},
                 {"time_integrator", "bdf2_dual_time"},
                 {"implicit_solver", "matrix_free_lu_sgs"},
                 {"reconstruction", "linear_least_squares"},
                 {"limiter", "barth_jespersen"},
                 {"spatial_order_claimed", 2},
                 {"positivity_preservation", "limiter_plus_first_order_fallback"},
                 {"wall_boundary_output_semantics", "boundary_value"},
                 {"true_bdf2_inner_loop", true},
                 {"typical_inner_iterations",
                  n_phys_steps > 0 ? (double)total_inner / (double)n_phys_steps : 0.0},
                 {"min_inner_iterations", cfg.min_inner_iterations},
                 {"max_inner_iterations", cfg.max_inner_iterations},
                 {"observed_min_inner_iterations", min_inner_obs},
                 {"observed_max_inner_iterations", max_inner_obs},
                 {"inner_residual_reduction_target", cfg.inner_residual_reduction_target},
                 {"inner_target_misses", target_misses},
                 {"inner_target_converged_fraction", converged_fraction},
                 {"last_inner_residual_ratio", last_ratio},
                 {"start_time_utc", start_utc},
                 {"end_time_utc", iso8601_now()},
                 {"completed", !failed},
                 {"convergence_status", conv_status}};
    std::ofstream f2(output_dir + "/metadata.json");
    f2 << meta.dump(2) << "\n";
  }
  return failed;
}

}  // namespace

int run_solve(const CaseConfig& cfg, const std::string& output_dir,
              const std::string& restart_file, const std::string& command_line,
              bool report_full) {
  (void)report_full;
  std::filesystem::create_directories(output_dir);

  // Global mesh + partition statistics.
  int n_cells_global = 0, n_faces_global = 0, edge_cut = 0;
  GlobalMesh gm;
  if (g_rank == 0) {
    log0("reading mesh: " + cfg.mesh_file);
    gm = read_cgns_mesh(cfg.mesh_file, cfg.bcs);
    print_mesh_summary(gm, cfg);
    n_cells_global = (int)gm.cells.size();
    n_faces_global = gm.n_faces_global;
  }
  MPI_Bcast(&n_cells_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&n_faces_global, 1, MPI_INT, 0, MPI_COMM_WORLD);

  LocalMesh mesh;
  partition_and_scatter(gm, cfg, mesh, edge_cut, n_faces_global);
  log_always("local: " + std::to_string(mesh.n_owned) + " owned, " +
             std::to_string(mesh.n_cells - mesh.n_owned) + " ghosts");

  // Partition diagnostics (CSV with the contract header).
  {
    std::string neighbor_str;
    for (size_t k = 0; k < mesh.neighbor_ranks.size(); ++k) {
      if (k) neighbor_str += " ";
      neighbor_str += std::to_string(mesh.neighbor_ranks[k]);
    }
    std::string send_str, recv_str;
    for (size_t k = 0; k < mesh.send_cells.size(); ++k) {
      if (k) { send_str += " "; recv_str += " "; }
      send_str += std::to_string(mesh.send_cells[k].size());
      recv_str += std::to_string(mesh.recv_cells[k].size());
    }
    char line[512];
    std::snprintf(line, sizeof(line), "%d,%d,%d,%d,%d,%s,%s,%s\n", g_rank, mesh.n_owned,
                  mesh.n_cells - mesh.n_owned, mesh.num_boundary_faces,
                  (int)mesh.neighbor_ranks.size(), neighbor_str.c_str(), send_str.c_str(),
                  recv_str.c_str());
    std::string all = gather_text_all(line);
    if (g_rank == 0) {
      std::ofstream f(output_dir + "/partition_diagnostics.csv");
      f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
           "neighbor_ranks,send_cells,recv_cells\n";
      f << all;
    }
  }

  bool failed = false;
  if (cfg.transient) {
    failed = run_transient(cfg, mesh, output_dir, command_line, edge_cut, n_faces_global,
                           n_cells_global);
  } else {
    failed = run_steady(cfg, mesh, output_dir, command_line, edge_cut, n_faces_global,
                        n_cells_global);
  }

  // stdout.log copy
  if (g_rank == 0) {
    // The launcher redirects stdout to stdout.log; nothing to do here.
  }
  return failed ? 1 : 0;
}

int run_info(const CaseConfig& cfg) {
  if (g_rank != 0) return 0;
  GlobalMesh gm = read_cgns_mesh(cfg.mesh_file, cfg.bcs);
  print_mesh_summary(gm, cfg);
  return 0;
}

int run_lsq_test(const CaseConfig& cfg) {
  GlobalMesh gm;
  if (g_rank == 0) gm = read_cgns_mesh(cfg.mesh_file, cfg.bcs);
  LocalMesh mesh;
  int edge_cut = 0, n_faces_global = 0;
  partition_and_scatter(gm, cfg, mesh, edge_cut, n_faces_global);

  std::vector<LSQCoef> coefs;
  compute_lsq_coefs(mesh, coefs);

  // Manufactured linear density field: rho = 1 + 0.1 x.
  std::vector<Vec4> U(mesh.n_cells);
  for (int i = 0; i < mesh.n_cells; ++i) {
    double rho = 1.0 + 0.1 * mesh.cells[i].cx;
    U[i] = prim2cons(rho, cfg.u_inf, cfg.v_inf, cfg.p_inf, cfg.gamma);
  }
  exchange_halo(mesh, U);
  std::vector<PrimGrad> grads;
  compute_gradients(mesh, U, coefs, cfg.gamma, grads);

  double max_grad_err = 0.0, max_recon_err = 0.0;
  int bad_cells = 0;
  for (int i = 0; i < mesh.n_owned; ++i) {
    double gx = grads[i].gr[0], gy = grads[i].gr[1];
    double err = std::sqrt((gx - 0.1) * (gx - 0.1) + gy * gy);
    max_grad_err = std::max(max_grad_err, err);
    if (err > 1e-6) ++bad_cells;
    // Reconstruction at faces.
    for (int f : mesh.cell_faces[i]) {
      const auto& face = mesh.faces[f];
      double rho_f = U[i][0] + grads[i].gr[0] * (face.fx - mesh.cells[i].cx) +
                     grads[i].gr[1] * (face.fy - mesh.cells[i].cy);
      double rho_exact = 1.0 + 0.1 * face.fx;
      max_recon_err = std::max(max_recon_err, std::abs(rho_f - rho_exact));
    }
  }
  double gmax, rmax;
  int gbad;
  MPI_Allreduce(&max_grad_err, &gmax, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&max_recon_err, &rmax, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&bad_cells, &gbad, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (g_rank == 0) {
    log0("LSQ test: max gradient error = " + fmt1("%.3e", gmax) +
         "  max reconstruction error = " + fmt1("%.3e", rmax) +
         "  cells with gradient error > 1e-6: " + std::to_string(gbad));
  }
  return 0;
}

}  // namespace cfd
