#pragma once

#include "io/Input.hpp"
#include "mesh/Partition.hpp"
#include "physics/Gas.hpp"

#include <mpi.h>

#include <climits>
#include <string>
#include <vector>

namespace cfds {

// Aggregated runtime statistics reported in metadata / run_status.
struct SolverStats {
  long long total_inner = 0;          // total inner iterations over the run
  int min_inner = INT_MAX;
  int max_inner = 0;
  double mean_inner = 0.0;
  long long outer_steps_with_inner = 0;
  long long target_misses = 0;        // inner target misses
  long long inner_target_converged_steps = 0;
  double last_inner_residual_ratio = 0.0;
  double residual_reduction_orders = 0.0;
  double wall_time_seconds = 0.0;
  int final_step = 0;
  double final_physical_time = 0.0;
  std::string convergence_status = "failed";
  long long positivity_fallbacks = 0;
  long long first_order_fallback_cells = 0;
  double last_residual_l2 = 0.0;
  double initial_residual_l2 = 0.0;
};

class Solver {
 public:
  Solver(const CaseConfig& cfg, const GasModel& gas, DistributedMesh& mesh,
         MPI_Comm comm);

  // Run the full simulation (steady or transient) and return statistics.
  SolverStats run();

  // Access to the final state (owned + ghost) for output/restart.
  const std::vector<ConsVec>& state() const { return U_; }
  const std::vector<std::array<std::array<double, 2>, 4>>& gradients() const {
    return grad_;
  }
  const std::vector<double>& limiters() const { return psi_; }

  // Replace the initial state with an externally loaded state (restart).
  void set_initial_state(const std::vector<ConsVec>& owned_U);

 private:
  const CaseConfig& cfg_;
  const GasModel& gas_;
  DistributedMesh& mesh_;
  MPI_Comm comm_;
  int rank_ = 0;
  int nranks_ = 1;

  std::vector<ConsVec> U_;          // owned + ghost
  std::vector<std::array<std::array<double, 2>, 4>> grad_;  // owned + ghost
  std::vector<double> psi_;         // limiter, owned cells
  std::vector<ConsVec> R_;          // residual (owned cells)
  std::vector<ConsVec> dU_;         // update (owned + ghost)
  std::vector<double> dt_local_;    // local pseudo time step (owned + ghost)
  std::vector<ConsVec> face_flux_base_;  // per local face (inviscid, unit len)
  std::vector<ConsVec> face_flux_viscous_base_;  // per local face (viscous)
  std::vector<std::array<std::array<double, 4>, 4>> diag_block_;  // 4x4 self
  // Exact 4x4 off-diagonal blocks per face (owner row / neighbor row).
  std::vector<std::array<std::array<double, 4>, 4>> face_off_owner_;
  std::vector<std::array<std::array<double, 4>, 4>> face_off_neighbor_;

  // Least-squares gradient precomputation.
  std::vector<int> lsq_offsets_;    // n_owned+1
  std::vector<double> lsq_w_;       // 2 doubles per neighbor entry
  std::vector<double> lsq_ainv_;    // 3 doubles per owned cell
  std::vector<int> lsq_neighbors_;  // neighbor local ids
  // Mirrored-ghost boundary samples in the LSQ stencil: per-cell offsets,
  // the local face index of each boundary sample, and its weights.
  std::vector<int> lsq_bnd_offsets_;  // n_owned+1
  std::vector<int> lsq_bnd_faces_;    // local face id per boundary sample
  std::vector<double> lsq_bnd_w_;     // 2 doubles per boundary sample

  // Freestream.
  Primitive far_;
  ConsVec far_U_;
  double q_inf_ = 0.0;
  double phys_coef_ = 1.5;   // BDF2 coefficient; 1.0 for the first (BDF1) step
  bool bdf1_step_ = false;

  // Halo exchange buffers (persistent).
  std::vector<std::vector<double>> send_buf_, recv_buf_;

  SolverStats stats_;
  bool external_state_ = false;
  bool second_order_active_ = true;
  // Reconstruction blend: ramps from 0 to 1 over the documented activation
  // window so the flow adapts smoothly to the second-order fluxes instead of
  // switching them on abruptly at full CFL.
  double recon_blend_ = 0.0;
  double last_cl_ = 0.0;
  double last_cd_ = 0.0;

  void init_state();
  void prepare_lsq();
  void exchange_halo(std::vector<ConsVec>& data);
  void exchange_halo_dU();
  void compute_gradients();
  void compute_limiters();
  double compute_spatial_residual(bool store_base);
  void refresh_face_fluxes();
  double compute_total_residual(double physical_time, double dt,
                                const std::vector<ConsVec>& Um1,
                                const std::vector<ConsVec>& Un);
  void compute_forces(int step, double physical_time, bool write_csv);
  void sweep_lusgs(const std::vector<ConsVec>& Rref, bool backward);
  double implicit_defect_ratio(const std::vector<ConsVec>& Rref);
  void positivity_safe_update();
  double local_dt(int cell) const { return dt_local_[cell]; }
  void compute_local_dt(double cfl, bool transient, double dt_phys);
  void compute_implicit_diagonal(bool transient, double dt_phys);

  double global_l2(const std::vector<ConsVec>& R) const;
  std::array<double, 4> component_rms(const std::vector<ConsVec>& R) const;
  double global_linf(const std::vector<ConsVec>& R) const;
  double global_max_dt() const;

  SolverStats run_steady();
  SolverStats run_transient();
};

}  // namespace cfds
