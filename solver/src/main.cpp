#include "mesh/cgns_reader.hpp"
#include "mesh/mesh_types.hpp"
#include "boundary/boundary_utils.hpp"
#include "partition/partition_types.hpp"
#include "partition/halo_exchange.hpp"
#include "solver/solver.hpp"
#include "solver/solver_state.hpp"
#include "solver/gas_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <mpi.h>

namespace fs = std::filesystem;

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " solve --case <case.json> --output <output-dir>\n";
    std::cout << "Options:\n";
    std::cout << "  --case <path>       JSON case file (required)\n";
    std::cout << "  --output <dir>      Output directory (required)\n";
    std::cout << "  --restart <file>    Restart file (optional)\n";
    std::cout << "  --report-level <level>  'brief' or 'full' (default: full)\n";
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank, n_ranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);

    // Parse CLI arguments
    std::string case_path, output_dir, restart_file;
    std::string report_level = "full";

    if (argc < 2) {
        if (rank == 0) print_usage(argv[0]);
        MPI_Finalize();
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd != "solve") {
        if (rank == 0) {
            std::cerr << "Unknown command: " << cmd << "\n";
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--case" && i + 1 < argc) {
            case_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--restart" && i + 1 < argc) {
            restart_file = argv[++i];
        } else if (arg == "--report-level" && i + 1 < argc) {
            report_level = argv[++i];
        }
    }

    if (case_path.empty() || output_dir.empty()) {
        if (rank == 0) {
            std::cerr << "Error: --case and --output are required\n";
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    try {
        // Parse case config
        if (rank == 0) {
            std::cout << "=== CFD Solver ===" << std::endl;
            std::cout << "Case: " << case_path << std::endl;
            std::cout << "Output: " << output_dir << std::endl;
            std::cout << "MPI ranks: " << n_ranks << std::endl;
        }

        cfd::CaseConfig config = cfd::parse_case_config(case_path);
        if (rank == 0) {
            std::cout << "Case ID: " << config.case_id << std::endl;
            std::cout << "Physics mode: " << config.physics_mode << std::endl;
            std::cout << "Mach: " << config.freestream.mach << std::endl;
            std::cout << "Run type: " << config.run_control.type << std::endl;
        }

        // Resolve mesh path relative to case file directory
        fs::path case_dir = fs::path(case_path).parent_path();
        fs::path mesh_path = case_dir / config.mesh_file;

        if (rank == 0) {
            std::cout << "Mesh file: " << mesh_path << std::endl;
        }

        // Read mesh
        cfd::Mesh mesh = cfd::read_cgns_mesh(mesh_path.string(), config.bc_mappings);

        if (rank == 0) {
            std::cout << "\nMesh loaded successfully:" << std::endl;
            std::cout << "  Cells: " << mesh.stats.n_cells << std::endl;
            std::cout << "  Faces: " << mesh.stats.n_faces << std::endl;
            std::cout << "  Boundary faces: " << mesh.stats.n_boundary_faces << std::endl;
            std::cout << "  Vertices: " << mesh.stats.n_vertices << std::endl;
            std::cout << "  Boundary patches: " << mesh.boundary_patches.size() << std::endl;
            for (auto& patch : mesh.boundary_patches) {
                std::cout << "    Patch '" << patch.family_name << "': "
                          << patch.face_ids.size() << " faces" << std::endl;
            }
        }

        // ==================================================================
        // (a) MPI partitioning: serial METIS on rank 0, then broadcast
        // ==================================================================
        const cfd::Int n_cells = mesh.stats.n_cells;
        std::vector<cfd::Int> cell_partition;
        cfd::Int edge_cut = 0;

        if (n_ranks == 1) {
            // Single rank: trivial partition, no METIS call needed
            cell_partition.assign(n_cells, 0);
        } else if (rank == 0) {
            cfd::PartitionResult pr =
                cfd::partition_metis(mesh.cell_neighbors, n_ranks);
            cell_partition = std::move(pr.cell_partition);
            edge_cut = pr.edge_cut;
        } else {
            cell_partition.resize(n_cells);
        }
        MPI_Bcast(cell_partition.data(), static_cast<int>(cell_partition.size()),
                  MPI_INT64_T, 0, MPI_COMM_WORLD);

        // ==================================================================
        // (b) Build rank-local partition on all ranks
        // ==================================================================
        // Global cell ids are the contiguous serial-read order
        std::vector<cfd::Int> cell_global_ids(n_cells);
        for (cfd::Int i = 0; i < n_cells; ++i) {
            cell_global_ids[i] = i;
        }

        // Extract the global face left/right cell arrays from the mesh
        // (boundary faces carry the INVALID_INDEX sentinel on the right)
        std::vector<cfd::Int> face_left_cells(mesh.faces.size());
        std::vector<cfd::Int> face_right_cells(mesh.faces.size());
        for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
            face_left_cells[fi] = mesh.faces[fi].left_cell;
            face_right_cells[fi] = mesh.faces[fi].right_cell;
        }

        cfd::RankPartition rp = cfd::build_rank_partition(
            cell_partition, cell_global_ids, mesh.cell_neighbors,
            face_left_cells, face_right_cells, rank, n_ranks, MPI_COMM_WORLD);

        // ==================================================================
        // (c) Rank-local solver state (owned + ghost cells), freestream init
        // ==================================================================
        const cfd::Int n_local = rp.n_owned + rp.n_ghost;

        cfd::GasModel gas(config.gas.gamma, config.gas.R, config.gas.prandtl);
        cfd::Primitive W_inf{config.freestream.rho, config.freestream.u,
                             config.freestream.v, config.freestream.pressure};
        cfd::Conserved U_inf = cfd::primitive_to_conserved(W_inf, gas.gm1);

        cfd::SolverState state;
        state.U.assign(n_local, U_inf);
        state.U_n.assign(n_local, U_inf);
        state.U_nm1.assign(n_local, U_inf);
        state.residual.assign(n_local, cfd::Conserved(0.0));
        state.cell_centroids.resize(n_local);
        state.cell_volumes.resize(n_local);
        for (cfd::Int li = 0; li < rp.n_owned; ++li) {
            const auto& cell = mesh.cells[rp.owned_cell_ids[li]];
            state.cell_centroids[li] = cell.centroid;
            state.cell_volumes[li] = cell.volume;
        }
        for (cfd::Int gi = 0; gi < rp.n_ghost; ++gi) {
            const auto& cell = mesh.cells[rp.ghost_cell_ids[gi]];
            state.cell_centroids[rp.n_owned + gi] = cell.centroid;
            state.cell_volumes[rp.n_owned + gi] = cell.volume;
        }

        // Local face geometry (subset of global faces touching owned cells)
        state.face_centroids.resize(rp.faces.size());
        state.face_normals.resize(rp.faces.size());
        for (size_t fi = 0; fi < rp.faces.size(); ++fi) {
            const auto& gface = mesh.faces[rp.faces[fi].global_face_id];
            state.face_centroids[fi] = gface.centroid;
            state.face_normals[fi] = gface.normal;
        }

        // ==================================================================
        // Halo exchange smoke test: ghosts start as NaN and must be filled
        // by the neighbor-scoped exchange (proves comm wiring works)
        // ==================================================================
        for (cfd::Int li = rp.n_owned; li < n_local; ++li) {
            state.U[li] = cfd::Conserved(std::nan(""));
        }
        {
            std::vector<std::vector<cfd::Int>> send_cells, recv_cells;
            std::vector<int> neighbor_ranks;
            for (const auto& hn : rp.neighbors) {
                send_cells.push_back(hn.send_cells);
                recv_cells.push_back(hn.recv_cells);
                neighbor_ranks.push_back(hn.neighbor_rank);
            }
            cfd::halo_exchange_conserved(state.U, send_cells, recv_cells,
                                         neighbor_ranks, MPI_COMM_WORLD);
        }
        bool halo_ok = true;
        for (cfd::Int li = rp.n_owned; li < n_local; ++li) {
            for (int c = 0; c < 4; ++c) {
                if (!std::isfinite(state.U[li][c])) {
                    halo_ok = false;
                    break;
                }
            }
            if (!halo_ok) break;
        }
        halo_ok = cfd::all_ranks_agree(halo_ok, MPI_COMM_WORLD);
        if (rank == 0) {
            std::cout << "\nHalo exchange check: " << (halo_ok ? "OK" : "FAILED")
                      << " (" << rp.n_owned << " owned, " << rp.n_ghost
                      << " ghost cells on rank 0)" << std::endl;
        }
        if (!halo_ok) {
            throw std::runtime_error(
                "halo exchange failed: ghost cells were not filled by neighbors");
        }

        // ==================================================================
        // (d) Partition diagnostics CSV (gathered to and written by rank 0)
        // ==================================================================
        cfd::Int n_boundary_faces_local = 0;
        for (const auto& lf : rp.faces) {
            const bool left_invalid = (lf.left_local == cfd::INVALID_INDEX);
            const bool right_invalid = (lf.right_local == cfd::INVALID_INDEX);
            if (left_invalid != right_invalid) n_boundary_faces_local++;
        }

        // Per-rank row: neighbor ranks plus per-neighbor send/recv cell counts
        std::ostringstream nb_ranks, nb_send, nb_recv;
        for (size_t ni = 0; ni < rp.neighbors.size(); ++ni) {
            if (ni > 0) {
                nb_ranks << ";";
                nb_send << ";";
                nb_recv << ";";
            }
            nb_ranks << rp.neighbors[ni].neighbor_rank;
            nb_send << rp.neighbors[ni].send_cells.size();
            nb_recv << rp.neighbors[ni].recv_cells.size();
        }

        const int LINE_BUF = 8192;
        std::vector<char> line(LINE_BUF, 0);
        std::snprintf(line.data(), LINE_BUF,
                      "%d,%lld,%lld,%lld,%zu,%s,%s,%s",
                      rank, static_cast<long long>(rp.n_owned),
                      static_cast<long long>(rp.n_ghost),
                      static_cast<long long>(n_boundary_faces_local),
                      rp.neighbors.size(), nb_ranks.str().c_str(),
                      nb_send.str().c_str(), nb_recv.str().c_str());

        std::vector<char> all_lines;
        if (rank == 0) all_lines.resize(static_cast<size_t>(n_ranks) * LINE_BUF);
        MPI_Gather(line.data(), LINE_BUF, MPI_CHAR,
                   all_lines.data(), LINE_BUF, MPI_CHAR, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            fs::create_directories(output_dir);
            std::ofstream csv(fs::path(output_dir) / "partition_diagnostics.csv");
            csv << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
                   "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
            for (int r = 0; r < n_ranks; ++r) {
                csv << all_lines.data() +
                           static_cast<size_t>(r) * LINE_BUF
                    << "\n";
            }
        }

        // ==================================================================
        // (e) Partition summary on rank 0
        // ==================================================================
        std::vector<cfd::Int> owned_counts(n_ranks);
        MPI_Gather(&rp.n_owned, 1, MPI_INT64_T,
                   owned_counts.data(), 1, MPI_INT64_T, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            cfd::Int min_owned = owned_counts[0];
            cfd::Int max_owned = owned_counts[0];
            long long sum_owned = 0;
            for (auto c : owned_counts) {
                min_owned = std::min(min_owned, c);
                max_owned = std::max(max_owned, c);
                sum_owned += static_cast<long long>(c);
            }
            const double mean_owned =
                static_cast<double>(sum_owned) / static_cast<double>(n_ranks);
            const double load_balance =
                (max_owned > 0) ? mean_owned / static_cast<double>(max_owned) : 0.0;

            std::cout << "\nPartition summary:" << std::endl;
            std::cout << "  Edge cut (METIS): " << edge_cut << std::endl;
            std::cout << "  Cells per rank:";
            for (auto c : owned_counts) std::cout << " " << c;
            std::cout << std::endl;
            std::cout << "  Owned cells: min=" << min_owned
                      << " max=" << max_owned
                      << " mean=" << mean_owned << std::endl;
            std::cout << "  Load balance ratio (mean/max, 1.0 = perfect): "
                      << load_balance << std::endl;
            std::cout << "  Partition diagnostics written to: "
                      << (fs::path(output_dir) / "partition_diagnostics.csv")
                      << std::endl;
        }

        // ==================================================================
        // (f) Main solver: implicit steady / BDF2 transient loop + outputs
        // ==================================================================
        cfd::run_solver(mesh, rp, config, output_dir, edge_cut);

    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        std::cout << "\nSolver completed successfully." << std::endl;
    }

    MPI_Finalize();
    return 0;
}
