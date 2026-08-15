#include <mpi.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/solver.hpp"

namespace {

std::string command_line(int argc, char** argv) {
  std::ostringstream os;
  for (int i = 0; i < argc; ++i) {
    if (i) os << ' ';
    os << argv[i];
  }
  return os.str();
}

void usage() {
  std::cerr << "usage: agentic_cfd solve --case <case-json> --output <output-dir> "
               "[--restart <restart-file>] [--report-level brief|full]\n";
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int exit_code = 0;
  try {
    if (argc < 2 || std::string(argv[1]) != "solve") {
      if (rank == 0) usage();
      MPI_Finalize();
      return 2;
    }

    cfd::SolveOptions opts;
    opts.command_line = command_line(argc, argv);
    for (int i = 2; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--case" && i + 1 < argc) {
        opts.case_path = argv[++i];
      } else if (arg == "--output" && i + 1 < argc) {
        opts.output_dir = argv[++i];
      } else if (arg == "--restart" && i + 1 < argc) {
        opts.restart_file = argv[++i];
      } else if (arg == "--report-level" && i + 1 < argc) {
        opts.report_level = argv[++i];
      } else {
        throw cfd::CfdError("unknown or incomplete argument: " + arg);
      }
    }
    if (opts.case_path.empty()) throw cfd::CfdError("--case is required");
    if (opts.output_dir.empty()) throw cfd::CfdError("--output is required");
    if (opts.report_level != "brief" && opts.report_level != "full") {
      throw cfd::CfdError("--report-level must be brief or full");
    }

    const cfd::RunSummary summary = cfd::solve_case(opts, MPI_COMM_WORLD);
    if (rank == 0 && summary.convergence_status == "failed") {
      std::cerr << "warning: run completed structurally but did not satisfy convergence criteria\n";
    }
  } catch (const std::exception& e) {
    if (rank == 0) std::cerr << "error: " << e.what() << "\n";
    exit_code = 1;
  }
  MPI_Finalize();
  return exit_code;
}
