// main.cpp - CLI entry point for the CFD2D solver.
//
// Usage: mpirun -np <ranks> cfd2d solve --case <case-json> --output <output-dir>
//        [--restart <restart-file>] [--report-level brief|full]
#include "solver.hpp"
#include <mpi.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace cfd2d;

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    
    int rank, nProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcs);
    
    // Simple CLI parser
    std::string caseJson, outputDir, restartFile;
    int reportLevel = 1;
    std::string mode = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "solve") {
            mode = "solve";
        } else if (arg == "--case" && i+1 < argc) {
            caseJson = argv[++i];
        } else if (arg == "--output" && i+1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--restart" && i+1 < argc) {
            restartFile = argv[++i];
        } else if (arg == "--report-level" && i+1 < argc) {
            std::string rl = argv[++i];
            reportLevel = (rl == "full") ? 2 : 1;
        } else if (arg == "--help" || arg == "-h") {
            if (rank == 0) {
                std::cout << "Usage: mpirun -np <ranks> cfd2d solve --case <case-json> "
                             "--output <output-dir> [--restart <restart-file>] [--report-level brief|full]\n";
            }
            MPI_Finalize();
            return 0;
        }
    }
    
    if (mode != "solve" || caseJson.empty() || outputDir.empty()) {
        if (rank == 0) {
            std::cerr << "Error: must specify 'solve --case <file> --output <dir>'\n";
        }
        MPI_Finalize();
        return 1;
    }
    
    Solver solver(MPI_COMM_WORLD);
    int ret = solver.run(caseJson, outputDir, restartFile, reportLevel);
    
    MPI_Finalize();
    return ret;
}
