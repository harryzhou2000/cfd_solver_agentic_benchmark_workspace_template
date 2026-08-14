#pragma once

#include "cfd/residual.hpp"

#include <mpi.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cfd {

inline constexpr const char* steady_linear_solver_name =
    "matrix_free_gmres_with_full_block_rusanov_lu_sgs";
inline constexpr const char* transient_linear_solver_name =
    "bdf_matrix_free_gmres_with_full_block_rusanov_lu_sgs";
inline constexpr const char* steady_fallback_solver_name =
    "full_block_rusanov_lu_sgs_pseudo_time_fallback";
inline constexpr const char* steady_rescue_solver_name =
    "matrix_free_newton_krylov_lu_sgs_rescue";
inline constexpr const char* steady_trust_region_retry_solver_name =
    "matrix_free_gmres_with_full_block_rusanov_lu_sgs_trust_region_retry";
inline constexpr const char* steady_implicit_bridge_solver_name =
    "full_block_rusanov_lu_sgs_implicit_pseudo_transient_bridge";

enum class SteadyAcceptanceMode {
  none,
  jfnk,
  implicit_trust_region_retry,
  newton_rescue,
  implicit_pseudo_transient_bridge,
  pseudo_time_fallback
};

const char* to_string(SteadyAcceptanceMode mode) noexcept;

struct RestartableSolution {
  std::vector<double> U;
  std::vector<double> U_n;
  std::vector<double> U_nm1;
  std::vector<double> U_best;
  std::size_t physical_step{};
  double time{};
};

struct InnerSolveStats {
  int nonlinear_iterations{};
  int linear_sweeps{};       // Iterations in the latest linear solve.
  int total_linear_sweeps{}; // Sum over all nonlinear iterations.
  std::string linear_solver;
  double initial_defect{};
  double final_defect{};
  double defect_ratio{1.0};
  double initial_nonlinear_residual{};
  double final_nonlinear_residual{};
  double nonlinear_ratio{1.0};
  bool converged{};
};

struct MatrixFreeGmresOptions {
  int restart{16};
  int minimum_iterations{3};
  int maximum_iterations{40};
  double relative_tolerance{0.1};
};

struct MatrixFreeGmresResult {
  int iterations{};
  double initial_residual{};
  double final_residual{};
  double residual_ratio{1.0};
  bool converged{};
};

using DistributedLinearAction =
    std::function<void(const std::vector<double>&, std::vector<double>&)>;

// Frozen first-order Rusanov Jacobian plus sigma/CFL I and an optional physical
// time diagonal. Interior faces retain all four conservative-variable
// couplings. SGS ordering is by global cell ID; remote corrections are
// block-lagged and exchanged between half-sweeps.
class FrozenRusanovLUSGSOperator {
 public:
  FrozenRusanovLUSGSOperator(const DistributedMesh& mesh,
                             const CaseConfig& config,
                             const std::vector<double>& state,
                             const ResidualResult& first_order_residual,
                             double cfl, MPI_Comm communicator,
                             double physical_diagonal = 0.0,
                             double pseudo_time_diagonal_scale = 1.0);

  void apply(const std::vector<double>& direction,
             std::vector<double>& product) const;
  InnerSolveStats solve(const std::vector<double>& right_hand_side,
                        int minimum_sweeps, int maximum_sweeps,
                        double relative_tolerance,
                        std::vector<double>& correction) const;

 private:
  const DistributedMesh& mesh_;
  MPI_Comm communicator_;
  std::vector<ConservativeJacobian> diagonal_;
  std::vector<ConservativeJacobian> inverse_diagonal_;
  std::vector<ConservativeJacobian> left_to_right_;
  std::vector<ConservativeJacobian> right_to_left_;
  std::vector<std::size_t> forward_order_;
};

