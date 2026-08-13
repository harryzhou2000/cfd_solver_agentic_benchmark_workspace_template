#include <mpi.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "case_io.hpp"
#include "forces.hpp"
#include "mesh_global.hpp"
#include "mesh_local.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "reconstruction.hpp"
#include "solver.hpp"

namespace {

struct Args {
  std::string case_file;
  std::string outdir;
  std::string restart;
  std::string report_level = "brief";
  int field_interval = 0;
  double cfl_cap = 0.0;  // 0 = use the case file value
  double relax = 1.0;
  int max_inner = 0;  // 0 = use the case file value
  int max_steps = 0;  // 0 = use the case file value
  bool first_order = false;
  bool wall_first = false;
  double diagf = 0.0;  // 0 = default
  int sweeps = 0;      // 0 = default (4 in the transient loop)
  bool have_restart = false;
};

bool parse_args(int argc, char** argv, Args& a, std::string& err) {
  if (argc < 2 || std::strcmp(argv[1], "solve") != 0) {
    err = "usage: cfd_solver solve --case <case.json> --output <dir> "
          "[--restart <file>] [--report-level brief|full] "
          "[--field-interval <n>] [--cfl-cap <v>] [--relax <v>]";
    return false;
  }
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto need = [&](const char* name) -> std::string {
      if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
      return argv[++i];
    };
    if (arg == "--case") {
      a.case_file = need("--case");
    } else if (arg == "--output") {
      a.outdir = need("--output");
    } else if (arg == "--restart") {
      a.restart = need("--restart");
      a.have_restart = true;
    } else if (arg == "--report-level") {
      a.report_level = need("--report-level");
    } else if (arg == "--field-interval") {
      a.field_interval = std::stoi(need("--field-interval"));
    } else if (arg == "--cfl-cap") {
      a.cfl_cap = std::stod(need("--cfl-cap"));
    } else if (arg == "--relax") {
      a.relax = std::stod(need("--relax"));
    } else if (arg == "--max-inner") {
      a.max_inner = std::stoi(need("--max-inner"));
    } else if (arg == "--max-steps") {
      a.max_steps = std::stoi(need("--max-steps"));
    } else if (arg == "--first-order") {
      a.first_order = true;
    } else if (arg == "--wall-first") {
      a.wall_first = true;
    } else if (arg == "--diagf") {
      a.diagf = std::stod(need("--diagf"));
    } else if (arg == "--sweeps") {
      a.sweeps = std::stoi(need("--sweeps"));
    } else {
      err = "unknown argument: " + arg;
      return false;
    }
  }
  if (a.case_file.empty() || a.outdir.empty()) {
    err = "--case and --output are required";
    return false;
  }
  return true;
}

