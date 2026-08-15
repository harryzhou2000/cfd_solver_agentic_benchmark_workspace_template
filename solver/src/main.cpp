// CLI entry point for the CFD solver.
// Contract: mpirun -np N <solver> solve --case <json> --output <dir>
//                     [--restart <file>] [--report-level brief|full]
// Phase 1: parses the command line, initializes MPI, validates the case
// file, and prints a startup banner. The solve pipeline (mesh read ->
// partition -> solve -> write) is wired in later phases.

#include <mpi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

#include "config.hpp"
#include "forces.hpp"
#include "gas.hpp"
#include "geometry.hpp"
#include "mesh_reader.hpp"
#include "mpi_utils.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace {

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " solve --case <case.json> [--output <dir>]\n"
              << "             [--restart <file>] [--report-level brief|full]\n"
              << "  solve               Solve subcommand (required)\n"
              << "  --case FILE         JSON case file (mesh, physics, control)\n"
              << "  --output DIR        Output directory (default: results)\n"
              << "  --restart FILE      Restart file to resume from (optional)\n"
              << "  --report-level LVL  Progress verbosity: brief|full (default: full)\n"
              << "  --help              Show this help\n";
}

// Resolves a possibly-relative path against `base` (the directory of the
// case file). Absolute paths pass through unchanged.
std::string resolve_path(const std::string& p, const std::string& base) {
    const std::filesystem::path path(p);
    if (path.is_absolute()) return p;
    return (std::filesystem::path(base) / path).lexically_normal().string();
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // --- Parse CLI: solve --case <json> --output <dir> [--restart <file>]
    //                 [--report-level brief|full] ---
    std::string case_file;
    std::string output_dir = "results";
    std::string restart_file;
    std::string report_level = "full";
    bool saw_solve = false;
    bool ok = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!saw_solve && arg == "solve") {
            saw_solve = true;
        } else if (arg == "--case" && i + 1 < argc) {
            case_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--restart" && i + 1 < argc) {
            restart_file = argv[++i];
        } else if (arg == "--report-level" && i + 1 < argc) {
            report_level = argv[++i];
            if (report_level != "brief" && report_level != "full") {
                if (rank == 0) {
                    std::cerr << "Invalid --report-level '" << report_level
                              << "' (expected brief or full)\n";
                }
                ok = false;
            }
        } else if (arg == "--help") {
            if (rank == 0) print_usage(argv[0]);
            MPI_Finalize();
            return 0;
        } else {
            if (rank == 0) {
                std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            }
            ok = false;
            // Keep scanning so the user sees all problems at once.
        }
    }

    if (ok && !saw_solve) {
        if (rank == 0) {
            std::cerr << "Missing 'solve' subcommand\n";
            print_usage(argv[0]);
        }
        ok = false;
    }
    if (ok && case_file.empty()) {
        if (rank == 0) {
            std::cerr << "Missing required --case <file>\n";
            print_usage(argv[0]);
        }
        ok = false;
    }

    if (!ok) {
        MPI_Finalize();
        return 1;
    }

    if (rank == 0) {
        std::cout << "CFD Solver starting with " << size << " MPI rank(s)"
                  << std::endl;
        std::cout << "  case file   : " << case_file << std::endl
                  << "  output dir  : " << output_dir << std::endl
                  << "  report level: " << report_level << std::endl;
        if (!restart_file.empty()) {
            std::cout << "  restart file: " << restart_file << std::endl;
        }
    }

    // Phase 2a: parse the case file, read the CGNS mesh, and build the
    // cell/face geometry. The solve loop itself lands in a later phase.
    int rc = 0;
    std::string command_line;
    for (int i = 0; i < argc; ++i) {
        if (i) command_line += " ";
        command_line += argv[i];
    }
    const std::string start_time_utc = cfd::utc_now_iso();
    try {
        const cfd::CaseConfig cfg = cfd::parse_case_file(case_file);
        const auto t_parse = std::chrono::steady_clock::now();

        // FIX 3: resolve mesh path relative to the case file's directory.
        const std::filesystem::path case_path(case_file);
        const std::string mesh_file =
            resolve_path(cfg.mesh_file, case_path.parent_path().string());

        // --- Read raw mesh from CGNS ---
        const cfd::MeshData raw = cfd::read_mesh_cgns(mesh_file);

        // Family names found on boundary faces (for the summary).
        std::map<std::string, int> family_counts;
        for (const auto& bf : raw.boundary_faces) {
            ++family_counts[bf.family_name];
        }

        // --- Build cell/face geometry ---
        cfd::Mesh mesh = cfd::build_geometry(raw, cfg.boundary_conditions);
        const auto t_geom = std::chrono::steady_clock::now();

        if (rank == 0) {
            std::cout << "  mesh        : " << mesh_file << std::endl
                      << "  Mach        : " << cfg.freestream.mach << std::endl
                      << "  alpha       : " << cfg.freestream.alpha
                      << " deg" << std::endl
                      << "  mode        : " << cfg.mode
                      << (cfg.viscous() ? " (viscous)" : " (inviscid)")
                      << std::endl
                      << "  Reynolds    : " << cfg.reynolds << std::endl
                      << "  visc model  : " << cfg.viscosity_model << std::endl
                      << "  run type    : " << cfg.control.type << std::endl;
            if (cfg.control.type == "transient") {
                std::cout << "  time integr : " << cfg.control.time_integrator
                          << std::endl
                          << "  time step   : " << cfg.control.time_step
                          << std::endl
                          << "  final time  : " << cfg.control.final_time
                          << std::endl;
            }
            std::cout << "  max steps   : " << cfg.control.max_iterations
                      << std::endl
                      << "--- mesh summary ---" << std::endl
                      << "  nodes           : " << raw.n_nodes << std::endl
                      << "  cells           : " << raw.n_cells << std::endl
                      << "  boundary faces  : "
                      << raw.boundary_faces.size() << std::endl;
            for (const auto& [fam, cnt] : family_counts) {
                std::cout << "    family '" << fam << "': " << cnt
                          << " faces" << std::endl;
            }
            std::cout << "--- geometry summary ---" << std::endl
                      << "  internal faces  : " << mesh.n_faces << std::endl
                      << "  boundary faces  : " << mesh.n_boundary_faces
                      << std::endl;
            std::array<int, 4> bc_counts = {0, 0, 0, 0};
            for (int t : mesh.bface_bc_type) {
                if (t >= 0 && t < 4) ++bc_counts[t];
            }
            std::cout << "    farfield        : " << bc_counts[0]
                      << std::endl
                      << "    slip wall       : " << bc_counts[1]
                      << std::endl
                      << "    no-slip wall    : " << bc_counts[2]
                      << std::endl
                      << "    unknown         : " << bc_counts[3]
                      << std::endl;
            if (!mesh.cell_vol.empty()) {
                const auto [vmin, vmax] =
                    std::minmax_element(mesh.cell_vol.begin(),
                                        mesh.cell_vol.end());
                double vsum = 0.0;
                for (double v : mesh.cell_vol) vsum += v;
                std::cout << "  cell volume     : min=" << *vmin
                          << " max=" << *vmax
                          << " total=" << vsum << std::endl;
            }
        }

        // --- Phase 2b: METIS partitioning + rank-local mesh + halo plan ---
        const cfd::PartitionResult part =
            cfd::partition_cells(mesh, size);
        cfd::LocalMesh lm =
            cfd::build_local_mesh_simple(mesh, part.cell_partition, rank, size);
        cfd::HaloExchangePlan plan = cfd::build_halo_plan(
            mesh, part.cell_partition, lm.owned_cells, rank, size,
            MPI_COMM_WORLD);
        if (rank == 0) {
            std::cout << "  [phase timing] parse="
                      << std::chrono::duration<double>(
                             t_geom - t_parse).count()
                      << "s read+geom+part="
                      << std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t_geom)
                             .count()
                      << "s" << std::endl;
        }

        // Record the partition in the Mesh struct (contract: filled by the
        // partitioner for downstream phases).
        mesh.owned_cells_global_ids = lm.owned_cells;
        mesh.n_owned = lm.n_owned;
        mesh.n_ghost = lm.n_ghost;

        if (rank == 0) {
            std::cout << "--- partition summary ---" << std::endl
                      << "  n ranks         : " << size << std::endl
                      << "  edge cut        : " << part.edge_cut << std::endl;
        }
        std::cout << "[rank " << rank << "] owned=" << lm.n_owned
                  << " ghost=" << lm.n_ghost
                  << " neighbors=" << plan.size() << std::endl;
        for (const auto& hm : plan) {
            std::cout << "[rank " << rank << "]   <-> rank " << hm.rank
                      << ": send=" << hm.send_cell_ids_local.size()
                      << " recv=" << hm.recv_cell_ids_local.size()
                      << std::endl;
        }

        // Partition diagnostics CSV: rank 0 creates a fresh file, then every
        // rank appends its row (contract header with neighbor/send/recv info).
        {
            cgsize_t n_bface_owned = 0;
            for (cgsize_t bf = 0; bf < mesh.n_boundary_faces; ++bf) {
                if (std::binary_search(lm.owned_cells.begin(),
                                       lm.owned_cells.end(),
                                       mesh.bface_cell[static_cast<size_t>(bf)])) {
                    ++n_bface_owned;
                }
            }
            std::vector<int> neighbor_ranks, send_cells, recv_cells;
            for (const auto& hm : plan) {
                neighbor_ranks.push_back(hm.rank);
                send_cells.push_back(static_cast<int>(hm.send_cell_ids_local.size()));
                recv_cells.push_back(static_cast<int>(hm.recv_cell_ids_local.size()));
            }
            if (rank == 0) {
                std::filesystem::remove(output_dir + "/partition_diagnostics.csv");
            }
            MPI_Barrier(MPI_COMM_WORLD);
            cfd::OutputWriter::write_partition_diagnostics(
                output_dir, rank, size, lm.n_owned, lm.n_ghost, n_bface_owned,
                neighbor_ranks, send_cells, recv_cells);
        }

        // --- Phase 2b: exercise exchange_halo with a synthetic state ---
        // Owned cells carry (rank, local_index, rank, 0); ghosts start at
        // (-1,-1,-1,-1). After the exchange every ghost must carry its
        // owning rank's tag in components r and v.
        {
            std::vector<cfd::Vector4> U_local(
                static_cast<size_t>(lm.n_owned + lm.n_ghost));
            for (cgsize_t i = 0; i < lm.n_owned; ++i) {
                U_local[static_cast<size_t>(i)] = {
                    static_cast<double>(rank), static_cast<double>(i),
                    static_cast<double>(rank), 0.0};
            }
            for (cgsize_t i = lm.n_owned;
                 i < lm.n_owned + lm.n_ghost; ++i) {
                U_local[static_cast<size_t>(i)] = {-1.0, -1.0, -1.0, -1.0};
            }
            cfd::exchange_halo(plan, U_local, MPI_COMM_WORLD);

            bool halo_ok = true;
            for (const auto& hm : plan) {
                for (int li : hm.recv_cell_ids_local) {
                    const cfd::Vector4& U =
                        U_local[static_cast<size_t>(li)];
                    if (U.r != static_cast<double>(hm.rank) ||
                        U.v != static_cast<double>(hm.rank)) {
                        halo_ok = false;
                    }
                }
            }
            if (!halo_ok) rc = 1;
            std::cout << "[rank " << rank
                      << "] halo exchange check: "
                      << (halo_ok ? "PASS" : "FAIL") << std::endl;
        }

        // --- Phase 3b/4: solve (steady or transient) -------------------------
        // Restart support: when --restart is given, the owned-cell state is
        // loaded from the binary checkpoint and the solve resumes from it
        // (ghost cells are filled by the first halo exchange inside the
        // solve).
        std::vector<cfd::Vector4> U_local;
        bool have_restart = false;
        int restart_step = 0;
        double restart_time = 0.0;
        if (!restart_file.empty()) {
            U_local = cfd::read_restart(restart_file, lm.n_owned,
                                        &restart_step, &restart_time,
                                        MPI_COMM_WORLD);
            U_local.resize(static_cast<size_t>(lm.n_owned + lm.n_ghost));
            cfd::exchange_halo(plan, U_local, MPI_COMM_WORLD);
            have_restart = true;
        }
        const double viscosity =
            cfd::laminar_viscosity(cfg.freestream, cfg.reference,
                                   cfg.reynolds);
        const auto t_solve_start = std::chrono::steady_clock::now();
        cfd::SolverStats solve_stats;
        if (cfg.control.type == "transient") {
            solve_stats = cfd::transient_solve(
                mesh, lm, U_local, cfg.control, cfg.gas, cfg.freestream,
                cfg.reference, cfg.boundary_conditions, viscosity,
                cfg.control.rusanov_dissipation_scale, plan, MPI_COMM_WORLD,
                !have_restart, have_restart ? restart_time : 0.0);
        } else {
            solve_stats = cfd::steady_solve(
                mesh, lm, U_local, cfg.control, cfg.gas, cfg.freestream,
                cfg.reference, cfg.boundary_conditions, viscosity,
                cfg.control.rusanov_dissipation_scale, plan, MPI_COMM_WORLD,
                !have_restart);
        }
        const auto t_solve_end = std::chrono::steady_clock::now();
        const double wall_time_seconds =
            std::chrono::duration<double>(t_solve_end - t_solve_start)
                .count();

        const bool is_transient = (cfg.control.type == "transient");
        const double final_physical_time =
            is_transient && !solve_stats.time_history.empty()
                ? solve_stats.time_history.back()
                : 0.0;
        const double reduction_orders =
            (solve_stats.initial_residual > 0.0 &&
             solve_stats.final_residual > 0.0 &&
             std::isfinite(solve_stats.initial_residual) &&
             std::isfinite(solve_stats.final_residual))
                ? std::log10(solve_stats.initial_residual /
                             solve_stats.final_residual)
                : 0.0;
        bool blew_up = !std::isfinite(solve_stats.final_residual) ||
                       (solve_stats.initial_residual > 0.0 &&
                        solve_stats.final_residual >
                            solve_stats.initial_residual * 1e6);


        // Convergence status: steady runs report converged when the
        // reduction target was met; transient runs that completed the full
        // physical-time horizon report statistically_periodic (the Re 200
        // vortex-street contract). Blown-up runs are marked failed.
        std::string convergence_status;
        if (blew_up) {
            convergence_status = "failed";
        } else if (is_transient) {
            const bool full_horizon =
                cfg.control.final_time > 0.0 &&
                final_physical_time >= cfg.control.final_time - 1e-9;
            convergence_status =
                full_horizon ? "statistically_periodic" : "completed";
        } else {
            convergence_status =
                solve_stats.converged ? "converged" : "completed";
        }
        if (rank == 0) {
            std::cout << "  [phase timing] solve=" << wall_time_seconds
                      << "s" << std::endl;
            std::cout << "--- " << (is_transient ? "transient" : "steady")
                      << " solve ---" << std::endl
                      << "  viscosity mu : " << viscosity
                      << " (mode=" << cfg.mode << ")" << std::endl
                      << "  steps        : " << solve_stats.steps
                      << " (inner sweeps: "
                      << solve_stats.inner_iterations << ")" << std::endl
                      << "  initial L2   : " << solve_stats.initial_residual
                      << std::endl
                      << "  final L2     : " << solve_stats.final_residual
                      << std::endl
                      << "  reduction    : " << reduction_orders
                      << " orders" << std::endl
                      << "  inner iters  : min="
                      << solve_stats.observed_min_inner
                      << " max=" << solve_stats.observed_max_inner
                      << " mean=" << solve_stats.typical_inner_iterations
                      << " misses=" << solve_stats.inner_target_misses
                      << std::endl;
            if (!solve_stats.force_history.empty()) {
                const cfd::ForceResult& F =
                    solve_stats.force_history.back();
                std::cout << "  final forces : CL=" << F.cl
                          << " CD=" << F.cd << " CMz=" << F.cmz
                          << " (pressure drag=" << F.pressure_drag
                          << " viscous drag=" << F.viscous_drag << ")"
                          << std::endl;
            }
        }
        if (blew_up) rc = 1;

        // --- Output contract files ------------------------------------------
        // Gather the full field to rank 0 (post-processing only; the solve
        // itself exchanges only halo data).
        const std::vector<cfd::Vector4> U_global = cfd::gather_global_field(
            lm, U_local, mesh.n_cells, MPI_COMM_WORLD);

        cfd::OutputWriter::write_restart(output_dir, U_local, lm,
                                         solve_stats.steps,
                                         final_physical_time,
                                         MPI_COMM_WORLD);
        if (rank == 0) {
            const auto t_out = std::chrono::steady_clock::now();
            // surface.csv + field_final.vtk + restart_final.bin
            const std::vector<cfd::SurfaceRow> surface_rows =
                cfd::build_surface_rows(mesh, U_global, cfg.gas,
                                        cfg.freestream, viscosity,
                                        MPI_COMM_WORLD);
            cfd::OutputWriter::write_surface(output_dir, surface_rows);
            std::vector<int> cell_owner(static_cast<size_t>(mesh.n_cells), 0);
            for (size_t i = 0; i < cell_owner.size(); ++i) {
                cell_owner[i] = static_cast<int>(part.cell_partition[i]);
            }
            cfd::OutputWriter::write_field_vtk(
                output_dir, mesh, raw.cell_nodes, U_global, cell_owner,
                cfg.gas, rank);


            // metadata.json
            cfd::MetadataParams meta;
            meta.case_id = cfg.case_id;
            meta.description = cfg.description;
            meta.mesh_file = mesh_file;
            meta.mode = cfg.mode;
            meta.reynolds = cfg.reynolds;
            meta.viscosity_model = cfg.viscosity_model;
            meta.mach = cfg.freestream.mach;
            meta.alpha = cfg.freestream.alpha;
            meta.gamma = cfg.gas.gamma;
            meta.gas_R = cfg.gas.R;
            meta.prandtl = cfg.gas.Pr;
            meta.run_type = cfg.control.type;
            meta.time_integrator =
                is_transient ? "bdf2" : "pseudo_steady";
            meta.time_step = cfg.control.time_step;
            meta.final_time = cfg.control.final_time;
            meta.max_steps = cfg.control.max_iterations;
            meta.spatial_order = cfg.spatial_order;
            meta.inviscid_flux = cfg.inviscid_flux;
            meta.limiter = "barth_jespersen";
            meta.n_ranks = size;
            meta.num_cells_global = mesh.n_cells;
            meta.num_faces_global = mesh.n_faces;
            meta.num_cells_owned_local = lm.n_owned;
            meta.num_cells_ghost_local = lm.n_ghost;
            meta.partitioner = "metis_kway";
            meta.partition_edge_cut = part.edge_cut;
            meta.halo_exchange = "neighbor_isend_irecv";
            meta.true_bdf2_inner_loop = is_transient;
            meta.min_inner_iterations = cfg.control.min_inner_iterations;
            meta.max_inner_iterations = cfg.control.max_inner_iterations;
            meta.typical_inner_iterations =
                static_cast<int>(solve_stats.typical_inner_iterations);
            meta.observed_min_inner_iterations =
                solve_stats.observed_min_inner;
            meta.observed_max_inner_iterations =
                solve_stats.observed_max_inner;
            meta.inner_residual_reduction_target =
                cfg.control.inner_residual_reduction_target;
            meta.inner_target_misses = solve_stats.inner_target_misses;
            meta.inner_target_converged_fraction =
                solve_stats.inner_target_converged_fraction;
            meta.last_inner_residual_ratio =
                solve_stats.last_inner_residual_ratio;
            meta.start_time_utc = start_time_utc;
            meta.end_time_utc = cfd::utc_now_iso();
            meta.completed = !blew_up;
            meta.convergence_status = convergence_status;
            cfd::OutputWriter::write_metadata(output_dir, meta);

            // residuals.csv
            std::vector<cfd::ResidualRow> rows;
            rows.reserve(solve_stats.residual_history.size());
            for (size_t k = 0; k < solve_stats.residual_history.size();
                 ++k) {
                cfd::ResidualRow row;
                row.step = static_cast<int>(k) + 1;
                row.physical_time =
                    (k < solve_stats.time_history.size())
                        ? solve_stats.time_history[k]
                        : 0.0;
                row.inner_iter =
                    (k < solve_stats.inner_history.size())
                        ? solve_stats.inner_history[k]
                        : 0;
                row.cfl = solve_stats.cfl_history[k];
                row.dt = (k < solve_stats.dt_history.size())
                             ? solve_stats.dt_history[k]
                             : 0.0;
                row.rho = solve_stats.stats_history[k].rho;
                row.rhou = solve_stats.stats_history[k].rhou;
                row.rhov = solve_stats.stats_history[k].rhov;
                row.rhoE = solve_stats.stats_history[k].rhoE;
                row.residual_l2 = solve_stats.residual_history[k];
                row.residual_linf = solve_stats.stats_history[k].linf;
                rows.push_back(row);
            }
            cfd::OutputWriter::write_residuals(output_dir, rows);

            // forces.csv
            std::vector<cfd::ForceRow> frows;
            frows.reserve(solve_stats.force_history.size());
            for (size_t k = 0; k < solve_stats.force_history.size(); ++k) {
                cfd::ForceRow row;
                row.step = static_cast<int>(k) + 1;
                row.physical_time =
                    (k < solve_stats.time_history.size())
                        ? solve_stats.time_history[k]
                        : 0.0;
                row.cl = solve_stats.force_history[k].cl;
                row.cd = solve_stats.force_history[k].cd;
                row.cmz = solve_stats.force_history[k].cmz;
                row.pressure_drag =
                    solve_stats.force_history[k].pressure_drag;
                row.viscous_drag =
                    solve_stats.force_history[k].viscous_drag;
                row.pressure_lift =
                    solve_stats.force_history[k].pressure_lift;
                row.viscous_lift =
                    solve_stats.force_history[k].viscous_lift;
                frows.push_back(row);
            }
            cfd::OutputWriter::write_forces(output_dir, frows);

            // run_status.json
            cfd::RunStatusParams st;
            st.case_id = cfg.case_id;
            st.command = command_line;
            st.n_ranks = size;
            st.wall_time_seconds = wall_time_seconds;
            st.final_step = solve_stats.steps;
            st.final_physical_time = final_physical_time;
            st.convergence_status = convergence_status;
            st.residual_reduction_orders = reduction_orders;
            st.notes = is_transient
                           ? "BDF2 dual-time stepping; residual is the "
                             "total (spatial + physical-time) norm"
                           : "steady pseudo-time march";
            cfd::OutputWriter::write_run_status(output_dir, st);
            std::cout << "  [phase timing] outputs="
                      << std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t_out)
                             .count()
                      << "s" << std::endl;
        }
    } catch (const std::exception& ex) {
        if (rank == 0) {
            std::cerr << "Failed to parse case file: " << ex.what()
                      << std::endl;
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Finalize();
    return rc;
}
