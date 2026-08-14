#include "types.hpp"
#include "mesh.hpp"
#include "solver.hpp"
#include "output.hpp"
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <cstdio>
#include <algorithm>
#include <chrono>

using namespace cfd2d;
namespace cfd2d { extern int g_nSweeps; }

static std::string getGitRev() {
  FILE* p = popen("git rev-parse --short HEAD 2>/dev/null", "r");
  if (!p) return "";
  char buf[64]; std::string s;
  if (fgets(buf, sizeof(buf), p)) { s = buf; while(!s.empty() && (s.back()=='\n'||s.back()=='\r')) s.pop_back(); }
  pclose(p);
  return s;
}

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

  try {
    // Parse CLI
    std::string caseFile, outputDir, restartFile, reportLevel = "full";
    std::string subcommand;
    int maxStepsOverride = -1;
    double cflScale = 1.0;
    bool firstOrder = false;
    int nSweeps = 1;
    double cflCap = 0;
    int maxPhysSteps = -1;
    for (int i = 1; i < argc; i++) {
      std::string a = argv[i];
      if (a == "solve") subcommand = "solve";
      else if (a == "--case" && i+1 < argc) caseFile = argv[++i];
      else if (a == "--output" && i+1 < argc) outputDir = argv[++i];
      else if (a == "--restart" && i+1 < argc) restartFile = argv[++i];
      else if (a == "--report-level" && i+1 < argc) reportLevel = argv[++i];
      else if (a == "--max-steps" && i+1 < argc) maxStepsOverride = std::atoi(argv[++i]);
      else if (a == "--cfl-scale" && i+1 < argc) cflScale = std::atof(argv[++i]);
      else if (a == "--first-order") firstOrder = true;
      else if (a == "--sweeps" && i+1 < argc) nSweeps = std::atoi(argv[++i]);
      else if (a == "--cfl-cap" && i+1 < argc) cflCap = std::atof(argv[++i]);
      else if (a == "--max-phys-steps" && i+1 < argc) maxPhysSteps = std::atoi(argv[++i]);
    }
    if (subcommand != "solve" || caseFile.empty() || outputDir.empty()) {
      if (rank == 0)
        std::cerr << "Usage: mpirun -np <ranks> cfd2d solve --case <case-json> --output <output-dir> [--restart <file>] [--report-level brief|full]\n";
      MPI_Finalize();
      return 1;
    }

    // Read case config (all ranks)
    CaseConfig cfg = parseCaseConfig(caseFile);
    if (maxStepsOverride > 0) cfg.maxSteps = maxStepsOverride;
    if (cflScale != 1.0) { cfg.cflInitial *= cflScale; cfg.cflMax *= cflScale; }
    if (cflCap > 0) cfg.cflMax = std::min(cfg.cflMax, cflCap);
    if (maxPhysSteps > 0) cfg.finalTime = maxPhysSteps * cfg.timeStep;
    // Store first-order flag in a way the solver can access
    if (firstOrder) { cfg.cflInitial = 1.0; cfg.cflMax = 1.0; cfg.cflRampSteps = 0; }
    g_nSweeps = nSweeps;
    if (rank == 0) {
      std::cout << "Case: " << cfg.caseId << " mode=" << cfg.mode
                << " M=" << cfg.fs.mach << " Re=" << cfg.reynolds
                << " type=" << cfg.runType << " np=" << nprocs << std::endl;
      std::cout << "Mesh: " << cfg.meshFile << std::endl;
      std::cout << "Freestream: rho=" << cfg.fs.rho << " V=" << cfg.fs.vel
                << " p=" << cfg.fs.pressure << " mu=" << cfg.viscosity() << std::endl;
    }

    // Create output directory
    if (rank == 0) {
      std::string cmd = "mkdir -p " + outputDir;
      system(cmd.c_str());
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // Redirect stdout to log file (rank 0)
    std::string logPath = outputDir + "/stdout.log";
    std::ofstream logFile;
    if (rank == 0) {
      logFile.open(logPath);
      // Save original stdout
      std::cout << "Logging to " << logPath << std::endl;
    }

    auto t0 = std::chrono::steady_clock::now();

    // Read global mesh (preprocessing - acceptable on all ranks)
    GlobalMesh gm;
    gm.readCGNS(cfg.meshFile, cfg.bcMap);
    if (rank == 0) {
      std::cout << "Global mesh: " << gm.nCells << " cells, " << gm.nVert << " vertices, "
                << gm.faces.size() << " faces" << std::endl;
      double vmin=1e30, vmax=0, flmin=1e30, flmax=0;
      for (int c=0;c<gm.nCells;c++){vmin=std::min(vmin,gm.cvol[c]);vmax=std::max(vmax,gm.cvol[c]);}
      for (size_t f=0;f<gm.faces.size();f++){flmin=std::min(flmin,gm.flen[f]);flmax=std::max(flmax,gm.flen[f]);}
      printf("  Cell vol: min=%.6e max=%.4f | Face len: min=%.6e max=%.4f\n", vmin, vmax, flmin, flmax);
    }

    // Partition and build local mesh
    LocalMesh lm;
    partitionAndBuildLocal(gm, rank, nprocs, lm, MPI_COMM_WORLD);
    if (rank == 0)
      std::cout << "Partitioned: edgeCut=" << lm.edgeCut
                << " loadBalance=" << lm.loadBalance << std::endl;
    if (rank == 0)
      std::cout << "Rank 0: " << lm.nOwned << " owned, " << lm.nGhost << " ghost, "
                << lm.faces.size() << " faces, " << lm.neighborRanks.size() << " neighbors" << std::endl;

    // Write partition diagnostics
    writePartitionDiagnostics(outputDir + "/partition_diagnostics.csv", lm, rank, nprocs);

    // Initialize solver
    Solver solver(lm, cfg, rank, nprocs, MPI_COMM_WORLD);
    solver.initialize();

    // Run
    std::string command = "mpirun -np " + std::to_string(nprocs) + " cfd2d solve --case " + caseFile + " --output " + outputDir;

    if (cfg.runType == "transient") {
      if (rank == 0) std::cout << "Starting transient run: dt=" << cfg.timeStep
                               << " tf=" << cfg.finalTime << std::endl;
      solver.runTransient();
    } else {
      if (rank == 0) std::cout << "Starting steady run: maxSteps=" << cfg.maxSteps << std::endl;
      solver.runSteady();
    }

    auto t1 = std::chrono::steady_clock::now();
    double wallTime = std::chrono::duration<double>(t1-t0).count();

    // Exchange ghost for final state
    solver.mesh.exchangeGhost(solver.U.data(), MPI_COMM_WORLD, rank);
    solver.prepareForOutput(); // compute primitive + gradients for output

    // Write outputs
    writeResidualsCSV(outputDir + "/residuals.csv", solver);
    writeForcesCSV(outputDir + "/forces.csv", solver);
    solver.writeSurface(outputDir + "/surface.csv");
    writeFieldVTK(outputDir + "/field_final.vtk", gm, solver, lm, rank, nprocs);
    writeRestart(outputDir + "/restart_final.dat", solver, lm, rank, nprocs);
    writeMetadata(outputDir + "/metadata.json", solver, cfg, lm, rank, nprocs, getGitRev());
    writeRunStatus(outputDir + "/run_status.json", solver, cfg, nprocs, command);

    // Also write stdout.log with summary
    if (rank == 0) {
      logFile << "=== Run Summary ===\n";
      logFile << "Case: " << cfg.caseId << "\n";
      logFile << "MPI ranks: " << nprocs << "\n";
      logFile << "Wall time: " << wallTime << " s\n";
      logFile << "Final step: " << solver.stats.finalStep << "\n";
      logFile << "Final physical time: " << solver.stats.finalPhysicalTime << "\n";
      logFile << "Convergence: " << solver.stats.convergenceStatus << "\n";
      logFile << "Residual reduction: " << solver.stats.residualReduction << " orders\n";
      if (!solver.history.empty()) {
        auto& last = solver.history.back();
        logFile << "Final cl=" << last.cl << " cd=" << last.cd << " cmz=" << last.cmz << "\n";
      }
      if (cfg.runType == "transient") {
        logFile << "Inner iters: min=" << solver.stats.obsMinInner
                << " max=" << solver.stats.obsMaxInner
                << " mean=" << solver.stats.meanInner << "\n";
        logFile << "Inner target misses: " << solver.stats.innerTargetMisses << "\n";
        logFile << "Inner converged fraction: " << solver.stats.innerConvergedFraction << "\n";
      }
      // Also capture the console output we printed
      logFile.close();
    }

    if (rank == 0)
      std::cout << "Done. Status: " << solver.stats.convergenceStatus
                << " Wall: " << wallTime << "s" << std::endl;

  } catch (const std::exception& e) {
    if (rank == 0) std::cerr << "ERROR: " << e.what() << std::endl;
    MPI_Finalize();
    return 1;
  }

  MPI_Finalize();
  return 0;
}
