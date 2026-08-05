#pragma once

#include <array>
#include <string>
#include <vector>

#include <mpi.h>

#include "cfd/config.hpp"
#include "cfd/partition.hpp"
#include "cfd/physics.hpp"

namespace cfd {

struct ResidualRecord {
  int step{0};
  double physical_time{0.0};
  int inner_iter{0};
  double cfl{0.0};
  double dt{0.0};
  std::array<double, 4> components{0.0, 0.0, 0.0, 0.0};
  double l2{0.0};
  double linf{0.0};
};

struct ForceRecord {
  int step{0};
  double physical_time{0.0};
  double cl{0.0};
  double cd{0.0};
  double cmz{0.0};
  double pressure_drag{0.0};
  double viscous_drag{0.0};
  double pressure_lift{0.0};
  double viscous_lift{0.0};
};

struct SurfaceRecord {
  double x{0.0};
  double y{0.0};
  double nx{0.0};
  double ny{0.0};
  double pressure{0.0};
  double cp{0.0};
  double cf{0.0};
  double rho{0.0};
  double u{0.0};
  double v{0.0};
  double mach{0.0};
  std::string tag;
};

struct InnerStatistics {
  int minimum{0};
  int maximum{0};
  double mean{0.0};
  int target_misses{0};
  double converged_fraction{0.0};
  double last_ratio{0.0};
};

struct RunSummary {
  std::vector<ResidualRecord> residuals;
  std::vector<ForceRecord> forces;
  std::vector<SurfaceRecord> local_surface;
  InnerStatistics inner_statistics;
  int final_step{0};
  double final_physical_time{0.0};
  double residual_reduction_orders{0.0};
  bool converged{false};
  bool statistically_periodic{false};
  std::string diagnostic;
};

/// Distributed cell-centred finite-volume Navier--Stokes solver.  State is
/// stored only for rank-local owned cells plus the one-ring ghost layer held by
/// LocalMesh.  HaloExchange is invoked before every reconstruction/residual.
class FlowSolver {
 public:
  FlowSolver(CaseConfig config, LocalMesh mesh, MPI_Comm communicator);

  void initialize();
  /// Restore locally owned conservative values from a restart piece. Ghost
  /// values are rebuilt through the normal neighbor halo exchange.
  void restore_owned_state(const std::vector<double>& owned_state);
  RunSummary solve();

  [[nodiscard]] const CaseConfig& config() const noexcept { return config_; }
  [[nodiscard]] const LocalMesh& mesh() const noexcept { return mesh_; }
  [[nodiscard]] const PerfectGas& gas() const noexcept { return gas_; }
  [[nodiscard]] const std::vector<double>& state() const noexcept { return state_; }
  [[nodiscard]] const RunSummary& summary() const noexcept { return summary_; }

 private:
  struct Assembly {
    std::vector<double> residual;
    std::vector<double> spectral_radius;
  };

  CaseConfig config_;
  LocalMesh mesh_;
  MPI_Comm comm_{MPI_COMM_NULL};
  int rank_{0};
  int ranks_{1};
  PerfectGas gas_;
  HaloExchange halo_;
  std::vector<double> state_;       // [local cell][rho,rhou,rhov,rhoE]
  // Boundary-aware, bounded least-squares primitive gradients for viscous
  // fluxes and wall traction.  Viscous discretization must not disappear when
  // an inviscid reconstruction continuation uses a zero face-gradient scale.
  std::vector<double> gradients_;   // [local cell][rho_x,rho_y,u_x,u_y,v_x,v_y,T_x,T_y]
  // Independently limited/scaled gradients used only to reconstruct inviscid
  // face states.  This is the array affected by reconstruction_gradient_scale.
  std::vector<double> reconstruction_gradients_;
  RunSummary summary_;
  long long positivity_limited_updates_{0};
  // Set only while forming a finite-difference Arnoldi action.  The cached
  // baseline gradients then define a frozen-gradient/Picard Jacobian; all
  // nonlinear acceptance and reporting assemblies rebuild gradients.
  bool freeze_gradients_for_jacobian_{false};
  bool initialized_{false};

  [[nodiscard]] Primitive primitive_at(int local_cell) const;
  [[nodiscard]] Primitive reconstructed_primitive(int local_cell, Vec2 point) const;
  [[nodiscard]] std::array<Vec2, 4> primitive_gradients(int local_cell) const;
  void synchronize_state();
  void reconstruct_gradients_and_limit();
  [[nodiscard]] Assembly assemble_spatial_residual();
  [[nodiscard]] ResidualRecord global_residual_record(int step, double time, int inner_iter,
                                                       double cfl, double dt,
                                                       const std::vector<double>& residual) const;
  [[nodiscard]] ForceRecord integrated_forces(int step, double time) const;
  [[nodiscard]] std::vector<SurfaceRecord> build_surface_records() const;
  [[nodiscard]] std::vector<double> local_time_steps(const std::vector<double>& spectral,
                                                      double cfl) const;
  [[nodiscard]] double face_rusanov_dissipation(const Primitive& left,
                                                 const Primitive& right) const;
  void implicit_update(const std::vector<double>& total_residual,
                       const std::vector<double>& spectral,
                       const std::vector<double>& time_diagonal,
                       double relaxation,
                       bool tensor_viscous_blocks = false);
  /// Safeguarded Jacobian-free Newton correction used only for difficult
  /// steady nonlinear plateaus.  The finite-volume residual remains the
  /// convergence metric; this routine mutates state only when a line-search
  /// candidate lowers that fully assembled residual.
  [[nodiscard]] bool try_matrix_free_newton_step(const Assembly& baseline);
  /// Fall back to one frozen block-Jacobian residual correction when the
  /// matrix-free Krylov direction is rejected.  It shares the same global
  /// physicality and residual-decrease acceptance tests.
  [[nodiscard]] bool try_block_residual_step(const Assembly& baseline);
  [[nodiscard]] double cfl_for_step(int step) const;
  [[nodiscard]] double global_norm(const std::vector<double>& residual) const;
  [[nodiscard]] double local_viscosity() const;
  [[nodiscard]] bool transient_force_is_periodic() const;
  void report_residual_peak(const std::vector<double>& residual) const;
  void validate_boundary_map() const;
};

}  // namespace cfd
