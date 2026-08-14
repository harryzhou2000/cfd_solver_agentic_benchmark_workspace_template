#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mpi.h>
#include <nlohmann/json.hpp>

#include "case_file.hpp"
#include "mesh.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace {

void usage() {
  fprintf(stderr,
          "usage:\n"
          "  cfd_solver solve --case <case.json> --output <dir> [--restart "
          "<restart-dir-or-file>] [--report-level brief|full]\n"
          "  cfd_solver partition --case <case.json> --np <ranks> --output "
          "<dir>\n");
}

std::string arg_value(int argc, char** argv, const std::string& key,
                      const std::string& def, bool required) {
  for (int i = 1; i < argc - 1; ++i)
    if (argv[i] == key) return argv[i + 1];
  if (required) throw std::runtime_error("missing required argument " + key);
  return def;
}

bool file_exists(const std::string& p) {
  std::ifstream f(p);
  return f.good();
}

void ensure_partitions(const cfd::CaseFile& cfg, int np,
                       const std::string& out_dir, int rank,
                       MPI_Comm comm) {
  std::string dir = cfd::partition_dir(out_dir, np);
  bool ready = file_exists(dir + "/rank_" + std::to_string(np - 1) + ".bin");
  MPI_Bcast(&ready, 1, MPI_C_BOOL, 0, comm);
  if (ready) return;
  if (rank == 0) {
    printf("building METIS partitions for np=%d in %s\n", np, dir.c_str());
    fflush(stdout);
    cfd::GlobalMesh gm = cfd::read_cgns_mesh(cfg.mesh_file);
    cfd::compute_cell_geometry(gm);
    std::vector<int64_t> stats;
    cfd::build_partitions(gm, np, dir, &stats);
    printf("mesh: %lld cells, %lld faces, edge cut %lld\n",
           (long long)stats[0], (long long)stats[1], (long long)stats[2]);
    fflush(stdout);
  }
  MPI_Barrier(comm);
  if (!file_exists(dir + "/rank_" + std::to_string(np - 1) + ".bin"))
    throw std::runtime_error("partition build failed for " + dir);
}

