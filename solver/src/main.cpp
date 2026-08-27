#include <mpi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <nlohmann/json.hpp>

#include "Config.hpp"
#include "MeshData.hpp"
#include "Physics.hpp"
#include "CgnsReader.hpp"
#include "Partitioner.hpp"
#include "MpiHalo.hpp"
#include "SteadySolver.hpp"
#include "TransientSolver.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " solve --case <case.json> --output <dir> [options]\n";
    std::cerr << "  --report-level brief|full\n";
    std::cerr << "  --restart <restart-file>\n";
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, n_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);

    // Parse command line
    std::string case_file, output_dir, restart_file, report_level = "brief";
    bool has_solve = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "solve") { has_solve = true; }
        else if (arg == "--case" && i+1 < argc) case_file = argv[++i];
        else if (arg == "--output" && i+1 < argc) output_dir = argv[++i];
        else if (arg == "--restart" && i+1 < argc) restart_file = argv[++i];
        else if (arg == "--report-level" && i+1 < argc) report_level = argv[++i];
    }

    if (!has_solve || case_file.empty() || output_dir.empty()) {
        if (rank == 0) {
            std::cerr << "Error: missing 'solve', --case, or --output arguments\n";
            printUsage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    // Read case config
    if (rank == 0) std::cout << "[CFD Solver] Reading case: " << case_file << "\n";

    std::ifstream cf(case_file);
    if (!cf.is_open()) {
        if (rank == 0) std::cerr << "Error: cannot open case file: " << case_file << "\n";
        MPI_Finalize();
        return 1;
    }
    json case_json;
    cf >> case_json;
    cf.close();

    std::string case_dir = fs::path(case_file).parent_path().string();
    if (case_dir.empty()) case_dir = ".";

    CaseConfig cfg;
    try {
        cfg = CaseConfig::fromJson(case_json, case_dir);
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Error parsing case JSON: " << e.what() << "\n";
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        std::cout << "[CFD Solver] Case: " << cfg.case_id << "\n";
        std::cout << "[CFD Solver] Mesh: " << cfg.mesh_file << "\n";
        std::cout << "[CFD Solver] Mode: " << (cfg.mode==PhysicsMode::Laminar ? "laminar" : "inviscid") << "\n";
        std::cout << "[CFD Solver] MPI ranks: " << n_ranks << "\n";
        std::cout.flush();
    }

    // Read mesh and build global mesh (rank 0 only)
    GlobalMesh gm;
    try {
        if (rank == 0) {
            std::cout << "[CFD Solver] Reading CGNS mesh...\n";
            std::cout.flush();
        }
        // All ranks read the mesh (simpler than broadcast, and it's small)
        gm = readCgnsMesh(cfg.mesh_file, cfg.boundary_conditions);
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Error reading mesh: " << e.what() << "\n";
        MPI_Finalize();
        return 1;
    }

    // Build local mesh with partitioning
    LocalMesh lm;
    try {
        if (rank == 0) std::cout << "[CFD Solver] Partitioning mesh...\n";
        MPI_Barrier(MPI_COMM_WORLD);
        lm = buildLocalMesh(gm, MPI_COMM_WORLD);
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Error partitioning mesh: " << e.what() << "\n";
        MPI_Finalize();
        return 1;
    }

    // Initialize conservative states
    int n_total = lm.n_owned + lm.n_ghost;
    std::vector<StateVec> states(n_total, freestream_state(cfg));

    // Load restart if provided
    if (!restart_file.empty()) {
        // Try to load rank-specific restart
        std::string rfile = restart_file + "_rank" + std::to_string(rank) + ".bin";
        std::ifstream rf(rfile, std::ios::binary);
        if (rf.is_open()) {
            int n;
            rf.read(reinterpret_cast<char*>(&n), sizeof(int));
            if (n == lm.n_owned) {
                for (int i = 0; i < lm.n_owned; i++)
                    rf.read(reinterpret_cast<char*>(states[i].data()), 4*sizeof(double));
                if (rank == 0) std::cout << "[CFD Solver] Loaded restart file\n";
            }
        }
    }

    // Run solver
    if (rank == 0) {
        fs::create_directories(output_dir);
        std::cout << "[CFD Solver] Starting solve...\n";
        std::cout.flush();
    }
    MPI_Barrier(MPI_COMM_WORLD);

    auto t_start = std::chrono::steady_clock::now();
    int exit_code = 0;

    try {
        if (cfg.run_control.type == RunType::Steady) {
            SteadyResult result = runSteady(lm, states, cfg, output_dir, MPI_COMM_WORLD);
            if (rank == 0) {
                std::cout << "[CFD Solver] Done. Steps=" << result.final_step
                          << " converged=" << result.converged
                          << " residual_reduction=" << result.residual_reduction << "\n";
            }
        } else {
            TransientResult result = runTransient(lm, states, cfg, output_dir, MPI_COMM_WORLD);
            if (rank == 0) {
                std::cout << "[CFD Solver] Done. Steps=" << result.n_steps
                          << " final_time=" << result.final_time
                          << " mean_inner=" << result.mean_inner_iters << "\n";
            }
        }
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Solver error: " << e.what() << "\n";
        exit_code = 2;
    }

    auto t_end = std::chrono::steady_clock::now();
    double wall_time = std::chrono::duration<double>(t_end - t_start).count();
    if (rank == 0)
        std::cout << "[CFD Solver] Wall time: " << wall_time << "s\n";

    MPI_Finalize();
    return exit_code;
}
