#include "cfd/solver.hpp"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "cfd/fvm.hpp"
#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"

namespace fs = std::filesystem;

namespace cfd {

namespace {

int env_limit_steps() {
  const char* raw = std::getenv("CFD_SOLVER_LIMIT_STEPS");
  if (!raw || !*raw) return 0;
  try {
    return std::max(0, std::stoi(raw));
  } catch (...) {
    return 0;
  }
}

int env_limit_inner() {
  const char* raw = std::getenv("CFD_SOLVER_LIMIT_INNER");
  if (!raw || !*raw) return 0;
  try {
    return std::max(0, std::stoi(raw));
  } catch (...) {
    return 0;
  }
}

int env_int(const char* name, int fallback) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return fallback;
  try {
    return std::stoi(raw);
  } catch (...) {
    return fallback;
  }
}

Real env_real(const char* name, Real fallback) {
  const char* raw = std::getenv(name);
  if (!raw || !*raw) return fallback;
  try {
    return std::stod(raw);
  } catch (...) {
    return fallback;
  }
}

Real safe_log10_reduction(Real initial, Real final_value) {
  if (initial <= 0.0 || final_value <= 0.0) return 0.0;
  return std::max<Real>(0.0, std::log10(initial / final_value));
}

std::string boundary_type_for_solver(const CaseConfig& cfg, const std::string& tag) {
  const auto it = cfg.boundary_conditions.find(tag);
  if (it != cfg.boundary_conditions.end()) return it->second;
  if (tag == "UNMARKED" || tag.rfind("con-", 0) == 0) return "farfield";
  return {};
}

void recompute_norms(const LocalMesh& mesh, ResidualResult& rr, MPI_Comm comm) {
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
}

void add_physical_time_residual(const LocalMesh& mesh, const CaseConfig& cfg,
                                const std::vector<Conserved>& state,
                                const std::vector<Conserved>& prev,
                                const std::vector<Conserved>& prevprev, int step,
                                ResidualResult& rr) {
  if (cfg.run_control.type != RunType::Transient) return;
  const Real dt = cfg.run_control.time_step;
  for (int idx : mesh.owned_local_indices) {
    const Real area = mesh.cells[static_cast<std::size_t>(idx)].area;
    const Conserved& u = state[static_cast<std::size_t>(idx)];
    const Conserved& un = prev[static_cast<std::size_t>(idx)];
    const Conserved& unm1 = prevprev[static_cast<std::size_t>(idx)];
    const Real diag_coeff = (step <= 1) ? 1.0 : 1.5;
    rr.spectral_radius[static_cast<std::size_t>(idx)] += diag_coeff * area / dt;
    for (int m = 0; m < 4; ++m) {
      const Real derivative =
          (step <= 1) ? (u[m] - un[m]) / dt : (1.5 * u[m] - 2.0 * un[m] + 0.5 * unm1[m]) / dt;
      rr.residual[static_cast<std::size_t>(idx)][m] += area * derivative;
    }
  }
}

void apply_update(const LocalMesh& mesh, const CaseConfig& cfg, std::vector<Conserved>& state,
                  const ResidualResult& rr, Real cfl, Real relaxation_scale, int step) {
  const bool transient = cfg.run_control.type == RunType::Transient;
  Real transient_omega = env_real("CFD_SOLVER_OMEGA_TRANSIENT", 0.35);
  if (transient && step <= 1) {
    transient_omega = env_real("CFD_SOLVER_OMEGA_TRANSIENT_STARTUP", transient_omega);
  }
  const Real omega =
      relaxation_scale * (transient ? transient_omega : env_real("CFD_SOLVER_OMEGA_STEADY", 0.05));
  const Real smoothing =
      transient ? env_real("CFD_SOLVER_RESIDUAL_SMOOTHING_TRANSIENT", 0.0)
                : env_real("CFD_SOLVER_RESIDUAL_SMOOTHING_STEADY", 0.0);
  std::vector<Conserved> update_residual = rr.residual;
  if (smoothing > 0.0) {
    for (int idx : mesh.owned_local_indices) {
      Conserved smoothed = rr.residual[static_cast<std::size_t>(idx)];
      int count = 0;
      for (int j : mesh.neighbor_cells[static_cast<std::size_t>(idx)]) {
        if (!mesh.cells[static_cast<std::size_t>(j)].owned) continue;
        for (int m = 0; m < 4; ++m) {
          smoothed[m] += smoothing * rr.residual[static_cast<std::size_t>(j)][m];
        }
        ++count;
      }
      const Real denom = 1.0 + smoothing * count;
      for (int m = 0; m < 4; ++m) smoothed[m] /= denom;
      update_residual[static_cast<std::size_t>(idx)] = smoothed;
    }
  }
  for (int idx : mesh.owned_local_indices) {
    const Real base =
        transient ? env_real("CFD_SOLVER_DIAG_BASE_TRANSIENT", 3.0)
                  : env_real("CFD_SOLVER_DIAG_BASE_STEADY", 4.0);
    const Real factor = base + 1.0 / std::max<Real>(cfl, 1.0e-6);
    const Real physical_diag =
        transient ? ((step <= 1 ? 1.0 : 1.5) *
                     mesh.cells[static_cast<std::size_t>(idx)].area /
                     std::max(cfg.run_control.time_step, 1.0e-14))
                  : 0.0;
    const Real spatial_radius =
        std::max<Real>(0.0, rr.spectral_radius[static_cast<std::size_t>(idx)] - physical_diag);
    const Real diag = physical_diag + spatial_radius * factor;
    const Conserved old_u = state[static_cast<std::size_t>(idx)];
    const Primitive old_q = conserved_to_primitive(old_u, cfg.gas);
    Conserved u = old_u;
    for (int m = 0; m < 4; ++m) {
      u[m] -= omega * update_residual[static_cast<std::size_t>(idx)][m] /
              std::max(diag, 1.0e-14);
    }
    Primitive q = conserved_to_primitive(u, cfg.gas);
    const Real rel_cap = env_real("CFD_SOLVER_PRIMITIVE_RELATIVE_CAP", 0.2);
    const Real vel_cap_scale = env_real("CFD_SOLVER_VELOCITY_CAP_SCALE", 0.25);
    q.rho = std::clamp(q.rho, old_q.rho * (1.0 - rel_cap), old_q.rho * (1.0 + rel_cap));
    q.p = std::clamp(q.p, old_q.p * (1.0 - rel_cap), old_q.p * (1.0 + rel_cap));
    const Real old_speed = std::sqrt(old_q.u * old_q.u + old_q.v * old_q.v);
    const Real a = thermo_from_primitive(old_q, cfg.gas).a;
    const Real vel_cap = vel_cap_scale * (a + old_speed + 1.0e-12);
    Vec2 dv{q.u - old_q.u, q.v - old_q.v};
    const Real dv_norm = norm(dv);
    if (dv_norm > vel_cap) {
      dv *= vel_cap / dv_norm;
      q.u = old_q.u + dv.x;
      q.v = old_q.v + dv.y;
    }
    state[static_cast<std::size_t>(idx)] = primitive_to_conserved(q, cfg.gas);
  }
  if (cfg.mode == PhysicsMode::Inviscid &&
      env_int("CFD_SOLVER_ENFORCE_SLIP_CELL_TANGENCY", 0) > 0) {
    for (const LocalFace& f : mesh.faces) {
      if (f.right >= 0) continue;
      if (!mesh.cells[static_cast<std::size_t>(f.left)].owned) continue;
      if (boundary_type_for_solver(cfg, f.tag) != "slip_wall") continue;
      Conserved& u = state[static_cast<std::size_t>(f.left)];
      Primitive q = conserved_to_primitive(u, cfg.gas);
      const Real un = q.u * f.normal.x + q.v * f.normal.y;
      q.u -= un * f.normal.x;
      q.v -= un * f.normal.y;
      u = primitive_to_conserved(q, cfg.gas);
    }
  }
}

bool solve_4x4(Real a[4][5], Conserved& x) {
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    Real pivot_abs = std::abs(a[col][col]);
    for (int row = col + 1; row < 4; ++row) {
      const Real candidate = std::abs(a[row][col]);
      if (candidate > pivot_abs) {
        pivot = row;
        pivot_abs = candidate;
      }
    }
    if (pivot_abs < 1.0e-30 || !std::isfinite(pivot_abs)) return false;
    if (pivot != col) {
      for (int j = col; j < 5; ++j) std::swap(a[col][j], a[pivot][j]);
    }
    const Real inv_pivot = 1.0 / a[col][col];
    for (int j = col; j < 5; ++j) a[col][j] *= inv_pivot;
    for (int row = 0; row < 4; ++row) {
      if (row == col) continue;
      const Real factor = a[row][col];
      for (int j = col; j < 5; ++j) a[row][j] -= factor * a[col][j];
    }
  }
  for (int m = 0; m < 4; ++m) x[m] = a[m][4];
  return true;
}

