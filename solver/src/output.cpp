#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <mpi.h>
#include <nlohmann/json.hpp>

namespace cfd {

using nlohmann::json;

namespace {

void gather_field(Solver& solver, int root, std::vector<Real>& all,
                  int n_owned, MPI_Comm comm, int n_ranks, int rank,
                  std::vector<int>& counts, std::vector<int>& displs) {
    int n = n_owned;
    counts.assign(n_ranks, 0);
    MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
    displs.assign(n_ranks, 0);
    int total = 0;
    for (int r = 0; r < n_ranks; r++) { displs[r] = total; total += counts[r]; }
    const int NVAR = 11;
    std::vector<Real> local(n * NVAR);
    for (int i = 0; i < n; i++) {
        const auto& U = solver.state()[i];
        const auto q = prims_from_state(U, solver.case_input().gamma, solver.case_input().R);
        const Real mach = (q.a > 0) ? std::sqrt(q.u * q.u + q.v * q.v) / q.a : 0;
        local[i * NVAR + 0] = U[0];
        local[i * NVAR + 1] = U[1] / U[0];
        local[i * NVAR + 2] = U[2] / U[0];
        local[i * NVAR + 3] = q.p;
        local[i * NVAR + 4] = mach;
        local[i * NVAR + 5] = q.T;
        local[i * NVAR + 6] = U[3];
        local[i * NVAR + 7] = (Real)rank;
        local[i * NVAR + 8] = solver.mesh().owned[i].centroid[0];
        local[i * NVAR + 9] = solver.mesh().owned[i].centroid[1];
        local[i * NVAR + 10] = (Real)solver.mesh().owned_to_global[i];
    }
    all.assign(total * NVAR, 0.0);
    std::vector<int> rc(n_ranks), rd(n_ranks);
    for (int r = 0; r < n_ranks; r++) {
        rc[r] = counts[r] * NVAR;
        rd[r] = displs[r] * NVAR;
    }
    MPI_Gatherv(local.data(), n * NVAR, MPI_DOUBLE, all.data(),
                rc.data(), rd.data(), MPI_DOUBLE, root, comm);
}

} // anonymous namespace

void write_field_vtu(Solver& solver, const std::string& path,
                     const GlobalMesh& global, int rank) {
    const int n_owned = (int)solver.mesh().n_owned;
    std::vector<Real> all;
    std::vector<int> counts, displs;
    gather_field(solver, 0, all, n_owned, solver.comm(), solver.n_ranks(),
                 rank, counts, displs);
    if (rank != 0) return;

    const int NVAR = 11;
    const Index n_cells = global.n_cells;
    std::vector<Real> by_gid(n_cells * NVAR, 0.0);
    const int total = (int)all.size() / NVAR;
    for (int i = 0; i < total; i++) {
        const Index gid = (Index)all[i * NVAR + 10];
        if (gid < 0 || gid >= n_cells) continue;
        for (int v = 0; v < NVAR; v++) by_gid[gid * NVAR + v] = all[i * NVAR + v];
    }

    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot write field: " + path);
    f << "<?xml version=\"1.0\"?>\n";
    f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    f << "  <UnstructuredGrid>\n";
    f << "    <Piece NumberOfPoints=\"" << n_cells << "\" NumberOfCells=\"" << n_cells << "\">\n";
    f << "      <Points>\n";
    f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    for (Index c = 0; c < n_cells; c++) {
        f << global.cells[c].centroid[0] << " " << global.cells[c].centroid[1] << " 0\n";
    }
    f << "        </DataArray>\n      </Points>\n";
    f << "      <Cells>\n";
    f << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
    for (Index c = 0; c < n_cells; c++) f << c << " ";
    f << "\n        </DataArray>\n";
    f << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
    for (Index c = 0; c < n_cells; c++) f << (c + 1) << " ";
    f << "\n        </DataArray>\n";
    f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (Index c = 0; c < n_cells; c++) f << "1 ";
    f << "\n        </DataArray>\n      </Cells>\n";
    f << "      <CellData>\n";
    const char* names[8] = {"density","velocity_x","velocity_y","pressure",
                            "mach","temperature","total_energy","rank"};
    for (int v = 0; v < 8; v++) {
        f << "        <DataArray type=\"Float64\" Name=\"" << names[v] << "\" format=\"ascii\">\n";
        for (Index c = 0; c < n_cells; c++) f << by_gid[c * NVAR + v] << " ";
        f << "\n        </DataArray>\n";
    }
    f << "      </CellData>\n    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
}

void write_restart(Solver& solver, const std::string& path, int rank, int n_ranks) {
    const int n = (int)solver.mesh().n_owned;
    std::string fname = path;
    if (n_ranks > 1) {
        size_t dot = fname.find_last_of('.');
        if (dot == std::string::npos) fname += "." + std::to_string(rank);
        else fname = fname.substr(0, dot) + "." + std::to_string(rank) + fname.substr(dot);
    }
    std::ofstream f(fname, std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("cannot write restart: " + fname);
    uint32_t magic = 0x43464452;
    f.write((const char*)&magic, 4);
    f.write((const char*)&n, 4);
    std::vector<double> buf(n * 4);
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) buf[i * 4 + c] = solver.state()[i][c];
    }
    f.write((const char*)buf.data(), n * 4 * sizeof(double));
}