void write_metadata_and_status(const cfd::CaseFile& cfg,
                               const cfd::Solver& solver,
                               const std::string& out_dir,
                               const std::string& command) {
  const cfd::RunStats& s = solver.stats();
  const cfd::LocalMesh& m = solver.mesh();

  double reduction_orders = 0.0;
  if (s.residual_first > 0.0 && s.residual_last > 0.0)
    reduction_orders = std::log10(s.residual_first / s.residual_last);

  nlohmann::json meta;
  meta["case_id"] = cfg.case_id;
  meta["solver_name"] = "cfd_bench_solver";
  meta["solver_version"] = "1.0.0";
  std::string rev = cfd::git_revision();
  meta["git_revision"] = rev == "unknown" ? nullptr : nlohmann::json(rev);
  meta["mpi_ranks"] = m.np;
  meta["mesh_file"] = cfg.mesh_file;
  meta["num_cells_global"] = m.num_cells_global;
  meta["num_faces_global"] = m.num_faces_global;
  meta["num_cells_owned_local"] = m.n_owned;
  meta["num_cells_ghost_local"] = m.n_ghost;
  meta["partitioner"] = m.np > 1 ? "metis_kway" : "metis_kway_serial";
  meta["partition_edge_cut"] = m.edge_cut;
  meta["halo_exchange"] = "neighbor_isend_irecv";
  meta["full_state_replication_during_iterations"] = false;
  meta["full_mesh_replication_during_iterations"] = false;
  meta["equation_set"] = "compressible_navier_stokes_2d";
  meta["inviscid_flux"] =
      cfg.flux_choice == 1 ? "roe" : "rusanov_llf_cellstate_dissipation";
  meta["entropy_fix"] = cfg.flux_choice == 1
                            ? nlohmann::json("harten_yee")
                            : nlohmann::json(nullptr);
  meta["low_mach_mref"] = cfg.low_mach_mref;
  meta["transient_kick"] = cfg.transient();
  meta["viscous_flux"] =
      cfg.viscous() ? "corrected_face_average_gradient_newtonian_fourier"
                    : "disabled";
  meta["time_integrator"] = cfg.transient()
                                ? "bdf2_physical_time_with_pseudo_inner_loop"
                                : "pseudo_time_backward_euler";
  meta["implicit_solver"] = "lusgs_scalar_jacobian_block_jacobi_across_ranks";
  meta["reconstruction"] =
      "piecewise_linear_weighted_least_squares_primitive_variables";
  meta["limiter"] = "venkatakrishnan";
  meta["spatial_order_claimed"] = 2;
  meta["positivity_preservation"] =
      "limited_reconstruction_with_first_order_fallback_and_state_floors";
  meta["wall_boundary_output_semantics"] = "boundary_value";
  meta["true_bdf2_inner_loop"] = cfg.transient();
  meta["typical_inner_iterations"] = s.observed_mean_inner;
  meta["min_inner_iterations"] = cfg.run.min_inner_iterations;
  meta["max_inner_iterations"] = cfg.run.max_inner_iterations;
  meta["observed_min_inner_iterations"] = s.observed_min_inner;
  meta["observed_max_inner_iterations"] = s.observed_max_inner;
  meta["inner_residual_reduction_target"] =
      cfg.run.inner_residual_reduction_target;
  meta["inner_target_misses"] = s.inner_target_misses;
  meta["inner_target_converged_fraction"] =
      s.inner_steps_total > 0
          ? 1.0 - (double)s.inner_target_misses / s.inner_steps_total
          : 0.0;
  meta["last_inner_residual_ratio"] = s.last_inner_residual_ratio;
  meta["start_time_utc"] = s.start_time_utc;
  meta["end_time_utc"] = s.end_time_utc;
  meta["completed"] = s.convergence_status != "failed";
  meta["convergence_status"] = s.convergence_status;
  meta["cfl_initial"] = cfg.run.cfl_initial;
  meta["cfl_max"] = cfg.run.cfl_max;
  meta["pseudo_cfl_ramp_steps"] = cfg.run.pseudo_cfl_ramp_steps;
  meta["max_steps"] = cfg.run.max_steps;
  meta["rusanov_dissipation_scale"] = cfg.run.rusanov_dissipation_scale;
  meta["positivity_fix_count"] = s.positivity_fixes;
  meta["viscosity_constant"] = cfg.viscous() ? cfg.viscosity() : 0.0;
  meta["final_cl"] = s.cl;
  meta["final_cd"] = s.cd;
  meta["final_cmz"] = s.cmz;
  meta["final_pressure_drag"] = s.pressure_drag;
  meta["final_viscous_drag"] = s.viscous_drag;
  meta["final_pressure_lift"] = s.pressure_lift;
  meta["final_viscous_lift"] = s.viscous_lift;

  nlohmann::json status;
  status["case_id"] = cfg.case_id;
  status["command"] = command;
  status["mpi_ranks"] = m.np;
  status["wall_time_seconds"] = s.wall_time;
  status["final_step"] = s.final_step;
  status["final_physical_time"] = s.final_time;
  status["convergence_status"] = s.convergence_status;
  status["residual_reduction_orders"] = reduction_orders;
  status["notes"] = s.notes;

  {
    std::ofstream o(out_dir + "/metadata.json");
    o << meta.dump(2) << "\n";
  }
  {
    std::ofstream o(out_dir + "/run_status.json");
    o << status.dump(2) << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int rc = 0;
  try {
    if (argc < 2) {
      usage();
      throw std::runtime_error("missing subcommand");
    }
    std::string sub = argv[1];
    if (sub == "partition") {
      std::string case_path = arg_value(argc, argv, "--case", "", true);
      int np = std::stoi(arg_value(argc, argv, "--np", "", true));
      std::string out = arg_value(argc, argv, "--output", "", true);
      cfd::CaseFile cfg = cfd::load_case_file(case_path);
      cfd::GlobalMesh gm = cfd::read_cgns_mesh(cfg.mesh_file);
      cfd::compute_cell_geometry(gm);
      std::vector<int64_t> stats;
      cfd::build_partitions(gm, np, cfd::partition_dir(out, np), &stats);
      printf("partitioned %s: %lld cells, %lld faces, edge cut %lld into %d "
             "parts\n",
             cfg.case_id.c_str(), (long long)stats[0], (long long)stats[1],
             (long long)stats[2], np);
    } else if (sub == "solve") {
      std::string case_path = arg_value(argc, argv, "--case", "", true);
      std::string out = arg_value(argc, argv, "--output", "", true);
      std::string restart = arg_value(argc, argv, "--restart", "", false);
      bool restart_as_initial =
          !arg_value(argc, argv, "--restart-as-initial", "", false).empty();
      arg_value(argc, argv, "--report-level", "brief", false);
      int np = 1;
      MPI_Comm_size(MPI_COMM_WORLD, &np);
      cfd::CaseFile cfg = cfd::load_case_file(case_path);
      std::string ms = arg_value(argc, argv, "--max-steps", "", false);
      if (!ms.empty()) cfg.run.max_steps = std::stoi(ms);
      std::string ft = arg_value(argc, argv, "--final-time", "", false);
      if (!ft.empty()) cfg.run.final_time = std::stod(ft);
      std::string fx = arg_value(argc, argv, "--flux", "", false);
      if (!fx.empty()) {
        if (fx == "roe") cfg.flux_choice = 1;
        else if (fx == "rusanov" || fx == "llf") cfg.flux_choice = 0;
        else throw std::runtime_error("unknown --flux: " + fx);
      }
      std::string lmm = arg_value(argc, argv, "--low-mach-mref", "", false);
      if (!lmm.empty()) cfg.low_mach_mref = std::stod(lmm);
      std::string fwe = arg_value(argc, argv, "--field-every", "", false);
      if (!fwe.empty()) cfg.write_field_every_time = std::stod(fwe);
      std::string cf0 = arg_value(argc, argv, "--cfl-init", "", false);
      if (!cf0.empty()) cfg.run.cfl_initial = std::stod(cf0);
      std::string cfm = arg_value(argc, argv, "--cfl-max", "", false);
      if (!cfm.empty()) cfg.run.cfl_max = std::stod(cfm);
      std::string rmp = arg_value(argc, argv, "--cfl-ramp", "", false);
      if (!rmp.empty()) cfg.run.pseudo_cfl_ramp_steps = std::stoi(rmp);
      std::string mii = arg_value(argc, argv, "--max-inner", "", false);
      if (!mii.empty()) cfg.run.max_inner_iterations = std::stoi(mii);
      std::string fo = arg_value(argc, argv, "--first-order", "", false);
      if (!fo.empty()) cfg.debug_first_order = true;
      if (!arg_value(argc, argv, "--explicit", "", false).empty())
        cfg.debug_explicit = true;
      ensure_partitions(cfg, np, out, rank, MPI_COMM_WORLD);
      cfd::LocalMesh lm =
          cfd::load_partition(cfd::partition_dir(out, np), np, rank);
      if (rank == 0) {
        printf("case %s: np=%d cells_global=%lld faces_global=%lld "
               "edge_cut=%lld\n",
               cfg.case_id.c_str(), np, (long long)lm.num_cells_global,
               (long long)lm.num_faces_global, (long long)lm.edge_cut);
        fflush(stdout);
      }
      cfd::Solver solver(cfg, std::move(lm), MPI_COMM_WORLD, out, restart);
      solver.set_restart_as_initial(restart_as_initial);
      solver.run();
      if (rank == 0) {
        std::string command;
        for (int i = 0; i < argc; ++i) {
          if (i) command += " ";
          command += argv[i];
        }
        char mpicmd[64];
        snprintf(mpicmd, sizeof(mpicmd), "mpirun -np %d ", np);
        write_metadata_and_status(cfg, solver, out,
                                  std::string(mpicmd) + "<solver> " + command);
        printf("done: status=%s steps=%d wall=%.1fs cd=%.5f cl=%.5f\n",
               solver.stats().convergence_status.c_str(),
               solver.stats().final_step, solver.stats().wall_time,
               solver.stats().cd, solver.stats().cl);
        fflush(stdout);
      }
    } else if (sub == "gradcheck" || sub == "visccheck") {
      std::string case_path = arg_value(argc, argv, "--case", "", true);
      std::string out = arg_value(argc, argv, "--output", "", true);
      int np = 1;
      MPI_Comm_size(MPI_COMM_WORLD, &np);
      cfd::CaseFile cfg = cfd::load_case_file(case_path);
      ensure_partitions(cfg, np, out, rank, MPI_COMM_WORLD);
      cfd::LocalMesh lm =
          cfd::load_partition(cfd::partition_dir(out, np), np, rank);
      cfd::Solver solver(cfg, std::move(lm), MPI_COMM_WORLD, out, "");
      if (sub == "gradcheck")
        solver.debug_gradcheck();
      else
        solver.debug_visccheck();
    } else {
      usage();
      throw std::runtime_error("unknown subcommand: " + sub);
    }
  } catch (const std::exception& e) {
    fprintf(stderr, "[rank %d] error: %s\n", rank, e.what());
    fflush(stderr);
    rc = 1;
  }
  MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Finalize();
  return rc;
}