// Right-preconditioned restarted GMRES. Vectors contain owned entries followed
// by optional ghost entries; all Krylov reductions use owned entries only.
MatrixFreeGmresResult restarted_gmres(
    const DistributedMesh& mesh, const std::vector<double>& right_hand_side,
    const DistributedLinearAction& apply_operator,
    const DistributedLinearAction& apply_right_preconditioner,
    const MatrixFreeGmresOptions& options, MPI_Comm communicator,
    std::vector<double>& solution);

// Matrix-free action for sigma/CFL I + J_R(U), where R uses the active spatial
// continuation blend. The finite-difference perturbation is
// globally scaled and reduced until every owned perturbed state is admissible.
class SteadyMatrixFreeOperator {
 public:
  SteadyMatrixFreeOperator(const DistributedMesh& mesh, ResidualOperator& residual,
                           const std::vector<double>& state,
                           const ResidualResult& base_residual, double cfl,
                           MPI_Comm communicator,
                           double reconstruction_blend = 1.0);

  void apply(const std::vector<double>& direction, std::vector<double>& product,
             double epsilon_multiplier = 1.0);
  double last_epsilon() const noexcept { return last_epsilon_; }
  int last_epsilon_halvings() const noexcept { return last_epsilon_halvings_; }

 private:
  const DistributedMesh& mesh_;
  ResidualOperator& residual_;
  const std::vector<double>& state_;
  const ResidualResult& base_residual_;
  double cfl_{};
  double reconstruction_blend_{1.0};
  MPI_Comm communicator_;
  std::array<double, 4> component_scale_{};
  double scaled_state_norm_{};
  double last_epsilon_{};
  int last_epsilon_halvings_{};
  std::vector<double> perturbed_state_;
  std::vector<double> positive_residual_value_;
  ResidualResult perturbed_residual_;
};

// Matrix-free physical Newton action for J_R(U) + alpha_0 V/dt I. The BDF
// histories are retained as immutable references so
// their dimensions and lifetime are explicit, while their derivative is
// exactly zero. BDF1 uses alpha_0=1 and BDF2 uses alpha_0=3/2.
class TransientMatrixFreeOperator {
 public:
  TransientMatrixFreeOperator(
      const DistributedMesh& mesh, ResidualOperator& residual,
      const std::vector<double>& state, const ResidualResult& base_residual,
      const std::vector<double>& history_n,
      const std::vector<double>& history_nm1, double time_step, int bdf_order,
      double cfl, MPI_Comm communicator);

  void apply(const std::vector<double>& direction,
             std::vector<double>& product,
             double epsilon_multiplier = 1.0);
  double last_epsilon() const noexcept { return last_epsilon_; }
  int last_epsilon_halvings() const noexcept { return last_epsilon_halvings_; }
  double physical_diagonal() const noexcept { return physical_diagonal_; }

 private:
  const DistributedMesh& mesh_;
  ResidualOperator& residual_;
  const std::vector<double>& state_;
  const std::vector<double>& history_n_;
  const std::vector<double>& history_nm1_;
  const ResidualResult& base_residual_;
  double cfl_{};
  double physical_diagonal_{};
  MPI_Comm communicator_;
  std::array<double, 4> component_scale_{};
  double scaled_state_norm_{};
  double last_epsilon_{};
  int last_epsilon_halvings_{};
  std::vector<double> perturbed_state_;
  ResidualResult perturbed_residual_;
};

// The active phase baseline is persisted in FlowSolverContinuation.  Keeping
// the multiplier in [1e-3, 1] prevents both nonlocal limiter secants near
// convergence and unbounded roundoff amplification.
inline constexpr double steady_jfnk_minimum_epsilon_multiplier = 1.0e-3;
double steady_jfnk_epsilon_multiplier(double nonlinear_residual,
                                      double reference_residual) noexcept;

Conservative bdf_physical_residual(const Conservative& Ustar,
                                   const Conservative& Un,
                                   const Conservative& Unm1,
                                   double cell_volume, double time_step,
                                   int order);

double steady_residual_growth_fraction(double cfl) noexcept;
bool steady_residual_within_envelope(double current_residual,
                                     double trial_residual,
                                     double best_residual,
                                     double cfl) noexcept;
