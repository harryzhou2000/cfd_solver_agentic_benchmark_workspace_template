#include <cstring>
#include <string>
#include <vector>

#include <mpi.h>

#include "common.hpp"
#include "config.hpp"
#include "driver.hpp"

namespace {

struct Args {
  std::string subcommand;
  std::string case_path;
  std::string output_dir;
  std::string restart_file;
  bool report_full = false;
  cfd::CaseConfig::Overrides overrides;
};

std::string need_value(int& i, int argc, char** argv, const std::string& flag) {
  if (i + 1 >= argc) {
    cfd::fatal("missing value for " + flag);
  }
  return argv[++i];
}

bool parse_args(int argc, char** argv, Args& a) {
  if (argc < 2) return false;
  a.subcommand = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--case") {
      a.case_path = need_value(i, argc, argv, "--case");
    } else if (arg == "--output") {
      a.output_dir = need_value(i, argc, argv, "--output");
    } else if (arg == "--restart") {
      a.restart_file = need_value(i, argc, argv, "--restart");
    } else if (arg == "--report-level") {
      std::string v = need_value(i, argc, argv, "--report-level");
      a.report_full = (v == "full");
    } else if (arg == "--max-steps") {
      a.overrides.max_steps = std::stoi(need_value(i, argc, argv, "--max-steps"));
    } else if (arg == "--final-time") {
      a.overrides.final_time = std::stod(need_value(i, argc, argv, "--final-time"));
    } else if (arg == "--min-inner") {
      a.overrides.min_inner = std::stoi(need_value(i, argc, argv, "--min-inner"));
    } else if (arg == "--max-inner") {
      a.overrides.max_inner = std::stoi(need_value(i, argc, argv, "--max-inner"));
    } else if (arg == "--cfl-initial") {
      a.overrides.cfl_initial = std::stod(need_value(i, argc, argv, "--cfl-initial"));
    } else if (arg == "--cfl-max") {
      a.overrides.cfl_max = std::stod(need_value(i, argc, argv, "--cfl-max"));
    } else if (arg == "--flux") {
      a.overrides.flux = need_value(i, argc, argv, "--flux");
    } else if (arg == "-h" || arg == "--help") {
      return false;
    } else {
      cfd::fatal("unknown argument: " + arg);
    }
  }
  return true;
}

void print_usage() {
  if (cfd::g_rank != 0) return;
  std::cout <<
      "cfd_solver - 2-D unstructured finite-volume compressible Navier-Stokes solver\n"
      "Usage:\n"
      "  mpirun -np <ranks> cfd_solver solve --case <case.json> --output <dir>\n"
      "      [--restart <file>] [--report-level brief|full]\n"
      "      [--max-steps N] [--final-time T] [--min-inner N] [--max-inner N]\n"
      "      [--cfl-initial X] [--cfl-max X] [--flux rusanov|roe]\n"
      "  mpirun -np <ranks> cfd_solver info --case <case.json>\n";
}

}  // namespace

int cfd::g_rank = 0;
int cfd::g_nranks = 1;

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &cfd::g_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &cfd::g_nranks);

  Args a;
  if (!parse_args(argc, argv, a)) {
    print_usage();
    MPI_Finalize();
    return 2;
  }

  if (a.case_path.empty()) {
    cfd::fatal("missing --case <case.json>");
  }

  std::string command_line;
  for (int i = 0; i < argc; ++i) {
    if (i) command_line += " ";
    command_line += argv[i];
  }

  cfd::CaseConfig cfg = cfd::load_case_config(a.case_path);
  cfg.overrides = a.overrides;
  if (cfg.overrides.max_steps) cfg.max_steps = *cfg.overrides.max_steps;
  if (cfg.overrides.final_time) cfg.final_time = *cfg.overrides.final_time;
  if (cfg.overrides.min_inner) cfg.min_inner_iterations = *cfg.overrides.min_inner;
  if (cfg.overrides.max_inner) cfg.max_inner_iterations = *cfg.overrides.max_inner;
  if (cfg.overrides.cfl_initial) cfg.cfl_initial = *cfg.overrides.cfl_initial;
  if (cfg.overrides.cfl_max) cfg.cfl_max = *cfg.overrides.cfl_max;
  if (cfg.overrides.flux) {
    if (*cfg.overrides.flux != "rusanov" && *cfg.overrides.flux != "roe") {
      cfd::fatal("--flux must be rusanov or roe");
    }
    cfg.inviscid_flux = *cfg.overrides.flux;
  }

  int rc = 0;
  if (a.subcommand == "solve") {
    if (a.output_dir.empty()) cfd::fatal("solve requires --output <dir>");
    rc = cfd::run_solve(cfg, a.output_dir, a.restart_file, command_line, a.report_full);
  } else if (a.subcommand == "info") {
    rc = cfd::run_info(cfg);
  } else if (a.subcommand == "test-lsq") {
    rc = cfd::run_lsq_test(cfg);
  } else {
    cfd::fatal("unknown subcommand '" + a.subcommand + "' (expected solve or info)");
  }

  MPI_Finalize();
  return rc;
}
