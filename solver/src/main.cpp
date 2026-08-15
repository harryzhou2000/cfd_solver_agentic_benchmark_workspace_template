// cfd_solver entry point.
//
// Phase 1: case parsing, CGNS mesh loading, geometry computation and mesh
// summary. MPI is initialized for the partitioning phase that follows.

#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config/case_config.hpp"
#include "mesh/cgns_reader.hpp"
#include "mesh/geometry.hpp"
#include "mesh/mesh.hpp"
#include "output/output_writer.hpp"
#include "parallel/halo_exchange.hpp"
#include "partition/partition.hpp"
#include "physics/forces.hpp"
#include "physics/gas_model.hpp"
#include "solver/residual.hpp"
#include "solver/setup.hpp"
#include "solver/steady_solver.hpp"
#include "solver/transient_solver.hpp"

namespace fs = std::filesystem;

#ifndef CFD_GIT_REVISION_STR
#define CFD_GIT_REVISION_STR ""
#endif

namespace {

int run_solve(const std::string& case_path, const std::string& output_dir,
              const std::optional<std::string>& restart_path,
              const std::string& report_level, int num_steps,
              int max_steps_override, const std::string& command,
              MPI_Comm comm) {
  (void)restart_path;  // restart support arrives in a later phase

  int rank = 0, nranks = 1;
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &nranks);

  // 1. Parse the case JSON (--max-steps overrides the case value, e.g. for
  //    short debug runs)
  cfd::CaseConfig cfg = cfd::load_case_config(case_path);
  if (max_steps_override > 0) cfg.run_control.max_steps = max_steps_override;
  if (rank == 0)
    fmt::print("case '{}': {}\n", cfg.case_id, cfg.description);
  if (rank == 0)
    fmt::print("  mesh: {} (format {}, {}D)\n", cfg.mesh.file, cfg.mesh.format,
               cfg.mesh.dimension);

