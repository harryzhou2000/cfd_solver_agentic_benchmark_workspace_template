#include "case.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"

#include <mpi.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void printUsage(const char* prog) {
  std::fprintf(stderr,
               "usage:\n"
               "  %s solve --case <case.json> --output <outdir> "
               "[--restart <file>] [--report-level brief|full]\n"
               "  %s dump-mesh --case <case.json>\n",
               prog, prog);
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0, size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  int rc = 0;
  try {
    if (argc < 2) throw std::runtime_error("missing subcommand");
    const std::string sub = argv[1];
    if (sub == "dump-mesh") {
      std::string casePath;
      for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) casePath = argv[++i];
      if (casePath.empty()) throw std::runtime_error("dump-mesh requires --case <json>");
      cfd::Case c = cfd::loadCase(casePath);
      cfd::GlobalMesh mesh = cfd::loadCgnsMesh(c.mesh_file);
      cfd::assignBoundaryConditions(mesh, c);
      if (rank == 0) {
        std::printf("case: %s\n", c.case_id.c_str());
        std::printf("mesh: %s\n", c.mesh_file.c_str());
        std::printf("zones: %zu\n", mesh.zoneNames.size());
        for (const auto& z : mesh.zoneNames) std::printf("  %s\n", z.c_str());
        std::printf("points: %zu\n", mesh.points.size());
        std::printf("cells: %ld\n", mesh.numCells());
        std::printf("faces: %ld (interior %ld, boundary %ld)\n", mesh.numFaces(),
                    mesh.numFaces() - mesh.numBoundaryFaces(), mesh.numBoundaryFaces());
        std::printf("1to1 connections: %ld\n", mesh.n1to1);
        int counts[3] = {0, 0, 0};
        for (int bc : mesh.faceBcType)
          if (bc >= 0) counts[bc]++;
        std::printf("BC counts: farfield=%d slip=%d noslip=%d\n", counts[0], counts[1],
                    counts[2]);
      }
    } else if (sub == "solve") {
      std::string casePath, outDir, restartPath, reportLevel = "brief";
      std::string commandLine;
      for (int i = 0; i < argc; ++i) {
        if (i) commandLine += " ";
        commandLine += argv[i];
      }
      for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) casePath = argv[++i];
        else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) outDir = argv[++i];
        else if (std::strcmp(argv[i], "--restart") == 0 && i + 1 < argc) restartPath = argv[++i];
        else if (std::strcmp(argv[i], "--report-level") == 0 && i + 1 < argc)
          reportLevel = argv[++i];
      }
      if (casePath.empty() || outDir.empty())
        throw std::runtime_error("solve requires --case and --output");

      // capture solver stdout/stderr into the output directory
      if (rank == 0) {
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        std::string log = outDir + "/stdout.log";
        if (freopen(log.c_str(), "w", stdout) == nullptr)
          throw std::runtime_error("cannot open stdout.log for writing: " + log);
        if (freopen(log.c_str(), "a", stderr) == nullptr)
          throw std::runtime_error("cannot open stdout.log for writing: " + log);
      }

      cfd::Case c = cfd::loadCase(casePath);
      // Preprocessing: every rank loads the full mesh, partitions with METIS
      // (deterministic), then keeps only its rank-local subset.
      cfd::GlobalMesh mesh = cfd::loadCgnsMesh(c.mesh_file);
      cfd::assignBoundaryConditions(mesh, c);
      cfd::PartitionResult part = cfd::partitionMesh(mesh, size, rank);
      cfd::LocalMesh lm = cfd::buildLocalMesh(mesh, part, rank, size);
      mesh = cfd::GlobalMesh{};  // drop the global mesh; solver uses rank-local data

      cfd::SolveResult res = cfd::runSolver(c, lm, rank, size, outDir, restartPath,
                                            commandLine, reportLevel);
      rc = res.exitCode;
      if (rank == 0)
        std::printf("done: %s status=%s orders=%.2f\n", c.case_id.c_str(),
                    res.stats.convergenceStatus.c_str(), res.stats.residualReductionOrders);
    } else {
      throw std::runtime_error("unknown subcommand: " + sub);
    }
  } catch (const std::exception& e) {
    if (rank == 0) {
      std::fprintf(stderr, "ERROR: %s\n", e.what());
      printUsage(argv[0]);
    }
    rc = 1;
  }

  MPI_Finalize();
  return rc;
}