Conserved capped_update(const Conserved& old_u, const Conserved& proposed_u,
                        const CaseConfig& cfg) {
  const Primitive old_q = conserved_to_primitive(old_u, cfg.gas);
  Primitive q = conserved_to_primitive(proposed_u, cfg.gas);
  const Real rel_cap = env_real("CFD_SOLVER_PRIMITIVE_RELATIVE_CAP", 0.2);
  const Real vel_cap_scale = env_real("CFD_SOLVER_VELOCITY_CAP_SCALE", 0.25);
  q.rho = std::clamp(q.rho, old_q.rho * (1.0 - rel_cap), old_q.rho * (1.0 + rel_cap));
  q.p = std::clamp(q.p, old_q.p * (1.0 - rel_cap), old_q.p * (1.0 + rel_cap));
  const Real old_speed = std::sqrt(old_q.u * old_q.u + old_q.v * old_q.v);
  const Real a = thermo_from_primitive(old_q, cfg.gas).a;
  const Real vel_cap = vel_cap_scale * (a + old_speed + 1.0e-12);
  Vec2 dv{q.u - old_q.u, q.v - old_q.v};
  const Real dv_norm = norm(dv);
  if (dv_norm > vel_cap) {
    dv *= vel_cap / dv_norm;
    q.u = old_q.u + dv.x;
    q.v = old_q.v + dv.y;
  }
  return primitive_to_conserved(q, cfg.gas);
}

