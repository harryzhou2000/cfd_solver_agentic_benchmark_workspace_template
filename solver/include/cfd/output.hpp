#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/case_config.hpp"

namespace cfd {

class OutputError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

enum class ConvergenceStatus { converged, statistically_periodic, failed };

[[nodiscard]] const char* to_string(ConvergenceStatus value) noexcept;

struct ResidualRecord {
    std::int64_t step{};
    Real physical_time{};
    int inner_iter{};
    Real cfl{};
    Real dt{};
    Real rho{};
    Real rhou{};
    Real rhov{};
    Real rhoE{};
    Real residual_l2{};
    Real residual_linf{};
};

struct ForceRecord {
    std::int64_t step{};
    Real physical_time{};
    Real cl{};
    Real cd{};
    Real cmz{};
    Real pressure_drag{};
    Real viscous_drag{};
    Real pressure_lift{};
    Real viscous_lift{};
};

struct SurfaceRecord {
    Real x{};
    Real y{};
    Real nx{};
    Real ny{};
    Real pressure{};
    Real cp{};
    Real cf{};
    Real rho{};
    Real u{};
    Real v{};
    Real mach{};
    std::string tag;
};

struct PartitionDiagnosticsRecord {
    int rank{};
    std::int64_t num_cells_owned{};
    std::int64_t num_cells_ghost{};
    std::int64_t num_boundary_faces{};
    int num_neighbor_ranks{};
    std::string neighbor_ranks;
    std::string send_cells;
    std::string recv_cells;
};

/// A rank-0 gathered cell. Connectivity is a polygon in counter-clockwise or
/// clockwise order; MPI gathering remains the responsibility of the caller.
struct GatheredPolygonRecord {
    GlobalIndex global_cell_id{};
    std::vector<Vec2> points;
    Real density{};
    Vec2 velocity{};
    Real pressure{};
    Real mach{};
    Real total_energy{};
    Real temperature{};
    int owner_rank{};
};

struct RestartStateRecord {
    GlobalIndex global_cell_id{};
    State state{};
};

struct FinalStateDescriptor {
    std::int64_t step{};
    Real physical_time{};
};

struct TransientStatistics {
    int typical_inner_iterations{};
    int observed_min_inner_iterations{};
    int observed_max_inner_iterations{};
    Real observed_mean_inner_iterations{};
    int inner_target_misses{};
    Real inner_target_converged_fraction{};
    Real last_inner_residual_ratio{};
};

struct OutputMetadata {
    std::string solver_name;
    std::string solver_version;
    std::optional<std::string> git_revision;
    int mpi_ranks{1};
    std::int64_t num_cells_global{};
    std::int64_t num_faces_global{};
    std::int64_t num_cells_owned_local{};
    std::int64_t num_cells_ghost_local{};
    std::string partitioner;
    std::int64_t partition_edge_cut{};
    std::string halo_exchange;
    bool full_state_replication_during_iterations{false};
    bool full_mesh_replication_during_iterations{false};
    std::string equation_set{"compressible_navier_stokes_2d"};
    std::string inviscid_flux;
    std::optional<std::string> entropy_fix;
    std::string viscous_flux;
    std::string time_integrator;
    std::string implicit_solver;
    std::string reconstruction;
    std::string limiter;
    int spatial_order_claimed{2};
    std::string positivity_preservation;
    std::string wall_boundary_output_semantics{"boundary_value"};
    bool true_bdf2_inner_loop{false};
    TransientStatistics transient_statistics;
    std::string start_time_utc;
};

struct RunStatus {
    std::string command;
    int mpi_ranks{1};
    Real wall_time_seconds{};
    FinalStateDescriptor final_state;
    ConvergenceStatus convergence_status{ConvergenceStatus::failed};
    Real residual_reduction_orders{};
    std::string notes;
};

/// Rank-zero-only writer for the benchmark output package. It intentionally has no
/// MPI calls: callers must gather global field/partition records before writing.
class OutputSession {
  public:
    OutputSession(const CaseConfig& case_config, std::filesystem::path output_directory,
                  OutputMetadata metadata, int mpi_rank = 0);
    ~OutputSession();

    OutputSession(const OutputSession&) = delete;
    OutputSession& operator=(const OutputSession&) = delete;
    OutputSession(OutputSession&&) = delete;
    OutputSession& operator=(OutputSession&&) = delete;

    [[nodiscard]] const std::filesystem::path& output_directory() const noexcept;

    void log(const std::string& message);
    void write_partition_diagnostics(const std::vector<PartitionDiagnosticsRecord>& records);
    void append_residual(const ResidualRecord& record);
    void append_force(const ForceRecord& record);
    void update_transient_statistics(const TransientStatistics& statistics);
    void write_final_surface(const std::vector<SurfaceRecord>& records,
                             FinalStateDescriptor state);
    void write_final_field_vtk(const std::vector<GatheredPolygonRecord>& records,
                               FinalStateDescriptor state);
    /// Writes an additional actual-state field without changing the final-state
    /// consistency bookkeeping. `filename` must be a basename ending in `.vtk`.
    void write_field_snapshot_vtk(const std::vector<GatheredPolygonRecord>& records,
                                  const std::string& filename);
    void write_final_restart(const std::vector<RestartStateRecord>& records);

    /// Writes metadata.json and run_status.json only after all mandatory output
    /// data is present and agrees on the final physical state.
    void complete(const RunStatus& status);

    /// Finalizes diagnostic artifacts honestly after a numerical failure.  The
    /// resulting metadata has completed=false and convergence_status=failed.
    void record_failure(const RunStatus& status);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Reads the endian-native, versioned restart format written by OutputSession.
[[nodiscard]] std::vector<RestartStateRecord> read_restart_file(
    const std::filesystem::path& restart_file);

}  // namespace cfd
