#include "output.hpp"
#include <nlohmann/json.hpp>
#include <mpi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace cfd {

using nlohmann::json;

namespace {

std::string now_utc() {
    // ISO-8601 UTC timestamp
    time_t t = time(nullptr);
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

void gather_cell_field(Solver& solver, int root, std::vector<real_t>& all,
                       int n_owned, MPI_Comm comm, int n_ranks, int rank,
                       std::vector<int>& counts, std::vector<int>& displs) {
    int n = n_owned;
    counts.assign(n_ranks, 0);
    MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
    displs.assign(n_ranks, 0);
    int total = 0;
    for (int r = 0; r < n_ranks; r++) { displs[r] = total; total += counts[r]; }
    const int NVAR = 11;  // 8 named variables + rank + centroid x/y + global id
    std::vector<real_t> local(n * NVAR);
    for (int i = 0; i < n; i++) {
        const auto& U = solver.state()[i];
        auto q = prims_from_conservative(U, solver.case_input().gamma, solver.case_input().R);
        real_t mach = q.a > 0 ? std::sqrt(q.u*q.u + q.v*q.v) / q.a : 0;
        local[i*NVAR+0] = U[0];
        local[i*NVAR+1] = U[1] / U[0];
        local[i*NVAR+2] = U[2] / U[0];
        local[i*NVAR+3] = q.p;
        local[i*NVAR+4] = mach;
        local[i*NVAR+5] = q.T;
        local[i*NVAR+6] = U[3];
        local[i*NVAR+7] = (real_t)rank;
        local[i*NVAR+8] = solver.mesh().owned_cells[i].centroid[0];
        local[i*NVAR+9] = solver.mesh().owned_cells[i].centroid[1];
        local[i*NVAR+10] = (real_t)solver.mesh().owned_to_global[i];
    }
    all.assign(total * NVAR, 0.0);
    std::vector<int> recvcounts(n_ranks), recvdispls(n_ranks);
    for (int r = 0; r < n_ranks; r++) {
        recvcounts[r] = counts[r] * NVAR;
        recvdispls[r] = displs[r] * NVAR;
    }
    MPI_Gatherv(local.data(), n * NVAR, MPI_DOUBLE, all.data(),
                recvcounts.data(), recvdispls.data(), MPI_DOUBLE, root, comm);
}

} // namespace

void write_field_vtu(Solver& solver, const std::string& path,
                     const Mesh& global_mesh, int rank) {
    const int n_owned = (int)solver.mesh().n_owned;
    std::vector<real_t> all;
    std::vector<int> counts, displs;
    gather_cell_field(solver, 0, all, n_owned, solver.comm(), solver.n_ranks(),
                      rank, counts, displs);
    if (rank != 0) return;

    const int NVAR = 11;
const idx_t n_cells = global_mesh.n_cells;
const idx_t n_points = (idx_t)global_mesh.nodes.size();
// Points are the cell centroids (n_cells entries) and each "cell" is a
// VTK_VERTEX, so the cell-centered field data lines up with the geometry.
// The gathered per-cell data is placed into global-cell-id order so the
// CellData arrays line up with the global mesh cells.
std::vector<real_t> by_gid(n_cells * NVAR, 0.0);
    const int total = (int)all.size() / NVAR;
    for (int i = 0; i < total; i++) {
        idx_t gid = (idx_t)all[i*NVAR+10];
        if (gid < 0 || gid >= n_cells) {
            throw std::runtime_error("field gather produced invalid global cell id");
        }
        for (int v = 0; v < NVAR; v++) by_gid[gid*NVAR+v] = all[i*NVAR+v];
    }

    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot write field file: " + path);
f << "<?xml version=\"1.0\"?>\n";
f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
f << "  <UnstructuredGrid>\n";
f << "    <Piece NumberOfPoints=\"" << n_cells << "\" NumberOfCells=\"" << n_cells << "\">\n";
f << "      <Points>\n";
f << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
for (idx_t c = 0; c < n_cells; c++) {
    f << global_mesh.cells[c].centroid[0] << " " << global_mesh.cells[c].centroid[1] << " 0\n";
}
f << "        </DataArray>\n      </Points>\n";
f << "      <Cells>\n";
f << "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n";
for (idx_t c = 0; c < n_cells; c++) {
    f << c << " ";
}
f << "\n        </DataArray>\n";
f << "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n";
for (idx_t c = 0; c < n_cells; c++) f << (c + 1) << " ";
f << "\n        </DataArray>\n";
f << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
for (idx_t c = 0; c < n_cells; c++) f << "1 ";
f << "\n        </DataArray>\n      </Cells>\n";
f << "      <CellData>\n";
    const char* names[8] = {"density","velocity_x","velocity_y","pressure",
                            "mach","temperature","total_energy","rank"};
    for (int v = 0; v < 8; v++) {
        f << "        <DataArray type=\"Float64\" Name=\"" << names[v] << "\" format=\"ascii\">\n";
        for (idx_t c = 0; c < n_cells; c++) f << by_gid[c*NVAR+v] << " ";
        f << "\n        </DataArray>\n";
    }
f << "      </CellData>\n";
f << "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n";
// n_points is intentionally unused in the centroid-based representation.
(void)n_points;
}

void write_restart(Solver& solver, const std::string& path, int rank, int n_ranks) {
    // Rank-local binary restart: magic, n_owned, then 4*n_owned doubles.
    const int n = (int)solver.mesh().n_owned;
    std::string fname = path;
    if (n_ranks > 1) {
        size_t dot = fname.find_last_of('.');
        if (dot == std::string::npos) fname += "." + std::to_string(rank);
        else fname = fname.substr(0, dot) + "." + std::to_string(rank) + fname.substr(dot);
    }
    std::ofstream f(fname, std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("cannot write restart file: " + fname);
    uint32_t magic = 0x43464452; // CFDR
    f.write((const char*)&magic, 4);
    f.write((const char*)&n, 4);
    std::vector<double> buf(n * 4);
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 4; c++) buf[i*4+c] = solver.state()[i][c];
    }
    f.write((const char*)buf.data(), n * 4 * sizeof(double));
}