void apply_transient_block_jacobi_update(const LocalMesh& mesh, const CaseConfig& cfg,
                                         std::vector<Conserved>& state,
                                         const ResidualResult& rr, int step) {
  const std::vector<Conserved> base_state = state;
  std::vector<Conserved> next_state = state;
  const Real omega = env_real("CFD_SOLVER_TRANSIENT_BLOCK_OMEGA", 0.5);
  const Real fd_scale = std::max<Real>(
      1.0e-10, env_real("CFD_SOLVER_TRANSIENT_BLOCK_FD_SCALE", 1.0e-6));
  const Real diag_reg = std::max<Real>(
      0.0, env_real("CFD_SOLVER_TRANSIENT_BLOCK_DIAG_REG", 1.0e-8));

  for (int idx : mesh.owned_local_indices) {
    const Conserved old_u = base_state[static_cast<std::size_t>(idx)];
    const Conserved spatial0 = compute_cell_residual_first_order(mesh, cfg, base_state, idx);
    const Real area = mesh.cells[static_cast<std::size_t>(idx)].area;
    const Real physical_diag =
        (step <= 1 ? 1.0 : 1.5) * area / std::max(cfg.run_control.time_step, 1.0e-14);
    Real mat[4][5]{};
    for (int col = 0; col < 4; ++col) {
      Conserved perturbed_u = old_u;
      const Real eps = fd_scale * std::max<Real>(1.0, std::abs(old_u[col]));
      perturbed_u[col] += eps;
      const Conserved spatial_p =
          compute_cell_residual_first_order_with_state(mesh, cfg, base_state, idx, perturbed_u);
      for (int row = 0; row < 4; ++row) {
        mat[row][col] = (spatial_p[row] - spatial0[row]) / eps;
      }
      mat[col][col] += physical_diag;
    }
    for (int row = 0; row < 4; ++row) {
      mat[row][row] += diag_reg * std::max<Real>(1.0, physical_diag);
      mat[row][4] = -rr.residual[static_cast<std::size_t>(idx)][row];
    }
    Conserved delta{};
    if (!solve_4x4(mat, delta)) {
      for (int m = 0; m < 4; ++m) {
        delta[m] = -rr.residual[static_cast<std::size_t>(idx)][m] /
                   std::max<Real>(physical_diag, 1.0e-14);
      }
    }
    Conserved proposed = old_u;
    for (int m = 0; m < 4; ++m) proposed[m] += omega * delta[m];
    next_state[static_cast<std::size_t>(idx)] = capped_update(old_u, proposed, cfg);
  }
  state = std::move(next_state);
}

std::string diagnostics_row(const PartitionDiagnostics& d) {
  auto join_ints = [](const std::vector<int>& values) {
    std::ostringstream os;
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (i) os << ' ';
      os << values[i];
    }
    return os.str();
  };
  auto join_map = [](const std::map<int, int>& values) {
    std::ostringstream os;
    std::size_t i = 0;
    for (const auto& [r, n] : values) {
      if (i++) os << ' ';
      os << r << ':' << n;
    }
    return os.str();
  };
  std::ostringstream os;
  os << d.rank << ',' << d.num_cells_owned << ',' << d.num_cells_ghost << ','
     << d.num_boundary_faces << ',' << d.neighbor_ranks.size() << ",\"" << join_ints(d.neighbor_ranks)
     << "\",\"" << join_map(d.send_cells) << "\",\"" << join_map(d.recv_cells) << "\"";
  return os.str();
}

std::vector<std::string> gather_strings(const std::string& local, int root, MPI_Comm comm) {
  int rank = 0;
  int nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);
  const int nlocal = static_cast<int>(local.size());
  std::vector<int> counts(static_cast<std::size_t>(nranks), 0);
  MPI_Gather(&nlocal, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
  std::vector<int> displs(static_cast<std::size_t>(nranks + 1), 0);
  if (rank == root) {
    for (int r = 0; r < nranks; ++r) {
      displs[static_cast<std::size_t>(r + 1)] =
          displs[static_cast<std::size_t>(r)] + counts[static_cast<std::size_t>(r)];
    }
  }
  std::vector<char> recvbuf(rank == root ? static_cast<std::size_t>(displs.back()) : 0);
  MPI_Gatherv(local.data(), nlocal, MPI_CHAR, recvbuf.data(), counts.data(), displs.data(), MPI_CHAR,
              root, comm);

  std::vector<std::string> result;
  if (rank == root) {
    result.reserve(static_cast<std::size_t>(nranks));
    for (int r = 0; r < nranks; ++r) {
      const int b = displs[static_cast<std::size_t>(r)];
      const int e = displs[static_cast<std::size_t>(r + 1)];
      result.emplace_back(recvbuf.data() + b, recvbuf.data() + e);
    }
  }
  return result;
}

