#pragma once

#include "common.h"
#include "case.h"
#include "mesh.h"
#include "partition.h"
#include "physics.h"

namespace cfd {

struct RunStats {
  double initial_residual_l2 = 0.0, initial_residual_linf = 0.0;
  double final_residual_l2 = 0.0, final_residual_linf = 0.0;
  double residual_reduction_orders = 0.0;
  long long total_inner = 0;
  int min_inner = 0, max_inner = 0;
  double mean_inner = 0.0;
  int target_misses = 0;
  double converged_fraction = 1.0;
  double last_inner_ratio = 0.0;
  int steps_done = 0;
  long long positivity_fallbacks = 0;
  long long update_limits = 0;
  std::string convergence_status = "failed";
  std::string notes;
  double final_cl = 0.0, final_cd = 0.0, final_cmz = 0.0;
  double final_pd = 0.0, final_vd = 0.0, final_pl = 0.0, final_vl = 0.0;
};

struct Outputs {
  std::string outdir;
  std::ofstream residuals_f, forces_f;
  bool csv_open = false;
};

// Creates the output directory and CSV files with the contract headers.
// Rank 0 only. With append=true the CSV files are reopened in append mode
// (used when resuming from a restart).
bool open_outputs(const CaseConfig& cfg, const std::string& outdir, Outputs& out,
                  std::string& err, bool append = false);

void append_residual_row(Outputs& out, int step, double phys_time, int inner,
                         double cfl, double dt, const double res_comp[4],
                         double l2, double linf);

void append_force_row(Outputs& out, int step, double phys_time, double cl, double cd,
                      double cmz, double pd, double vd, double pl, double vl);

void close_outputs(Outputs& out);

// Field output: rank 0 writes a VTU. `owned_data` has ncomp doubles per
// global cell: rho,u,v,p,mach,E,T,rank[,vorticity] (ncomp 8 or 9).
bool write_field_vtu(const CaseConfig& cfg, const Mesh& mesh,
                     const std::vector<double>& owned_data, int ncomp, int rank,
                     const std::string& filename, std::string& err);

// Surface output: rank 0 gathers wall rows and writes surface.csv.
struct SurfaceRow {
  double x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
  std::string tag;
};
bool write_surface_csv(const std::vector<SurfaceRow>& rows, const std::string& outdir,
                       std::string& err);

// Restart file (binary). `states` has ncells_global * nstates * 4 doubles in
// global cell order (nstates=1 steady, 3 transient). Rank 0 only.
bool write_restart(const std::string& path, const std::string& case_id, int step,
                   double phys_time, int nstates, const std::vector<double>& states,
                   std::string& err);
bool read_restart(const std::string& path, std::string& case_id, int& step,
                  double& phys_time, int& nstates, std::vector<double>& states,
                  std::string& err);

// Metadata / status / partition diagnostics (rank 0 writes).
bool write_metadata_json(const CaseConfig& cfg, const LocalMesh& lm,
                         const RunStats& stats, const std::string& outdir,
                         const std::string& solver_version, double wall_time,
                         const std::string& start_utc, const std::string& end_utc,
                         bool completed, const std::string& git_revision,
                         std::string& err);

bool write_partition_diagnostics(const LocalMesh& lm, const std::string& outdir,
                                 std::string& err);

bool write_run_status(const CaseConfig& cfg, const RunStats& stats,
                      const std::string& outdir, const std::string& command,
                      int mpi_ranks, double wall_time, std::string& err);

}  // namespace cfd
