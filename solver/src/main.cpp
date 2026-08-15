#include "solver.hpp"
#include "io.hpp"
#include <mpi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace cfd;

static void ensureDir(const std::string& path) {
    mkdir(path.c_str(), 0777);
}

static std::string resolveMeshPath(const std::string& caseFile, const std::string& meshRel) {
    // mesh path is relative to the case-file directory
    std::string dir = caseFile.substr(0, caseFile.find_last_of('/'));
    if (dir == caseFile) dir = ".";
    // try relative
    std::string p = dir + "/" + meshRel;
    std::ifstream test(p);
    if (test.good()) return p;
    // try absolute
    test.clear(); test.open(meshRel);
    if (test.good()) return meshRel;
    return p; // fallback (will error later)
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank; MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // parse args: solve --case <json> --output <dir> [--restart <file>]
    std::string caseFile, outDir, restartFile;
    bool haveRestart = false;
    std::string reportLevel = "full";
    std::vector<std::string> args(argv+1, argv+argc);
    std::string subcommand;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (i==0 && a != "--" && a[0] != '-') subcommand = a;
        else if (a == "--case" && i+1 < args.size()) caseFile = args[++i];
        else if (a == "--output" && i+1 < args.size()) outDir = args[++i];
        else if (a == "--restart" && i+1 < args.size()) { restartFile = args[++i]; haveRestart = true; }
        else if (a == "--report-level" && i+1 < args.size()) reportLevel = args[++i];
    }

    if (caseFile.empty() || outDir.empty()) {
        if (rank==0)
            std::cerr << "Usage: mpirun -np <ranks> cfdns2d solve --case <case-json> --output <dir> "
                         "[--restart <restart-file>] [--report-level brief|full]\n";
        MPI_Finalize();
        return 1;
    }

    try {
        CaseInput in = parseCase(caseFile, ".");
        // resolve mesh path
        in.meshFile = resolveMeshPath(caseFile, in.meshFile);
        std::ifstream mtest(in.meshFile);
        if (!mtest.good()) throw std::runtime_error("mesh file not found: " + in.meshFile);

        ensureDir(outDir);
        Solver solver;
        solver.setup(in, MPI_COMM_WORLD);

        if (haveRestart) {
            if (!readRestart(restartFile, solver.lm, solver.U) && rank==0)
                std::cerr << "[warn] restart read failed, starting from freestream\n";
        }

        if (in.transient) solver.runTransient(outDir);
        else solver.runSteady(outDir);

        if (rank==0) std::cerr << "[done] case=" << in.caseId << " output=" << outDir << "\n";
    } catch (const std::exception& e) {
        if (rank==0) std::cerr << "[ERROR] " << e.what() << "\n";
        MPI_Finalize();
        return 2;
    }

    MPI_Finalize();
    return 0;
}
