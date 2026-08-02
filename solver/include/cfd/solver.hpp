#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <mpi.h>

#include "cfd/case_config.hpp"
#include "cfd/partition.hpp"
#include "cfd/types.hpp"

namespace cfd {

struct SolverResidualSample {
    int step{};
    Real physical_time{};
    int inner_iteration{};
    Real cfl{};
    Real physical_dt{};
    State component_l2{};
    Real residual_l2{};
    Real residual_linf{};
};

struct SolverForceSample {
    int step{};
    Real physical_time{};
    Real lift{};
    Real drag{};
    Real moment_z{};
    Real pressure_drag{};
    Real viscous_drag{};
    Real pressure_lift{};
    Real viscous_lift{};
};

struct SolverSurfaceSample {
    Vec2 center{};
    Vec2 outward_normal{};
    Real pressure{};
    Real pressure_coefficient{};
    Real skin_friction_coefficient{};
    Real density{};
    Real velocity_x{};
    Real velocity_y{};
    Real mach{};
    std::array<char, 64> tag{};
};

struct SolverFieldCell {
    GlobalIndex global_id{-1};
    int vertex_count{};
    std::array<Vec2, 4> vertices{};
    State state{};
    int owner_rank{};
    Real vorticity{};
};

/// Per-owned-cell data needed to restart a true BDF2 physical-time sequence.
/// `previous` is U^n and `older` is U^(n-1), both from accepted steps only.
struct SolverTransientState {
    GlobalIndex global_id{-1};
    State previous{};
    State older{};
};

struct SolverTransientCheckpoint {
    int step{};
    Real physical_time{};
    /// First-inner total residual at physical step one, retained so resumed
    /// run-status reduction evidence uses the original production baseline.
    Real initial_global_residual{};
    /// Whether the documented wake seed was applied before physical step one.
    bool initial_symmetry_seed_applied{};
    std::vector<SolverTransientState> states;
    /// Globally reduced accepted force samples, retained so an interrupted late
    /// run has the same statistical-periodicity evidence as an uninterrupted run.
    std::vector<SolverForceSample> force_history;
    /// Accepted inner-iteration counts through `step`; required for final
    /// metadata to agree exactly with the complete residual CSV after resume.
    std::vector<int> inner_iterations;
};

struct SolverInnerStatistics {
    int requested_min{};
    int requested_max{};
    int observed_min{};
    int observed_max{};
    Real observed_mean{};
    int target_misses{};
    Real converged_fraction{};
    Real last_residual_ratio{};
};

struct SolverSummary {
    int final_step{};
    Real final_physical_time{};
    Real residual_reduction_orders{};
    std::string convergence_status;
    std::string notes;
    SolverInnerStatistics inner_statistics{};
    std::uint64_t positivity_backtracks{};
    std::uint64_t hllc_fallback_faces{};
};

struct SolverCallbacks {
    /// Called on rank zero after a globally reduced state evaluation.
    std::function<void(const SolverResidualSample&)> residual;
    /// Called on rank zero; the force sample always corresponds to the same state
    /// as the residual sample for that step/physical time.
    std::function<void(const SolverForceSample&)> force;
    /// Called collectively at requested transient snapshot times.  Each rank passes
    /// only its owned-cell records; the callback may gather them to rank zero.
    std::function<void(int, Real, const std::vector<SolverFieldCell>&)> snapshot;
    /// Called collectively at durable steady checkpoint intervals.  Each rank
    /// passes only owned cells; the callback may atomically replace a restart
    /// on rank zero without changing final-state completion bookkeeping.
    std::function<void(int, const std::vector<SolverFieldCell>&)> checkpoint;
    /// Called collectively after an accepted transient physical step at the
    /// configured durable-checkpoint cadence.  The BDF histories are already
    /// advanced and never contain a trial inner iterate.
    std::function<void(const SolverTransientCheckpoint&)> transient_checkpoint;
    /// Called on rank zero for concise progress diagnostics.
    std::function<void(const std::string&)> log;
};

class FlowSolver {
  public:
    FlowSolver(CaseConfig config, DistributedMesh mesh, MPI_Comm communicator);
    ~FlowSolver();

    FlowSolver(FlowSolver&&) noexcept;
    FlowSolver& operator=(FlowSolver&&) noexcept;
    FlowSolver(const FlowSolver&) = delete;
    FlowSolver& operator=(const FlowSolver&) = delete;

    /// Replaces the freestream initialization for owned cells.  Ghost states are
    /// synchronized from their owning ranks before the first residual evaluation.
    void set_initial_owned_states(const std::vector<State>& states);
    /// Restores an accepted transient BDF2 state.  The supplied histories are
    /// matched by global cell id by the caller; ghost histories are exchanged
    /// before the next residual evaluation.
    void set_transient_owned_states(int accepted_step, Real initial_global_residual,
                                    bool initial_symmetry_seed_applied,
                                    const std::vector<State>& previous,
                                    const std::vector<State>& older,
                                    const std::vector<SolverForceSample>& force_history,
                                    const std::vector<int>& inner_iterations);
    /// Reapplies the documented transient symmetry seed after a cross-case
    /// precursor restart and synchronizes both BDF histories.
    void apply_transient_symmetry_seed();
    [[nodiscard]] SolverSummary solve(const SolverCallbacks& callbacks);
    [[nodiscard]] std::vector<SolverFieldCell> local_field_cells() const;
    [[nodiscard]] std::vector<SolverSurfaceSample> local_surface_samples() const;
    [[nodiscard]] const DistributedMesh& mesh() const noexcept;
    [[nodiscard]] const CaseConfig& config() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

/// Output-only root gather.  This is never used in solver iterations.
[[nodiscard]] std::vector<SolverFieldCell> gather_field_cells(
    const std::vector<SolverFieldCell>& local, MPI_Comm communicator, int root = 0);
[[nodiscard]] std::vector<SolverSurfaceSample> gather_surface_samples(
    const std::vector<SolverSurfaceSample>& local, MPI_Comm communicator, int root = 0);
[[nodiscard]] std::vector<SolverTransientState> gather_transient_states(
    const std::vector<SolverTransientState>& local, MPI_Comm communicator, int root = 0);

}  // namespace cfd