void write_outputs(Solver& solver, const std::string& output_dir,
                   const CaseInput& ci, const LocalMesh& lm,
                   const Partition& part, const GlobalMesh& gm,
                   const SteadyResult* steady,
                   const TransientResult* transient,
                   const std::string& command_line,
                   int rank, int n_ranks, double wall_time,
                   const std::string& start_utc,
                   const std::string& end_utc) {
    // metadata.json
    json md;
    md["case_id"] = ci.case_id;
    md["solver_name"] = "cfd_solver_2d";
    md["solver_version"] = "1.0.0";
    md["git_revision"] = nullptr;
    md["mpi_ranks"] = n_ranks;
    md["mesh_file"] = ci.mesh_file;
    md["num_cells_global"] = gm.n_cells;
    md["num_faces_global"] = gm.n_faces;
    md["num_cells_owned_local"] = lm.n_owned;
    md["num_cells_ghost_local"] = lm.n_ghost;
    md["partitioner"] = "metis_kway";
    md["partition_edge_cut"] = part.edge_cut;
    md["halo_exchange"] = "neighbor_isend_irecv";
    md["full_state_replication_during_iterations"] = false;
    md["full_mesh_replication_during_iterations"] = false;
    md["equation_set"] = "compressible_navier_stokes_2d";
    md["inviscid_flux"] = "rusanov_llf";
    md["entropy_fix"] = nullptr;
    md["viscous_flux"] = (ci.physics_mode == "laminar") ? "central_gradient_navier_stokes" : "disabled";
    md["time_integrator"] = (ci.run_type == "transient") ? "bdf2_dual_time" : "pseudo_time_implicit";
    md["implicit_solver"] = "lusgs_symmetric_sweeps";
    md["reconstruction"] = "piecewise_linear_weighted_least_squares";
    md["limiter"] = "barth_jespersen";
    md["spatial_order_claimed"] = 2;
    md["positivity_preservation"] = "reconstruction_positivity_fallback";
    md["wall_boundary_output_semantics"] = "boundary_value";
    md["true_bdf2_inner_loop"] = (ci.run_type == "transient");
    md["start_time_utc"] = start_utc;
    md["end_time_utc"] = end_utc;
    md["completed"] = true;

    if (steady) md["convergence_status"] = steady->convergence_status;
    else if (transient) md["convergence_status"] = transient->convergence_status;
    else md["convergence_status"] = "failed";

    const InnerStats* is = steady ? &steady->inner : (transient ? &transient->inner : nullptr);
    if (is) {
        md["min_inner_iterations"] = ci.min_inner_iterations;
        md["max_inner_iterations"] = ci.max_inner_iterations;
        md["observed_min_inner_iterations"] = is->observed_min;
        md["observed_max_inner_iterations"] = is->observed_max;
        md["inner_residual_reduction_target"] = ci.inner_residual_reduction_target;
        md["inner_target_misses"] = is->target_misses;
        md["inner_target_converged_fraction"] = (is->steps_counted > 0)
            ? 1.0 - (Real)is->target_misses / (Real)is->steps_counted : 0.0;
        md["last_inner_residual_ratio"] = is->last_ratio;
        md["typical_inner_iterations"] = (is->steps_counted > 0)
            ? (double)is->total_inner / (double)is->steps_counted : 0.0;
    }

    if (rank == 0) {
        std::ofstream f(output_dir + "/metadata.json");
        f << md.dump(2) << "\n";
    }

    // run_status.json
    if (rank == 0) {
        json st;
        st["case_id"] = ci.case_id;
        st["command"] = command_line;
        st["mpi_ranks"] = n_ranks;
        st["wall_time_seconds"] = wall_time;
        if (steady) {
            st["final_step"] = steady->steps_run;
            st["final_physical_time"] = 0.0;
            st["convergence_status"] = steady->convergence_status;
            st["residual_reduction_orders"] = steady->residual_reduction;
            st["notes"] = steady->notes;
        } else if (transient) {
            st["final_step"] = transient->physical_steps_run;
            st["final_physical_time"] = (double)transient->physical_steps_run * ci.time_step;
            st["convergence_status"] = transient->convergence_status;
            st["residual_reduction_orders"] = 0.0;
            st["notes"] = "BDF2 dual-time transient run";
        }
        std::ofstream f(output_dir + "/run_status.json");
        f << st.dump(2) << "\n";
    }

    // partition_diagnostics.csv
    {
        int n_owned = (int)lm.n_owned;
        int n_ghost = (int)lm.n_ghost;
        int n_bnd = (int)lm.n_boundary_faces;
        int n_nbr = (int)lm.neighbors.size();
        std::vector<int> owned_all(n_ranks), ghost_all(n_ranks), bnd_all(n_ranks), nbr_all(n_ranks);
        MPI_Gather(&n_owned, 1, MPI_INT, owned_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_ghost, 1, MPI_INT, ghost_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_bnd, 1, MPI_INT, bnd_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_nbr, 1, MPI_INT, nbr_all.data(), 1, MPI_INT, 0, solver.comm());

        constexpr int BUF = 16384;
        char nbuf[BUF] = {0}, sbuf[BUF] = {0}, rbuf[BUF] = {0};
        for (const auto& nb : lm.neighbors) {
            std::snprintf(nbuf + strlen(nbuf), BUF - strlen(nbuf), "%lld;", (long long)nb.rank);
            std::snprintf(sbuf + strlen(sbuf), BUF - strlen(sbuf), "%zu;", nb.send_cells.size());
            std::snprintf(rbuf + strlen(rbuf), BUF - strlen(rbuf), "%zu;", nb.recv_cells.size());
        }
        std::vector<char> nall(n_ranks * BUF), sall(n_ranks * BUF), rall(n_ranks * BUF);
        MPI_Gather(nbuf, BUF, MPI_CHAR, nall.data(), BUF, MPI_CHAR, 0, solver.comm());
        MPI_Gather(sbuf, BUF, MPI_CHAR, sall.data(), BUF, MPI_CHAR, 0, solver.comm());
        MPI_Gather(rbuf, BUF, MPI_CHAR, rall.data(), BUF, MPI_CHAR, 0, solver.comm());

        if (rank == 0) {
            std::ofstream f(output_dir + "/partition_diagnostics.csv");
            f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
            for (int r = 0; r < n_ranks; r++) {
                f << r << "," << owned_all[r] << "," << ghost_all[r] << ","
                  << bnd_all[r] << "," << nbr_all[r] << ","
                  << std::string(nall.data() + r * BUF) << ","
                  << std::string(sall.data() + r * BUF) << ","
                  << std::string(rall.data() + r * BUF) << "\n";
            }
        }
    }

    write_field_vtu(solver, output_dir + "/field_final.vtu", gm, rank);
    write_restart(solver, output_dir + "/restart_final.bin", rank, n_ranks);
    MPI_Barrier(solver.comm());
}

} // namespace cfd
