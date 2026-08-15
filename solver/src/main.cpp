#include "common.h"
#include "case.h"
#include "mesh.h"
#include "partition.h"
#include "solver.h"

#include <filesystem>

namespace {

struct Args {
  std::string command;
  std::string case_file;
  std::string outdir;
  std::string restart_file;
  bool brief = false;
  bool ok = true;
  std::string err;
};

Args parse_args(int argc, char** argv) {
  Args a;
  std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    a.ok = false;
    a.err = "usage: cfd_solver solve --case <case.json> --output <dir> "
            "[--restart <file>] [--report-level brief|full]";
    return a;
  }
  a.command = args[0];
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& s = args[i];
    auto val = [&](const std::string& name) -> std::string {
      if (s == name && i + 1 < args.size()) return args[++i];
      if (s.rfind(name + "=", 0) == 0) return s.substr(name.size() + 1);
      return "";
    };
    if (s == "--case" || s.rfind("--case=", 0) == 0) {
      a.case_file = val("--case");
    } else if (s == "--output" || s.rfind("--output=", 0) == 0) {
      a.outdir = val("--output");
    } else if (s == "--restart" || s.rfind("--restart=", 0) == 0) {
      a.restart_file = val("--restart");
    } else if (s == "--report-level" || s.rfind("--report-level=", 0) == 0) {
      std::string v = val("--report-level");
      if (v == "brief") a.brief = true;
      else if (v != "full") {
        a.ok = false;
        a.err = "invalid --report-level '" + v + "' (expected brief|full)";
        return a;
      }
    } else if (s == "--help" || s == "-h") {
      a.ok = false;
      a.err = "usage: cfd_solver solve --case <case.json> --output <dir> "
              "[--restart <file>] [--report-level brief|full]";
      return a;
    } else {
      a.ok = false;
      a.err = "unknown argument: " + s;
      return a;
    }
  }
  if (a.command != "solve") {
    a.ok = false;
    a.err = "unknown command '" + a.command + "' (expected 'solve')";
    return a;
  }
  if (a.case_file.empty() || a.outdir.empty()) {
    a.ok = false;
    a.err = "both --case and --output are required";
    return a;
  }
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, nranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);
  cfd::g_log.enabled = (rank == 0);

  Args args = parse_args(argc, argv);
  if (!args.ok) {
    if (rank == 0) fprintf(stderr, "error: %s\n", args.err.c_str());
    MPI_Finalize();
    return 1;
  }

  // Open the log file (rank 0) before solver logging starts.
  if (rank == 0) {
    std::error_code ec;
    std::filesystem::create_directories(args.outdir, ec);
    cfd::g_log.open(args.outdir + "/stdout.log");
  }

  cfd::CaseConfig cfg;
  std::string err;
  if (!cfd::load_case(args.case_file, cfg, err)) {
    if (rank == 0) fprintf(stderr, "error: %s\n", err.c_str());
    MPI_Finalize();
    return 1;
  }
  if (!std::filesystem::exists(cfg.mesh_file)) {
    if (rank == 0) fprintf(stderr, "error: mesh file not found: %s\n", cfg.mesh_file.c_str());
    MPI_Finalize();
    return 1;
  }

  cfd::Mesh mesh;
  if (!cfd::read_mesh(cfg.mesh_file, cfg.bc_map, mesh, err)) {
    if (rank == 0) fprintf(stderr, "error: %s\n", err.c_str());
    MPI_Finalize();
    return 1;
  }

  cfd::LocalMesh lm;
  if (!cfd::build_local_mesh(mesh, nranks, rank, lm, err)) {
    if (rank == 0) fprintf(stderr, "error: %s\n", err.c_str());
    MPI_Finalize();
    return 1;
  }

  cfd::RunStats stats;
  int rc = cfd::run_solver(cfg, lm, mesh, args.outdir, args.restart_file,
                           args.brief, stats, err);
  if (rc != 0 && rank == 0) {
    fprintf(stderr, "error: %s\n", err.c_str());
  }
  cfd::g_log.close();
  MPI_Finalize();
  return rc;
}
