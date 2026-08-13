#pragma once

#include "case_config.hpp"
#include "halo.hpp"
#include "output.hpp"

#include <mpi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace aerofv {

// Original cell-centred finite-volume implementation used by the benchmark
// executable.  All per-iteration storage follows LocalMesh ordering: owned
// cells first and a one-layer, neighbor-exchanged ghost region second.
class FlowSolver {
public:
  FlowSolver(const CaseConfig &config, const LocalMesh &mesh,
             MPI_Comm communicator);

  RunSummary run(OutputWriter &output,
                 const std::optional<std::filesystem::path> &restart_file,
                 const std::string &command);

  [[nodiscard]] const std::vector<Conservative> &states() const noexcept {
    return states_;
  }
  [[nodiscard]] const std::vector<PrimitiveGradient> &gradients() const noexcept {
    return gradients_;
  }
  [[nodiscard]] const std::vector<SurfaceRecord> &surface_rows() const noexcept {
    return surface_rows_;
  }
  [[nodiscard]] const MethodMetadata &method_metadata() const noexcept {
    return method_metadata_;
  }

private:
  struct ResidualNorm {
    std::array<double, 4> component_l2{};
    double total_l2{0.0};
    double linf{0.0};
  };

  struct SpatialAssembly {
    std::vector<Conservative> residual;
    std::vector<double> spectral_sum;
    std::vector<double> face_coupling;
    ForceRecord force;
  };

  struct LinearResult {
    std::vector<Conservative> increment;
    int sweeps{0};
    double final_ratio{1.0};
  };

  const CaseConfig &config_;
  const LocalMesh &mesh_;
  MPI_Comm communicator_{MPI_COMM_NULL};
  int rank_{0};
  int ranks_{1};
  Primitive freestream_{};
  Conservative freestream_state_{};
  double dynamic_viscosity_{0.0};
  double dynamic_pressure_{1.0};
  Vec2 drag_direction_{1.0, 0.0};
  Vec2 lift_direction_{0.0, 1.0};

  std::vector<Conservative> states_;
  std::vector<Primitive> primitives_;
  std::vector<PrimitiveGradient> gradients_;
  std::vector<PrimitiveLimiter> limiters_;
  std::vector<SurfaceRecord> surface_rows_;
  MethodMetadata method_metadata_;
  bool reconstruction_active_{true};
  double reconstruction_blend_{1.0};
  double reconstruction_blend_override_{-1.0};
  bool initialized_from_restart_{false};

  BoundaryType boundary_type(const Face &face) const;
  int other_cell(const Face &face, int local_cell) const;
  Vec2 outward_normal(const Face &face, int local_cell) const;
  Primitive virtual_neighbor(int local_cell, const Face &face) const;

  void initialize_state(const std::optional<std::filesystem::path> &restart_file);
  void load_restart(const std::filesystem::path &path);
  void refresh_primitives();
  void compute_gradients_and_limiters(bool update_limiters = true);
  SpatialAssembly assemble_spatial(std::int64_t step, double physical_time,
                                   bool freeze_limiters = false);
  ResidualNorm residual_norm(const std::vector<Conservative> &residual) const;
  LinearResult solve_linearized(const std::vector<Conservative> &residual,
                                const SpatialAssembly &spatial,
                                const std::vector<double> &physical_diagonal,
                                double cfl, int minimum_sweeps,
                                int maximum_sweeps, double target_ratio);
  LinearResult solve_newton_krylov(const SpatialAssembly &spatial,
                                   const std::vector<Conservative> &residual,
                                   const std::vector<double> &physical_diagonal,
                                   double cfl, std::int64_t step,
                                   double physical_time,
                                   double pseudo_mass_scale,
                                   int minimum_iterations,
                                   int maximum_iterations,
                                   double target_ratio);
  void apply_increment(const std::vector<Conservative> &increment,
                       double relaxation,
                       double maximum_relative_change = 0.15);

  Primitive face_reconstruction(int local_cell, const Face &face) const;
  PrimitiveGradient viscous_face_gradient(const Face &face) const;
  std::vector<SurfaceRecord> build_surface_rows() const;
  double ramped_cfl(std::int64_t step) const;
  void warm_start_transient(OutputWriter &output);

  RunSummary run_steady(OutputWriter &output, const std::string &command,
                        double start_seconds);
  RunSummary run_transient(OutputWriter &output, const std::string &command,
                           double start_seconds);
};

} // namespace aerofv
