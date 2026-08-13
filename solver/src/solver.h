#pragma once
// Phase 4: second-order finite-volume solver core with matrix-free LU-SGS
// implicit pseudo-time marching.
//
// Residual assembly (second-order MUSCL-type reconstruction with the
// Barth-Jespersen limiter and a first-order positivity fallback, plus the
// laminar viscous flux), wall-force integration, global MPI reductions,
// the implicit LU-SGS smoother, and the steady marching loop.

#include <string>
#include <vector>

#include "gradient.h"
#include "halo.h"
#include "limiter.h"
#include "partition.h"
#include "types.h"

namespace cfd {

// Numerics configuration assembled from RunConfig (run_control + outputs)
// plus the Phase 4 implicit-solver knobs.
struct SolverConfig {
  // Marching-step cap. For steady runs 0 falls back to a default cap (see
  // make_solver_config); for transient runs 0 means "uncapped — run the
  // final_time / time_step physical steps" and only a positive value caps
  // the number of physical steps (e.g. --max-steps smoke tests).
  int max_steps = 0;
  double cfl_initial = 1.0;
  double cfl_max = 100.0;
  int pseudo_cfl_ramp_steps = 0;
  double residual_reduction_target = 0.0;  // convergence target, in orders
  int min_inner_iterations = 0;
  int max_inner_iterations = 0;
  double inner_residual_reduction_target = 0.0;
  double rusanov_dissipation_scale = 1.0;

  // Spatial discretization: 1 = first order (no reconstruction), 2 = second
  // order (least-squares gradients + Barth-Jespersen limiter). Taken from
  // the case's numerics_required.spatial_order.
  int spatial_order = 1;

  // Laminar viscous terms: mu is the constant viscosity from the freestream
  // Reynolds number (0 for inviscid), viscous enables the viscous flux and
  // the viscous spectral-radius contribution to the LU-SGS diagonal.
  double mu = 0.0;
  bool viscous = false;

  // Restart: when true the caller has loaded the owned-cell states from a
  // restart file (read_restart_binary) and the marching loops must NOT
  // re-initialize U_local to freestream.
  bool restart = false;

  int write_residuals_every = 1;
  int write_forces_every = 1;

  // Current CFL (updated during marching).
  double cfl = 0.0;
};

// Per-step solver statistics. Residual columns are global (MPI-reduced)
// values; force columns are global coefficients. The inner-iteration
// aggregates (min/max/mean, misses, last ratio) are running per-rank values
// updated every step; the final summary reduces them across ranks.
struct SolverStats {
  int step = 0;
  double physical_time = 0.0;  // steady: always 0
  int inner_iter = 0;          // inner LU-SGS iterations of this step
  double cfl = 0.0;            // CFL used for this step's update
  double dt = 0.0;             // mean local pseudo time step

  // Residual norms (L2 over all owned cells, Linf over all owned cells).
  double residual_rho = 0.0;
  double residual_rhou = 0.0;
  double residual_rhov = 0.0;
  double residual_rhoE = 0.0;
  double residual_l2 = 0.0;    // max of the four component L2 norms
  double residual_linf = 0.0;  // max abs over all components and cells

  // Force coefficients (global, from the case reference quantities).
  double cl = 0.0;
  double cd = 0.0;
  double cmz = 0.0;
  double pressure_drag = 0.0;
  double viscous_drag = 0.0;
  double pressure_lift = 0.0;
  double viscous_lift = 0.0;

  // Inner-iteration statistics (Phase 4). min/max/mean/misses are running
  // aggregates over the steps completed so far; last_inner_residual_ratio is
  // the residual reduction ratio of the most recent step's inner loop.
  int min_inner_its = 0;
  int max_inner_its = 0;
  double mean_inner_its = 0.0;
  int inner_target_misses = 0;
  double last_inner_residual_ratio = 1.0;

