#pragma once

#include "config.hpp"
#include "physics.hpp"
#include "partition.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cfd {

struct FreeStream {
  double rho = 1.0, u = 1.0, v = 0.0, p = 0.0;
  double mach = 0.0;
  double aoa = 0.0;            // radians
  double qinf = 0.5;           // dynamic pressure
  std::array<double, 4> U{1.0, 1.0, 0.0, 0.0};
};

struct ForceRow {
  double cl = 0.0, cd = 0.0, cmz = 0.0;
  double pressure_drag = 0.0, viscous_drag = 0.0;
  double pressure_lift = 0.0, viscous_lift = 0.0;
};

struct ResidualRow {
  int step = 0;
  double physical_time = 0.0;
  int inner_iter = 0;
  double cfl = 0.0;
  double dt = 0.0;
  std::array<double, 4> comp{0.0, 0.0, 0.0, 0.0};  // component L2 norms
  double l2 = 0.0, linf = 0.0;
};

struct RunStats {
  int final_step = 0;
  double final_physical_time = 0.0;
  double residual_reduction_orders = 0.0;
  std::string convergence_status = "failed";
  std::string notes;
  // inner-iteration statistics (transient or steady)
  int64_t total_inner = 0;
  int64_t target_misses = 0;
  int64_t converged_steps = 0;
  int64_t min_inner = 0;
  int64_t max_inner = 0;
  double mean_inner = 0.0;
  double last_inner_ratio = 1.0;
  int64_t n_steps = 0;
};

// The numerical core: residual assembly, LU-SGS implicit relaxation,
// steady pseudo-time march and BDF2 transient loop.
class Solver {
 public:
  Solver(const CaseConfig& cfg, const GlobalMesh& mesh, int rank, int nranks);

  void run();

  // Public state for the output writer.
  const CaseConfig& cfg;
  Partition part;
  FreeStream fs;
  Gas gas;
  double mu = 0.0;   // viscosity (0 for inviscid)
  double kcond = 0.0;  // thermal conductivity
  double qinf = 0.0;

  std::vector<double> U;       // conservative state, N_local*4
  RunStats stats;
  std::vector<ResidualRow> residual_history;
  std::vector<ForceRow> force_history;
  double wall_time_seconds = 0.0;
  std::string command_line;
  std::string out_dir;
  std::string start_time_utc;
  int restart_step = 0;
  double restart_time = 0.0;

  // Gradients/limiters are needed by the output writer (surface rows).
  std::vector<double> grads_;   // primitive gradients N_local*8
  std::vector<double> psi_;     // limiter N_local

  // Last-step diagnostics used by outputs.
  double last_residual_l2 = 0.0;
  double initial_residual_l2 = 0.0;
  int step_ = 0;   // current step counter (diagnostics)

 private:
  int rank_ = 0, nranks_ = 1;
  std::vector<double> dU_;      // LU-SGS increments N_local*4
  std::vector<double> R_;       // residual scratch N_owned*4
  std::vector<double> lam_;     // spectral radius sums N_owned
  std::vector<double> dtau_;    // local pseudo time step N_owned
  std::vector<double> dblk_;    // 4x4 block diagonal (row-major), N_owned*16
  std::vector<double> hist_n_;   // BDF2 history U^n (local states)
  std::vector<double> hist_nm1_; // BDF2 history U^{n-1}
  bool trans_bdf1_ = false;

  void prims_of(int loc, std::array<double, 4>& q) const;
  void cons_of(const std::array<double, 4>& q, std::array<double, 4>& U) const;
  void face_state(int loc, const FaceRef& f, const std::array<double, 4>& q,
                  const double* g, double psi_c, std::array<double, 4>& Uf) const;

  void compute_gradients();
  void compute_limiter();
  // Assemble the spatial residual into R (no MPI reductions).
  void assemble_residual(std::vector<double>& R);
  // Global norms of a residual vector.
  void residual_norms(const std::vector<double>& R, std::array<double, 4>& comp,
                      double& l2, double& linf);
  void spectral_radii(double cfl, double trans_a);
  // Apply the LU-SGS preconditioner: out = M^{-1} rhs (frozen linearization).
  void lusgs_apply(const std::vector<double>& rhs, std::vector<double>& out,
                   double trans_a);
  // L2/Linf norm of the linear-system residual R - (D-C)dU (global).
  void linear_residual_norm(const std::vector<double>& R, double& l2, double& linf);
  void update_state();
  // Evaluate the nonlinear residual of the current state (with fresh
  // exchanges/gradients/limiter); returns norms via out-params.
  void evaluate_residual(std::array<double, 4>& comp, double& l2, double& linf,
                         bool with_transient = true);
  // Evaluate the residual of an arbitrary state (state is temporarily
  // installed; R_out receives the spatial (+transient source) residual).
  // With update_limiter=false the limiter is frozen at its current values,
  // which keeps the Jacobian-vector product smooth (standard implicit-CFD
  // linearization; the nonlinear residual is unaffected).
  void residual_of_state(std::vector<double>& Utrial, std::vector<double>& R_out,
                         bool with_transient, bool update_limiter = true);
  // Matrix-free Newton step: solve (vol/dtau - J)dU = R via restarted GMRES
  // with the LU-SGS preconditioner.  R is the (spatial or total) residual
  // that defines the RHS.  Returns the iteration count.
  int solve_newton_step(double trans_a, const std::vector<double>& R, double tol,
                        int max_iter, bool* converged = nullptr);

  ForceRow compute_forces();

  void run_steady();
  void run_transient();

  double gamma() const { return cfg.gamma; }
};

}  // namespace cfd
