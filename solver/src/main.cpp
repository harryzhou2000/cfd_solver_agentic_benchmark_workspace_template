/// @file main.cpp
/// CLI entry point for the 2-D unstructured CFD solver.
///
/// Usage:
///   ./cfd_solver solve --case <case-json> --output <output-dir>
///                      [--restart <restart-file>]
///                      [--report-level brief|full]
///
/// Phase 3: MPI parallelism + BDF2 transient solver.

#include <argparse/argparse.hpp>
#include <cgnslib.h>
#include <mpi.h>

#include "case_reader.hpp"
#include "cgns_reader.hpp"
#include "common.hpp"
#include "geometry.hpp"
#include "logging.hpp"
#include "solver.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

/// Suppress CGNS's default error printing to stdout.
void install_cgns_error_handler() {
    cg_error_handler([](int, char*) {});
}

std::string boundary_tag_name(int tag) {
    switch (tag) {
        case static_cast<int>(cfd::BoundaryType::Farfield): return "farfield";
        case static_cast<int>(cfd::BoundaryType::SlipWall): return "slip_wall";
        case static_cast<int>(cfd::BoundaryType::NoSlipAdiabaticWall): return "no_slip_adiabatic_wall";
        default: return "unknown";
    }
}

int run_solve(const std::string& case_path, const std::string& output_dir,
              const std::string& restart_file, const std::string& report_level,
              MPI_Comm comm) {
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);
    cfd::logging::rank() = rank;

    // Suppress non-zero rank logging by default
    cfd::logging::suppress_nonzero() = true;

    // --- 1. Parse the case file -----------------------------------------------
    const cfd::CaseConfig config = cfd::read_case(case_path);
    LOG_INFO("Loaded case '{}' (schema v{})", config.case_id, config.schema_version);
    LOG_INFO("  description: {}", config.description);
    LOG_INFO("  mesh: {} (format {}, {}D)", config.mesh.file, config.mesh.format,
             config.mesh.dimension);
    LOG_INFO("  physics: mode={} gamma={} R={}", config.physics.mode == cfd::PhysicsMode::Inviscid
                                                       ? "inviscid"
                                                       : "laminar",
             config.gas.gamma, config.gas.R);
    LOG_INFO("  freestream: M={} AoA={} deg, rho={} p={} U={} (u={:.6f}, v={:.6f}) T={:.6f} a={:.6f}",
             config.freestream.mach, config.freestream.aoa_degrees, config.freestream.rho,
             config.freestream.pressure, config.freestream.velocity_magnitude,
             config.freestream.velocity.x(), config.freestream.velocity.y(),
             config.freestream.temperature, config.freestream.speed_of_sound);
    if (config.physics.mode == cfd::PhysicsMode::Laminar) {
        LOG_INFO("  laminar: Re={} mu={:.6e} ({} viscosity)", config.physics.reynolds,
                 config.freestream.viscosity, config.physics.viscosity_model);
    }
    LOG_INFO("  run: type={} max_steps={} residual_target={:.1e}",
             config.run_control.type == cfd::RunType::Steady ? "steady" : "transient",
             config.run_control.max_steps, config.run_control.residual_reduction_target);
    LOG_INFO("  numerics: spatial_order={} inviscid_flux={} viscous_flux={}",
             config.numerics_required.spatial_order,
             config.numerics_required.inviscid_flux,
             config.numerics_required.viscous_flux);
    LOG_INFO("  MPI: {} rank(s)", size);

    // --- 2. Read the mesh (rank 0 reads, then broadcasts for parallel) -----
    cfd::Mesh mesh;
    if (rank == 0) {
        mesh = cfd::read_cgns_mesh(config.mesh.file, config.boundary_conditions);
    }

    // --- 3. Compute geometry ----------------------------------------------------
    if (rank == 0) {
        cfd::compute_geometry(mesh);
    }

    // --- 4. Output directory ----------------------------------------------------
    if (rank == 0) {
        std::filesystem::create_directories(output_dir);
    }

    // --- 5. Diagnostic summary -------------------------------------------------
    if (rank == 0) {
        std::size_t n_boundary_faces = 0;
        for (const auto& [tag, faces] : mesh.boundary_faces) {
            n_boundary_faces += faces.size();
        }
        LOG_INFO("=== Mesh summary ===");
        LOG_INFO("Zone: {}", mesh.zone_name);
        LOG_INFO("Cells: {}", mesh.n_cells());
        LOG_INFO("Faces: {} ({} internal, {} boundary)", mesh.n_faces(),
                 mesh.n_faces() - n_boundary_faces, n_boundary_faces);
        LOG_INFO("Nodes: {}", mesh.n_nodes());
        for (const auto& [tag, faces] : mesh.boundary_faces) {
            LOG_INFO("  boundary '{}' (tag {}): {} faces", boundary_tag_name(tag), tag, faces.size());
        }
        LOG_INFO("Bounding box: x=[{:.6e}, {:.6e}], y=[{:.6e}, {:.6e}]", mesh.min_coord.x(),
                 mesh.max_coord.x(), mesh.min_coord.y(), mesh.max_coord.y());

        cfd::Real total_area = 0.0;
        for (const auto& cell : mesh.cells) total_area += cell.volume;
        LOG_INFO("Total cell area: {:.10e}", total_area);

        if (report_level == "full") {
            for (std::size_t i = 0; i < mesh.cells.size(); ++i) {
                const auto& c = mesh.cells[i];
                LOG_INFO("cell {:6d}: centroid=({: .6e}, {: .6e}) area={: .6e} nfaces={} nneighbors={}",
                         i, c.centroid.x(), c.centroid.y(), c.volume, c.faces.size(),
                         c.neighbors.size());
            }
        }
    }

    // --- 6. Barrier before solver (all ranks proceed together) ---------------
    if (size > 1) {
        MPI_Barrier(comm);
    }

    // ==========================================================================
    // Solver execution
    // ==========================================================================

    if (!restart_file.empty()) {
        LOG_WARN("Restart from file '{}' not yet implemented", restart_file);
    }

    LOG_INFO("Rank {}: Creating solver...", rank);
    cfd::Solver solver(config, mesh, comm);

    LOG_INFO("Rank {}: Initializing with freestream...", rank);
    solver.initialize();

    const auto run_type = config.run_control.type;
    if (run_type == cfd::RunType::Transient) {
        LOG_INFO("Rank {}: Starting BDF2 transient solve...", rank);
        solver.run_transient(output_dir);
    } else {
        LOG_INFO("Rank {}: Starting steady solve...", rank);
        solver.run_steady(output_dir);
    }

    LOG_INFO("Rank {}: Case '{}' completed. Output in: {}", rank, config.case_id, output_dir);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    // --- MPI initialization ---
    MPI_Init(&argc, &argv);

    install_cgns_error_handler();

    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    cfd::logging::rank() = rank;

    argparse::ArgumentParser program("cfd_solver", "2-D unstructured CFD solver");

    argparse::ArgumentParser solve_cmd("solve");
    solve_cmd.add_description("Run a CFD case from JSON configuration");
    solve_cmd.add_argument("--case")
        .help("Path to the case JSON file")
        .required();
    solve_cmd.add_argument("--output")
        .help("Directory for solver outputs")
        .required();
    solve_cmd.add_argument("--restart")
        .help("Optional restart file")
        .default_value(std::string(""));
    solve_cmd.add_argument("--report-level")
        .help("Diagnostic detail: brief (default) or full")
        .default_value(std::string("brief"));

    program.add_subparser(solve_cmd);

    int ret = 0;

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        LOG_ERROR("Argument error: {}", e.what());
        std::cerr << program << '\n';
        ret = 1;
        MPI_Finalize();
        return ret;
    }

    if (!program.is_subcommand_used("solve")) {
        LOG_ERROR("No subcommand given. Usage: cfd_solver solve --case <case-json> --output <dir>");
        std::cerr << program << '\n';
        MPI_Finalize();
        return 1;
    }

    try {
        const std::string case_path = solve_cmd.get<std::string>("--case");
        const std::string output_dir = solve_cmd.get<std::string>("--output");
        const std::string restart_file = solve_cmd.get<std::string>("--restart");
        const std::string report_level = solve_cmd.get<std::string>("--report-level");

        ret = run_solve(case_path, output_dir, restart_file, report_level, MPI_COMM_WORLD);
    } catch (const std::exception& e) {
        LOG_ERROR("{}", e.what());
        ret = 1;
    } catch (...) {
        LOG_ERROR("Unknown error");
        ret = 1;
    }

    MPI_Finalize();
    return ret;
}
