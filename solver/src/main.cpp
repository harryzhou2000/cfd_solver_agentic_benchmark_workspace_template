#include "case_io.hpp"
#include "mesh.hpp"
#include "partition.hpp"
#include "numerics.hpp"
#include "driver.hpp"
#include "output.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <argparse/argparse.hpp>
#include <mpi.h>

using namespace cfd;

static std::string iso_now() {
    time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

static bool load_restart(Solver& solver, const std::string& path,
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
    if (magic != 0x43464452) throw std::runtime_error("bad restart magic");
    if (n != (int32_t)solver.mesh().n_owned) throw std::runtime_error("restart cell count mismatch");
    std::vector<double> buf(n * 4);
    f.read((char*)buf.data(), n * 4 * sizeof(double));
    auto& U = solver.state();
    for (int32_t i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) U[i][c] = buf[i * 4 + c];
    }
    return true;
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

        if (rank == 0) std::filesystem::create_directories(out_dir);
        MPI_Barrier(MPI_COMM_WORLD);

        auto t0 = std::chrono::steady_clock::now();
        std::string start_utc = iso_now();

        std::string cmdline;
        for (int i = 0; i < argc; i++) {
            if (i) cmdline += " ";
            cmdline += argv[i];
        }

        CaseInput ci = load_case(case_path);
        GlobalMesh gm = read_mesh(ci.mesh_file);
        Partition part = partition_mesh(gm, n_ranks);
        LocalMesh lm = build_local_mesh(gm, part, rank);

        if (rank == 0) {
            std::cout << "case=" << ci.case_id << " ranks=" << n_ranks
                      << " owned=" << lm.n_owned << " ghost=" << lm.n_ghost
                      << " edge_cut=" << part.edge_cut << std::endl;
        }

        Solver solver(ci, lm, gm, part, MPI_COMM_WORLD, rank, n_ranks);

        if (!restart_file.empty()) {
            if (!load_restart(solver, restart_file, rank, n_ranks)) {
                if (rank == 0) std::cerr << "WARNING: restart file not found: " << restart_file << "\n";
            }
        }

        SteadyResult steady;
        TransientResult transient;
        bool is_transient = (ci.run_type == "transient");

        if (is_transient) {
            transient = run_transient(solver, out_dir, case_path, rank, n_ranks);
        } else {
            steady = run_steady(solver, out_dir, case_path, rank, n_ranks);
        }

        solver.exchange_state();
        solver.compute_forces(true);

        auto t1 = std::chrono::steady_clock::now();
        double wall_time = std::chrono::duration<double>(t1 - t0).count();
        std::string end_utc = iso_now();

        write_outputs(solver, out_dir, ci, lm, part, gm,
                      is_transient ? nullptr : &steady,
                      is_transient ? &transient : nullptr,
                      cmdline, rank, n_ranks, wall_time, start_utc, end_utc);

        if (rank == 0) {
            std::cout << "run complete: " << ci.case_id
                      << " status=" << (is_transient ? transient.convergence_status : steady.convergence_status)
                      << " wall=" << wall_time << "s" << std::endl;
        }
    } catch (const std::exception& e) {
        if (rank == 0) std::cerr << "ERROR: " << e.what() << std::endl;
        rc = 1;
    }

    MPI_Finalize();
    return rc;
}