inline constexpr std::size_t steady_fallback_window_capacity = 32U;
inline constexpr std::size_t steady_fallback_jfnk_retry_interval = 32U;
inline constexpr double steady_fallback_minimum_cfl = 1.0e-6;
inline constexpr std::size_t steady_rescue_stagnation_steps = 16U;
inline constexpr std::size_t steady_rescue_retry_interval = 32U;
inline constexpr std::size_t steady_trust_region_retry_interval = 16U;
inline constexpr std::size_t steady_fallback_maximum_bridge_steps = 64U;
inline constexpr std::size_t steady_nonmonotone_window_capacity = 10U;
inline constexpr std::size_t steady_nonmonotone_stagnation_attempts = 3U;
inline constexpr std::size_t steady_nonmonotone_watchdog_steps = 20U;
inline constexpr double steady_nonmonotone_best_residual_cap = 1.01;
// A best-residual update must exceed both a conservative floating-point noise
// floor and this residual-scaled relative floor before it can reset
// globalization stagnation or the nonmonotone watchdog.
inline constexpr double steady_meaningful_best_relative_decrease = 1.0e-12;
inline constexpr double steady_nonmonotone_surrogate_fraction = 0.1;
inline constexpr double steady_nonmonotone_surrogate_minimum_fraction = 1.0e-8;
inline constexpr double steady_nonmonotone_activation_envelope_fraction = 1.0e-4;
inline constexpr double steady_implicit_bridge_initial_cfl = 0.1;
inline constexpr double steady_implicit_bridge_residual_cap = 1.25;
inline constexpr double steady_implicit_bridge_growth_contraction_threshold =
    1.05;
inline constexpr std::size_t steady_implicit_bridge_watchdog_steps = 500U;
// Limit one interval without a meaningful best, not cumulative productive
// bridge work. Cumulative accounting must not permanently disable a bridge
// that continues to lower the best residual.
inline constexpr std::size_t steady_implicit_bridge_maximum_steps = 500U;

bool steady_implicit_fallback_allowed(double reconstruction_blend,
                                      bool disabled,
                                      std::size_t consecutive_steps) noexcept;
bool steady_fallback_mode_has_history(bool requested_mode, bool disabled,
                                      std::size_t residual_samples) noexcept;
void steady_nonmonotone_push_residual(std::vector<double>& window,
                                      double residual);
double steady_nonmonotone_reference(const std::vector<double>& window,
                                    double current_residual) noexcept;
bool steady_nonmonotone_trial_acceptable(
    double reference_residual, double trial_residual, double best_residual,
    double predicted_merit_reduction) noexcept;
bool steady_nonmonotone_eligible(
    bool full_order_implicit, bool bridge_allowed, double best_residual,
    const std::vector<double>& residual_window) noexcept;
bool steady_meaningful_best_decrease(double best_residual,
                                     double trial_residual) noexcept;
std::size_t steady_nonmonotone_stagnation_after_attempt(
    std::size_t current_streak, bool full_order_implicit,
    bool meaningful_best_improvement) noexcept;
bool steady_limiter_nonlinearity_active(
    const GlobalDiagnostics& diagnostics) noexcept;
bool steady_nonmonotone_descent_bypass_allowed(
    bool nonmonotone_eligible, bool gmres_converged,
    double merit_slope, double residual_norm) noexcept;
double steady_nonmonotone_surrogate_slope(
    double residual_norm, double gmres_residual_ratio) noexcept;
double steady_nonmonotone_activation_reference(double best_residual) noexcept;
double steady_nonmonotone_update_envelope_reference(
    double current_reference, double best_residual, bool seed_now) noexcept;
double steady_nonmonotone_bounded_reference(
    const std::vector<double>& window, double current_residual,
    double best_residual, double envelope_reference) noexcept;
double steady_implicit_bridge_bounded_initial_cfl(
    double minimum_cfl, double maximum_cfl) noexcept;
