#include "output.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fv {

namespace {
std::ofstream g_log;
int g_logRank = 0;
}  // namespace

void openLog(const std::string& path) {
  MPI_Comm_rank(MPI_COMM_WORLD, &g_logRank);
  if (g_logRank == 0) g_log.open(path, std::ios::out | std::ios::trunc);
}

void slog(const std::string& msg) {
  if (g_logRank != 0) return;
  std::cout << msg << std::endl;
  if (g_log) g_log << msg << std::endl;
}

void closeLog() {
  if (g_log) g_log.close();
}

std::string nowUtcIso() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

void writeMetadataJson(const std::string& path, const CaseConfig& cfg,
                       const LocalMesh& lm0, const GlobalMesh* gm, int mpiRanks,
                       long edgeCut, const std::string& fluxName,
                       const std::string& gitRev, const std::string& startUtc,
                       const std::string& endUtc, const RunStats& stats) {
  nlohmann::json j;
  j["case_id"] = cfg.case_id;
  j["solver_name"] = "fv2d";
  j["solver_version"] = "1.0.0";
  j["git_revision"] = gitRev;
  j["mpi_ranks"] = mpiRanks;
  j["mesh_file"] = cfg.mesh_file;
  j["num_cells_global"] = gm ? gm->nCells : 0;
  j["num_faces_global"] = gm ? gm->nFaces : 0;
  j["num_cells_owned_local"] = lm0.nOwned;
  j["num_cells_ghost_local"] = lm0.nGhost;
  j["partitioner"] = "metis_kway";
  j["partition_edge_cut"] = edgeCut;
  j["halo_exchange"] = "neighbor_isend_irecv";
  j["full_state_replication_during_iterations"] = false;
  j["full_mesh_replication_during_iterations"] = false;
  j["equation_set"] = "compressible_navier_stokes_2d";
  j["inviscid_flux"] = fluxName;
  j["entropy_fix"] = nullptr;
  j["viscous_flux"] =
      cfg.viscous() ? "laminar_newtonian_fourier_constant_mu" : "disabled";
  j["time_integrator"] =
      cfg.transient() ? "bdf2" : "pseudo_time_backward_euler";
  j["implicit_solver"] = "lu_sgs";
  j["reconstruction"] = "piecewise_linear_least_squares";
  j["limiter"] = "barth_jespersen";
  j["spatial_order_claimed"] = 2;
  j["positivity_preservation"] =
      "limited_reconstruction_with_first_order_fallback_and_update_damping";
  j["wall_boundary_output_semantics"] = "boundary_value";
  j["true_bdf2_inner_loop"] = cfg.transient();
  j["typical_inner_iterations"] = stats.typicalInnerIterations;
  j["min_inner_iterations"] = cfg.min_inner_iterations;
  j["max_inner_iterations"] = cfg.max_inner_iterations;
  j["observed_min_inner_iterations"] = stats.innerMin;
  j["observed_max_inner_iterations"] = stats.innerMax;
  j["observed_mean_inner_iterations"] = stats.innerMean;
  j["inner_residual_reduction_target"] = cfg.inner_residual_reduction_target;
  j["inner_target_misses"] = stats.innerTargetMisses;
  j["inner_target_converged_fraction"] = stats.innerConvergedFraction;
  j["last_inner_residual_ratio"] = stats.lastInnerResidualRatio;
  j["start_time_utc"] = startUtc;
  j["end_time_utc"] = endUtc;
  j["completed"] = stats.convergenceStatus != "failed";
  j["convergence_status"] = stats.convergenceStatus;
  std::ofstream os(path);
  os << j.dump(2) << "\n";
}

void writeRunStatusJson(const std::string& path, const CaseConfig& cfg,
                        const std::string& commandLine, int mpiRanks,
                        const RunStats& stats) {
  nlohmann::json j;
  j["case_id"] = cfg.case_id;
  j["command"] = commandLine;
  j["mpi_ranks"] = mpiRanks;
  j["wall_time_seconds"] = stats.wallTimeSeconds;
  j["final_step"] = stats.finalStep;
  j["final_physical_time"] = stats.finalTime;
  j["convergence_status"] = stats.convergenceStatus;
  j["residual_reduction_orders"] = stats.residualReductionOrders;
  j["notes"] = stats.convergenceNotes;
  std::ofstream os(path);
  os << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
// run() dispatcher
// ---------------------------------------------------------------------------
RunStats Solver::run(const std::string& outDir_, const std::string& restartFile,
                     const GlobalMesh* gm, long edgeCut, const std::string& commandLine,
                     const std::string& gitRev, double cflOverride) {
  outDir = outDir_;
  if (rank == 0) std::filesystem::create_directories(outDir);
  MPI_Barrier(comm);
  openLog(outDir + "/stdout.log");
  const std::string startUtc = nowUtcIso();
  const double t0 = MPI_Wtime();

  {
    std::ostringstream msg;
    msg << "fv2d: case " << cfg.case_id << " ranks=" << size
        << " owned=" << m.nOwned << " ghost=" << m.nGhost << " faces=" << m.nFaces;
    slog(msg.str());
  }

  initFields(restartFile, gm);

  RunStats stats;
  if (cfg.transient())
    stats = runTransient(gm, edgeCut);
  else
    stats = runSteady(gm, edgeCut, cflOverride);

  stats.wallTimeSeconds = MPI_Wtime() - t0;
  const std::string endUtc = nowUtcIso();

  {
    std::ostringstream msg;
    msg << "fv2d: case " << cfg.case_id << " finished step=" << stats.finalStep
        << " status=" << stats.convergenceStatus << " wall=" << stats.wallTimeSeconds
        << "s cd=" << stats.finalForces.cd << " cl=" << stats.finalForces.cl;
    slog(msg.str());
  }

  const std::string fluxName =
      fluxType == InviscidFluxType::HLLC ? "hllc" : "rusanov_llf";
  if (rank == 0) {
    writeMetadataJson(outDir + "/metadata.json", cfg, m, gm, size, edgeCut, fluxName,
                      gitRev, startUtc, endUtc, stats);
    writeRunStatusJson(outDir + "/run_status.json", cfg, commandLine, size, stats);
  }
  closeLog();
  return stats;
}

}  // namespace fv
