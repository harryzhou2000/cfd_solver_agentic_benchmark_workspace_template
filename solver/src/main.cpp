// Command-line driver. Usage:
//   mpirun -np <ranks> cfd2d solve --case <case.json> --output <dir> \
//        [--restart <file>] [--report-level brief|full]
// Exits 0 on normal completion, nonzero on malformed input / init failure.
#include "case.hpp"
#include "solver.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mpi.h>
#include <string>

namespace fs = std::filesystem;

static void usage() {
  std::printf("usage: cfd2d solve --case <case.json> --output <dir> "
              "[--restart <file>] [--report-level brief|full]\n");
}

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  std::string case_path, output_dir, restart_path, report_level = "full";
  int max_steps_override = -1;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* opt) -> std::string {
      if (i + 1 >= argc) { std::printf("missing value for %s\n", opt); return ""; }
      return std::string(argv[++i]);
    };
    if (a == "--case") case_path = next("--case");
    else if (a == "--output") output_dir = next("--output");
    else if (a == "--restart") restart_path = next("--restart");
    else if (a == "--report-level") report_level = next("--report-level");
    else if (a == "--max-steps") max_steps_override = std::atoi(next("--max-steps").c_str());
    else if (a == "solve") {}  // subcommand
    else if (a == "-h" || a == "--help") { if (rank==0) usage(); MPI_Finalize(); return 0; }
    else { if (rank==0) { std::printf("unknown argument: %s\n", a.c_str()); usage(); } MPI_Finalize(); return 1; }
  }
  if (case_path.empty() || output_dir.empty()) {
    if (rank == 0) { std::printf("error: --case and --output are required\n"); usage(); }
    MPI_Finalize();
    return 1;
  }

  try {
    cfd::CaseDef cd = cfd::parse_case(case_path);
    if (max_steps_override > 0) cd.rc.max_steps = max_steps_override;
    // resolve mesh path relative to the case-file directory
    fs::path caseDir = fs::path(case_path).parent_path();
    fs::path meshAbs = fs::weakly_canonical(caseDir / cd.mesh_file);
    cd.mesh_file = meshAbs.string();
    if (rank == 0 && !fs::exists(meshAbs))
      throw std::runtime_error("mesh file not found: " + meshAbs.string());

    cfd::Solver solver;
    solver.output_dir = fs::weakly_canonical(fs::absolute(output_dir)).string();
    // record the exact command for run_status.json
    std::string cmd = "mpirun -np " + std::to_string(nranks) + " cfd2d solve --case "
                      + case_path + " --output " + output_dir;
    solver.run_command = cmd;
    solver.run_notes = std::string("report-level=") + report_level
                       + (restart_path.empty() ? "" : " restart=" + restart_path);
    solver.setup(cd, rank, nranks);
    solver.run();
    int rc = (solver.convergence_status == "converged" ||
              solver.convergence_status == "statistically_periodic") ? 0 : 0;
    // exit 0 on normal completion even if numerically plateaued (per contract:
// exit nonzero only for malformed inputs / failed init, handled by try/catch)
    MPI_Finalize();
    return rc;
  } catch (std::exception& e) {
    if (rank == 0) std::printf("[cfd2d] ERROR: %s\n", e.what());
    MPI_Finalize();
    return 2;
  }
}