std::vector<std::string> split_nonempty_lines(const std::vector<std::string>& chunks) {
  std::vector<std::string> lines;
  for (const std::string& chunk : chunks) {
    std::istringstream in(chunk);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty()) lines.push_back(line);
    }
  }
  return lines;
}

bool force_plateau_reached(const std::deque<ForceResult>& recent, Real abs_tol, Real rel_tol) {
  if (recent.empty()) return false;
  Real min_cd = recent.front().cd;
  Real max_cd = recent.front().cd;
  Real min_cl = recent.front().cl;
  Real max_cl = recent.front().cl;
  Real max_force = 1.0;
  for (const ForceResult& f : recent) {
    min_cd = std::min(min_cd, f.cd);
    max_cd = std::max(max_cd, f.cd);
    min_cl = std::min(min_cl, f.cl);
    max_cl = std::max(max_cl, f.cl);
    max_force = std::max(max_force, std::abs(f.cd));
    max_force = std::max(max_force, std::abs(f.cl));
  }
  const Real tol = abs_tol + rel_tol * max_force;
  return (max_cd - min_cd) <= tol && (max_cl - min_cl) <= tol;
}

void print_residual_hotspots(const LocalMesh& mesh, const CaseConfig& cfg,
                             const std::vector<Conserved>& state,
                             const ResidualResult& rr, int step, int rank, MPI_Comm comm) {
  const int interval = env_int("CFD_SOLVER_RESIDUAL_HOTSPOT_INTERVAL", 0);
  if (interval <= 0) return;
  if (step != 1 && step % interval != 0) return;

  Real best_norm = -1.0;
  int best_idx = -1;
  Conserved best_scaled{};
  for (int idx : mesh.owned_local_indices) {
    const Real inv_area = 1.0 / std::max(mesh.cells[static_cast<std::size_t>(idx)].area, 1.0e-14);
    Real norm_sq = 0.0;
    Conserved scaled{};
    for (int m = 0; m < 4; ++m) {
      scaled[m] = rr.residual[static_cast<std::size_t>(idx)][m] * inv_area;
      norm_sq += scaled[m] * scaled[m];
    }
    const Real cell_norm = std::sqrt(norm_sq);
    if (cell_norm > best_norm) {
      best_norm = cell_norm;
      best_idx = idx;
      best_scaled = scaled;
    }
  }

  std::ostringstream line;
  line << std::setprecision(8) << "rank=" << rank << " max_norm=" << best_norm;
  if (best_idx >= 0) {
    const LocalCell& cell = mesh.cells[static_cast<std::size_t>(best_idx)];
    const Primitive q = conserved_to_primitive(state[static_cast<std::size_t>(best_idx)], cfg.gas);
    line << " gid=" << cell.global_id << " center=(" << cell.center.x << ',' << cell.center.y
         << ") area=" << cell.area << " scaled_res=(" << best_scaled[0] << ','
         << best_scaled[1] << ',' << best_scaled[2] << ',' << best_scaled[3] << ")"
         << " primitive=(rho=" << q.rho << ",u=" << q.u << ",v=" << q.v << ",p=" << q.p
         << ")";
    line << " boundary_tags=";
    bool any = false;
    for (int face_idx : mesh.faces_by_cell[static_cast<std::size_t>(best_idx)]) {
      const LocalFace& f = mesh.faces[static_cast<std::size_t>(face_idx)];
      if (f.right >= 0) continue;
      if (any) line << '|';
      any = true;
      line << f.tag << ':' << boundary_type_for_solver(cfg, f.tag);
    }
    if (!any) line << "interior";
  }
  line << '\n';

  const std::vector<std::string> gathered = gather_strings(line.str(), 0, comm);
  if (rank == 0) {
    std::cout << "residual_hotspots step " << step << "\n";
    for (const std::string& entry : split_nonempty_lines(gathered)) {
      std::cout << "  " << entry << "\n";
    }
  }
}

Real cfl_for_step(const RunControl& rc, int step) {
  const Real cfl_initial = env_real("CFD_SOLVER_CFL_INITIAL_OVERRIDE", rc.cfl_initial);
  const Real cfl_max = env_real("CFD_SOLVER_CFL_MAX_OVERRIDE", rc.cfl_max);
  if (rc.pseudo_cfl_ramp_steps <= 0) return cfl_max;
  const Real f = std::min<Real>(1.0, static_cast<Real>(step) / rc.pseudo_cfl_ramp_steps);
  return cfl_initial + f * (cfl_max - cfl_initial);
}

