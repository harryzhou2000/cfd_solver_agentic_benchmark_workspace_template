#include "io.hpp"
#include "mesh.hpp"
#include "solver.hpp"
#include "driver.hpp"
#include "output.hpp"
#include <mpi.h>
#include <argparse/argparse.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace cfd;

static bool load_restart_state(Solver& solver, const std::string& path,
                               int rank, int n_ranks) {
    std::string fname = path;
    if (n_ranks > 1) {
        size_t dot = fname.find_last_of('.');
        if (dot == std::string::npos) fname += "." + std::to_string(rank);
        else fname = fname.substr(0, dot) + "." + std::to_string(rank) + fname.substr(dot);
    }
    std::ifstream f(fname, std::ios::binary);
    if (!f.is_open()) return false;
    uint32_t magic = 0;
    int32_t n = 0;
    f.read((char*)&magic, 4);
    f.read((char*)&n, 4);
    if (magic != 0x43464452) throw std::runtime_error("bad restart magic in " + fname);
    if (n != (int32_t)solver.mesh().n_owned) {
        throw std::runtime_error("restart cell-count mismatch in " + fname);
    }
    std::vector<double> buf(n * 4);
    f.read((char*)buf.data(), n * 4 * sizeof(double));
    auto& U = solver.state();
    for (int32_t i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) U[i][c] = buf[i * 4 + c];
    }
    return true;
}

static std::string iso_now_utc() {
    time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, n_ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &n_ranks);

    int rc = 0;
    try {
        argparse::ArgumentParser program("cfd_solver");
        program.add_argument("solve").default_value(true).implicit_value(true);
        program.add_argument("--case").required();
        program.add_argument("--output").required();
        program.add_argument("--restart").default_value(std::string(""));
        program.add_argument("--report-level").default_value(std::string("brief"));
        std::vector<std::string> args(argv + 1, argv + argc);
        program.parse_args(args);

        std::string case_path = program.get<std::string>("--case");
        std::string out_dir = program.get<std::string>("--output");
        std::string restart_file = program.get<std::string>("--restart");

        if (rank == 0) {
            std::filesystem::create_directories(out_dir);
        }
        MPI_Barrier(MPI_COMM_WORLD);

        auto t0 = std::chrono::steady_clock::now();
        std::string start_utc = iso_now_utc();

        // Build the command line for run_status.json.
        std::string cmdline;
        for (int i = 0; i < argc; i++) {
            if (i) cmdline += " ";
            cmdline += argv[i];
        }

        CaseInput ci = load_case(case_path);
        Mesh global_mesh = read_cgns_mesh(ci.mesh_file);
        PartitionInfo part = partition_mesh_metis(global_mesh, n_ranks);
        LocalMesh local_mesh;
        build_local_mesh(global_mesh, part, rank, local_mesh);

        if (rank == 0) {
            std::cout << "case=" << ci.case_id << " ranks=" << n_ranks
                      << " owned=" << local_mesh.n_owned
                      << " ghost=" << local_mesh.n_ghost
                      << " edge_cut=" << part.edge_cut << std::endl;
        }

        Solver solver(ci, local_mesh, global_mesh, part, MPI_COMM_WORLD, rank, n_ranks);

        if (!restart_file.empty()) {
            if (!load_restart_state(solver, restart_file, rank, n_ranks)) {
                if (rank == 0) {
                    std::cerr << "WARNING: restart file not found: " << restart_file << std::endl;
                }
            }
        }

        SteadyStats steady;
        TransientStats transient;
        bool is_transient = (ci.run_type == "transient");

        if (is_transient) {
            transient = run_transient(solver, out_dir, case_path, rank, n_ranks);
        } else {
            steady = run_steady(solver, out_dir, case_path, rank, n_ranks);
        }

        // final field/surface/restart write must reflect the last state
        solver.exchange_state();
        // forces with surface gathering (collective on all ranks)
        solver.compute_forces(true);

        auto t1 = std::chrono::steady_clock::now();
        double wall_time = std::chrono::duration<double>(t1 - t0).count();
        std::string end_utc = iso_now_utc();

        write_outputs(solver, out_dir, ci, local_mesh, part, global_mesh,
                      is_transient ? nullptr : &steady,
                      is_transient ? &transient : nullptr,
                      cmdline, rank, n_ranks, wall_time, start_utc, end_utc);

        if (rank == 0) {
            std::cout << "run complete: " << ci.case_id
                      << " status=" << (is_transient ? transient.convergence_status : steady.convergence_status)
                      << " wall=" << wall_time << "s" << std::endl;
        }
    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "ERROR: " << e.what() << std::endl;
        }
        rc = 1;
    }

    MPI_Finalize();
    return rc;
}
