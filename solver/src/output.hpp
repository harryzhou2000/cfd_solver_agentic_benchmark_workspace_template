#pragma once

#include "case_config.hpp"
#include "partition.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aerofv {

struct ResidualRecord {
  std::int64_t step{0};
  double physical_time{0.0};
  int inner_iter{0};
  double cfl{0.0};
  double dt{0.0};
  double rho{0.0};
  double rhou{0.0};
  double rhov{0.0};
  double rhoE{0.0};
  double residual_l2{0.0};
  double residual_linf{0.0};
};

struct ForceRecord {
  std::int64_t step{0};
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

// Method-specific values must describe the algorithms used in this actual run,
// rather than generic solver capabilities.
struct MethodMetadata {
  std::string solver_name{"aerofv"};
  std::string solver_version{"unknown"};
  std::optional<std::string> git_revision;
  std::string partitioner{"metis_kway"};
  std::string halo_exchange{"neighbor_isend_irecv"};
  std::string inviscid_flux{"rusanov"};
  std::optional<std::string> entropy_fix;
  std::string viscous_flux{"central_gradient"};
  std::string time_integrator{"implicit_pseudo_time"};
  std::string implicit_solver{"block_jacobi"};
  std::string reconstruction{"least_squares_linear"};
  std::string limiter{"barth_jespersen"};
  std::string positivity_preservation{"conservative_face_state_scaling"};
  std::string wall_boundary_output_semantics{"boundary_value"};
  bool true_bdf2_inner_loop{false};
  int typical_inner_iterations{0};
  int observed_min_inner_iterations{0};
  int observed_max_inner_iterations{0};
  double observed_mean_inner_iterations{0.0};
  int inner_target_misses{0};
  double inner_target_converged_fraction{1.0};
  double last_inner_residual_ratio{0.0};
  // Steady-only nonlinear safeguard. These values are emitted even when
  // inactive so each result can prove whether its scheduled CFL was capped.
  bool steady_recovery_activated{false};
  std::int64_t steady_recovery_activation_step{0};
  double steady_recovery_cfl_cap{0.0};
  double steady_recovery_relaxation_cap{0.0};
};

struct RunSummary {
  std::string command;
  double wall_time_seconds{0.0};
  std::int64_t final_step{0};
  double final_physical_time{0.0};
  std::string convergence_status{"converged"};
  double residual_reduction_orders{0.0};
  std::string notes;
  bool completed{true};
  // If empty, OutputWriter timestamps finalization in UTC.
  std::string end_time_utc;
};

class OutputError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// All methods are collective on communicator except append_*, log, and
// update_method_metadata, which are rank-local and write only on rank zero.
class OutputWriter {
public:
  OutputWriter(const CaseConfig &config, const LocalMesh &local_mesh,
               std::filesystem::path output_dir, MPI_Comm communicator,
               MethodMetadata method);
  ~OutputWriter();

  OutputWriter(const OutputWriter &) = delete;
  OutputWriter &operator=(const OutputWriter &) = delete;

  // Rank zero creates the directory and exact-header history files, then all
  // ranks synchronize before solver iteration begins.
  void prepare();
  void append_residual(const ResidualRecord &record);
  void append_force(const ForceRecord &record);
  void log(const std::string &message);
  // Replace constructor defaults with measurements from the completed solve.
  // This is rank-local and must be called before collective write_final().
  void update_method_metadata(MethodMetadata method);

  // Collectively records one actual LocalMesh diagnostic row for every rank.
  void write_partition_diagnostics();

  // Optional collective transient snapshot. It gathers only owned final-like
  // cell state/gradient data and writes field_t########.vtu on rank zero.
  void write_transient_field(const std::vector<Conservative> &local_states,
                             const std::vector<PrimitiveGradient> &local_gradients,
                             std::int64_t step, double physical_time);

  // Collectively gathers owned final cells/states/gradients and local wall
  // rows only at finalization. Rank zero writes all final artifacts from this
  // one snapshot, ensuring force step, field, restart, and surface agreement.
  void write_final(const std::vector<Conservative> &local_states,
                   const std::vector<PrimitiveGradient> &local_gradients,
                   const std::vector<SurfaceRecord> &local_surface_rows,
                   const RunSummary &summary);

  [[nodiscard]] const std::filesystem::path &output_dir() const noexcept {
    return output_dir_;
  }

private:
  CaseConfig config_;
  const LocalMesh &local_mesh_;
  std::filesystem::path output_dir_;
  MPI_Comm communicator_{MPI_COMM_NULL};
  MethodMetadata method_;
  int rank_{0};
  int ranks_{1};
  bool prepared_{false};
  bool partition_diagnostics_written_{false};
  std::optional<ResidualRecord> last_residual_;
  std::optional<ForceRecord> last_force_;
  std::ofstream residuals_;
  std::ofstream forces_;
  std::ofstream stdout_log_;
  std::string start_time_utc_;
};

} // namespace aerofv