void write_outputs(Solver& solver, const std::string& output_dir,
                   const CaseInput& ci, const LocalMesh& local_mesh,
                   const PartitionInfo& part, const Mesh& global_mesh,
                   const SteadyStats* steady, const TransientStats* transient,
                   const std::string& command_line, int rank, int n_ranks,
                   double wall_time, const std::string& start_utc,
                   const std::string& end_utc) {

    // metadata
    json md;
    md["case_id"] = ci.case_id;
    md["solver_name"] = "cfd_agentic_solver";
    md["solver_version"] = "1.0.0";
    md["git_revision"] = nullptr;
    md["mpi_ranks"] = n_ranks;
    md["mesh_file"] = ci.mesh_file;
    md["num_cells_global"] = global_mesh.n_cells;
    md["num_faces_global"] = global_mesh.n_faces;
    md["num_cells_owned_local"] = local_mesh.n_owned;
    md["num_cells_ghost_local"] = local_mesh.n_ghost;
    md["partitioner"] = "metis_kway";
    md["partition_edge_cut"] = part.edge_cut;
    md["halo_exchange"] = "neighbor_isend_irecv";
    md["full_state_replication_during_iterations"] = false;
    md["full_mesh_replication_during_iterations"] = false;
    md["equation_set"] = "compressible_navier_stokes_2d";
    md["inviscid_flux"] = "rusanov_llf";
    md["entropy_fix"] = nullptr;
md["viscous_flux"] = ci.physics_mode == "laminar" ? "central_gradient_navier_stokes" : "disabled";
md["time_integrator"] = ci.run_type == "transient" ? "bdf2_dual_time" : "pseudo_time_implicit";
md["implicit_solver"] = getenv("CFD_USE_LUSGS")
    ? "lusgs_symmetric_sweeps"
    : "block_jacobi_damped_sweeps";
    md["reconstruction"] = "piecewise_linear_weighted_least_squares";
    md["limiter"] = "barth_jespersen";
    md["spatial_order_claimed"] = 2;
    md["positivity_preservation"] = "reconstruction_positivity_fallback";
    md["wall_boundary_output_semantics"] = "boundary_value";
    md["true_bdf2_inner_loop"] = (ci.run_type == "transient");
    md["start_time_utc"] = start_utc;
    md["end_time_utc"] = end_utc;
    md["completed"] = true;

    if (steady) {
        md["convergence_status"] = steady->convergence_status;
    } else if (transient) {
        md["convergence_status"] = transient->convergence_status;
    } else {
        md["convergence_status"] = "failed";
    }

    if (ci.run_type == "transient") {
        md["min_inner_iterations"] = ci.min_inner_iterations;
        md["max_inner_iterations"] = ci.max_inner_iterations;
        md["observed_min_inner_iterations"] = transient->observed_min_inner;
        md["observed_max_inner_iterations"] = transient->observed_max_inner;
        md["inner_residual_reduction_target"] = ci.inner_residual_reduction_target;
        md["inner_target_misses"] = transient->inner_target_misses;
        md["inner_target_converged_fraction"] = transient->inner_target_converged_fraction;
        md["last_inner_residual_ratio"] = transient->last_inner_residual_ratio;
        md["typical_inner_iterations"] = transient->physical_steps_run > 0
            ? (double)transient->total_inner_iterations / (double)transient->physical_steps_run : 0.0;
    } else {
        md["min_inner_iterations"] = ci.min_inner_iterations;
        md["max_inner_iterations"] = ci.max_inner_iterations;
        md["observed_min_inner_iterations"] = steady->observed_min_inner;
        md["observed_max_inner_iterations"] = steady->observed_max_inner;
        md["inner_residual_reduction_target"] = ci.inner_residual_reduction_target;
        md["inner_target_misses"] = steady->inner_target_misses;
        md["inner_target_converged_fraction"] = steady->inner_target_converged_fraction;
        md["last_inner_residual_ratio"] = steady->last_inner_residual_ratio;
        md["typical_inner_iterations"] = steady->steps_run > 0
            ? (double)steady->total_inner_iterations / (double)steady->steps_run : 0.0;
    }

    if (rank == 0) {
        std::ofstream f(output_dir + "/metadata.json");
        f << md.dump(2) << "\n";
    }

    // run_status.json (rank 0)
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
        } else if (transient) {
            st["final_step"] = transient->physical_steps_run;
            st["final_physical_time"] =
                (double)transient->physical_steps_run * ci.time_step;
            st["convergence_status"] = transient->convergence_status;
            st["residual_reduction_orders"] = 0.0;
        }
        if (steady) st["notes"] = steady->notes;
        else if (transient) st["notes"] = "BDF2 dual-time transient run";
        else st["notes"] = "see metadata.json";
        std::ofstream f(output_dir + "/run_status.json");
        f << st.dump(2) << "\n";
    }

    // Partition diagnostics: collectives run on every rank, rank 0 writes.
    {
        int n_owned = (int)local_mesh.n_owned;
        int n_ghost = (int)local_mesh.n_ghost;
        int n_bnd = (int)local_mesh.n_boundary_faces;
        int n_nbr = (int)local_mesh.neighbors.size();
        std::vector<int> owned_all(n_ranks), ghost_all(n_ranks), bnd_all(n_ranks), nbr_all(n_ranks);
        MPI_Gather(&n_owned, 1, MPI_INT, owned_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_ghost, 1, MPI_INT, ghost_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_bnd, 1, MPI_INT, bnd_all.data(), 1, MPI_INT, 0, solver.comm());
        MPI_Gather(&n_nbr, 1, MPI_INT, nbr_all.data(), 1, MPI_INT, 0, solver.comm());

        constexpr int BUF = 16384;
        char nbuf[BUF] = {0}, sbuf[BUF] = {0}, rbuf[BUF] = {0};
        for (const auto& nb : local_mesh.neighbors) {
            snprintf(nbuf + strlen(nbuf), BUF - strlen(nbuf), "%lld;", (long long)nb.rank);
            snprintf(sbuf + strlen(sbuf), BUF - strlen(sbuf), "%zu;", nb.send_cells.size());
            snprintf(rbuf + strlen(rbuf), BUF - strlen(rbuf), "%zu;", nb.recv_cells.size());
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

    // field + restart + surface
    write_field_vtu(solver, output_dir + "/field_final.vtu", global_mesh, rank);
    write_restart(solver, output_dir + "/restart_final.bin", rank, n_ranks);

    MPI_Barrier(solver.comm());
}

} // namespace cfd
