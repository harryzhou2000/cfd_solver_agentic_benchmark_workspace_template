#include "solver/case_config.hpp"
#include "solver/mesh_reader.hpp"
#include "solver/geometry.hpp"
#include "solver/adjacency.hpp"
#include "solver/partition.hpp"
#include "solver/partition_types.hpp"
#include <mpi.h>
#include <fmt/core.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <sys/stat.h>

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    int rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    if (argc < 2) {
        if (rank == 0) fmt::print("Usage: {} solve --case <json> --output <dir>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd != "solve") {
        if (rank == 0) fmt::print("Unknown command: {}\n", cmd);
        MPI_Finalize();
        return 1;
    }

    std::string case_file, output_dir, restart_file, report_level;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--case" && i+1 < argc) case_file = argv[++i];
        else if (a == "--output" && i+1 < argc) output_dir = argv[++i];
        else if (a == "--restart" && i+1 < argc) { restart_file = argv[++i]; }
        else if (a == "--report-level" && i+1 < argc) { report_level = argv[++i]; }
        else {
            if (rank == 0) fmt::print(stderr, "Unknown option: {}\n", a);
            MPI_Finalize();
            return 1;
        }
    }
    if (!restart_file.empty() && rank == 0) {
        fmt::print(stderr,
                   "Warning: --restart '{}' is accepted but not yet "
                   "implemented; ignoring\n",
                   restart_file);
    }
    if (!report_level.empty() && rank == 0) {
        fmt::print(stderr,
                   "Warning: --report-level '{}' is accepted but not yet "
                   "implemented; ignoring\n",
                   report_level);
    }
    if (case_file.empty() || output_dir.empty()) {
        if (rank == 0) fmt::print("Usage: {} solve --case <json> --output <dir>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    // Create output directory
    if (rank == 0) mkdir(output_dir.c_str(), 0755);
    MPI_Barrier(MPI_COMM_WORLD);

    // Rank-0 preprocessing: case parsing, mesh reading and partitioning.
    // If any step throws, broadcast an error flag so the other ranks exit
    // instead of hanging at the MPI_Bcast/MPI_Recv calls below.
    bool ok = true;
    std::string err_msg;
    solver::CaseConfig config;
    solver::Mesh global_mesh;
    int num_global_cells = 0;
    std::vector<int> partition;
    if (rank == 0) {
        try {
            config = solver::parse_case_config(case_file);
            fmt::print("=== Case: {} ({})\n", config.case_id, config.description);
            fmt::print("=== MPI ranks: {}\n", nranks);
            fmt::print("=== Mode: {}, Mach: {}\n", config.physics.mode,
                       config.freestream.mach);

            // Read and process mesh on rank 0
            global_mesh = solver::read_cgns_mesh(config.mesh.file, config);
            solver::compute_geometry(global_mesh);
            num_global_cells = global_mesh.num_cells;
            fmt::print("[Rank 0] Read mesh: {} cells, {} faces, {} bnd faces\n",
                       num_global_cells, global_mesh.num_faces,
                       global_mesh.num_bnd_faces);

            // Partition
            auto graph = solver::build_cell_graph(global_mesh);
            solver::PartitionResult pres =
                solver::partition_mesh(graph, num_global_cells, nranks);
            partition = pres.assignment;
            fmt::print("[Rank 0] METIS edge cut: {}\n", pres.edge_cut);

            // Count cells per rank
            std::vector<int> counts(nranks, 0);
            for (int p : partition) counts[p]++;
            fmt::print("[Rank 0] Partition: ");
            for (int r = 0; r < nranks; ++r) fmt::print("{} ", counts[r]);
            fmt::print("\n");
        } catch (const std::exception& e) {
            ok = false;
            err_msg = e.what();
        }
    }
    MPI_Bcast(&ok, 1, MPI_CXX_BOOL, 0, MPI_COMM_WORLD);
    if (!ok) {
        if (rank == 0) fmt::print(stderr, "Error: {}\n", err_msg);
        MPI_Finalize();
        return 1;
    }
    MPI_Bcast(&num_global_cells, 1, MPI_INT, 0, MPI_COMM_WORLD);
    partition.resize(num_global_cells);
    MPI_Bcast(partition.data(), num_global_cells, MPI_INT, 0, MPI_COMM_WORLD);

    // Build distributed mesh (collective: rank 0 sends local mesh data to
    // every other rank, ranks > 0 receive).
    auto dist = solver::build_distributed_mesh(global_mesh, partition,
                                               MPI_COMM_WORLD);

    // Print per-rank diagnostics
    int owned = dist.owned_cells.size();
    int ghost = dist.ghost_cells.size();
    int nfaces = dist.local_faces.size();
    int nneigh = dist.neighbors.size();
    
    std::vector<int> all_owned(nranks), all_ghost(nranks);
    MPI_Gather(&owned, 1, MPI_INT, all_owned.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&ghost, 1, MPI_INT, all_ghost.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        fmt::print("\n=== Partition diagnostics ===\n");
        fmt::print("Rank  Owned  Ghost  Faces  Neighbors\n");
        fmt::print("{}     {}     {}     {}      {}\n", rank, owned, ghost, nfaces, nneigh);
    }
    // Other ranks print their diagnostics after rank 0's table
    for (int r = 1; r < nranks; ++r) {
        if (rank == r) {
            fmt::print("Rank {}: owned={}, ghost={}, faces={}, neighbors={}\n", 
                       r, owned, ghost, nfaces, nneigh);
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
    
    if (rank == 0) {
        int total_owned = std::accumulate(all_owned.begin(), all_owned.end(), 0);
        fmt::print("Total owned: {} (expected: {})\n", total_owned, num_global_cells);
    }

    MPI_Finalize();
    return 0;
}
