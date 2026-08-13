#include "solver.hpp"
#include "io.hpp"
#include "implicit.hpp"
#include "transient.hpp"
#include "output.hpp"
#include <argparse/argparse.hpp>
#include <mpi.h>
#include <iostream>
#include <chrono>
#include <sys/stat.h>

using namespace cfd;

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    argparse::ArgumentParser program("cfd_solver");
    program.add_argument("solve");
    program.add_argument("--case").required().help("Path to case JSON file");
    program.add_argument("--output").required().help("Output directory");
    
    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        if (rank == 0) {
            std::cerr << err.what() << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    
    std::string case_path = program.get<std::string>("--case");
    std::string output_dir = program.get<std::string>("--output");
    
    // Create output directory
    if (rank == 0) {
        mkdir(output_dir.c_str(), 0755);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    
    // Parse case
    CaseInput ci;
    try {
        ci = parse_case(case_path);
    } catch (const std::exception& err) {
        if (rank == 0) {
            std::cerr << "Failed to parse case: " << err.what() << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    
    if (rank == 0) {
        std::cout << "Case: " << ci.case_id << std::endl;
        std::cout << "Mesh: " << ci.mesh_file << std::endl;
        std::cout << "Mode: " << ci.physics_mode << " Mach: " << ci.mach << std::endl;
        std::cout << "Run type: " << ci.run_type << std::endl;
        std::cout << "MPI ranks: " << size << std::endl;
    }
    
    // Initialize solver
    Solver solver(ci, rank, size);
    
    try {
        solver.init(ci.mesh_file);
    } catch (const std::exception& err) {
        if (rank == 0) {
            std::cerr << "Initialization failed: " << err.what() << std::endl;
        }
        MPI_Finalize();
        return 1;
    }
    
    auto t_start = std::chrono::high_resolution_clock::now();
    
    if (ci.run_type == "transient") {
        auto stats = run_transient(solver, output_dir);
        
        auto t_end = std::chrono::high_resolution_clock::now();
        real_t wall_time = std::chrono::duration<real_t>(t_end - t_start).count();
        
        // Write outputs
        write_transient_metadata(solver, output_dir, stats, wall_time);
        write_run_status(solver, output_dir, stats.physical_steps_run,
                        ci.final_time, "statistically_periodic",
                        std::log10(1.0 / std::max(stats.last_inner_residual_ratio, 1e-15)),
                        wall_time, "Transient BDF2 run to t=" + std::to_string(ci.final_time));
        write_surface(solver, output_dir);
        write_field_vtu(solver, output_dir);
        write_partition_diagnostics(solver, output_dir);
        write_restart(solver, output_dir);
        
    } else {
        auto stats = run_steady(solver, output_dir);
        
        auto t_end = std::chrono::high_resolution_clock::now();
        real_t wall_time = std::chrono::duration<real_t>(t_end - t_start).count();
        
        write_metadata(solver, output_dir, stats.steps_run,
                      stats.final_res_l2, stats.convergence_status == "converged");
        write_run_status(solver, output_dir, stats.steps_run, 0.0,
                        stats.convergence_status, stats.residual_reduction,
                        wall_time, stats.notes);
        write_surface(solver, output_dir);
        write_field_vtu(solver, output_dir);
        write_partition_diagnostics(solver, output_dir);
        write_restart(solver, output_dir);
    }
    
    if (rank == 0) {
        std::cout << "Run complete." << std::endl;
    }
    
    MPI_Finalize();
    return 0;
}