std::string command_string(int argc, char** argv) {
  std::string s;
  for (int i = 0; i < argc; ++i) {
    if (!s.empty()) s += " ";
    s += argv[i];
  }
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  int provided = 0;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  Args args;
  std::string err;
  bool ok = parse_args(argc, argv, args, err);
  if (!ok) {
    if (rank == 0) std::fprintf(stderr, "ERROR: %s\n", err.c_str());
    MPI_Finalize();
    return 1;
  }

  int exit_code = 0;
  try {
    if (rank == 0) cfd::create_output_dir(args.outdir);
    MPI_Barrier(MPI_COMM_WORLD);

    // Capture solver stdout/stderr into the output directory (rank 0 writes).
    {
      const std::string log = args.outdir + "/stdout.log";
      FILE* f = std::freopen(log.c_str(), "w", stdout);
      if (f) std::freopen(log.c_str(), "a", stderr);
    }

    cfd::Case c = cfd::load_case(args.case_file);
    if (args.max_steps > 0) c.max_steps = args.max_steps;
    cfd::FreeStream fs = cfd::freestream_from_case(c);
    cfd::Gas gas(c);

    // Preprocessing: rank 0 reads the mesh, partitions with METIS, and writes
    // per-rank partition files. Solver iterations only load rank-local data.
    if (rank == 0) {
      cfd::GlobalMesh m = cfd::read_cgns_mesh(c.mesh_file, c);
      cfd::Partition p = cfd::partition_cells(m, nranks);
      cfd::write_partition_files(m, p, nranks, args.outdir);
      FILE* sf = std::fopen((args.outdir + "/partition/summary.txt").c_str(), "w");
      if (sf) {
        std::fprintf(sf, "%d\n", p.edge_cut);
        std::fclose(sf);
      }
      std::printf("mesh: %d cells, %d faces, %d nodes; partition edge cut %d\n",
                  m.num_cells_global(), m.num_faces_global(),
                  static_cast<int>(m.node_x.size()), p.edge_cut);
      std::fflush(stdout);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    cfd::LocalMesh lm = cfd::load_local_mesh(args.outdir, rank);
    cfd::Solver s;
    s.c = &c;
    s.fs = fs;
    s.gas = gas;
    s.mesh = lm;
    s.rank = rank;
    s.nranks = nranks;
    s.comm = MPI_COMM_WORLD;
    s.nc = lm.n_cells_local();
    s.U.assign(4 * s.nc, 0.0);
    s.U_prev1.assign(4 * s.nc, 0.0);
    s.U_prev2.assign(4 * s.nc, 0.0);
    s.Q.assign(4 * s.nc, 0.0);
    s.grad.assign(8 * s.nc, 0.0);
    s.phi.assign(4 * s.nc, 1.0);
    s.rec_off.assign(8 * s.nc * 8, 0.0);
    s.rec_off_faces.assign(s.nc, 0);
    s.dU.assign(4 * s.nc, 0.0);
    s.U0s.assign(4 * s.nc, 0.0);
    s.R.assign(4 * s.mesh.n_owned, 0.0);
    s.R_solve.assign(4 * s.mesh.n_owned, 0.0);
    s.dt_ps.assign(s.mesh.n_owned, 0.0);
    s.diag.assign(s.mesh.n_owned, 0.0);
    s.cell_lam.assign(s.mesh.n_owned, 0.0);
    s.wall_cell.assign(s.nc, 0);
    for (const auto& f : s.mesh.faces) {
      if (f.c1 < 0 &&
          (f.bc == cfd::BCType::SlipWall ||
           f.bc == cfd::BCType::NoSlipAdiabaticWall)) {
        s.wall_cell[f.c0] = 1;
      }
    }
    // The wall-adjacent flag is used by the wall-face pressure averaging
    // (which excludes other wall-adjacent cells). Ghost cells must carry the
    // flag of their owning rank, otherwise the exclusion set differs between
    // partitions (the observed cause of rank-dependent wall forces).
    {
      std::vector<double> wf(s.nc, 0.0);
      for (int i = 0; i < s.nc; ++i) wf[i] = s.wall_cell[i] ? 1.0 : 0.0;
      cfd::halo_exchange(s.mesh, MPI_COMM_WORLD, 1, wf.data());
      for (int i = 0; i < s.nc; ++i)
        if (wf[i] > 0.5) s.wall_cell[i] = 1;
    }
    s.rho_min = 1e-8 * fs.rho;
    s.p_min = 1e-8 * fs.p;
    if (args.field_interval > 0) c.write_field_interval = args.field_interval;
    if (args.cfl_cap > 0) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.6g", args.cfl_cap);
      setenv("CFD_CFL_CAP", buf, 1);
    }
    if (args.relax != 1.0) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.6g", args.relax);
      setenv("CFD_OMEGA", buf, 1);
    }
    if (args.max_inner > 0) c.max_inner_iterations = args.max_inner;
    if (args.first_order) setenv("CFD_FIRST_ORDER", "1", 1);
    if (args.wall_first) setenv("CFD_WALL_FIRST", "1", 1);
    if (args.diagf > 0.0) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.6g", args.diagf);
      setenv("CFD_DIAGF", buf, 1);
    }
    if (args.sweeps > 0) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%d", args.sweeps);
      setenv("CFD_SWEEPS", buf, 1);
    }

    cfd::initialize_state(s);

    int start_step = 0;
    double start_time = 0.0;
    if (args.have_restart) {
      auto rt = cfd::read_restart(s, args.restart);
      start_step = rt.first;
      // A --max-steps override counts ADDITIONAL steps beyond the restart
      // point (it replaces the absolute case-file horizon otherwise).
      if (args.max_steps > 0) c.max_steps = start_step + args.max_steps;
      start_time = rt.second;
      if (rank == 0)
        std::printf("restart from step %d (t=%.6g)\n", start_step, start_time);
    }

    cfd::init_csv_files(s, args.outdir);
    const std::string start_utc = cfd::now_iso();

    auto t0 = std::chrono::steady_clock::now();
    std::string status;
    int final_step = 0;
    double final_time = 0.0;
    if (!c.transient) {
      status = cfd::run_steady(s, args.outdir, start_step);
      final_step = start_step + s.inner_stats.steps;
      final_time = 0.0;
    } else {
      status = cfd::run_transient(s, args.outdir, start_step);
      final_step = s.physical_step;
      final_time = final_step * c.time_step;
    }
    auto t1 = std::chrono::steady_clock::now();
    const double wall =
        std::chrono::duration<double>(t1 - t0).count();

    // Final surface rows at the final state.
    std::vector<cfd::SurfaceRow> rows;
    cfd::ResidualNorms fnorm;
    cfd::compute_residual(s, false, nullptr, &fnorm, &rows);
    cfd::write_surface_csv(s, args.outdir, rows);
    cfd::write_field_vtu(s, args.outdir, "field_final");
    cfd::write_restart(s, args.outdir + "/restart_final.bin", final_step,
                       final_time);
    cfd::write_partition_diagnostics(s, args.outdir);

    cfd::RunResult rr;
    rr.case_id = c.case_id;
    rr.start_time_utc = start_utc;
    rr.end_time_utc = cfd::now_iso();
    rr.convergence_status = status;
    rr.final_step = final_step;
    rr.final_physical_time = final_time;
    rr.wall_time_seconds = wall;
    rr.residual_reduction_orders =
        std::log10(fnorm.l2 / (s.residual_initial_l2 + 1e-300) + 1e-300);
    rr.notes = "ran with " + std::to_string(nranks) + " MPI ranks; status " +
               status;
    cfd::write_metadata(s, args.outdir, rr);
    cfd::write_run_status(s, args.outdir, command_string(argc, argv), rr);

    if (rank == 0) {
      std::printf("case %s status=%s wall=%.1fs final_step=%d "
                  "res_reduction=%.2f orders\n",
                  c.case_id.c_str(), status.c_str(), wall, final_step,
                  rr.residual_reduction_orders);
      std::fflush(stdout);
    }
  } catch (const std::exception& e) {
    if (rank == 0) std::fprintf(stderr, "ERROR: %s\n", e.what());
    exit_code = 1;
  }

  MPI_Finalize();
  return exit_code;
}
