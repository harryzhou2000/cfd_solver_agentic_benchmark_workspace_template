#include <mpi.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
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
    int start_step = -1;  // resume step for transient checkpoints
    bool inspect_mesh = false;
};

void print_usage(const char* argv0) {
    std::printf(
        "usage: %s solve --case <case.json> --output <dir> "
        "[--restart <file>] [--report-level brief|full]\n"
        "       %s inspect-mesh --case <case.json>\n"
        "diagnostic options: --max-steps N --final-time T --time-step DT "
        "--cfl-cap C --start-step N\n",
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
        } else if (a == "--start-step") {
            opt.start_step = std::atoi(next("--start-step").c_str());
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
        if (opt.start_step >= 0)
            case_input.run_control.start_step_override = opt.start_step;

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

        // Final runs redirect the rank-0 console stream into the case output
        // directory so `stdout.log` is part of the reproducible output
        // contract without requiring a wrapper script.
        if (rank == 0) {
            fs::create_directories(opt.output_dir);
            const std::string log_path = opt.output_dir + "/stdout.log";
            if (!std::freopen(log_path.c_str(), "w", stdout))
                throw std::runtime_error("cannot open stdout.log for writing");
            if (!std::freopen(log_path.c_str(), "a", stderr))
                throw std::runtime_error("cannot open stdout.log for stderr");
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
        if (!opt.restart_file.empty()) {
            // Restart format: a small JSON descriptor plus a binary sibling
            // containing all conservative states in global cell id order.
            std::vector<double> local_restart;
            std::vector<int> counts(nranks, 0);
            std::vector<int> displs(nranks, 0);
            std::vector<double> sendbuf;
            if (rank == 0) {
                fs::path json_path(opt.restart_file);
                std::ifstream jf(json_path);
                if (!jf)
                    throw std::runtime_error("cannot open restart descriptor: " +
                                             json_path.string());
                nlohmann::json desc;
                try {
                    jf >> desc;
                } catch (const std::exception& e) {
                    throw std::runtime_error(
                        "invalid restart descriptor " + json_path.string() +
                        ": " + e.what());
                }
                const int64_t ncells =
                    desc.value("n_cells_global", static_cast<int64_t>(-1));
                if (ncells != global.num_cells())
                    throw std::runtime_error(
                        "restart cell count does not match the case mesh");
                fs::path bin_path = json_path;
                bin_path.replace_extension(".bin");
                std::ifstream bf(bin_path, std::ios::binary);
                if (!bf)
                    throw std::runtime_error("cannot open restart state: " +
                                             bin_path.string());
                std::vector<double> all(ncells * cfd::kNC);
                bf.read(reinterpret_cast<char*>(all.data()),
                        static_cast<std::streamsize>(all.size() *
                                                     sizeof(double)));
                if (bf.gcount() != static_cast<std::streamsize>(
                                      all.size() * sizeof(double)))
                    throw std::runtime_error("truncated restart state file: " +
                                             bin_path.string());

                std::vector<std::vector<double>> bufs(nranks);
                for (int r = 0; r < nranks; ++r)
                    bufs[r].reserve(partition.cell_part.size() / nranks * 4 +
                                    nranks * 4);
                for (int g = 0; g < global.num_cells(); ++g) {
                    const int r = partition.cell_part[g];
                    ++counts[r];
                    for (int v = 0; v < cfd::kNC; ++v)
                        bufs[r].push_back(all[g * cfd::kNC + v]);
                }
                sendbuf.reserve(static_cast<size_t>(ncells) * cfd::kNC);
                for (int r = 0; r < nranks; ++r) {
                    displs[r] = static_cast<int>(sendbuf.size());
                    sendbuf.insert(sendbuf.end(), bufs[r].begin(),
                                   bufs[r].end());
                }
            }
            MPI_Bcast(counts.data(), nranks, MPI_INT, 0, MPI_COMM_WORLD);
            MPI_Bcast(displs.data(), nranks, MPI_INT, 0, MPI_COMM_WORLD);
            local_restart.resize(counts[rank] * cfd::kNC);
            std::vector<int> rcounts(nranks), rdispls(nranks, 0);
            for (int r = 0; r < nranks; ++r)
                rcounts[r] = counts[r] * cfd::kNC;
            for (int r = 1; r < nranks; ++r)
                rdispls[r] = rdispls[r - 1] + rcounts[r - 1];
            MPI_Scatterv(sendbuf.data(), rcounts.data(), rdispls.data(),
                         MPI_DOUBLE, local_restart.data(), rcounts[rank],
                         MPI_DOUBLE, 0, MPI_COMM_WORLD);

            // The binary arrives in ascending global cell id order. Map it
            // back to the rank-local ordering.
            solver.set_restart_state_global_order(local_restart);
        }
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