  // End-of-run summary (filled by the marching loops; used by main for the
  // metadata.json / run_status.json output contract). Steady: residual
  // reduction relative to the step-0 residual. Transient: reduction of the
  // total (spatial + physical-time) residual relative to the step-0 value.
  double initial_residual_l2 = 0.0;
  double residual_reduction_orders = 0.0;
  bool converged = false;  // steady: convergence target met; transient: all
                           // scheduled physical steps completed without failure
};

// Assemble the solver configuration from the case configuration.
SolverConfig make_solver_config(const RunConfig& cfg);

// Compute the residual of all owned cells:
//   R_local[i] = sum of face fluxes of owned cell i
// U_local holds owned + ghost states (the caller exchanges the halo first).
//
// Second order (when `second_order` is true; the reconstruction data must
// then be provided in `grads`, `ghost_grads`, `limiters`): each face state
// is reconstructed from the cell
// center with the limited gradient — U_face = U_i + (phi*grad_i) . dr with
// dr = face centroid - cell centroid — and falls back to the first-order
// cell-center value when the reconstructed state is non-physical (rho <= 0
// or p <= 0). Boundary states are built from the reconstructed interior
// state (second-order boundary treatment).
//
// Viscous (when `mu > 0`): the laminar viscous flux is added at internal and
// inter-rank faces using the face-averaged cell gradients and reconstructed
// face velocity/temperature; at no-slip adiabatic wall faces a one-sided
// gradient (wall value minus cell value over the wall distance, dT/dn = 0)
// is used. Slip-wall and farfield faces contribute no viscous flux.
void compute_residual(const std::vector<ConsState>& U_local,
                      std::vector<ConsState>& R_local, const LocalMesh& lm,
                      const GasConfig& gas, const Freestream& fs,
                      double dissipation_scale = 1.0,
                      bool second_order = false,
                      const CellGradients* grads = nullptr,
                      const GhostGradients* ghost_grads = nullptr,
                      const Limiters* limiters = nullptr, double mu = 0.0,
                      bool viscous = false,
                      const std::vector<ConsState>* boundary_states = nullptr);

// Accumulate this rank's wall-force contributions (pressure plus, for
// no-slip adiabatic walls in laminar runs, the one-sided shear stress) into
// stats. The values are LOCAL sums; the global reduction happens in
// compute_global_norms. Force on the body: dF = (p n - tau . n) dA with n
// the outward area vector of the fluid domain (pointing into the body).
void compute_forces(const std::vector<ConsState>& U_local, SolverStats& stats,
                    const LocalMesh& lm, const GasConfig& gas,
                    const Freestream& fs, const RunConfig& cfg,
                    double mu = 0.0, bool viscous = false);

// Reduce residual norms and force sums across ranks (MPI_Allreduce) and fill
// global_stats (identical on every rank). n_owned_global is the total number
// of owned cells over all ranks (L2 = sqrt(sum of squares / n_owned_global)).
void compute_global_norms(const std::vector<ConsState>& R_local,
                          const SolverStats& local_stats,
                          SolverStats& global_stats, int n_owned_global);

// Steady pseudo-time marching loop (Phase 4: second-order reconstruction and
// matrix-free LU-SGS). Runs from the given initial U_local (freestream on
// owned cells), writes residuals.csv / forces.csv into out_dir (rank 0),
// prints a convergence summary, and returns 0 on success, 1 on failure. All
// ranks return the same code. If final_stats is non-null it receives a copy
// of the global (MPI-reduced) stats of the final step plus the end-of-run
// summary fields (initial residual, reduction orders, converged flag).
int run_steady_solver(const RunConfig& cfg, const SolverConfig& scfg,
                      std::vector<ConsState>& U_local, const LocalMesh& lm,
                      HaloScratch& scratch, const std::string& out_dir,
                      SolverStats* final_stats = nullptr);

// Physical-time marching loop (Phase 5: BDF2 with inner point-implicit
// iterations). The first physical step uses backward Euler (first order);
// subsequent steps use BDF2. The history states U^n and U^{n-1} are frozen
// during the inner iterations and updated only after the inner solve of each
// physical step converges (or reaches max_inner_iterations). The inner loop
// measures the TOTAL residual (spatial + physical-time terms) and performs
// at least min_inner_iterations and at most max_inner_iterations point
// implicit updates per physical step.
//
// The number of physical steps is final_time / time_step, capped by
// scfg.max_steps (so --max-steps gives a bounded smoke test). Writes
// residuals.csv / forces.csv into out_dir (rank 0). If final_stats is
// non-null it receives the global stats of the final physical step plus the
// end-of-run summary fields (step = number of physical steps completed,
// physical_time = final physical time).
void run_transient_solver(std::vector<ConsState>& U_local, const LocalMesh& lm,
                          const RunConfig& cfg, const SolverConfig& scfg,
                          const GasConfig& gas, const Freestream& fs,
                          HaloScratch& scratch, GradientHaloScratch& gscratch,
                          const std::string& out_dir,
                          SolverStats* final_stats);

}  // namespace cfd
