#pragma once

#include "cfd/solver.hpp"

#include <mpi.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cfd {

inline constexpr const char* residuals_csv_header =
    "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf";
inline constexpr const char* forces_csv_header =
    "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift";
inline constexpr const char* surface_csv_header =
    "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag";
inline constexpr const char* partition_csv_header =
    "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells";

struct CliOptions {
  std::filesystem::path case_file;
  std::filesystem::path output_directory;
  std::optional<std::filesystem::path> restart_file;
  bool resume_output{};
  std::string report_level{"brief"};
  std::optional<int> max_steps_override;
  std::optional<double> final_time_override;
  std::optional<int> progress_every;
  std::optional<int> flush_every;
};

CliOptions parse_cli(int argc, char** argv);
std::string cli_usage();
std::string shell_join(int argc, char** argv);
std::string utc_timestamp();

struct InnerStatistics {
  std::size_t samples{};
  int minimum{};
  int maximum{};
  double mean{};
  std::size_t target_misses{};
  double target_converged_fraction{};
  double last_ratio{1.0};

  void observe(int iterations, bool target_met, double ratio);
};

struct PeriodicityResult {
  bool passed{};
  std::size_t samples{};
  int first_half_upcrossings{};
  int second_half_upcrossings{};
  double relative_drag_mean_drift{};
  double relative_lift_rms_drift{};
  double period_coefficient_of_variation{};
  double mean_drag{};
  double lift_rms{};
  std::string explanation;
};

struct SteadyAcceptanceStatistics {
  std::size_t jfnk_attempts{};
  std::size_t jfnk_accepted_steps{};
  std::size_t fallback_accepted_steps{};
  std::size_t fallback_attempts{};
  std::size_t fallback_rejected_steps{};
  std::size_t fallback_cfl_halvings{};
  double last_fallback_cfl{};
  bool fallback_mode{};
  double operating_fallback_cfl{0.1};
  std::size_t fallback_window_samples{};
  std::size_t fallback_steps_since_jfnk{};
  std::size_t rescue_attempts{};
  std::size_t rescue_accepted_steps{};
  std::size_t rescue_total_gmres_iterations{};
  int rescue_last_gmres_iterations{};
  int rescue_max_gmres_iterations{};
  double rescue_last_gmres_ratio{1.0};
  double rescue_last_cfl{};
  double rescue_last_line_scale{};
  std::size_t rescue_cooldown_attempts{};
  std::size_t trust_region_retry_batches{};
  std::size_t trust_region_retry_candidates{};
  std::size_t trust_region_retry_accepted_steps{};
  std::size_t trust_region_retry_total_gmres_iterations{};
  std::size_t trust_region_retry_last_candidate_count{};
  int trust_region_retry_last_total_gmres_iterations{};
  int trust_region_retry_last_accepted_gmres_iterations{};
  double trust_region_retry_last_accepted_cfl{};
  double trust_region_retry_last_line_scale{};
  double trust_region_retry_last_initial_residual{-1.0};
  double trust_region_retry_last_final_residual{-1.0};
  std::size_t trust_region_retry_cooldown_attempts{};
  bool fallback_disabled{};
  std::size_t fallback_growth_disables{};
  std::size_t lusgs_preconditioner_applications{};
  std::size_t lusgs_preconditioner_sweeps{};
  double lusgs_last_defect_ratio{1.0};
  double jfnk_epsilon_reference_residual{-1.0};
  double jfnk_epsilon_multiplier{1.0};
  double jfnk_last_epsilon{};
  int jfnk_last_epsilon_halvings{};
  std::size_t nonmonotone_window_samples{};
  std::size_t strict_decrease_stagnation_streak{};
  bool nonmonotone_bridge_active{};
  bool nonmonotone_bridge_disabled{};
  std::size_t nonmonotone_steps_since_strict_best{};
  std::size_t nonmonotone_accepted_steps{};
  double nonmonotone_max_relative_increase{};
  std::size_t nonmonotone_strict_best_improvements{};
  std::size_t nonmonotone_watchdog_resets{};
  std::size_t nonmonotone_bypass_attempts{};
  std::size_t nonmonotone_bypass_accepted_steps{};
  std::size_t nonmonotone_bypass_trial_evaluations{};
  double nonmonotone_bypass_last_actual_trial_residual{-1.0};
  double nonmonotone_bypass_last_gmres_ratio{1.0};
  double nonmonotone_envelope_reference{-1.0};
  std::size_t nonmonotone_envelope_accepted_steps{};
  double nonmonotone_envelope_max_relative_increase{};
  SteadyImplicitBridgeState implicit_bridge;
};

struct SteadySpatialOrderStatistics {
  std::size_t first_order_target_steps{};
  std::size_t ramp_target_steps{};
  std::size_t first_order_accepted_steps{};
  std::size_t ramp_accepted_steps{};
  std::size_t full_order_accepted_steps{};
  std::size_t minimum_full_order_steps{};
  double final_blend{1.0};
  double original_initial_residual{-1.0};
  double full_order_initial_residual{-1.0};
  double full_order_best_residual{-1.0};
  bool final_outputs_full_order{true};
  bool promoted_by_newton_rescue{};
};

PeriodicityResult test_force_periodicity(const std::vector<std::pair<double, double>>& forces);

