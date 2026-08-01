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

}  // namespace cfd
