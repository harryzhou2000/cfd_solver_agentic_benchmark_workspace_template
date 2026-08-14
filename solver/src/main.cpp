#include "case_io.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "mesh_local.hpp"
#include "solver.hpp"
#include "forces.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mpi.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace cfd {

static std::string dirname(const std::string& path) {
    auto pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? "." : path.substr(0, pos);
}

static std::string resolve_path(const std::string& path, const std::string& ref) {
    if (path.empty() || path[0] == '/') return path;
    if (ref.empty()) return path;
    return dirname(ref) + "/" + path;
}

static void mkdir_p(const std::string& path) {
    std::string cmd = "mkdir -p " + path;
    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("failed to create directory: " + path);
    }
}

}  // namespace cfd

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, n_ranks = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);

    try {
        // Parse CLI.
        if (argc < 2 || std::strcmp(argv[1], "solve") != 0) {
            if (rank == 0) {
                std::fprintf(stderr, "Usage: mpirun -np N cfd_solver solve --case <case.json> --output <dir> [--restart <file>] [--report-level brief|full]\n");
            }
            MPI_Finalize();
            return 1;
        }
        std::string case_path, out_dir, restart_file, report_level = "brief";
        int opt_max_steps = 0;
        double opt_cfl_cap = 0.0;
        int opt_max_inner = 0;
        bool opt_first_order = false;
        for (int i = 2; i < argc; ++i) {
            if (std::strcmp(argv[i], "--case") == 0 && i + 1 < argc) case_path = argv[++i];
            else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) out_dir = argv[++i];
            else if (std::strcmp(argv[i], "--restart") == 0 && i + 1 < argc) restart_file = argv[++i];
            else if (std::strcmp(argv[i], "--report-level") == 0 && i + 1 < argc) report_level = argv[++i];
            else if (std::strcmp(argv[i], "--max-steps") == 0 && i + 1 < argc) opt_max_steps = std::atoi(argv[++i]);
            else if (std::strcmp(argv[i], "--cfl-cap") == 0 && i + 1 < argc) opt_cfl_cap = std::atof(argv[++i]);
            else if (std::strcmp(argv[i], "--max-inner") == 0 && i + 1 < argc) opt_max_inner = std::atoi(argv[++i]);
            else if (std::strcmp(argv[i], "--first-order") == 0) opt_first_order = true;
        }
        if (case_path.empty() || out_dir.empty()) {
            if (rank == 0) {
                std::fprintf(stderr, "Error: --case and --output are required\n");
            }
            MPI_Finalize();
            return 1;
        }

        // Build command line string for run_status.
        std::string cmd_line;
        for (int i = 0; i < argc; ++i) {
            if (i) cmd_line += " ";
            cmd_line += argv[i];
        }

        // Read case config.
        cfd::CaseConfig cfg = cfd::read_case_json(case_path);
        cfg.mesh_file = cfd::resolve_path(cfg.mesh_file, case_path);
        if (opt_max_steps > 0) cfg.max_steps = opt_max_steps;
        if (opt_cfl_cap > 0.0) cfg.cfl_max = opt_cfl_cap;
        if (opt_max_inner > 0) cfg.max_inner_iterations = opt_max_inner;
        if (opt_first_order) cfg.first_order = true;

        // Rank 0: create output dir, read mesh, partition, write partition files.
        cfd::PartitionInfo pinfo;
        if (rank == 0) {
            cfd::mkdir_p(out_dir);
            cfd::mkdir_p(out_dir + "/partition");
            std::fprintf(stderr, "[rank 0] Reading mesh: %s\n", cfg.mesh_file.c_str());
            cfd::GlobalMesh gm = cfd::read_cgns_mesh(cfg.mesh_file, cfg.bc_map);
            std::fprintf(stderr, "[rank 0] Mesh: %d nodes, %d cells, %d faces\n",
                        gm.num_nodes_global, gm.num_cells_global, gm.num_faces_global);
            pinfo = cfd::write_partition_files(gm, n_ranks, out_dir + "/partition");
            std::fprintf(stderr, "[rank 0] Partition written to %s/partition/\n", out_dir.c_str());
        }
        MPI_Bcast(&pinfo, sizeof(pinfo), MPI_BYTE, 0, MPI_COMM_WORLD);

        // All ranks: load local mesh.
        char partfile[512];
        std::snprintf(partfile, sizeof(partfile), "%s/partition/partition_rank_%d.bin",
                     out_dir.c_str(), rank);
        cfd::LocalMesh mesh = cfd::load_local_mesh(partfile);
        mesh.partitioner = "metis_kway";
        mesh.edge_cut = pinfo.edge_cut;
        mesh.num_cells_global = pinfo.num_cells_global;
        mesh.num_faces_global = pinfo.num_faces_global;
        mesh.num_nodes_global = pinfo.num_nodes_global;

        if (rank == 0) {
            std::fprintf(stderr, "[rank 0] Local mesh: %d owned, %d ghost, %d faces, %d bfaces\n",
                        mesh.n_owned, mesh.n_ghost, mesh.n_faces_internal(), (int)mesh.bfaces.size());
        }

        // Run solver.
        double start_wall = MPI_Wtime();
        cfd::SolverResults res = cfd::run_solver(mesh, cfg, rank, n_ranks, MPI_COMM_WORLD, start_wall);

        // Write outputs (rank 0 for global files, per-rank for gather).
        cfd::GasModel gas(cfg.gamma, cfg.gas_R, cfg.prandtl);
        double q_inf = 0.5 * cfg.rho_inf * cfg.vel_mag * cfg.vel_mag;

        // Write CSV outputs.
        MPI_Barrier(MPI_COMM_WORLD);
        cfd::write_residuals_csv(out_dir, res);
        MPI_Barrier(MPI_COMM_WORLD);
        cfd::write_forces_csv(out_dir, res);
        MPI_Barrier(MPI_COMM_WORLD);
        cfd::write_partition_diagnostics(out_dir, mesh, rank, n_ranks, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
        cfd::write_metadata(out_dir, mesh, cfg, res, rank, n_ranks, MPI_COMM_WORLD);
        MPI_Barrier(MPI_COMM_WORLD);
        cfd::write_run_status(out_dir, cfg, res, rank, n_ranks, cmd_line);
        MPI_Barrier(MPI_COMM_WORLD);

        // Write surface, field, restart (need final state).
        if (!res.final_U.empty()) {
            MPI_Barrier(MPI_COMM_WORLD);
            cfd::write_surface_csv(out_dir, mesh, res.final_U.data(), res.final_U_ghost.data(),
                                   gas, rank, n_ranks, MPI_COMM_WORLD, cfg, q_inf);
            MPI_Barrier(MPI_COMM_WORLD);
            cfd::write_field_vtu(out_dir, mesh, res.final_U.data(), rank, n_ranks,
                                 MPI_COMM_WORLD, "final");
            MPI_Barrier(MPI_COMM_WORLD);
            cfd::write_restart(out_dir, mesh, res.final_U.data(), res.final_step,
                              res.final_physical_time, rank, n_ranks, MPI_COMM_WORLD);
            MPI_Barrier(MPI_COMM_WORLD);
        }

        if (rank == 0) {
            std::fprintf(stderr, "Done. Outputs in %s\n", out_dir.c_str());
        }
    } catch (std::exception& e) {
        if (rank == 0) std::fprintf(stderr, "ERROR: %s\n", e.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Finalize();
    return 0;
}
