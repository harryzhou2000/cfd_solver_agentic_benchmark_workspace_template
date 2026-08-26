#include "solver.h"
#include <argparse/argparse.hpp>
#include <fmt/format.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    argparse::ArgumentParser program("cfd2d");
    argparse::ArgumentParser solve_cmd("solve");
    solve_cmd.add_description("Run the CFD solver");
    solve_cmd.add_argument("--case").required().help("Path to case JSON file");
    solve_cmd.add_argument("--output").required().help("Output directory");
    solve_cmd.add_argument("--restart").default_value(std::string("")).help("Restart file");
    solve_cmd.add_argument("--report-level").default_value(std::string("brief")).help("Report level");
    program.add_subparser(solve_cmd);

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        if (rank == 0) {
            std::cerr << err.what() << std::endl;
            std::cerr << program;
        }
        MPI_Finalize();
        return 1;
    }

    if (!program.is_subcommand_used("solve")) {
        if (rank == 0) {
            std::cerr << "Error: 'solve' subcommand required\n";
            std::cerr << program;
        }
        MPI_Finalize();
        return 1;
    }

    std::string case_file = solve_cmd.get<std::string>("--case");
    std::string output_dir = solve_cmd.get<std::string>("--output");

    CFDSolver solver;
    solver.mpi_rank = rank;
    solver.mpi_size = size;
    solver.start_time = std::chrono::steady_clock::now();

    try {
        if (rank == 0) fmt::print("Loading case: {}\n", case_file);
        solver.load_case(case_file);

        if (rank == 0) fmt::print("Reading mesh: {}\n", solver.config.mesh_file);
        solver.build_mesh_from_cgns(solver.config.mesh_file);

        if (rank == 0) {
            fmt::print("Mesh: {} nodes, {} cells, {} faces\n",
                      solver.global_mesh.num_nodes_global,
                      solver.global_mesh.num_cells_global,
                      solver.global_mesh.num_faces_global);
        }

        // Compute geometry on global mesh first
        solver.compute_geometry();

        if (rank == 0) fmt::print("Partitioning mesh into {} parts...\n", size);
        solver.partition_mesh();

        if (rank == 0) fmt::print("Building local mesh...\n");
        solver.build_local_mesh();

        // Recompute geometry on local mesh
        if (size > 1) {
            solver.compute_geometry();
        }

        if (rank == 0) {
            fmt::print("Rank 0: {} owned cells, {} ghost cells\n",
                      solver.num_owned, solver.num_ghost);
        }

        solver.initialize_solution();

        if (solver.config.run_control.type == "transient") {
            if (rank == 0) fmt::print("Starting transient solve (BDF2)...\n");
            solver.solve_transient();
        } else {
            if (rank == 0) fmt::print("Starting steady solve...\n");
            solver.solve_steady();
        }

        if (rank == 0) fmt::print("Writing outputs to {}...\n", output_dir);
        solver.write_outputs(output_dir);

        if (rank == 0) {
            fmt::print("Completed: status={}, wall_time={:.1f}s\n",
                      solver.convergence_status, solver.wall_time_seconds);
        }

    } catch (const std::exception& e) {
        fmt::print(stderr, "Error on rank {}: {}\n", rank, e.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Finalize();
    return 0;
}