struct ContinuationState {
  double steady_residual_baseline{-1.0};
  double last_residual{-1.0};
  InnerStatistics inner;
  std::vector<std::pair<double, double>> force_window;
  std::string original_start_time_utc;
  bool all_accepted_transient_targets{true};
  bool history_complete{true};
  std::size_t rollbacks{};
  std::size_t total_attempted_steps{};
  int last_inner_iterations{};
  std::size_t residual_output_rows{};
  std::size_t force_output_rows{};
  std::optional<std::size_t> last_residual_output_step;
  std::optional<std::size_t> last_force_output_step;
  FlowSolverContinuation solver;
};

struct RestartData {
  RestartableSolution solution;
  ContinuationState continuation;
};

struct PhysicsGateResult {
  bool passed{};
  bool finite_positive_owned{};
  bool finite_positive_surface{};
  bool nontrivial_wall_cp{};
  bool positive_body_drag{};
  bool inviscid_viscous_zero{};
  bool no_slip_speed_zero{};
  double minimum_owned_rho{};
  double minimum_owned_pressure{};
  double minimum_surface_rho{};
  double minimum_surface_pressure{};
  double wall_cp_minimum{};
  double wall_cp_maximum{};
  double wall_cp_range{};
  double body_drag{};
  double maximum_no_slip_speed{};
  std::string explanation;
};

struct RunReport {
  std::string command;
  std::string start_time_utc;
  std::string end_time_utc;
  double wall_time_seconds{};
  std::size_t final_step{};
  double final_physical_time{};
  std::string convergence_status{"failed"};
  bool completed{};
  double residual_reduction_orders{};
  double full_order_residual_reduction_orders{};
  std::string notes;
  InnerStatistics inner;
  PeriodicityResult periodicity;
  PhysicsGateResult physics_gates;
  GlobalDiagnostics final_reconstruction_diagnostics;
  SteadyAcceptanceStatistics steady_acceptance;
  SteadySpatialOrderStatistics steady_spatial_order;
  std::vector<std::pair<std::string, std::string>> diagnostic_overrides;
  std::string git_revision;
  bool source_dirty{};
  std::string executable_sha256;
  std::string mesh_fingerprint;
  std::string case_fingerprint;
  bool resumed_output{};
  bool history_complete{true};
};

class RunOutput {
 public:
  RunOutput(const std::filesystem::path& directory, int rank, bool resume = false,
            std::size_t checkpoint_step = 0U,
            std::optional<std::size_t> checkpoint_residual_step = std::nullopt,
            std::optional<std::size_t> checkpoint_force_step = std::nullopt);

  void log(const std::string& message);
  void write_residual(std::size_t step, double time, int inner_iter, double cfl,
                      double dt, const ResidualNorms& residual);
  void write_force(std::size_t step, double time, const ForceCoefficients& force);
  void flush();
  std::optional<std::size_t> last_residual_step() const noexcept {
    return last_residual_step_;
  }
  std::optional<std::size_t> last_force_step() const noexcept { return last_force_step_; }
  std::size_t residual_rows() const noexcept { return residual_rows_; }
  std::size_t force_rows() const noexcept { return force_rows_; }

  const std::filesystem::path& directory() const noexcept { return directory_; }

 private:
  std::filesystem::path directory_;
  int rank_{};
  std::ofstream residuals_;
  std::ofstream forces_;
  std::ofstream log_;
  std::optional<std::size_t> last_residual_step_;
  std::optional<std::size_t> last_force_step_;
  std::size_t residual_rows_{};
  std::size_t force_rows_{};
};

void write_partition_diagnostics(const std::filesystem::path& path,
                                 const DistributedMesh& mesh,
                                 MPI_Comm communicator);

std::string mesh_fingerprint(const DistributedMesh& mesh, MPI_Comm communicator);
std::string case_fingerprint(const CaseConfig& config);
std::string sha256_file(const std::filesystem::path& path);
std::filesystem::path running_executable_path(const char* argv0);
std::string build_git_revision();
bool build_source_dirty();
void write_restart(const std::filesystem::path& path, const std::string& fingerprint,
                   const std::string& executable_sha256,
                   const CaseConfig& config, const DistributedMesh& mesh,
                   const RestartableSolution& solution,
                   const ContinuationState& continuation, MPI_Comm communicator);
RestartData read_restart(const std::filesystem::path& path,
                         const std::string& fingerprint,
                         const std::string& executable_sha256,
                         const CaseConfig& config,
                         const DistributedMesh& mesh,
                         MPI_Comm communicator);

struct FinalEvaluation {
  ResidualResult residual;
  ForceCoefficients forces;
};

FinalEvaluation evaluate_final_state(const DistributedMesh& mesh,
                                     const CaseConfig& config,
                                     RestartableSolution& solution, double cfl,
                                     MPI_Comm communicator);
PhysicsGateResult evaluate_physics_gates(const DistributedMesh& mesh,
                                         const CaseConfig& config,
                                         const RestartableSolution& solution,
                                         const ResidualResult& residual,
                                         const ForceCoefficients& forces,
                                         MPI_Comm communicator);
void write_surface(const std::filesystem::path& path, const CaseConfig& config,
                   const DistributedMesh& mesh, const ResidualResult& residual,
                   MPI_Comm communicator);
void write_vtu(const std::filesystem::path& path, const CaseConfig& config,
               const DistributedMesh& mesh, const RestartableSolution& solution,
               MPI_Comm communicator);
void write_metadata_and_status(const std::filesystem::path& directory,
                               const CaseConfig& config,
                               const DistributedMesh& mesh,
                               const RunReport& report);

}  // namespace cfd
