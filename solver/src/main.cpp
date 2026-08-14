#include "types.hpp"
#include "io/case_reader.hpp"
#include "mesh/cgns_reader.hpp"
#include "mesh/face_geometry.hpp"
#include "solver/fvm_solver.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <mpi.h>

using namespace omo;

void print_usage() {
    std::cout << "Usage: mpirun -np N omo_cfd_solver solve --case <case.json> --output <output_dir>\n";
    std::cout << "  --case PATH     : Path to case JSON file\n";
    std::cout << "  --output DIR    : Output directory\n";
    std::cout << "  --restart FILE  : Restart file (optional)\n";
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    // Parse arguments
    std::string case_path, output_dir;
    int max_steps_override = 0;

    if (argc < 2) {
        if (rank == 0) print_usage();
        MPI_Finalize();
        return 0;
    }

    std::string cmd = argv[1];
    if (cmd == "solve") {
        run_solver = true;
    } else {
        if (rank == 0) {
            std::cerr << "Unknown command: " << cmd << "\n";
            print_usage();
        }
        MPI_Finalize();
        return 1;
    }

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--case" && i + 1 < argc) {
            case_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        }
    }

    if (case_path.empty() || output_dir.empty()) {
        if (rank == 0) {
            std::cerr << "ERROR: --case and --output are required\n";
            print_usage();
        }
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        std::cout << "========================================\n";
        std::cout << "  OMO CFD Solver v0.1.0\n";
        std::cout << "  MPI ranks: " << nranks << "\n";
        std::cout << "  Case: " << case_path << "\n";
        std::cout << "  Output: " << output_dir << "\n";
        std::cout << "========================================\n";
    }

    try {
        // Read case configuration
        CaseConfig cfg = CaseReader::read(case_path);

        // Resolve mesh path relative to case file
        std::string case_dir = case_path;
        size_t last_slash = case_dir.find_last_of('/');
        if (last_slash != std::string::npos) {
            case_dir = case_dir.substr(0, last_slash + 1);
        } else {
            case_dir = "./";
        }
        std::string mesh_path = case_dir + cfg.mesh_file;

        if (rank == 0) {
            std::cout << "[Main] Reading mesh: " << mesh_path << std::endl;
            MeshData mesh = CGNSReader::read(mesh_path);

            // Compute face geometry
            FaceGeometry::compute(mesh);

            // Run solver
            FVMSolver solver(cfg, mesh);
            solver.solve(output_dir);
        }

    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "ERROR: " << e.what() << "\n";
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Finalize();
    return 0;
}
