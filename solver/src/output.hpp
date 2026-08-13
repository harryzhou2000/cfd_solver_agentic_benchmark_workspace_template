#pragma once

#include <string>
#include <vector>

#include "solver.hpp"

namespace cfd {

std::string now_iso();

struct RunResult {
  std::string case_id;
  std::string start_time_utc;
  std::string end_time_utc;
  std::string convergence_status;
  int final_step = 0;
  double final_physical_time = 0.0;
  double wall_time_seconds = 0.0;
  double residual_reduction_orders = 0.0;
  std::string notes;
  bool completed = true;
};

// Rank 0: creates the output directory tree.
void create_output_dir(const std::string& outdir);

// Rank 0: writes CSV headers for residuals/forces.
void init_csv_files(const Solver& s, const std::string& outdir);

void write_residual_row(const Solver& s, const std::string& outdir, int step,
                        double physical_time, int inner_iter, double cfl,
                        double dt_global, const ResidualNorms& norms);
void write_force_row(const Solver& s, const std::string& outdir, int step,
                     double physical_time, const ForceData& f);

// Collects wall surface rows from all ranks and writes surface.csv (rank 0).
void write_surface_csv(const Solver& s, const std::string& outdir,
                       const std::vector<SurfaceRow>& rows);

// Writes partition_diagnostics.csv (rank 0 assembles per-rank rows).
void write_partition_diagnostics(const Solver& s, const std::string& outdir);

// Rank 0 writes metadata.json and run_status.json.
void write_metadata(const Solver& s, const std::string& outdir,
                    const RunResult& rr);
void write_run_status(const Solver& s, const std::string& outdir,
                      const std::string& command, const RunResult& rr);

void write_field_vtu(const Solver& s, const std::string& outdir,
                     const std::string& name);

// Restart: U, U_prev1, U_prev2 for all owned cells in global order.
void write_restart(const Solver& s, const std::string& path, int step,
                   double physical_time);
std::pair<int, double> read_restart(Solver& s, const std::string& path);

}  // namespace cfd