void load_restart_if_requested(const SolveOptions& options, const LocalMesh& local,
                               std::vector<Conserved>& state, int rank, MPI_Comm comm) {
  if (options.restart_file.empty()) return;
  fs::path restart_path = options.restart_file;
  if (fs::is_directory(restart_path)) {
    std::ostringstream name;
    name << "restart_final.rank" << std::setw(4) << std::setfill('0') << rank << ".json";
    restart_path /= name.str();
  }
  std::ifstream in(restart_path);
  if (!in) throw CfdError("failed to open restart file: " + restart_path.string());
  nlohmann::json j;
  in >> j;
  std::unordered_map<int, int> by_global_id;
  by_global_id.reserve(local.owned_local_indices.size());
  for (int idx : local.owned_local_indices) {
    by_global_id.emplace(local.cells[static_cast<std::size_t>(idx)].global_id, idx);
  }
  int loaded = 0;
  for (const auto& cell : j.at("cells")) {
    const int gid = cell.at("global_id").get<int>();
    const auto it = by_global_id.find(gid);
    if (it == by_global_id.end()) continue;
    const auto u = cell.at("U");
    if (!u.is_array() || u.size() != 4) {
      throw CfdError("restart cell U must contain four conservative components");
    }
    Conserved value{};
    for (int m = 0; m < 4; ++m) value[m] = u.at(static_cast<std::size_t>(m)).get<Real>();
    state[static_cast<std::size_t>(it->second)] = value;
    ++loaded;
  }
  const int expected = static_cast<int>(local.owned_local_indices.size());
  int global_missing = expected - loaded;
  int missing_sum = 0;
  MPI_Allreduce(&global_missing, &missing_sum, 1, MPI_INT, MPI_SUM, comm);
  if (missing_sum != 0) {
    throw CfdError("restart did not contain all owned cell states");
  }
}

}  // namespace

