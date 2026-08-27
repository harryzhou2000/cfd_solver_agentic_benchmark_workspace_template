#pragma once
// Implicit time integration: steady pseudo-time march with CFL ramp and
// symmetric Gauss--Seidel (LU-SGS with simplified Jacobians) inner solves,
// plus a true two-level BDF2 transient driver for the cylinder Re200 case.

#include <mpi.h>
#include <string>
#include "case_file.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "spatial.hpp"

namespace cfd {

struct SolverConfig {
  int limiter = 2;          // 0 none, 1 Barth-Jespersen, 2 Venkatakrishnan
  double venkat_k = 5.0;
  int inviscid_flux = 0;    // 0 Roe, 1 Rusanov
  double pert_aoa_deg = 0.0;
  double pert_duration = 0.0;
  int transient_sweeps_per_inner = 6;  // SGS sweep pairs per nonlinear refresh
  double transient_pseudo_cfl = 100.0;  // pseudo-time safeguard CFL (dual time)
};

struct ResidualNorms {
  double per_eq[4] = {0, 0, 0, 0};  // RMS of R/V per equation
  double l2 = 0.0;                  // combined RMS
  double linf = 0.0;                // max |component|
  double dt_min = 0.0;              // min local pseudo time step at CFL=1
};

struct ForceCoeffs {
  double cl = 0, cd = 0, cmz = 0;
  double pressure_drag = 0, viscous_drag = 0;
  double pressure_lift = 0, viscous_lift = 0;
  double pressure_moment = 0, viscous_moment = 0;
};

struct InnerStats {
  int observed_min = -1;
  int observed_max = 0;
  double observed_mean = 0.0;
  long total_steps = 0;
  int target_misses = 0;
  double converged_fraction = 1.0;
  double last_ratio = 0.0;
  void add(int iters, bool hit_target, double ratio);
  void finalize();
};

// Owns the distributed state and all scratch arrays.
struct SolverContext {
  MPI_Comm comm = MPI_COMM_WORLD;
  int rank = 0, n_ranks = 1;
  const CaseFile* cs = nullptr;
  LocalMesh mesh;
  PartitionInfo pinfo;
  SolverConfig cfg;
  FlowState st;
  Halo halo;
  std::vector<double> pack_buf;  // halo scratch for combined grad+phi exchange

  // BDF2 history (transient): owned+ghost layout not needed; owned only plus
  // halo exchange after acceptance.
  std::vector<Vec4> U_n, U_nm1;

  void init();  // allocate, set freestream initial condition
  // Full residual evaluation pipeline on st.U (halo sync included).
  void eval_residual(const AssembleOpts& o, ForceSums& forces_local);
  ResidualNorms global_norms() const;                 // uses st.R
  ResidualNorms global_norms_F(const std::vector<Vec4>& F) const;
  ForceCoeffs reduce_forces(const ForceSums& local) const;
  double local_dt(int i, double cfl) const;           // pseudo time step
};

struct RunResult {
  long final_step = 0;
  double final_time = 0.0;
  std::string status = "failed";
  double residual_reduction = 0.0;
  InnerStats inner;
  ForceCoeffs last_forces;
  int n_physical_steps = 0;
};

RunResult run_steady(SolverContext& ctx, OutputContext& out, long start_step = 0);
RunResult run_transient(SolverContext& ctx, OutputContext& out, long start_step = 0,
                        double start_time = 0.0);

}  // namespace cfd