bool steady_implicit_bridge_residual_within_cap(
    double entry_best_residual, double trial_residual) noexcept;
bool steady_implicit_bridge_eligible(
    bool disabled, bool active, std::size_t stagnation_attempts,
    double best_residual) noexcept;
double steady_implicit_bridge_epoch_reference(
    double best_residual, double previous_residual) noexcept;
double steady_phase_best_with_entry_evidence(
    double best_residual, double evaluated_entry_residual) noexcept;
double steady_largest_positivity_safe_scale(
    const std::vector<double>& state, const std::vector<double>& direction,
    std::size_t owned_cell_count,
    const CaloricallyPerfectGas& gas) noexcept;
struct SteadyImplicitBridgeState {
  bool active{};
  bool disabled{};
  double cfl{steady_implicit_bridge_initial_cfl};
  double entry_best_residual{-1.0};
  std::size_t attempts{};
  std::size_t accepted_steps{};
  std::size_t rejected_steps{};
  std::size_t accepted_steps_since_best{};
  double residual_minimum{-1.0};
  double residual_maximum{-1.0};
  double maximum_relative_growth{};
  std::size_t meaningful_best_improvements{};
  std::size_t watchdog_stops{};
  std::size_t linear_sweeps{};
};
void update_steady_implicit_bridge(
    SteadyImplicitBridgeState& state, bool accepted,
    bool meaningful_best_improvement, double previous_residual,
    double trial_residual, double minimum_cfl, double maximum_cfl) noexcept;
void note_steady_implicit_bridge_external_best(
    SteadyImplicitBridgeState& state) noexcept;
bool steady_nonmonotone_watchdog_expired(
    std::size_t accepted_steps_since_strict_best) noexcept;
struct SteadyNonmonotoneWatchdogState {
  bool active{};
  bool disabled{};
  std::size_t accepted_steps_since_strict_best{};
  std::size_t resets{};
};
void update_steady_nonmonotone_watchdog(
    SteadyNonmonotoneWatchdogState& state, bool accepted_step,
    bool strict_best_improvement) noexcept;

struct SteadySpatialOrderSchedule {
  std::size_t first_order_steps{};
  std::size_t ramp_steps{};
};

SteadySpatialOrderSchedule steady_spatial_order_schedule(
    int pseudo_cfl_ramp_steps) noexcept;
std::size_t steady_full_order_minimum_steps(
    int pseudo_cfl_ramp_steps) noexcept;
bool steady_convergence_gate(double reconstruction_blend,
                             std::size_t full_order_accepted_steps,
                             int pseudo_cfl_ramp_steps,
                             double original_initial_residual,
                             double current_residual,
                             double residual_reduction_target) noexcept;
double smooth_reconstruction_blend(std::size_t ramp_step,
                                   std::size_t ramp_steps) noexcept;
double steady_fallback_runaway_bound(
    const std::vector<double>& recent_residuals,
    double initial_residual, double best_residual,
    double current_residual) noexcept;
bool steady_fallback_within_runaway_bound(
    const std::vector<double>& recent_residuals,
    double initial_residual, double best_residual,
    double current_residual, double trial_residual) noexcept;

std::vector<double> steady_trust_region_retry_cfls(double current_cfl,
                                                   double maximum_cfl);
std::optional<std::size_t> best_strict_residual_decrease(
    double initial_residual, const std::vector<double>& trial_residuals) noexcept;
bool strict_steady_merit_decrease(double initial_residual,
                                  double trial_residual,
                                  double predicted_merit_reduction) noexcept;

struct ImplicitTrustRegionCandidateDiagnostics {
  double cfl{};
  int gmres_iterations{};
  double gmres_ratio{1.0};
  bool gmres_converged{};
  int line_search_evaluations{};
  double best_line_scale{};
  double best_trial_residual{};
  double best_update_norm{};
  std::array<double, 4> initial_component_l2{};
  std::array<double, 4> best_component_l2{};
  double epsilon_reference_residual{};
  double epsilon_multiplier{1.0};
  double last_epsilon{};
  int last_epsilon_halvings{};
  bool strictly_decreasing{};
};

