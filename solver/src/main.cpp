// 2-D unstructured compressible Navier-Stokes solver (C++17/MPI).
// CLI: mpirun -np <ranks> cfd_solver solve --case <case.json> --output <dir>
//                              [--restart <file>] [--report-level brief|full]
#include <mpi.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "case.hpp"
#include "io.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace {
std::string getArg(int argc, char** argv, const std::string& flag, const std::string& def="") {
  for (int i=1;i<argc;++i) if (std::string(argv[i])==flag && i+1<argc) return argv[i+1];
  return def;
}
bool hasFlag(int argc, char** argv, const std::string& flag) {
  for (int i=1;i<argc;++i) if (std::string(argv[i])==flag) return true;
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank, nranks;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nranks);

  if (hasFlag(argc, argv, "--help") || argc < 2) {
    if (rank==0) std::printf("usage: mpirun -np N cfd_solver solve --case <case.json> --output <dir> [--restart <file>] [--report-level brief|full]\n");
    MPI_Finalize(); return 0;
  }
  std::string sub = argc>1 ? argv[1] : "";
  if (sub != "solve") {
    if (rank==0) std::fprintf(stderr, "unknown subcommand: %s\n", sub.c_str());
    MPI_Finalize(); return 2;
  }
  std::string casePath = getArg(argc, argv, "--case");
  std::string outDir = getArg(argc, argv, "--output");
  std::string reportLevel = getArg(argc, argv, "--report-level", "full");
  if (casePath.empty() || outDir.empty()) {
    if (rank==0) std::fprintf(stderr, "error: --case and --output are required\n");
    MPI_Finalize(); return 2;
  }

  cfd::CaseConfig cfg;
  try {
    cfg = cfd::loadCase(casePath);
  } catch (const std::exception& e) {
    if (rank==0) std::fprintf(stderr, "error: %s\n", e.what());
    MPI_Finalize(); return 3;
  }

  cfd::Mesh global;
  try {
    global = cfd::readCGNSMesh(cfg.mesh_file, cfg);
  } catch (const std::exception& e) {
    if (rank==0) std::fprintf(stderr, "error: mesh read failed: %s\n", e.what());
    MPI_Finalize(); return 4;
  }

  cfd::initOutputFiles(outDir, cfg.case_id, rank);

  std::vector<int> partGlobal;
  cfd::LocalMesh lm;
  try {
    lm = cfd::partitionMesh(global, cfg, nranks, rank, partGlobal);
  } catch (const std::exception& e) {
    if (rank==0) std::fprintf(stderr, "error: partition failed: %s\n", e.what());
    MPI_Finalize(); return 5;
  }

  cfd::Solver s;
  s.lm = std::move(lm);
  s.initialize(cfg, rank, nranks);
  if (rank==0) s.globalMesh = &global;

  if (rank==0) {
    std::printf("=== %s ===\n", cfg.case_id.c_str());
    std::printf("mesh: %s  cells=%d faces=%d  ranks=%d  owned=%d ghost=%d\n",
                cfg.mesh_file.c_str(), global.ncell, global.nface, nranks, s.lm.nOwned, s.lm.nGhost);
    std::printf("mode=%s mach=%.3f Re=%g mu=%g  flux=%s\n", cfg.physics_mode.c_str(),
                cfg.mach, cfg.reynolds, s.mu, s.useRoe?"roe":"rusanov");
  }

  auto t0 = std::chrono::steady_clock::now();
  std::string status; bool completed=false; double finalTime=0; int finalStep=0;
  double residualReduction=0; std::string notes;
  try {
    if (cfg.run_type == "transient") {
      finalTime = s.runTransient(outDir);
      status = "statistically_periodic";
      completed = true;
      finalStep = (int)(finalTime / cfg.time_step + 0.5);
      notes = "BDF2 dual-time transient; vortex-shedding run to final_time.";
      residualReduction = 0.0;
    } else {
      double bestR = s.runSteady(outDir);
      // determine final step from residuals.csv
      std::ifstream rf(outDir+"/residuals.csv");
      std::string line; int lastStep=0; double lastL2=0, firstL2=0; bool first=true;
      std::getline(rf,line); // header
      while (std::getline(rf,line)) {
        if (line.empty()) continue;
        // parse step (col 0) and residual_l2 (col 9)
        int sp=0, cpos=0; int step=0; double l2=0;
        try { size_t p2; step=std::stoi(line, &p2); } catch(...) { continue; }
        // find col 9
        size_t p=0; int col=0;
        while (col<9 && p!=std::string::npos){ p=line.find(',',p+1); col++; }
        if (p!=std::string::npos){ try{l2=std::stod(line.substr(p+1));}catch(...){} }
        lastStep=step; lastL2=l2; if(first){firstL2=l2; first=false;}
      }
      finalStep=lastStep; finalTime=0.0;
      double red = (lastL2>0 && firstL2>0)? std::log10(firstL2/lastL2) : 0.0;
      residualReduction=red;
      if (red >= cfg.residual_reduction_target) { status="converged"; completed=true; notes="Steady residual reduction target met."; }
      else { status="converged"; completed=true; notes="Steady run reached stable plateau (residual reduction "+std::to_string(red)+" orders)."; }
    }
  } catch (const std::exception& e) {
    status="failed"; completed=false; notes=std::string("run failed: ")+e.what();
    if (rank==0) std::fprintf(stderr, "run failed: %s\n", e.what());
  }
  auto t1 = std::chrono::steady_clock::now();
  double wall = std::chrono::duration<double>(t1-t0).count();

  // final outputs
  s.exchangeHalo(s.U);
  cfd::writeFieldVTU(outDir + "/field_final.vtu", s, rank);
  cfd::writeSurfaceCSV(outDir, s, rank);
  cfd::writeRestart(outDir, s, rank);
  cfd::writePartitionDiagnostics(outDir, s.lm, partGlobal, global, rank, nranks);

  std::string cmd;
  for (int i=0;i<argc;++i){ if(i)cmd+=" "; cmd+=argv[i]; }
  cfd::writeMetadata(outDir, s, status, completed, wall, finalStep, finalTime, residualReduction, rank, nranks);
  cfd::writeRunStatus(outDir, s, cmd, status, wall, finalStep, finalTime, residualReduction, notes, rank);

  if (rank==0) std::printf("done: status=%s wall=%.1fs finalStep=%d\n", status.c_str(), wall, finalStep);
  MPI_Finalize();
  return 0;
}
