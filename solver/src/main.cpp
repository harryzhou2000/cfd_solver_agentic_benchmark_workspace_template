#include "fv/Solver.hpp"
#include "io/Input.hpp"
#include "io/Output.hpp"
#include "mesh/GlobalMesh.hpp"
#include "mesh/Partition.hpp"
#include "physics/Gas.hpp"

#include <mpi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

namespace {

void print_usage() {
  std::fprintf(stderr,
      "usage: cfd_solver solve --case <case.json> --output <dir> "
      "[--restart <file>] [--report-level brief|full]\n");
}

// Recursively create a directory.
void mkdir_p(const std::string& path) {
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur += path[i];
    if (path[i] == '/' || i + 1 == path.size()) {
      if (!cur.empty() && cur != "/") ::mkdir(cur.c_str(), 0755);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::setvbuf(stderr, nullptr, _IOLBF, 0);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  std::string case_path, output_dir, restart_path;
  bool have_restart = false;
  bool ok_args = false;
  if (argc >= 2 && std::strcmp(argv[1], "solve") == 0) {
    for (int i = 2; i < argc; ++i) {
      if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) {
        case_path = argv[++i];
      } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
        output_dir = argv[++i];
      } else if (std::strcmp(argv[i], "--restart") == 0 && i + 1 < argc) {
        restart_path = argv[++i];
        have_restart = true;
      } else if (std::strcmp(argv[i], "--report-level") == 0 && i + 1 < argc) {
        ++i;  // accepted; level does not change solver behavior
      } else {
        ok_args = false;
        break;
      }
    }
    ok_args = !case_path.empty() && !output_dir.empty();
  }

  if (!ok_args) {
    if (rank == 0) print_usage();
    MPI_Finalize();
    return 1;
  }

  try {
    // Prepare the output directory and move into it (all ranks).
    if (rank == 0) mkdir_p(output_dir);
    MPI_Barrier(MPI_COMM_WORLD);
    if (::chdir(output_dir.c_str()) != 0) {
      if (rank == 0)
        std::fprintf(stderr, "[cfds] cannot chdir to %s\n", output_dir.c_str());
      MPI_Finalize();
      return 1;
    }

    // Case configuration (parsed on every rank; deterministic).
    const cfds::CaseConfig cfg = cfds::parse_case_file(case_path);

    // Serial preprocessing: rank 0 loads the CGNS mesh, broadcasts it, every
    // rank builds its local partition mesh. The full mesh is retained only on
    // rank 0 (for writing final field/restart files).
    cfds::GlobalMesh global;
    if (rank == 0) global = cfds::load_cgns_mesh(cfg.mesh_file);
    global = cfds::broadcast_global_mesh(global, MPI_COMM_WORLD);
    cfds::DistributedMesh mesh;
    const int edge_cut = cfds::distribute_mesh(global, cfg, mesh, MPI_COMM_WORLD);
    if (rank != 0) global = cfds::GlobalMesh{};  // drop full mesh on workers

    const cfds::GasModel gas = cfds::make_gas_model(cfg);
    cfds::Solver solver(cfg, gas, mesh, MPI_COMM_WORLD);

    if (have_restart) {
      std::vector<cfds::ConsVec> U0;
      int step0 = 0;
      double t0 = 0.0;
      const bool ok = cfds::read_restart(restart_path, mesh, U0, step0, t0,
                                         MPI_COMM_WORLD);
      if (!ok) {
        if (rank == 0)
          std::fprintf(stderr, "[cfds] restart load failed: %s\n", restart_path.c_str());
        MPI_Finalize();
        return 1;
      }
      solver.set_initial_state(U0);
      if (rank == 0)
        std::printf("restarted from %s (step %d, t=%.6e)\n",
                    restart_path.c_str(), step0, t0);
    }

    // CSV headers (rank 0).
    cfds::write_csv_headers(cfg);

    const std::string start_utc = cfds::utc_now();
    const cfds::SolverStats stats = solver.run();
    const std::string end_utc = cfds::utc_now();

    const cfds::PartitionSummary summary =
        cfds::partition_summary(mesh, MPI_COMM_WORLD);

    // Command string for run_status.json.
    std::string command = "cfd_solver solve --case " + case_path +
                          " --output " + output_dir;
    if (have_restart) command += " --restart " + restart_path;

    cfds::OutputContext ctx{cfg, gas, (rank == 0) ? &global : nullptr, mesh,
                            stats, solver, edge_cut, summary, command,
                            GIT_REVISION, start_utc, end_utc, MPI_COMM_WORLD};
    cfds::write_outputs(ctx);

    if (rank == 0) {
      std::printf("run finished: case %s status %s final_step %d "
                  "residual_orders %.2f wall_time %.1f s\n",
                  cfg.case_id.c_str(), stats.convergence_status.c_str(),
                  stats.final_step, stats.residual_reduction_orders,
                  stats.wall_time_seconds);
    }
  } catch (const std::exception& e) {
    if (rank == 0) std::fprintf(stderr, "[cfds] error: %s\n", e.what());
    MPI_Finalize();
    return 1;
  }

  MPI_Finalize();
  return 0;
}
