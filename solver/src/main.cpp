#include <mpi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "case_input.hpp"
#include "distributed_mesh.hpp"
#include "global_mesh.hpp"
#include "partition.hpp"
#include "solver.hpp"

namespace fs = std::filesystem;

namespace {

struct CliOptions {
    std::string command = "solve";
    std::string case_path;
    std::string output_dir;
    std::string restart_file;
    std::string report_level = "full";
    int max_steps = -1;       // diagnostic override
    double final_time = -1.0; // diagnostic override
    double time_step = -1.0;  // diagnostic override
    double cfl_cap = -1.0;
    bool inspect_mesh = false;
};

void print_usage(const char* argv0) {
    std::printf(
        "usage: %s solve --case <case.json> --output <dir> "
        "[--restart <file>] [--report-level brief|full]\n"
        "       %s inspect-mesh --case <case.json>\n"
        "diagnostic options: --max-steps N --final-time T --time-step DT "
        "--cfl-cap C\n",
        argv0, argv0);
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions opt;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage(argv[0]);
        std::exit(2);
    }
    opt.command = args[0];
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "error: %s requires a value\n", flag);
                std::exit(2);
            }
            return args[++i];
        };
        if (a == "--case") {
            opt.case_path = next("--case");
        } else if (a == "--output") {
            opt.output_dir = next("--output");
        } else if (a == "--restart") {
            opt.restart_file = next("--restart");
        } else if (a == "--report-level") {
            opt.report_level = next("--report-level");
        } else if (a == "--max-steps") {
            opt.max_steps = std::atoi(next("--max-steps").c_str());
        } else if (a == "--final-time") {
            opt.final_time = std::atof(next("--final-time").c_str());
        } else if (a == "--time-step") {
            opt.time_step = std::atof(next("--time-step").c_str());
        } else if (a == "--cfl-cap") {
            opt.cfl_cap = std::atof(next("--cfl-cap").c_str());
        } else {
            std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
            std::exit(2);
        }
    }
    if (opt.command != "solve" && opt.command != "inspect-mesh") {
        std::fprintf(stderr, "error: unknown command '%s'\n",
                     opt.command.c_str());
        std::exit(2);
    }
    if (opt.case_path.empty()) {
        std::fprintf(stderr, "error: --case is required\n");
        std::exit(2);
    }
    if (opt.command == "solve" && opt.output_dir.empty()) {
        std::fprintf(stderr, "error: --output is required for solve\n");
        std::exit(2);
    }
    if (opt.report_level != "brief" && opt.report_level != "full") {
        std::fprintf(stderr, "error: --report-level must be brief or full\n");
        std::exit(2);
    }
    return opt;
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, nranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    int exit_code = 0;
    try {
        const CliOptions opt = parse_args(argc, argv);
        const std::string command_line =
            [&]() {
                std::string s;
                for (int i = 0; i < argc; ++i) {
                    if (i) s += ' ';
                    s += argv[i];
                }
                return s;
            }();

        cfd::CaseInput case_input = cfd::parse_case(opt.case_path);
        if (opt.max_steps > 0) case_input.run_control.max_steps_override =
            opt.max_steps;
        if (opt.final_time >= 0.0) case_input.run_control.final_time_override =
            opt.final_time;
        if (opt.time_step > 0.0) case_input.run_control.time_step_override =
            opt.time_step;
        if (opt.cfl_cap > 0.0) case_input.run_control.cfl_cap = opt.cfl_cap;

        if (opt.command == "inspect-mesh") {
            if (rank == 0) {
                cfd::GlobalMesh mesh =
                    cfd::read_cgns_mesh(case_input.mesh_file, case_input.bc_map);
                std::printf(
                    "case=%s\ncells=%d faces=%d boundary_faces=%d\n",
                    case_input.case_id.c_str(), mesh.num_cells(),
                    mesh.num_faces(), mesh.num_boundary_faces());
                std::map<cfd::BcType, int> bc_counts;
                for (const auto& f : mesh.boundary_faces) ++bc_counts[f.bc];
                for (const auto& [t, n] : bc_counts)
                    std::printf("  bc %-24s faces=%d\n", cfd::bc_to_string(t),
                                n);
            }
            MPI_Barrier(MPI_COMM_WORLD);
            MPI_Finalize();
            return 0;
        }

        // Serial preprocessing on rank 0: read + partition.
        cfd::GlobalMesh global;
        cfd::PartitionResult partition;
        if (rank == 0) {
            global = cfd::read_cgns_mesh(case_input.mesh_file,
                                         case_input.bc_map);
            partition =
                cfd::partition_cells_metis(global, nranks, 1u);
        }
        cfd::DistributedMesh dmesh =
            cfd::build_distributed_mesh(global, partition, MPI_COMM_WORLD);
        if (std::getenv("CFD_DEBUG_MESH") && rank == 0) {
            std::printf("[mesh-dbg] owned=%d ghost=%d local_faces=%zu\n",
                        dmesh.n_owned, dmesh.n_ghost, dmesh.faces.size());
        }

        cfd::Solver solver(std::move(case_input), std::move(dmesh),
                           MPI_COMM_WORLD);
        const cfd::RunStats stats = solver.run(opt.output_dir);
        solver.write_run_status(opt.output_dir, stats, command_line);

        if (rank == 0) {
            std::printf("status=%s wall_time=%.1fs final_step=%d\n",
                        stats.convergence_status.c_str(),
                        stats.wall_time_seconds, stats.final_step);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal error: %s\n", e.what());
        exit_code = 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return exit_code;
}
