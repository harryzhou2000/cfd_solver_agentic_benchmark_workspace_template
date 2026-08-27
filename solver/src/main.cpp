#include "types.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"
#include "output.hpp"
#include "mpi_comm.hpp"
#include <nlohmann/json.hpp>
#include <mpi.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <cstring>

namespace fs = std::filesystem;
using json = nlohmann::json;

static BCType parse_bc_type(const std::string& s) {
    if (s == "farfield") return BCType::FARFIELD;
    if (s == "slip_wall") return BCType::SLIP_WALL;
    if (s == "no_slip_adiabatic_wall") return BCType::NO_SLIP_ADIABATIC_WALL;
    throw std::runtime_error("Unknown BC type: " + s);
}

static CaseConfig load_case(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open case file: " + path);
    json j;
    f >> j;

    CaseConfig cfg;
    cfg.case_id = j["case_id"];

    std::string mesh_rel = j["mesh"]["file"];
    fs::path case_dir = fs::path(path).parent_path();
    cfg.mesh_file = fs::canonical(case_dir / mesh_rel).string();

    std::string mode = j["physics"]["mode"];
    cfg.physics_mode = (mode == "inviscid") ? PhysicsMode::INVISCID : PhysicsMode::LAMINAR;
    if (j["physics"].contains("reynolds")) cfg.reynolds = j["physics"]["reynolds"];

    cfg.gas.gamma = j["gas"]["gamma"];
    cfg.gas.R = j["gas"]["R"];
    cfg.gas.Pr = j["gas"]["prandtl"];

    cfg.freestream.mach = j["freestream"]["mach"];
    cfg.freestream.aoa_deg = j["freestream"]["aoa_degrees"];
    cfg.freestream.rho = j["freestream"]["rho"];
    cfg.freestream.vel_mag = j["freestream"]["velocity_magnitude"];
    cfg.freestream.pressure = j["freestream"]["pressure"];

    cfg.reference.length = j["reference"]["length"];
    cfg.reference.area = j["reference"]["area"];
    cfg.reference.moment_center = Vec2(j["reference"]["moment_center"][0], j["reference"]["moment_center"][1]);
    cfg.reference.reynolds_length = j["reference"]["reynolds_length"];

    for (auto& [key, val] : j["boundary_conditions"].items()) {
        cfg.boundary_conditions[key] = parse_bc_type(val);
    }

    auto& rc = j["run_control"];
    std::string rtype = rc["type"];
    cfg.run_control.type = (rtype == "transient") ? RunType::TRANSIENT : RunType::STEADY;

    if (rc.contains("max_steps")) cfg.run_control.max_steps = rc["max_steps"];
    if (rc.contains("residual_reduction_target")) cfg.run_control.residual_reduction_target = rc["residual_reduction_target"];
    if (rc.contains("cfl_initial")) cfg.run_control.cfl_initial = rc["cfl_initial"];
    if (rc.contains("cfl_max")) cfg.run_control.cfl_max = rc["cfl_max"];
    if (rc.contains("pseudo_cfl_ramp_steps")) cfg.run_control.pseudo_cfl_ramp_steps = rc["pseudo_cfl_ramp_steps"];
    if (rc.contains("min_inner_iterations")) cfg.run_control.min_inner_iterations = rc["min_inner_iterations"];
    if (rc.contains("max_inner_iterations")) cfg.run_control.max_inner_iterations = rc["max_inner_iterations"];
    if (rc.contains("inner_residual_reduction_target")) cfg.run_control.inner_residual_reduction_target = rc["inner_residual_reduction_target"];
    if (rc.contains("time_step")) cfg.run_control.time_step = rc["time_step"];
    if (rc.contains("final_time")) cfg.run_control.final_time = rc["final_time"];
    if (rc.contains("rusanov_dissipation_scale")) cfg.run_control.rusanov_dissipation_scale = rc["rusanov_dissipation_scale"];

    auto& out = j["outputs"];
    if (out.contains("write_forces_every")) cfg.write_forces_every = out["write_forces_every"];
    if (out.contains("write_residuals_every")) cfg.write_residuals_every = out["write_residuals_every"];
    if (out.contains("write_field_every_time")) cfg.write_field_every_time = out["write_field_every_time"];

    return cfg;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, nranks;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    std::string case_file, output_dir;
    std::string report_level = "brief";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "solve") == 0) continue;
        if (strcmp(argv[i], "--case") == 0 && i + 1 < argc) { case_file = argv[++i]; continue; }
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) { output_dir = argv[++i]; continue; }
        if (strcmp(argv[i], "--report-level") == 0 && i + 1 < argc) { report_level = argv[++i]; continue; }
    }

    if (case_file.empty() || output_dir.empty()) {
        if (rank == 0) {
            std::cerr << "Usage: mpirun -np <N> cfd2d solve --case <case.json> --output <dir> [--report-level brief|full]\n";
        }
        MPI_Finalize();
        return 1;
    }

    try {
        CaseConfig config = load_case(case_file);
        if (rank == 0) {
            fprintf(stderr, "=== Case: %s ===\n", config.case_id.c_str());
            fprintf(stderr, "Mesh: %s\n", config.mesh_file.c_str());
            fprintf(stderr, "Physics: %s, Mach=%.2f\n",
                    config.physics_mode == PhysicsMode::INVISCID ? "inviscid" : "laminar",
                    config.freestream.mach);
            fprintf(stderr, "MPI ranks: %d\n", nranks);
        }

        Mesh global_mesh;
        PartitionInfo pinfo;

        if (rank == 0) {
            fprintf(stderr, "Reading mesh...\n");
            global_mesh = read_cgns_mesh(config.mesh_file, config.boundary_conditions);
            build_cell_face_adjacency(global_mesh);
            compute_geometry(global_mesh);
            fprintf(stderr, "Mesh: %d cells, %d faces, %d nodes\n",
                    (int)global_mesh.cells.size(), (int)global_mesh.faces.size(),
                    (int)global_mesh.nodes.size());

            fprintf(stderr, "Partitioning with METIS (%d parts)...\n", nranks);
            pinfo = partition_mesh(global_mesh, nranks);
            fprintf(stderr, "METIS edge cut: %d\n", pinfo.edge_cut);
        }

        int ncells_global = 0;
        if (rank == 0) ncells_global = (int)global_mesh.cells.size();
        MPI_Bcast(&ncells_global, 1, MPI_INT, 0, MPI_COMM_WORLD);

        if (rank != 0) pinfo.cell_partition.resize(ncells_global);
        MPI_Bcast(pinfo.cell_partition.data(), ncells_global, MPI_INT, 0, MPI_COMM_WORLD);

        int nfaces_global = 0, nnodes_global = 0;
        if (rank == 0) {
            nfaces_global = (int)global_mesh.faces.size();
            nnodes_global = (int)global_mesh.nodes.size();
        }

        int edge_cut_val = 0;
        if (rank == 0) edge_cut_val = pinfo.edge_cut;
        MPI_Bcast(&edge_cut_val, 1, MPI_INT, 0, MPI_COMM_WORLD);
        pinfo.edge_cut = edge_cut_val;

        {
            int nbg = 0;
            if (rank == 0) nbg = (int)global_mesh.boundary_groups.size();
            MPI_Bcast(&nbg, 1, MPI_INT, 0, MPI_COMM_WORLD);
            if (rank != 0) global_mesh.boundary_groups.resize(nbg);
            for (int b = 0; b < nbg; b++) {
                int nlen = 0;
                if (rank == 0) nlen = (int)global_mesh.boundary_groups[b].family_name.size();
                MPI_Bcast(&nlen, 1, MPI_INT, 0, MPI_COMM_WORLD);
                if (rank != 0) global_mesh.boundary_groups[b].family_name.resize(nlen);
                MPI_Bcast(&global_mesh.boundary_groups[b].family_name[0], nlen, MPI_CHAR, 0, MPI_COMM_WORLD);
                int bt = 0;
                if (rank == 0) bt = (int)global_mesh.boundary_groups[b].type;
                MPI_Bcast(&bt, 1, MPI_INT, 0, MPI_COMM_WORLD);
                if (rank != 0) global_mesh.boundary_groups[b].type = (BCType)bt;
            }
        }

        if (rank == 0) {
            MPI_Bcast(&nnodes_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
        } else {
            MPI_Bcast(&nnodes_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
            global_mesh.nodes.resize(nnodes_global);
        }
        {
            std::vector<double> coords(nnodes_global * 2);
            if (rank == 0) {
                for (int i = 0; i < nnodes_global; i++) {
                    coords[i*2] = global_mesh.nodes[i].x();
                    coords[i*2+1] = global_mesh.nodes[i].y();
                }
            }
            MPI_Bcast(coords.data(), nnodes_global * 2, MPI_DOUBLE, 0, MPI_COMM_WORLD);
            if (rank != 0) {
                for (int i = 0; i < nnodes_global; i++) {
                    global_mesh.nodes[i] = Vec2(coords[i*2], coords[i*2+1]);
                }
            }
        }

        {
            MPI_Bcast(&ncells_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
            if (rank != 0) global_mesh.cells.resize(ncells_global);

            std::vector<int> cell_sizes(ncells_global);
            std::vector<int> all_nodes;
            if (rank == 0) {
                for (int i = 0; i < ncells_global; i++) {
                    cell_sizes[i] = (int)global_mesh.cells[i].nodes.size();
                    for (int n : global_mesh.cells[i].nodes) all_nodes.push_back(n);
                }
            }
            MPI_Bcast(cell_sizes.data(), ncells_global, MPI_INT, 0, MPI_COMM_WORLD);

            int total_nodes_count = 0;
            if (rank == 0) total_nodes_count = (int)all_nodes.size();
            MPI_Bcast(&total_nodes_count, 1, MPI_INT, 0, MPI_COMM_WORLD);
            if (rank != 0) all_nodes.resize(total_nodes_count);
            MPI_Bcast(all_nodes.data(), total_nodes_count, MPI_INT, 0, MPI_COMM_WORLD);

            if (rank != 0) {
                int idx = 0;
                for (int i = 0; i < ncells_global; i++) {
                    global_mesh.cells[i].nodes.resize(cell_sizes[i]);
                    for (int k = 0; k < cell_sizes[i]; k++) {
                        global_mesh.cells[i].nodes[k] = all_nodes[idx++];
                    }
                    global_mesh.cells[i].global_id = i;
                }
            }
        }

        if (rank != 0) {
            global_mesh.num_cells_global = ncells_global;
            build_cell_face_adjacency(global_mesh);
            compute_geometry(global_mesh);
        }

        MPI_Bcast(&nfaces_global, 1, MPI_INT, 0, MPI_COMM_WORLD);
        global_mesh.num_faces_global = nfaces_global;

        if (rank == 0) fprintf(stderr, "Building local mesh...\n");
        LocalMesh local_mesh = build_local_mesh(global_mesh, pinfo, rank, nranks);
        fprintf(stderr, "Rank %d: %d owned, %d ghost, %d faces, %d neighbors\n",
                rank, local_mesh.num_owned, local_mesh.num_ghost,
                (int)local_mesh.mesh.faces.size(), (int)local_mesh.neighbor_ranks.size());

        MPI_Barrier(MPI_COMM_WORLD);

        Solver solver;
        solver.init(config, local_mesh, MPI_COMM_WORLD);

        if (rank == 0) fprintf(stderr, "Starting solve...\n");
        SolverStats stats = solver.run(output_dir);

        OutputWriter out;
        out.init(config, local_mesh, MPI_COMM_WORLD);
        out.write_partition_diagnostics(local_mesh, rank, nranks, output_dir, MPI_COMM_WORLD);

        if (rank == 0) {
            out.write_metadata(config, local_mesh, stats, output_dir);

            std::ostringstream cmd_ss;
            cmd_ss << "mpirun -np " << nranks << " cfd2d solve --case " << case_file << " --output " << output_dir;
            out.write_run_status(config, stats, output_dir, nranks, cmd_ss.str());

            std::ofstream log(output_dir + "/stdout.log");
            log << "Case: " << config.case_id << "\n";
            log << "MPI ranks: " << nranks << "\n";
            log << "Steps: " << stats.total_steps << "\n";
            log << "Wall time: " << stats.wall_time_seconds << " s\n";
            log << "Convergence: " << stats.convergence_status << "\n";
            log << "Residual reduction: " << stats.residual_reduction_orders << " orders\n";

            fprintf(stderr, "\n=== DONE: %s ===\n", config.case_id.c_str());
            fprintf(stderr, "Status: %s\n", stats.convergence_status.c_str());
            fprintf(stderr, "Steps: %d, Wall time: %.1f s\n", stats.total_steps, stats.wall_time_seconds);
            fprintf(stderr, "Residual reduction: %.2f orders\n", stats.residual_reduction_orders);
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "ERROR on rank %d: %s\n", rank, e.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    MPI_Finalize();
    return 0;
}