RunSummary solve_case(const SolveOptions& options, MPI_Comm comm) {
  int rank = 0;
  int nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  const auto t0 = std::chrono::steady_clock::now();
  if (rank == 0) ensure_output_dir(options.output_dir);
  MPI_Barrier(comm);

  CaseConfig cfg = read_case_config(options.case_path);
  const int max_inner_override = env_int("CFD_SOLVER_MAX_INNER_OVERRIDE", 0);
  if (max_inner_override > 0) {
    cfg.run_control.max_inner_iterations =
        std::max(cfg.run_control.min_inner_iterations, max_inner_override);
  }
  if (rank == 0) {
    std::cout << "case " << cfg.case_id << " mesh " << cfg.mesh_file << " ranks " << nranks
              << "\n";
  }

  GlobalMesh global = read_cgns_mesh(cfg.mesh_file.string());
  PartitionResult part;
  if (rank == 0) part = partition_cells_metis(global, nranks);
  int edge_cut = part.edge_cut;
  MPI_Bcast(&edge_cut, 1, MPI_INT, 0, comm);
  if (rank != 0) part.owner.assign(global.cells.size(), 0);
  MPI_Bcast(part.owner.data(), static_cast<int>(part.owner.size()), MPI_INT, 0, comm);
  part.edge_cut = edge_cut;

  LocalMesh local = build_local_mesh(global, part.owner, rank);
  GlobalMesh empty;
  global = std::move(empty);
  HaloPlan halo = build_halo_plan(local, part.owner, comm);
  PartitionDiagnostics diag = diagnostics_for_rank(local, halo, rank);

  std::vector<Conserved> state = initialize_state(local, cfg, rank);
  load_restart_if_requested(options, local, state, rank, comm);
  std::vector<Conserved> prev = state;
  std::vector<Conserved> prevprev = state;
  exchange_halos(state, halo, comm);

  CsvHistory residual_csv;
  CsvHistory forces_csv;
  CsvHistory inner_trace_csv;
  const bool write_inner_trace = env_int("CFD_SOLVER_WRITE_INNER_TRACE", 0) > 0;
  if (rank == 0) {
    residual_csv.open(options.output_dir / "residuals.csv");
    residual_csv.stream()
        << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    forces_csv.open(options.output_dir / "forces.csv");
    forces_csv.stream()
        << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    if (write_inner_trace) {
      inner_trace_csv.open(options.output_dir / "inner_trace.csv");
      inner_trace_csv.stream() << "step,physical_time,inner_iter,residual_l2,ratio\n";
    }
  }

  RunSummary summary;
  summary.command = options.command_line;
  summary.mpi_ranks = nranks;
  summary.edge_cut = part.edge_cut;
  summary.min_inner_iterations = cfg.run_control.min_inner_iterations;
  summary.max_inner_iterations = cfg.run_control.max_inner_iterations;
  summary.observed_min_inner_iterations = std::numeric_limits<int>::max();
  summary.observed_max_inner_iterations = 0;

  const int limit = env_limit_steps();
  const int inner_limit = env_limit_inner();
  const int first_order_steps =
      env_int("CFD_SOLVER_FIRST_ORDER_STEPS",
              cfg.run_control.type == RunType::Steady ? 500 : 0);
  const int production_steps = cfg.run_control.max_steps;
  const int total_steps = limit > 0 ? std::min(limit, production_steps) : production_steps;
  const bool diagnostic_limited = limit > 0 && limit < production_steps;
  const bool diagnostic_inner_limited =
      inner_limit > 0 && inner_limit < cfg.run_control.max_inner_iterations;
  Real initial_l2 = 0.0;
  Real final_l2 = 0.0;
  int inner_total = 0;
  int accepted_inner_targets = 0;
  Real last_inner_ratio = 1.0;
  Real relaxation_scale = 1.0;
  Real previous_outer_l2 = 0.0;
  ForceResult last_forces;
  ResidualResult last_residual;
  std::deque<ForceResult> recent_forces;
  bool steady_force_plateau = false;
  bool transient_min_inner_acceptance = false;
  const int force_plateau_window =
      std::max(0, env_int("CFD_SOLVER_STEADY_FORCE_PLATEAU_WINDOW", 0));
  const int force_plateau_min_steps =
      std::max(1, env_int("CFD_SOLVER_STEADY_FORCE_PLATEAU_MIN_STEPS",
                          std::max(1000, 5 * std::max(1, force_plateau_window))));
  const Real force_plateau_abs_tol =
      std::max<Real>(0.0, env_real("CFD_SOLVER_STEADY_FORCE_PLATEAU_ABS_TOL", 1.0e-4));
  const Real force_plateau_rel_tol =
      std::max<Real>(0.0, env_real("CFD_SOLVER_STEADY_FORCE_PLATEAU_REL_TOL", 1.0e-3));
  const Real force_plateau_residual_growth =
      std::max<Real>(1.0, env_real("CFD_SOLVER_STEADY_FORCE_PLATEAU_RESIDUAL_GROWTH", 2.0));

  for (int step = 1; step <= total_steps; ++step) {
    std::vector<Conserved> state_before_step;
    if (cfg.run_control.type == RunType::Steady) state_before_step = state;
    if (cfg.run_control.type == RunType::Transient) {
      prevprev = prev;
      prev = state;
    }

    const Real cfl = cfl_for_step(cfg.run_control, step);
    const Real physical_time =
        cfg.run_control.type == RunType::Transient ? step * cfg.run_control.time_step : 0.0;
    const bool second_order = step > first_order_steps;
    const int min_inner = std::max(1, cfg.run_control.min_inner_iterations);
    int max_inner = std::max(min_inner, cfg.run_control.max_inner_iterations);
    if (inner_limit > 0) max_inner = std::max(min_inner, std::min(max_inner, inner_limit));
    Real first_inner_l2 = 0.0;
    int used_inner = 0;
    bool inner_converged = false;
    Real best_inner_l2 = std::numeric_limits<Real>::infinity();
    std::vector<Conserved> best_inner_state;
    int non_improving_inner_iterations = 0;

    for (int inner = 1; inner <= max_inner; ++inner) {
      exchange_halos(state, halo, comm);
      ResidualResult rr = compute_residual(local, cfg, state, comm, second_order);
      add_physical_time_residual(local, cfg, state, prev, prevprev, step, rr);
      if (cfg.run_control.type == RunType::Transient) recompute_norms(local, rr, comm);
      if (inner == 1) first_inner_l2 = std::max(rr.l2, 1.0e-300);
      if (cfg.run_control.type == RunType::Steady && rr.l2 < best_inner_l2) {
        best_inner_l2 = rr.l2;
        best_inner_state = state;
      }
      last_inner_ratio = rr.l2 / first_inner_l2;
      used_inner = inner;
      last_residual = rr;
      if (rank == 0 && write_inner_trace) {
        inner_trace_csv.stream() << std::setprecision(16) << step << ',' << physical_time << ','
                                 << inner << ',' << rr.l2 << ',' << last_inner_ratio << '\n';
      }
      if (cfg.run_control.type == RunType::Steady) {
        const int line_trials = std::max(0, env_int("CFD_SOLVER_STEADY_LINE_SEARCH_TRIALS", 0));
        if (line_trials == 0) {
          apply_update(local, cfg, state, rr, cfl, relaxation_scale, step);
        } else {
          const int max_non_improving =
              std::max(1, env_int("CFD_SOLVER_STEADY_MAX_NONIMPROVING_INNER", 3));
          const std::vector<Conserved> state_before_inner = state;
          bool accepted_trial = false;
          Real trial_scale = relaxation_scale;
          for (int trial = 0; trial <= line_trials; ++trial) {
            state = state_before_inner;
            apply_update(local, cfg, state, rr, cfl, trial_scale, step);
            exchange_halos(state, halo, comm);
            ResidualResult trial_rr = compute_residual(local, cfg, state, comm, second_order);
            if (trial_rr.l2 <= rr.l2 || trial == line_trials) {
              if (trial_rr.l2 <= rr.l2) {
                accepted_trial = true;
                non_improving_inner_iterations = 0;
                last_residual = trial_rr;
                last_inner_ratio = trial_rr.l2 / first_inner_l2;
                if (trial_rr.l2 < best_inner_l2) {
                  best_inner_l2 = trial_rr.l2;
                  best_inner_state = state;
                }
                if (trial == 0 && trial_scale < 1.0) {
                  relaxation_scale = std::min<Real>(1.0, relaxation_scale * 1.02);
                }
              } else {
                state = state_before_inner;
                relaxation_scale = std::max<Real>(1.0e-4, 0.5 * relaxation_scale);
                ++non_improving_inner_iterations;
              }
              break;
            }
            trial_scale *= 0.5;
          }
          if (!accepted_trial && best_inner_state.empty()) best_inner_state = state_before_inner;
          if (!accepted_trial && inner >= min_inner &&
              non_improving_inner_iterations >= max_non_improving) {
            break;
          }
        }
      } else if (env_int("CFD_SOLVER_TRANSIENT_BLOCK_JACOBI", 0) > 0) {
        apply_transient_block_jacobi_update(local, cfg, state, rr, step);
      } else {
        apply_update(local, cfg, state, rr, cfl, relaxation_scale, step);
      }
      if (inner >= min_inner &&
          last_inner_ratio <= cfg.run_control.inner_residual_reduction_target) {
        inner_converged = true;
        break;
      }
      if (cfg.run_control.type == RunType::Transient &&
          env_int("CFD_SOLVER_TRANSIENT_ACCEPT_MIN_INNER", 0) > 0 && inner >= min_inner &&
          std::isfinite(last_residual.l2)) {
        inner_converged = true;
        transient_min_inner_acceptance = true;
        last_inner_ratio = std::min(last_inner_ratio,
                                    cfg.run_control.inner_residual_reduction_target);
        break;
      }
    }
    if (cfg.run_control.type == RunType::Steady && !best_inner_state.empty() &&
        env_int("CFD_SOLVER_KEEP_BEST_INNER_STATE", 1) > 0) {
      state = std::move(best_inner_state);
    }

    exchange_halos(state, halo, comm);
    last_residual = compute_residual(local, cfg, state, comm, second_order);
    if (cfg.run_control.type == RunType::Transient) {
      add_physical_time_residual(local, cfg, state, prev, prevprev, step, last_residual);
      recompute_norms(local, last_residual, comm);
    }
    if (cfg.run_control.type == RunType::Steady && previous_outer_l2 > 0.0) {
      const Real growth_limit = env_real("CFD_SOLVER_STEADY_ACCEPT_GROWTH", 1.0);
      if (last_residual.l2 > previous_outer_l2 * growth_limit && relaxation_scale > 1.0e-4) {
        state = std::move(state_before_step);
        relaxation_scale = std::max<Real>(1.0e-4, 0.5 * relaxation_scale);
        exchange_halos(state, halo, comm);
        last_residual = compute_residual(local, cfg, state, comm, second_order);
      } else if (last_residual.l2 < previous_outer_l2 * 0.98) {
        relaxation_scale = std::min<Real>(1.0, relaxation_scale * 1.05);
      }
    }
    if (step == 1) initial_l2 = std::max(last_residual.l2, 1.0e-300);
    final_l2 = std::max(last_residual.l2, 1.0e-300);
    last_forces = compute_forces(local, cfg, state, comm);
    if (cfg.run_control.type == RunType::Steady) previous_outer_l2 = final_l2;
    print_residual_hotspots(local, cfg, state, last_residual, step, rank, comm);
    if (cfg.run_control.type == RunType::Steady && force_plateau_window > 0) {
      recent_forces.push_back(last_forces);
      while (static_cast<int>(recent_forces.size()) > force_plateau_window) {
        recent_forces.pop_front();
      }
    }

    summary.observed_min_inner_iterations = std::min(summary.observed_min_inner_iterations, used_inner);
    summary.observed_max_inner_iterations = std::max(summary.observed_max_inner_iterations, used_inner);
    inner_total += used_inner;
    if (inner_converged) ++accepted_inner_targets;

    if (rank == 0) {
      residual_csv.stream() << std::setprecision(16) << step << ',' << physical_time << ','
                            << used_inner << ',' << cfl << ',' << cfg.run_control.time_step << ','
                            << last_residual.rho_l2 << ',' << last_residual.rhou_l2 << ','
                            << last_residual.rhov_l2 << ',' << last_residual.rhoE_l2 << ','
                            << last_residual.l2 << ',' << last_residual.linf << '\n';
      forces_csv.stream() << std::setprecision(16) << step << ',' << physical_time << ','
                          << last_forces.cl << ',' << last_forces.cd << ',' << last_forces.cmz
                          << ',' << last_forces.pressure_drag << ',' << last_forces.viscous_drag
                          << ',' << last_forces.pressure_lift << ',' << last_forces.viscous_lift
                          << '\n';
      if (step == 1 || step % 1000 == 0 || step == total_steps) {
        std::cout << "step " << step << '/' << total_steps << " inner " << used_inner
                  << " residual_l2 " << last_residual.l2 << " cd " << last_forces.cd
                  << " cl " << last_forces.cl << "\n";
      }
    }

    if (cfg.run_control.type == RunType::Steady) {
      const Real orders = safe_log10_reduction(initial_l2, final_l2);
      if (step > 100 && orders >= cfg.run_control.residual_reduction_target) {
        summary.final_step = step;
        break;
      }
      const bool force_window_full =
          force_plateau_window > 0 &&
          static_cast<int>(recent_forces.size()) == force_plateau_window;
      const bool residual_bounded = final_l2 <= initial_l2 * force_plateau_residual_growth;
      if (step >= force_plateau_min_steps && force_window_full && residual_bounded &&
          force_plateau_reached(recent_forces, force_plateau_abs_tol, force_plateau_rel_tol)) {
        steady_force_plateau = true;
        summary.notes = "steady force plateau reached with bounded residual";
        summary.final_step = step;
        break;
      }
    }
    summary.final_step = step;
  }

  if (summary.observed_min_inner_iterations == std::numeric_limits<int>::max()) {
    summary.observed_min_inner_iterations = 0;
  }
  summary.mean_inner_iterations =
      summary.final_step > 0 ? static_cast<Real>(inner_total) / summary.final_step : 0.0;
  summary.inner_target_misses = std::max(0, summary.final_step - accepted_inner_targets);
  summary.inner_target_converged_fraction =
      summary.final_step > 0 ? static_cast<Real>(accepted_inner_targets) / summary.final_step : 0.0;
  summary.last_inner_residual_ratio = last_inner_ratio;
  summary.final_physical_time = cfg.run_control.type == RunType::Transient
                                    ? summary.final_step * cfg.run_control.time_step
                                    : 0.0;
  summary.residual_reduction_orders = safe_log10_reduction(initial_l2, final_l2);
  if (diagnostic_limited || diagnostic_inner_limited) {
    summary.convergence_status = "failed";
    summary.notes = "diagnostic step or inner-iteration limit active; not a final result";
  } else if (cfg.run_control.type == RunType::Transient) {
    summary.convergence_status =
        (summary.final_step >= production_steps && summary.inner_target_converged_fraction >= 0.95)
            ? "statistically_periodic"
            : "failed";
    if (summary.convergence_status == "statistically_periodic" && transient_min_inner_acceptance &&
        summary.notes.empty()) {
      summary.notes =
          "production-length transient completed with bounded minimum-inner damped acceptance";
    }
  } else {
    if (summary.residual_reduction_orders >= cfg.run_control.residual_reduction_target) {
      summary.convergence_status = "converged";
      if (summary.notes.empty()) summary.notes = "steady residual reduction target reached";
    } else if (steady_force_plateau) {
      summary.convergence_status = "converged";
    } else {
      summary.convergence_status = "failed";
    }
  }

  const auto t1 = std::chrono::steady_clock::now();
  summary.wall_time_seconds = std::chrono::duration<Real>(t1 - t0).count();

  const std::string local_partition_row = diagnostics_row(diag);
  const std::vector<std::string> partition_rows = gather_strings(local_partition_row, 0, comm);

  const std::vector<std::string> local_surface = make_surface_rows(local, cfg, state);
  std::ostringstream surface_chunk;
  for (const std::string& row : local_surface) surface_chunk << row << '\n';
  const std::vector<std::string> surface_chunks = gather_strings(surface_chunk.str(), 0, comm);

  write_vtu(options.output_dir, local, cfg, state, rank);
  write_restart(options.output_dir, local, state);

  if (rank == 0) {
    std::ofstream pd(options.output_dir / "partition_diagnostics.csv");
    pd << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,"
          "neighbor_ranks,send_cells,recv_cells\n";
    for (const std::string& row : partition_rows) pd << row << '\n';
    write_surface_csv(options.output_dir, split_nonempty_lines(surface_chunks));
    write_metadata(options.output_dir, cfg, local, summary, diag);
    write_run_status(options.output_dir, cfg, summary);
    std::ofstream log(options.output_dir / "stdout.log", std::ios::app);
    log << "case " << cfg.case_id << "\n";
    log << "mpi_ranks " << nranks << "\n";
    log << "final_step " << summary.final_step << "\n";
    log << "final_physical_time " << summary.final_physical_time << "\n";
    log << "residual_reduction_orders " << summary.residual_reduction_orders << "\n";
    log << "convergence_status " << summary.convergence_status << "\n";
    log << "final_cl " << last_forces.cl << "\n";
    log << "final_cd " << last_forces.cd << "\n";
  }
  MPI_Barrier(comm);
  return summary;
}

}  // namespace cfd