  // 2. Create the output directory and redirect rank-0 stdout to
  //    <output_dir>/stdout.log (all later prints land in the log file).
  if (rank == 0) {
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
      fmt::print(stderr, "[warning] cannot create output directory '{}': {}\n",
                 output_dir, ec.message());
    } else if (std::freopen((fs::path(output_dir) / "stdout.log").c_str(),
                            "w", stdout) == nullptr) {
      fmt::print(stderr, "[warning] cannot redirect stdout to '{}'\n",
                 (fs::path(output_dir) / "stdout.log").string());
    }
  }

  // 3. Read the mesh (serial preprocessing: every rank reads the same mesh;
  //    only rank 0 prints the reader diagnostics)
  cfd::MeshReadResult res =
      cfd::read_cgns_mesh(cfg.mesh.file, cfg.boundary_conditions,
                          /*verbose=*/rank == 0);

  // 4. Geometry is completed by read_cgns_mesh itself (cell
  //    centers/volumes, face centers/normals/areas).

  // 5. Mesh summary (rank 0 only)
  if (rank == 0) {
    long long ntri = 0, nquad = 0;
    for (const auto& c : res.mesh.cells) {
      if (c.type == cfd::CellType::Triangle) {
        ++ntri;
      } else {
        ++nquad;
      }
    }

    long long n_shared = 0, n_boundary = 0;
    for (const auto& f : res.mesh.faces) {
      const bool has_l = f.left_cell >= 0, has_r = f.right_cell >= 0;
      if (has_l == has_r) {
        if (has_l) ++n_shared;
      } else {
        ++n_boundary;
      }
    }

    fmt::print("\n=== mesh summary ===\n");
    fmt::print("zone name        : {}\n", res.mesh.zone_name);
    fmt::print("vertices         : {}\n", res.mesh.num_vertices);
    fmt::print("cells            : {} ({} triangles, {} quads)\n",
               res.mesh.num_cells, ntri, nquad);
    fmt::print("faces            : {} ({} shared, {} boundary)\n",
               res.mesh.num_faces, n_shared, n_boundary);
    for (const auto& kv : res.mesh.boundary_faces) {
      const cfd::Face2D& f = res.mesh.faces[kv.second.front()];
      fmt::print("  boundary '{}'   : {} faces ({})\n", kv.first,
                 kv.second.size(), cfd::bc_type_name(f.bc_type));
    }
    // Warn about case-file BC tags that did not receive any faces.
    for (const auto& kv : cfg.boundary_conditions) {
      if (!res.mesh.boundary_faces.count(kv.first)) {
        fmt::print(stderr,
                   "[warning] case BC tag '{}' has no boundary faces in the "
                   "loaded zone(s)\n",
                   kv.first);
      }
    }
    if (report_level == "full") {
      double vol_sum = 0.0, vol_min = 1e300, vol_max = 0.0;
      for (const auto& c : res.mesh.cells) {
        vol_sum += c.volume;
        vol_min = std::min(vol_min, c.volume);
        vol_max = std::max(vol_max, c.volume);
      }
      fmt::print("total area       : {:.6e}\n", vol_sum);
      fmt::print("cell area        : min {:.6e}, max {:.6e}, mean {:.6e}\n",
                 vol_min, vol_max,
                 vol_sum / static_cast<double>(res.mesh.num_cells));
    }
  }

  // 6. Partition the mesh and build the rank-local distributed setup
  if (rank == 0) fmt::print("\n=== partitioning ===\n");
  cfd::SolverSetup setup = cfd::setup_distributed(res.mesh, rank, nranks, comm);
  cfd::partition_diagnostics(setup.dmesh, comm);
  cfd::write_partition_diagnostics(output_dir, setup.dmesh, comm);

  // 7. Halo exchange self-check: ghosts must receive their owner's state
  const double halo_err = cfd::run_halo_selfcheck(setup, comm);
  double halo_err_global = 0.0;
  MPI_Allreduce(&halo_err, &halo_err_global, 1, MPI_DOUBLE, MPI_MAX, comm);
  if (rank == 0) {
    fmt::print("[halo] exchange self-check: max ghost error = {:.3e} ({})\n",
               halo_err_global,
               halo_err_global == 0.0 ? "PASS" : "FAIL");
  }

  if (rank == 0) {
    fmt::print("\nPhase 2 complete — mesh partitioned across {} rank(s), "
               "distributed mesh ready\n",
               nranks);
  }
  if (halo_err_global != 0.0) return 1;

  // 8. Initialize the freestream state and the viscosity
  if (rank == 0) fmt::print("\n=== solver setup ===\n");
  setup.U = cfd::init_freestream(setup.dmesh, cfg);
  double mu = 0.0;
  if (cfg.physics.mode == "laminar") {
    if (!cfg.physics.reynolds.has_value())
      throw std::runtime_error("laminar case '" + cfg.case_id +
                               "' is missing physics.reynolds");
    mu = cfg.freestream.rho * cfg.freestream.velocity_magnitude *
         cfg.reference.reynolds_length / *cfg.physics.reynolds;
  }
  if (rank == 0) {
    fmt::print("[solver] mode={} gamma={:.4f} R={:.4f} Pr={:.4f} mu={:.6e} "
               "rusanov_scale={:.3f}\n",
               cfg.physics.mode, cfg.gas.gamma, cfg.gas.R, cfg.gas.prandtl, mu,
               cfg.run_control.rusanov_dissipation_scale.value_or(1.0));
  }

  // 9. Residual verification loop. NOTE: Phase 3 has no state update yet —
  // each iteration re-evaluates the residual on the identical state (the
  // pseudo-time/transient update arrives in Phase 4). --num-steps exists for
  // debug runs of the residual pipeline (halo exchange + assembly + norms).
  std::vector<double> residual;
  for (int step = 0; step < num_steps; ++step) {
    cfd::halo_exchange(setup.dmesh, setup.U, setup.nvars, comm);
    cfd::compute_residual(setup.U, setup.dmesh, cfg, mu, residual);
    const cfd::ResidualNorm norm =
        cfd::compute_norms(residual, setup.dmesh, comm);
    if (rank == 0) {
      fmt::print(
          "[residual] step {:4d}: L2 rho={:.6e} rhou={:.6e} rhov={:.6e} "
          "rhoE={:.6e} | aggregate L2={:.6e} Linf={:.6e}\n",
          step, norm.l2_per_var[0], norm.l2_per_var[1], norm.l2_per_var[2],
          norm.l2_per_var[3], norm.l2, norm.linf);
    }
  }

  // 10. Forces on wall boundary faces (summed across ranks)
  const cfd::ForceResult F = cfd::compute_forces(setup.U, setup.dmesh, cfg, mu);
  double ftot[5] = {F.pressure_drag, F.viscous_drag, F.pressure_lift,
                    F.viscous_lift, F.moment_z};
  double ftot_g[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
  MPI_Allreduce(ftot, ftot_g, 5, MPI_DOUBLE, MPI_SUM, comm);
  if (rank == 0) {
    fmt::print("[forces] Cd_p={:+.6e} Cd_v={:+.6e} Cl_p={:+.6e} Cl_v={:+.6e} "
               "Cm={:+.6e}\n",
               ftot_g[0], ftot_g[1], ftot_g[2], ftot_g[3], ftot_g[4]);
    fmt::print("\nPhase 3 complete — residual assembly verified "
               "(first-order fluxes, {} step(s))\n",
               num_steps);
  }

  // 11. Production solver (second-order, implicit): steady or transient.
  if (rank == 0) fmt::print("\n=== production solver ===\n");
  cfd::write_residual_header(output_dir);
  cfd::write_forces_header(output_dir);
  const std::string started_utc = cfd::utc_now_iso8601();
  const auto wall_start = std::chrono::steady_clock::now();
  cfd::SteadyResult sr;
  cfd::TransientResult tr;
  if (cfg.run_control.type == "steady") {
    sr = cfd::run_steady_solver(cfg, setup.dmesh, setup.U, output_dir, comm);
    if (rank == 0) {
      fmt::print("[steady] finished: steps={} inner_iterations={} status='{}' "
                 "L2={:.6e} Linf={:.6e} reduction={:.3f} orders cfl={:.3f}\n",
                 sr.steps_run, sr.inner_iterations_total, sr.convergence_status,
                 sr.final_residual_l2, sr.final_residual_linf,
                 sr.residual_reduction_orders, sr.final_cfl);
      fmt::print(
          "[steady] inner stats: min={} max={} misses={} converged_fraction="
          "{:.3f} last_ratio={:.4e}\n",
          sr.observed_min_inner_iterations, sr.observed_max_inner_iterations,
          sr.inner_target_misses, sr.inner_target_converged_fraction,
          sr.last_inner_residual_ratio);
    }
  } else {
    tr = cfd::run_transient_solver(cfg, setup.dmesh, setup.U, output_dir,
                                   comm);
    if (rank == 0) {
      fmt::print("[transient] finished: steps={} t={:.4f} "
                 "inner_iterations={} status='{}'\n",
                 tr.steps_run, tr.final_time, tr.inner_iterations_total,
                 tr.convergence_status);
      fmt::print(
          "[transient] inner stats: min={} max={} misses={} converged_"
          "fraction={:.3f} last_ratio={:.4e}\n",
          tr.observed_min_inner_iterations, tr.observed_max_inner_iterations,
          tr.inner_target_misses, tr.inner_target_converged_fraction,
          tr.last_inner_residual_ratio);
    }
  }

  const double wall_seconds = std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - wall_start)
                                    .count();
  MPI_Barrier(comm);

  // 12. Final outputs (field, surface, metadata, status, restart). All ranks
  //     participate (per-rank VTU pieces, surface rows, restart cells and the
  //     metadata global reductions); rank 0 materializes the master files.
  cfd::write_field_final(output_dir, setup.dmesh, setup.U, cfg, comm);
  if (cfg.outputs.write_surface)
    cfd::write_surface(output_dir, setup.U, setup.dmesh, cfg, comm);
  if (cfg.run_control.type == "steady") {
    cfd::write_run_status(output_dir, cfg, sr.convergence_status,
                          sr.steps_run, 0.0, sr.residual_reduction_orders,
                          nranks, wall_seconds, command);
    cfd::write_metadata(output_dir, setup.dmesh, cfg, sr, "cfd_solver",
                        "0.1.0", CFD_GIT_REVISION_STR, started_utc);
    if (rank == 0)
      fmt::print("[output] metadata + run status written (status='{}', "
                 "{} steps, {:.1f} s)\n",
                 sr.convergence_status, sr.steps_run, wall_seconds);
  } else {
    cfd::write_run_status(output_dir, cfg, tr.convergence_status,
                          static_cast<int>(tr.steps_run), tr.final_time,
                          0.0, nranks, wall_seconds, command);
    cfd::write_metadata(output_dir, setup.dmesh, cfg, tr, "cfd_solver",
                        "0.1.0", CFD_GIT_REVISION_STR, started_utc);
    if (rank == 0)
      fmt::print("[output] metadata + run status written (status='{}', "
                 "{} steps, {:.1f} s)\n",
                 tr.convergence_status, tr.steps_run, wall_seconds);
  }
  cfd::write_restart(output_dir, setup.dmesh, setup.U, cfg, comm);

  if (rank == 0)
    fmt::print("\nPhase 5 complete — all outputs written to '{}'\n",
               output_dir);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);

  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  int rc = 0;
  try {
    argparse::ArgumentParser program("cfd_solver", "0.1.0");
    program.add_description("2D unstructured finite-volume CFD solver");

    argparse::ArgumentParser solve("solve");
    solve.add_description("Run a CFD case");
    solve.add_argument("--case")
        .required()
        .help("path to the case JSON file");
    solve.add_argument("--output")
        .required()
        .help("directory for solver outputs");
    solve.add_argument("--restart")
        .help("path to a restart file (reserved; not yet supported)");
    solve.add_argument("--report-level")
        .default_value(std::string("full"))
        .choices("brief", "full")
        .help("verbosity of the run report (brief|full)");
    solve.add_argument("--num-steps")
        .default_value(std::string("1"))
        .help("number of residual evaluation steps for debug runs");
    solve.add_argument("--max-steps")
        .help("override run_control.max_steps (steady) / physical step cap "
              "(transient), e.g. for short debug runs");
    // The partition count is controlled by MPI_Comm_size (mpirun -np);
    // this flag is accepted for documentation purposes and validated below.
    solve.add_argument("--np")
        .help("expected partition count (must match mpirun -np; optional)");
    program.add_subparser(solve);

    try {
      program.parse_args(argc, argv);
    } catch (const std::exception& err) {
      if (rank == 0) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
      }
      rc = 1;
      MPI_Finalize();
      return rc;
    }

    if (program.is_subcommand_used("solve")) {
      auto& cmd = program.at<argparse::ArgumentParser>("solve");
      const auto np = cmd.present("--np");
      int nranks = 1;
      MPI_Comm_size(MPI_COMM_WORLD, &nranks);
      if (np.has_value() && std::stoi(*np) != nranks) {
        if (rank == 0)
          std::cerr << "Error: --np " << *np
                    << " does not match the number of MPI ranks (" << nranks
                    << ")" << std::endl;
        rc = 1;
      } else {
        int num_steps = 1;
        try {
          num_steps = std::stoi(cmd.get("--num-steps"));
        } catch (const std::exception&) {
          if (rank == 0)
            std::cerr << "Error: --num-steps must be an integer" << std::endl;
          rc = 1;
        }
        if (num_steps < 1) num_steps = 1;
        int max_steps_override = -1;
        if (const auto ms = cmd.present("--max-steps"); ms.has_value()) {
          try {
            max_steps_override = std::stoi(*ms);
          } catch (const std::exception&) {
            if (rank == 0)
              std::cerr << "Error: --max-steps must be an integer" << std::endl;
            rc = 1;
          }
        }
        if (rc == 0) {
          std::string command = "cfd_solver";
          for (int i = 1; i < argc; ++i) {
            command += " ";
            command += argv[i];
          }
          rc = run_solve(cmd.get("--case"), cmd.get("--output"),
                         cmd.present("--restart"), cmd.get("--report-level"),
                         num_steps, max_steps_override, command,
                         MPI_COMM_WORLD);
        }
      }
    } else {
      throw std::runtime_error(
          "no subcommand given; use 'solve' (see --help)");
    }
  } catch (const std::exception& e) {
    if (rank == 0) std::cerr << "Error: " << e.what() << std::endl;
    rc = 1;
  }

  MPI_Finalize();
  return rc;
}
