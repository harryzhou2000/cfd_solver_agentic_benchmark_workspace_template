// cns2d -- implementation of the benchmark output contract.
//
// Writes, into the run's output directory:
//   metadata.json, run_status.json, partition_diagnostics.csv,
//   residuals.csv, forces.csv, surface.csv, field_final.vtu, restart_final.bin
//
// CSV histories are streamed as the run proceeds (so a long run leaves usable
// history even if it is interrupted), while the final-state files are written
// once at the end from the SAME state, which is what keeps the last force row
// consistent with surface.csv and field_final.vtu.
//
// Only rank 0 writes; the history appenders are no-ops on other ranks.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/case_input.h"
#include "core/options.h"
#include "post/forces.h"
#include "solve/solver_context.h"

namespace cns2d {

// One row of residuals.csv.
struct ResidualRow {
  int step{0};
  Real physical_time{0.0};
  int inner_iter{0};
  Real cfl{0.0};
  Real dt{0.0};
  std::array<Real, kNumVars> component{};
  Real l2{0.0};
  Real linf{0.0};
};

// Description of the numerical methods, recorded in metadata.json.
struct MethodDescription {
  std::string inviscid_flux;
  std::string entropy_fix;
  std::string viscous_flux;
  std::string time_integrator;
  std::string implicit_solver;
  std::string reconstruction;
  std::string limiter;
  int spatial_order_claimed{2};
  std::string positivity_preservation;
  std::string wall_boundary_output_semantics;
  bool true_bdf2_inner_loop{false};
};

class OutputWriter {
 public:
  OutputWriter(const std::string &output_dir, const CaseInput &input,
               const CommandLineOptions &options, SolverContext &context);

  // Create the directory and open the streaming CSV files.
  void begin();

  void appendResidual(const ResidualRow &row);
  void appendForces(int step, Real physical_time, const ForceResult &forces);

  // Write the final state files: surface.csv, field_final.vtu, restart_final.bin.
  // Called once, after the last force row, from the final state.
  void writeFinalState(int step, Real physical_time);

  // Optional intermediate field dump for transient runs.
  void writeIntermediateField(int index, Real physical_time);

  void writeMetadata(const MethodDescription &methods, const RunOutcome &outcome,
                     const std::string &start_time_utc, const std::string &end_time_utc);
  void writeRunStatus(const RunOutcome &outcome, Real wall_time_seconds);
  void writePartitionDiagnostics();

  const std::string &outputDir() const { return output_dir_; }
  const std::string &logPath() const { return log_path_; }

  ~OutputWriter();

 private:
  std::string output_dir_;
  std::string log_path_;
  CaseInput input_;
  CommandLineOptions options_;
  SolverContext &context_;

  void *residual_stream_{nullptr};
  void *force_stream_{nullptr};
  bool is_root_{false};
};

// Current UTC time as an ISO-8601 string.
std::string utcTimestamp();

}  // namespace cns2d