struct SteadyCflAdaptationState {
  double cfl{};
  double trend_reference_residual{-1.0};
  int trend_samples{};
  bool probe_active{};
  int rejected_attempts{};
  double recovery_probe_cfl{};
  bool recovery_restore_pending{};
};

void update_steady_cfl_adaptation(SteadyCflAdaptationState& state,
                                  bool accepted,
                                  bool residual_increased,
                                  bool tiny_line_scale,
                                  double residual,
                                  double cfl_floor,
                                  double scheduled_cfl) noexcept;

struct TransientConvergenceStats {
  int observed_min{};
  double observed_mean{};
  int observed_max{};
  std::size_t target_misses{};
  double target_met_fraction{};
  double last_ratio{1.0};
};

struct FlowSolverContinuation {
  double cfl{};
  std::size_t nonlinear_steps{};
  double steady_previous_residual{-1.0};
  double steady_best_residual{-1.0};
  double steady_trend_reference_residual{-1.0};
  int steady_trend_samples{};
  bool steady_probe_active{};
  int steady_rejected_attempts{};
  double steady_recovery_probe_cfl{};
  bool steady_recovery_restore_pending{};
  std::size_t steady_jfnk_accepted_steps{};
  std::size_t steady_fallback_accepted_steps{};
  std::size_t steady_fallback_attempts{};
  std::size_t steady_fallback_rejected_steps{};
  std::size_t steady_fallback_cfl_halvings{};
  double steady_last_fallback_cfl{};
  bool steady_fallback_mode{};
  double steady_fallback_cfl{0.1};
  std::vector<double> steady_fallback_residual_window;
  std::size_t steady_fallback_steps_since_jfnk{};
  int steady_jfnk_failure_streak{};
  std::size_t steady_jfnk_attempts{};
  double steady_initial_residual_scale{-1.0};
  bool steady_initial_residual_is_original_run{true};
  double steady_jfnk_epsilon_reference_residual{-1.0};
  double steady_jfnk_epsilon_multiplier{1.0};
  double steady_jfnk_last_epsilon{};
  int steady_jfnk_last_epsilon_halvings{};
  double steady_reconstruction_blend{};
  std::size_t steady_first_order_accepted_steps{};
  std::size_t steady_order_ramp_accepted_steps{};
  std::size_t steady_full_order_accepted_steps{};
  double steady_full_order_initial_residual{-1.0};
  double steady_full_order_best_residual{-1.0};
  bool steady_order_rescue_promoted{};
  std::size_t steady_rescue_attempts{};
  std::size_t steady_rescue_accepted_steps{};
  std::size_t steady_rescue_total_gmres_iterations{};
  int steady_rescue_last_gmres_iterations{};
  int steady_rescue_max_gmres_iterations{};
  double steady_rescue_last_gmres_ratio{1.0};
  double steady_rescue_last_cfl{};
  double steady_rescue_last_line_scale{};
  double steady_rescue_reference_residual{-1.0};
  std::size_t steady_rescue_stagnation_count{};
  std::size_t steady_rescue_cooldown_attempts{};
  std::size_t steady_trust_region_retry_batches{};
  std::size_t steady_trust_region_retry_candidates{};
  std::size_t steady_trust_region_retry_accepted_steps{};
  std::size_t steady_trust_region_retry_total_gmres_iterations{};
  std::size_t steady_trust_region_retry_last_candidate_count{};
  int steady_trust_region_retry_last_total_gmres_iterations{};
  int steady_trust_region_retry_last_accepted_gmres_iterations{};
  double steady_trust_region_retry_last_accepted_cfl{};
  double steady_trust_region_retry_last_line_scale{};
  double steady_trust_region_retry_last_initial_residual{-1.0};
  double steady_trust_region_retry_last_final_residual{-1.0};
  std::size_t steady_trust_region_retry_cooldown_attempts{};
  bool steady_fallback_disabled{};
  std::size_t steady_fallback_consecutive_accepted_steps{};
  std::size_t steady_fallback_growth_disables{};
  std::size_t steady_lusgs_preconditioner_applications{};
  std::size_t steady_lusgs_preconditioner_sweeps{};
  double steady_lusgs_last_defect_ratio{1.0};
  TransientConvergenceStats transient_stats{};
  std::size_t transient_samples{};
  double transient_iteration_sum{};
  bool steady_target_met{};
  std::vector<double> steady_nonmonotone_residual_window;
  std::size_t steady_strict_decrease_stagnation_streak{};
  bool steady_nonmonotone_bridge_active{};
  bool steady_nonmonotone_bridge_disabled{};
  std::size_t steady_nonmonotone_steps_since_strict_best{};
  std::size_t steady_nonmonotone_accepted_steps{};
  double steady_nonmonotone_max_relative_increase{};
  std::size_t steady_nonmonotone_strict_best_improvements{};
  std::size_t steady_nonmonotone_watchdog_resets{};
  std::size_t steady_nonmonotone_bypass_attempts{};
  std::size_t steady_nonmonotone_bypass_accepted_steps{};
  std::size_t steady_nonmonotone_bypass_trial_evaluations{};
  double steady_nonmonotone_bypass_last_actual_trial_residual{-1.0};
  double steady_nonmonotone_bypass_last_gmres_ratio{1.0};
  double steady_nonmonotone_envelope_reference{-1.0};
  std::size_t steady_nonmonotone_envelope_accepted_steps{};
  double steady_nonmonotone_envelope_max_relative_increase{};
  SteadyImplicitBridgeState steady_implicit_bridge;
};

