#include "solver/case_config.hpp"
#include "solver/mesh_reader.hpp"
#include "solver/geometry.hpp"
#include "solver/adjacency.hpp"
#include "solver/partition.hpp"
#include "solver/partition_types.hpp"
#include "solver/boundary.hpp"
#include "solver/residual.hpp"
#include "solver/time_integrator.hpp"
#include "solver/output_writer.hpp"
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

    // Redirect stdout to stdout.log (rank 0) / /dev/null (other ranks).
    if (rank == 0) {
        FILE* log = freopen((output_dir + "/stdout.log").c_str(), "w", stdout);
        (void)log;
    } else {
        FILE* nul = freopen("/dev/null", "w", stdout);
        (void)nul;
    }

    // Reconstruct the command line for run_status.json.
    std::string command = "cfd_solver";
    for (int i = 1; i < argc; ++i) {
        command += " ";
        command += argv[i];
    }

    // Rank-0 preprocessing: case parsing, mesh reading and partitioning.
    // If any step throws, broadcast an error flag so the other ranks exit
    // instead of hanging at the MPI_Bcast/MPI_Recv calls below.
    bool ok = true;
    std::string err_msg;
    solver::CaseConfig config;
    solver::Mesh global_mesh;
    int num_global_cells = 0;
    int edge_cut = -1;
    std::vector<int> partition;

    // Parse case config on all ranks (shared filesystem — no MPI broadcast needed)
    if (rank == 0) {
        try {
            config = solver::parse_case_config(case_file);
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
    // All ranks parse the same file (shared filesystem)
    if (rank != 0) config = solver::parse_case_config(case_file);

    if (rank == 0) {
        fmt::print("=== Case: {} ({})\n", config.case_id, config.description);
        fmt::print("=== MPI ranks: {}\n", nranks);
        fmt::print("=== Mode: {}, Mach: {}\n", config.physics.mode,
                   config.freestream.mach);
    }

    if (rank == 0) {
        try {

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
            edge_cut = pres.edge_cut;
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

    // Phase 5: partition diagnostics output (one row per rank).
    solver::write_partition_diagnostics(dist, output_dir, MPI_COMM_WORLD, rank);

    // --- Phase 4/5: implicit time integration with per-step output ---
    std::vector<double> state;
    solver::set_freestream_state(dist, state, config);

    std::string flux_type = "rusanov";
    if (config.freestream.mach > 1.5) flux_type = "roe";  // Roe for supersonic

    const std::string start_utc = solver::iso8601_utc_now();
    solver::SolverStats stats;
    if (config.run_control.type == "transient") {
        stats = solver::run_transient_solve(dist, state, config, flux_type,
                                            output_dir);
    } else {
        stats = solver::run_steady_solve(dist, state, config, flux_type,
                                         output_dir);
    }
    const std::string end_utc = solver::iso8601_utc_now();

    // compute_forces is collective (MPI reductions); every rank must call it.
    auto forces = solver::compute_forces(dist, state, config);
    if (rank == 0) {
        fmt::print("\n=== Final results ===\n");
        fmt::print("Steps: {}, Status: {}\n", stats.total_steps,
                   stats.convergence_status);
        fmt::print("Final residual: L2 = {:.6e}, Linf = {:.6e}\n",
                   stats.final_residual_l2, stats.final_residual_linf);
        fmt::print("Residual reduction: {:.2f} orders\n",
                   stats.residual_reduction_orders);
        fmt::print("Inner its: min={}, max={}, mean={:.1f}, misses={}, "
                   "converged fraction={:.2f}, last ratio={:.3e}\n",
                   stats.min_inner_its, stats.max_inner_its,
                   stats.mean_inner_its, stats.inner_target_misses,
                   stats.converged_fraction, stats.last_inner_residual_ratio);
        fmt::print("Forces: CL = {:.6f}, CD = {:.6f}, CMz = {:.6f}\n",
                   forces.cl, forces.cd, forces.cmz);
        fmt::print("Wall time: {:.2f}s\n", stats.wall_time_seconds);
    }

    // Phase 5: final output files (surface, field, restart, metadata, status).
    solver::write_surface_csv(dist, state, config, output_dir, MPI_COMM_WORLD,
                              rank);
    solver::write_field_vtu(dist, state, config, output_dir, MPI_COMM_WORLD,
                            rank);
    solver::write_restart(dist, state, output_dir, MPI_COMM_WORLD, rank);
    solver::write_metadata_json(dist, config, stats, flux_type, output_dir,
                                num_global_cells, global_mesh.num_faces,
                                edge_cut, start_utc, end_utc);
    // run_status final_step must match the last row of forces.csv: rows are
    // 1-based per executed step; a converged steady run stops before the
    // step that detected convergence.
    const bool steady_converged =
        (config.run_control.type != "transient") &&
        (stats.convergence_status == "converged");
    const int final_step =
        steady_converged ? stats.total_steps - 1 : stats.total_steps;
    solver::write_run_status_json(config, stats, final_step, command,
                                  output_dir, nranks);

    MPI_Finalize();
    return 0;
}
