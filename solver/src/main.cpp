#include "config.hpp"
#include "mesh.hpp"
#include "output.hpp"
#include "solver.hpp"

#include <mpi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::string iso8601_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

void usage() {
  std::cerr << "usage: cfd_solver solve --case <case.json> --output <dir> "
               "[--restart <restart.bin>] [--report-level brief|full]\n";
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  int rc = 1;
  try {
    if (argc < 2 || std::strcmp(argv[1], "solve") != 0) {
      if (rank == 0) usage();
      MPI_Finalize();
      return 2;
    }
    std::string case_file, out_dir, restart_file, report_level = "brief";
    int max_steps_override = 0;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--case" && i + 1 < argc)
        case_file = argv[++i];
      else if (a == "--output" && i + 1 < argc)
        out_dir = argv[++i];
      else if (a == "--restart" && i + 1 < argc)
        restart_file = argv[++i];
      else if (a == "--report-level" && i + 1 < argc)
        report_level = argv[++i];
      else if (a == "--max-steps" && i + 1 < argc)
        max_steps_override = std::atoi(argv[++i]);
      else {
        if (rank == 0) {
          std::cerr << "ERROR: unknown or malformed argument: " << a << "\n";
          usage();
        }
        MPI_Finalize();
        return 2;
      }
    }
    if (case_file.empty() || out_dir.empty()) {
      if (rank == 0) {
        std::cerr << "ERROR: --case and --output are required\n";
        usage();
      }
      MPI_Finalize();
      return 2;
    }
    (void)report_level;

    if (rank == 0) {
      std::cout << "cfd_solver: reading case " << case_file << "\n";
      std::cout.flush();
    }
    const cfd::CaseConfig cfg = cfd::load_case(case_file);
    cfd::GlobalMesh mesh;
    if (rank == 0) {
      mesh = cfd::read_mesh(cfg);
    }
    cfd::broadcast_mesh(mesh, rank);
    if (rank == 0) {
      std::cout << "cfd_solver: mesh " << mesh.mesh_file << " with "
                << mesh.cells.size() << " cells, " << mesh.faces.size() << " interior faces, " \
                << mesh.bfaces.size() << " boundary faces\n";
    }

    cfd::CaseConfig cfg_eff = cfg;
    if (max_steps_override > 0) cfg_eff.max_steps = max_steps_override;
    cfd::Solver solver(cfg_eff, mesh, rank, nranks);
    solver.out_dir = out_dir;
    solver.start_time_utc = iso8601_now();
    {
      std::string cmd = "cfd_solver solve --case " + case_file + " --output " + out_dir;
      if (!restart_file.empty()) cmd += " --restart " + restart_file;
      solver.command_line = cmd;
    }

    if (!restart_file.empty()) {
      std::string err;
      if (!cfd::read_restart(solver, restart_file, err))
        throw std::runtime_error(err);
      if (rank == 0) {
        std::cout << "cfd_solver: resumed from restart at step " << solver.restart_step
                  << ", time " << solver.restart_time << "\n";
        std::cout.flush();
      }
    }

    const auto t0 = std::chrono::steady_clock::now();
    solver.run();
    const auto t1 = std::chrono::steady_clock::now();
    const double wall = std::chrono::duration<double>(t1 - t0).count();

    cfd::write_final_outputs(solver, out_dir, wall, solver.command_line);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
      std::cout << "cfd_solver: run completed in " << wall << " s; outputs in " << out_dir
                << "\n";
      std::cout.flush();
    }
    rc = 0;
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::cerr << "ERROR: " << e.what() << "\n";
      std::cerr.flush();
    }
    rc = 1;
  }

  MPI_Finalize();
  return rc;
}
