#include <iostream>
#include <mpi.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include "common/types.hpp"
#include "common/gas_model.hpp"
#include "io/json_reader.hpp"
#include "mesh/cgns_reader.hpp"
#include "mesh/geometry.hpp"
#include "partition/metis_partitioner.hpp"
#include "partition/mpi_distributor.hpp"
#include "partition/halo_exchange.hpp"
#include "solver/time_stepper.hpp"

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " solve --case <case.json> --output <dir> [--restart <file>] [--report-level brief|full]\n";
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Parse CLI arguments
    std::string case_file, output_dir, restart_file, report_level = "brief";
    std::string command;

    if (argc < 2) { print_usage(argv[0]); MPI_Finalize(); return 1; }
    command = argv[1];
    if (command != "solve") { print_usage(argv[0]); MPI_Finalize(); return 1; }

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--case" && i+1 < argc) case_file = argv[++i];
        else if (arg == "--output" && i+1 < argc) output_dir = argv[++i];
        else if (arg == "--restart" && i+1 < argc) restart_file = argv[++i];
        else if (arg == "--report-level" && i+1 < argc) report_level = argv[++i];
        else { std::cerr << "Unknown option: " << arg << "\n"; MPI_Finalize(); return 1; }
    }

    if (case_file.empty() || output_dir.empty()) {
        if (rank == 0) { print_usage(argv[0]); }
        MPI_Finalize(); return 1;
    }

    if (rank == 0) {
        std::cout << "=== CFD Solver ===\n"
                  << "Case: " << case_file << "\n"
                  << "Output: " << output_dir << "\n"
                  << "MPI ranks: " << size << "\n";
    }

    // 1. Parse case file
    CaseConfig cfg;
    try {
        cfg = parse_case_json(case_file);
        if (rank == 0) std::cout << "Case loaded: " << cfg.case_id << "\n";
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Error parsing case file: " << e.what() << "\n";
        MPI_Finalize(); return 1;
    }

    // 2. Read mesh (rank 0 only)
    Mesh global_mesh;
    if (rank == 0) {
        try {
            std::string mesh_path = cfg.mesh_file;
            if (mesh_path[0] != '/') {
                size_t pos = case_file.rfind('/');
                if (pos != std::string::npos) {
                    mesh_path = case_file.substr(0, pos + 1) + mesh_path;
                }
            }
            global_mesh = read_cgns_mesh(mesh_path);
            std::cout << "Mesh loaded: " << global_mesh.num_cells << " cells\n";
        } catch (const std::exception& e) {
            std::cerr << "Error reading mesh: " << e.what() << "\n";
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    // 3. Compute geometry
    compute_geometry(global_mesh);
    classify_boundary_faces(global_mesh, cfg);

    // 4. METIS partitioning
    std::vector<int> cell_rank(global_mesh.num_cells, 0);
    if (rank == 0) {
        cell_rank = partition_mesh_metis(global_mesh, size);
    }

    // 5. Distribute mesh
    RankMesh rank_mesh;
    distribute_mesh(global_mesh, cell_rank, size, rank_mesh, MPI_COMM_WORLD);
    build_neighbor_lists(rank_mesh, global_mesh, cell_rank, MPI_COMM_WORLD);

    // 6. Initialize solution
    GasModel gas{cfg.gamma, cfg.R_gas, cfg.prandtl};
    StateVector U_inf;
    {
        Real rho = cfg.rho_inf;
        Real u   = cfg.u_inf;
        Real v   = cfg.v_inf;
        Real p   = cfg.p_inf;
        Real e   = p / ((cfg.gamma - 1.0) * rho);
        Real E   = e + 0.5 * (u*u + v*v);
        U_inf << rho, rho*u, rho*v, rho*E;
    }

    for (auto& U : rank_mesh.owned_state) U = U_inf;
    for (auto& U : rank_mesh.ghost_state) U = U_inf;

    if (rank == 0) {
        std::cout << "Initialization complete.\n"
                  << "Owned cells: " << rank_mesh.owned_cells.size() << "\n"
                  << "Ghost cells: " << rank_mesh.ghost_cells.size() << "\n"
                  << "Neighbors: " << rank_mesh.neighbors.size() << "\n";
    }

    // 7. Create output directory
    if (rank == 0) {
        mkdir(output_dir.c_str(), 0755);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // 8. Run solver
    try {
        if (cfg.run_type == RunType::Steady) {
            steady_march(rank_mesh, cfg, gas);
        } else {
            // transient_march(rank_mesh, cfg, gas); // TODO Phase 4
            if (rank == 0) std::cerr << "Transient solver not yet implemented\n";
        }
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "Solver error: " << e.what() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Finalize();
    return 0;
}