struct StepResult {
  bool accepted{};
  bool target_met{};
  double wall_time_seconds{};
  ResidualNorms residual{};
  ForceCoefficients forces{};
  InnerSolveStats inner{};
  double cfl{};
  double line_search_scale{};
  double predictor_scale{};  // Zero for BDF1, blend fraction for BDF2.
  bool nonmonotone_residual_increase{};
  SteadyAcceptanceMode steady_acceptance{SteadyAcceptanceMode::none};
  bool fallback_attempted{};
  double fallback_cfl{};
  int fallback_sweeps{};
  int fallback_cfl_halvings{};
  int jfnk_iterations{};
  bool jfnk_attempted{};
  double jfnk_epsilon_reference_residual{-1.0};
  double jfnk_epsilon_multiplier{1.0};
  double jfnk_last_epsilon{};
  int jfnk_last_epsilon_halvings{};
  bool fallback_mode{};
  double reconstruction_blend{1.0};
  std::size_t first_order_accepted_steps{};
  std::size_t order_ramp_accepted_steps{};
  std::size_t full_order_accepted_steps{};
  double steady_initial_residual_baseline{-1.0};
  double full_order_residual_baseline{-1.0};
  bool rescue_attempted{};
  bool rescue_accepted{};
  double rescue_cfl{};
  int rescue_gmres_iterations{};
  double rescue_gmres_ratio{1.0};
  double rescue_initial_residual{};
  double rescue_final_residual{};
  bool trust_region_retry_attempted{};
  bool trust_region_retry_accepted{};
  int trust_region_retry_candidates{};
  int trust_region_retry_total_gmres_iterations{};
  int trust_region_retry_accepted_gmres_iterations{};
  double trust_region_retry_accepted_cfl{};
  double trust_region_retry_initial_residual{};
  double trust_region_retry_final_residual{};
  std::vector<ImplicitTrustRegionCandidateDiagnostics>
      trust_region_retry_diagnostics;
  int lusgs_preconditioner_applications{};
  int lusgs_preconditioner_sweeps{};
  double lusgs_last_defect_ratio{1.0};
  bool limiter_active{};
  bool nonmonotone_bridge_attempted{};
  bool nonmonotone_bridge_accepted{};
  bool nonmonotone_direction_descent{};
  bool nonmonotone_descent_bypass_attempted{};
  bool nonmonotone_descent_bypass_accepted{};
  int nonmonotone_trial_evaluations{};
  double nonmonotone_best_trial_residual{-1.0};
  double nonmonotone_bypass_gmres_ratio{1.0};
  bool nonmonotone_envelope_seeded{};
  bool nonmonotone_envelope_accepted{};
  double nonmonotone_envelope_reference{-1.0};
  double nonmonotone_envelope_relative_increase{};
  bool implicit_bridge_attempted{};
  bool implicit_bridge_accepted{};
  double implicit_bridge_cfl{};
  double implicit_bridge_line_scale{};
  double implicit_bridge_initial_residual{-1.0};
  double implicit_bridge_final_residual{-1.0};
  double implicit_bridge_relative_growth{};
  int implicit_bridge_linear_sweeps{};
  double nonmonotone_reference_residual{};
  double nonmonotone_relative_increase{};
  bool meaningful_strict_best_improvement{};
  bool noise_scale_strict_best_improvement{};
};

