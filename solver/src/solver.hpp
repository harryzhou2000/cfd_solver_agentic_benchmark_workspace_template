#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "case_io.hpp"
#include "mesh_local.hpp"
#include "physics.hpp"

namespace cfd {

struct ForceData {
  double cl = 0.0, cd = 0.0, cmz = 0.0;
  double pressure_drag = 0.0, viscous_drag = 0.0;
  double pressure_lift = 0.0, viscous_lift = 0.0;
  double moment = 0.0;  // total pitching moment (about reference center)
  int wall_faces = 0;
};

struct ResidualNorms {
  double l2 = 0.0, linf = 0.0;
};

struct SurfaceRow {
  double x = 0.0, y = 0.0, nx = 0.0, ny = 0.0;
  double pressure = 0.0, cp = 0.0, cf = 0.0;
  double rho = 0.0, u = 0.0, v = 0.0, mach = 0.0;
  std::string tag;
  int face_global_id = -1;
};

// Per-step inner-solve statistics.
struct InnerStats {
  long long steps = 0;
  long long total_inner = 0;
  int min_inner = 0;
  int max_inner = 0;
  long long target_misses = 0;
  long long converged_steps = 0;
  double last_inner_residual_ratio = 0.0;
  void add(int iters, bool converged, double ratio) {
    ++steps;
    total_inner += iters;
    min_inner = (steps == 1) ? iters : std::min(min_inner, iters);
    max_inner = std::max(max_inner, iters);
    if (converged)
      ++converged_steps;
    else
      ++target_misses;
    last_inner_residual_ratio = ratio;
  }
  double mean_inner() const {
    return steps ? static_cast<double>(total_inner) / steps : 0.0;
  }
  double converged_fraction() const {
    return steps ? static_cast<double>(converged_steps) / steps : 0.0;
  }
};

// All solver state, rank-local.
struct Solver {
  const Case* c = nullptr;
  FreeStream fs;
  Gas gas{Case{}};
  LocalMesh mesh;
  int rank = 0, nranks = 1;
  MPI_Comm comm = MPI_COMM_WORLD;
  int nc = 0;  // local cells (owned + ghost)

  // Fields are flattened: [cell][component]
  std::vector<double> U;     // 4*nc conservative
  std::vector<double> U_prev1;  // 4*nc U^n (transient)
  std::vector<double> U_prev2;  // 4*nc U^{n-1} (transient)
  std::vector<double> Q;     // 4*nc primitives (rho,u,v,p)
  std::vector<double> grad;  // 8*nc gradients of (rho,u,v,p)
  std::vector<double> phi;   // 4*nc Barth limiters per component
  std::vector<double> rec_off;  // 4*nc*maxfaces reconstruction offsets (frozen)
  std::vector<int> rec_off_faces;  // per-cell face counts
  std::vector<double> dU;    // 4*nc accumulated delta (LU-SGS)
  std::vector<double> U0s;   // 4*nc LU-SGS sweep-start snapshot
  std::vector<double> R;     // 4*n_owned residual (spatial; or total transient)
  std::vector<double> R_solve;  // 4*n_owned frozen linear-system RHS
  std::vector<uint8_t> wall_cell;  // nc flags: cell adjacent to a solid wall

  std::vector<double> dt_ps;      // n_owned pseudo time steps
  std::vector<double> diag;       // n_owned implicit diagonal
  std::vector<double> cell_lam;   // n_owned spectral radii (diagnostics)

  double rho_min = 0.0, p_min = 0.0;
  long long positivity_fallbacks = 0;

  // Run metadata
  InnerStats inner_stats;
  double residual_initial_l2 = 0.0;
  double residual_initial_linf = 0.0;

  // Transient bookkeeping
  int physical_step = 0;
  bool bdf2 = true;
};

// Initializes U to freestream and performs the initial state halo exchange.
void initialize_state(Solver& s);

// Full nonlinear residual (spatial or total-transient) over owned cells,
// with global MPI norms. Optionally computes forces and wall surface values.
//   include_physical_time: for transient BDF2/BDF1 total residual.
//   physical_terms: precomputed per-cell [4] physical-time source (U^n, U^{n-1})
//   force_out / surface_out: optional outputs.
void compute_residual(Solver& s, bool include_physical_time,
                      ForceData* force_out, ResidualNorms* norms,
                      std::vector<SurfaceRow>* surface_out);

// Computes pseudo time step and LU-SGS diagonal for each owned cell.
//   cfl: local pseudo CFL
//   physical_diag: 0 for steady, else (3V/(2dt)) for BDF2 or (V/dt) for BDF1
void compute_dt_and_diag(Solver& s, double cfl, double physical_diag_factor);

// One LU-SGS forward+backward sweep with neighbor halo exchanges of dU.
void lusgs_sweep(Solver& s);

// Steady implicit solve; returns convergence status string.
std::string run_steady(Solver& s, const std::string& outdir, int start_step);

// Transient BDF2/BDF1 dual-time solve; returns convergence status string.
std::string run_transient(Solver& s, const std::string& outdir, int start_step);

// Writes restart file (all ranks participate).
void write_restart(const Solver& s, const std::string& path, int step,
                   double physical_time);
// Loads restart file; returns (step, physical_time).
std::pair<int, double> read_restart(Solver& s, const std::string& path);

// Saves a VTU field snapshot (rank 0 assembles and writes).
void write_field_vtu(const Solver& s, const std::string& outdir,
                     const std::string& name);

// Appends a row to residuals.csv / forces.csv (rank 0 only).
void write_residual_row(const Solver& s, const std::string& outdir, int step,
                        double physical_time, int inner_iter, double cfl,
                        double dt_global, const ResidualNorms& norms);
void write_force_row(const Solver& s, const std::string& outdir, int step,
                     double physical_time, const ForceData& f);

// Final surface.csv (rank 0 assembles from all ranks).
void write_surface_csv(const Solver& s, const std::string& outdir);

}  // namespace cfd
