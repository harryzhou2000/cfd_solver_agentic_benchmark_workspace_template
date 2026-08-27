// fv2d: 2-D unstructured finite-volume compressible Navier-Stokes solver.
// CLI:
//   mpirun -np <ranks> fv2d solve --case <case.json> --output <dir>
//        [--restart <restart-file>] [--report-level brief|full]
//        [--flux roe|rusanov]
#include "common.hpp"
#include "case_config.hpp"
#include "global_mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"
#include "output.hpp"
#include <mpi.h>

using namespace fv;

namespace {

void usage(const char* msg) {
  if (msg) std::fprintf(stderr, "error: %s\n", msg);
  std::fprintf(stderr,
               "usage: mpirun -np <ranks> fv2d solve --case <case.json> --output <dir>\n"
               "          [--restart <restart-file>] [--report-level brief|full]\n"
               "          [--flux roe|rusanov]\n");
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int exitCode = 0;
  try {
    // ---- parse CLI
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    std::string cmdline;
    for (int i = 0; i < argc; ++i) {
      if (i) cmdline += " ";
      cmdline += argv[i];
    }
    setCommandLine(std::string("mpirun -np <ranks> ") + cmdline);
#ifdef GIT_REVISION
    setGitRevision(GIT_REVISION);
#endif
    if (args.empty() || args[0] != "solve") {
      if (rank == 0) usage(args.empty() ? "missing subcommand" : "unknown subcommand");
      MPI_Finalize();
      return 2;
    }
    std::string caseFile, outDir, restartFile, reportLevel = "full", fluxOpt = "";
    for (size_t i = 1; i < args.size(); ++i) {
      auto need = [&](const std::string& k) -> std::string {
        if (i + 1 >= args.size()) die("missing value for " + k);
        return args[++i];
      };
      if (args[i] == "--case") caseFile = need("--case");
      else if (args[i] == "--output") outDir = need("--output");
      else if (args[i] == "--restart") restartFile = need("--restart");
      else if (args[i] == "--report-level") reportLevel = need("--report-level");
      else if (args[i] == "--flux") fluxOpt = need("--flux");
      else die("unknown argument: " + args[i]);
    }
    if (caseFile.empty()) die("--case is required");
    if (outDir.empty()) die("--output is required");
    if (reportLevel != "brief" && reportLevel != "full") die("--report-level must be brief|full");

    // ---- case + mesh + partition
    CaseConfig cfg = loadCaseConfig(caseFile);
    // flux selection: Roe for steady cases by default; Rusanov when the case
    // specifies a Rusanov dissipation scale (transient reference case) or via CLI
    bool useRoe = true;
    if (cfg.transient()) useRoe = false;  // production transient uses Rusanov with scale 1.0
    if (!fluxOpt.empty()) {
      if (fluxOpt == "roe") useRoe = true;
      else if (fluxOpt == "rusanov") useRoe = false;
      else die("--flux must be roe|rusanov");
    }
    GlobalMesh gm;
    if (rank == 0) {
      gm = readCgnsMesh(cfg.mesh_file);
      buildFaces(gm);
      std::fprintf(stderr, "[fv2d] mesh %s: %d nodes, %d cells, %d faces, families:",
                   cfg.mesh_file.c_str(), gm.numNodes(), gm.numCells(), gm.numFaces());
      for (auto& f : gm.families) std::fprintf(stderr, " %s", f.c_str());
      std::fprintf(stderr, "\n");
    }
    LocalMesh lm = partitionMesh(gm, cfg.bc_map, MPI_COMM_WORLD);
    {
      std::fprintf(stderr, "[fv2d rank %d] owned %d ghost %d faces %zu neighbors %zu\n", rank,
                   lm.nOwn, lm.nGhost, lm.faces.size(), lm.neighbors.size());
    }
    Solver solver(std::move(cfg), std::move(lm), MPI_COMM_WORLD, useRoe);
    solver.run(outDir, restartFile);
  } catch (const std::exception& e) {
    if (rank == 0) std::fprintf(stderr, "fatal error: %s\n", e.what());
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  MPI_Finalize();
  return exitCode;
}