class FlowSolver {
 public:
  FlowSolver(const DistributedMesh& mesh, const CaseConfig& config,
             MPI_Comm communicator);

  RestartableSolution uniform_initial_solution() const;
  StepResult steady_step(RestartableSolution& solution);
  StepResult transient_step(RestartableSolution& solution);
  std::vector<StepResult> run_steady(RestartableSolution& solution);
  std::vector<StepResult> run_transient(RestartableSolution& solution);

  const TransientConvergenceStats& transient_stats() const noexcept {
    return transient_stats_;
  }
  bool steady_target_met() const noexcept { return steady_target_met_; }
  FlowSolverContinuation continuation_state() const noexcept;
  void restore_continuation_state(const FlowSolverContinuation& state);

 private:
  StepResult implicit_step(RestartableSolution& solution, bool physical_time,
                           bool attempt_steady_jfnk = true,
                           bool attempt_trust_region_retry = false,
                           bool attempt_newton_rescue = false,
                           bool allow_steady_fallback = true,
                           bool allow_steady_nonmonotone = false,
                           bool nonmonotone_envelope_seeded = false,
                           bool allow_steady_implicit_bridge = false);
  void validate_solution(const RestartableSolution& solution) const;

  const DistributedMesh& mesh_;
  const CaseConfig& config_;
  MPI_Comm communicator_;
  ResidualOperator residual_;
  double cfl_{};
  std::size_t nonlinear_steps_{};
  TransientConvergenceStats transient_stats_{};
  std::size_t transient_samples_{};
  double transient_iteration_sum_{};
  bool steady_target_met_{};
  double steady_previous_residual_{-1.0};
  double steady_best_residual_{-1.0};
  double steady_trend_reference_residual_{-1.0};
  int steady_trend_samples_{};
  bool steady_probe_active_{};
  int steady_rejected_attempts_{};
  double steady_recovery_probe_cfl_{};
  bool steady_recovery_restore_pending_{};
  std::size_t steady_jfnk_accepted_steps_{};
  std::size_t steady_fallback_accepted_steps_{};
  std::size_t steady_fallback_attempts_{};
  std::size_t steady_fallback_rejected_steps_{};
  std::size_t steady_fallback_cfl_halvings_{};
  double steady_last_fallback_cfl_{};
  bool steady_fallback_mode_{};
  double steady_fallback_cfl_{0.1};
  std::vector<double> steady_fallback_residual_window_;
  std::size_t steady_fallback_steps_since_jfnk_{};
  int steady_jfnk_failure_streak_{};
  std::size_t steady_jfnk_attempts_{};
  double steady_initial_residual_scale_{-1.0};
  double steady_jfnk_epsilon_reference_residual_{-1.0};
  double steady_jfnk_epsilon_multiplier_{1.0};
  double steady_jfnk_last_epsilon_{};
  int steady_jfnk_last_epsilon_halvings_{};
  double steady_reconstruction_blend_{};
  std::size_t steady_first_order_accepted_steps_{};
  std::size_t steady_order_ramp_accepted_steps_{};
  std::size_t steady_full_order_accepted_steps_{};
  double steady_full_order_initial_residual_{-1.0};
  double steady_full_order_best_residual_{-1.0};
  bool steady_order_rescue_promoted_{};
  std::size_t steady_rescue_attempts_{};
  std::size_t steady_rescue_accepted_steps_{};
  std::size_t steady_rescue_total_gmres_iterations_{};
  int steady_rescue_last_gmres_iterations_{};
  int steady_rescue_max_gmres_iterations_{};
  double steady_rescue_last_gmres_ratio_{1.0};
  double steady_rescue_last_cfl_{};
  double steady_rescue_last_line_scale_{};
  double steady_rescue_reference_residual_{-1.0};
  std::size_t steady_rescue_stagnation_count_{};
  std::size_t steady_rescue_cooldown_attempts_{};
  std::size_t steady_trust_region_retry_batches_{};
  std::size_t steady_trust_region_retry_candidates_{};
  std::size_t steady_trust_region_retry_accepted_steps_{};
  std::size_t steady_trust_region_retry_total_gmres_iterations_{};
  std::size_t steady_trust_region_retry_last_candidate_count_{};
  int steady_trust_region_retry_last_total_gmres_iterations_{};
  int steady_trust_region_retry_last_accepted_gmres_iterations_{};
  double steady_trust_region_retry_last_accepted_cfl_{};
  double steady_trust_region_retry_last_line_scale_{};
  double steady_trust_region_retry_last_initial_residual_{-1.0};
  double steady_trust_region_retry_last_final_residual_{-1.0};
  std::size_t steady_trust_region_retry_cooldown_attempts_{};
  bool steady_fallback_disabled_{};
  std::size_t steady_fallback_consecutive_accepted_steps_{};
  std::size_t steady_fallback_growth_disables_{};
  std::size_t steady_lusgs_preconditioner_applications_{};
  std::size_t steady_lusgs_preconditioner_sweeps_{};
  double steady_lusgs_last_defect_ratio_{1.0};
  std::vector<double> steady_nonmonotone_residual_window_;
  std::size_t steady_strict_decrease_stagnation_streak_{};
  bool steady_nonmonotone_bridge_active_{};
  bool steady_nonmonotone_bridge_disabled_{};
  std::size_t steady_nonmonotone_steps_since_strict_best_{};
  std::size_t steady_nonmonotone_accepted_steps_{};
  double steady_nonmonotone_max_relative_increase_{};
  std::size_t steady_nonmonotone_strict_best_improvements_{};
  std::size_t steady_nonmonotone_watchdog_resets_{};
  std::size_t steady_nonmonotone_bypass_attempts_{};
  std::size_t steady_nonmonotone_bypass_accepted_steps_{};
  std::size_t steady_nonmonotone_bypass_trial_evaluations_{};
  double steady_nonmonotone_bypass_last_actual_trial_residual_{-1.0};
  double steady_nonmonotone_bypass_last_gmres_ratio_{1.0};
  double steady_nonmonotone_envelope_reference_{-1.0};
  std::size_t steady_nonmonotone_envelope_accepted_steps_{};
  double steady_nonmonotone_envelope_max_relative_increase_{};
  SteadyImplicitBridgeState steady_implicit_bridge_;
  ResidualResult current_residual_workspace_;
  ResidualResult trial_residual_workspace_;
  std::vector<double> nonlinear_residual_workspace_;
  std::vector<double> rhs_workspace_;
  std::vector<double> trial_total_workspace_;
  std::vector<double> cached_total_workspace_;
  std::vector<double> original_u_workspace_;
  std::vector<double> rollback_u_workspace_;
  std::vector<double> predictor_rollback_u_workspace_;
  std::vector<double> correction_workspace_;
};

}  // namespace cfd
