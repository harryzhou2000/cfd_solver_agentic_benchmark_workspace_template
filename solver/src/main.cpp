#include <mpi.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "case_file.hpp"
#include "mesh.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace {

std::string gitRevision() {
  std::string rev = "unknown";
  FILE* p = popen("git rev-parse HEAD 2>/dev/null", "r");
  if (p) {
    char buf[128];
    if (std::fgets(buf, sizeof(buf), p)) {
      rev = buf;
      while (!rev.empty() && (rev.back() == '\n' || rev.back() == '\r')) rev.pop_back();
      if (rev.empty()) rev = "unknown";
    }
    pclose(p);
  }
  return rev;
}

void usage() {
  std::cout << "usage:\n"
            << "  mpirun -np <ranks> fv2d solve --case <case.json> --output <dir> "
               "[--restart <file>] [--report-level brief|full] [--flux hllc|rusanov] "
               "[--cfl-max <v>]\n"
            << "  mpirun -np <ranks> fv2d meshinfo --case <case.json>\n";
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  std::string cmdLine;
  for (int i = 0; i < argc; ++i) {
    if (i) cmdLine += " ";
    cmdLine += argv[i];
  }

  try {
    if (argc < 2) {
      if (rank == 0) usage();
      MPI_Finalize();
      return 1;
    }
    const std::string mode = argv[1];
    std::string casePath, outDir, restart, reportLevel = "full", fluxStr = "";
    double cflOverride = -1.0;
    double finalTimeOverride = -1.0;
    long maxStepsOverride = -1;
    double innerCfl = -1.0;
    int innerSweeps = -1;
    for (int i = 2; i < argc; ++i) {
      const std::string a = argv[i];
      auto next = [&](const char* name) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
        return argv[++i];
      };
      if (a == "--case")
        casePath = next("--case");
      else if (a == "--output")
        outDir = next("--output");
      else if (a == "--restart")
        restart = next("--restart");
      else if (a == "--report-level")
        reportLevel = next("--report-level");
      else if (a == "--flux")
        fluxStr = next("--flux");
      else if (a == "--cfl-max")
        cflOverride = std::stod(next("--cfl-max"));
      else if (a == "--final-time")
        finalTimeOverride = std::stod(next("--final-time"));
      else if (a == "--max-steps")
        maxStepsOverride = std::stol(next("--max-steps"));
      else if (a == "--inner-cfl")
        innerCfl = std::stod(next("--inner-cfl"));
      else if (a == "--inner-sweeps")
        innerSweeps = std::stoi(next("--inner-sweeps"));
      else
        throw std::runtime_error("unknown argument: " + a);
    }
    if (casePath.empty()) throw std::runtime_error("--case is required");

    fv::CaseConfig cfg = fv::loadCaseFile(casePath);
    if (finalTimeOverride > 0.0 && cfg.transient()) {
      cfg.final_time = finalTimeOverride;
      cfg.max_steps = static_cast<long>(std::llround(finalTimeOverride / cfg.time_step));
    }
    if (maxStepsOverride > 0) cfg.max_steps = maxStepsOverride;
    if (innerCfl > 0.0) {
      cfg.cfl_initial = innerCfl;
      cfg.cfl_max = innerCfl;
    }

    // rank 0 reads the global mesh (preprocessing only; never replicated)
    std::unique_ptr<fv::GlobalMesh> gm;
    if (rank == 0) {
      gm = std::make_unique<fv::GlobalMesh>(fv::readCgnsMesh(cfg.mesh_file));
      std::cout << "mesh: " << cfg.mesh_file << " cells=" << gm->nCells
                << " faces=" << gm->nFaces << " nodes=" << gm->nNodes
                << " boundary families:";
      for (const auto& n : gm->bcNames) std::cout << " " << n;
      std::cout << std::endl;
    }
    if (mode == "meshinfo") {
      MPI_Finalize();
      return 0;
    }
    if (mode != "solve") throw std::runtime_error("unknown mode: " + mode);
    if (outDir.empty()) throw std::runtime_error("--output is required");

    long edgeCut = 0;
    std::string partitionerName;
    fv::LocalMesh lm =
        fv::partitionAndScatter(gm.get(), MPI_COMM_WORLD, edgeCut, partitionerName);
    if (rank == 0)
      std::cout << "partition: " << partitionerName << " edge_cut=" << edgeCut
                << std::endl;

    fv::InviscidFluxType fluxType = fv::InviscidFluxType::HLLC;
    bool fluxExplicit = false;
    if (!fluxStr.empty()) {
      if (fluxStr == "rusanov" || fluxStr == "llf") {
        fluxType = fv::InviscidFluxType::RUSANOV;
        fluxExplicit = true;
      } else if (fluxStr == "hllc") {
        fluxExplicit = true;
      } else {
        throw std::runtime_error("unknown --flux value: " + fluxStr);
      }
    }
    // default flux by Mach: LLF/Rusanov for supersonic cases (robust shock
    // convergence), HLLC otherwise. Overridable with --flux.
    if (!fluxExplicit) fluxType = cfg.mach >= 1.5 ? fv::InviscidFluxType::RUSANOV
                                                  : fv::InviscidFluxType::HLLC;
    if (rank == 0)
      std::cout << "inviscid flux: "
                << (fluxType == fv::InviscidFluxType::HLLC ? "hllc" : "rusanov_llf")
                << std::endl;

    if (innerSweeps > 0) fv::Solver::overrideInnerSweeps = innerSweeps;
    fv::Solver solver(std::move(lm), cfg, MPI_COMM_WORLD, fluxType);
    const std::string gitRev = gitRevision();
    fv::RunStats stats = solver.run(outDir, restart, gm.get(), edgeCut, cmdLine, gitRev,
                                    cflOverride);
    if (rank == 0 && stats.convergenceStatus == "failed") {
      std::cerr << "run failed: " << stats.convergenceNotes << std::endl;
      MPI_Finalize();
      return 2;
    }
  } catch (const std::exception& e) {
    std::cerr << "error (rank " << rank << "): " << e.what() << std::endl;
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
  MPI_Finalize();
  return 0;
}
