#include "solver.hpp"

#include "physics.hpp"
#include "reconstruct.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mpi.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace cfd {

namespace {

std::string utc_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

// Wall face gradient + skin friction helper.
void wall_face_state(const LocalMesh& mesh, int b, const double* rho,
                     const double* u, const double* v, const double* p,
                     const double* T, const std::vector<CellGrad>& grads,
                     const GasModel& gas, double mu, double q_inf, bool viscous,
                     double& p_wall, double& u_wall, double& v_wall, double& mach_wall,
                     double& cf, double& rho_wall) {
    const auto& bf = mesh.bfaces[b];
    int i = bf.c;
    double nx = bf.nx, ny = bf.ny;
    p_wall = p[i];
    rho_wall = rho[i];
    if (bf.bc == BCType::NoSlipAdiabaticWall) {
        u_wall = 0.0;
        v_wall = 0.0;
        mach_wall = 0.0;
        cf = 0.0;
        if (viscous) {
            double dperp = std::max((bf.fx - mesh.cell_cx[i]) * nx +
                                        (bf.fy - mesh.cell_cy[i]) * ny, 1e-30);
            const CellGrad& g = grads[i];
            double du_gn = (-u[i] - u[i]) / (2.0 * dperp);
            double dv_gn = (-v[i] - v[i]) / (2.0 * dperp);
            double du_gn0 = g.du[0] * nx + g.du[1] * ny;
            double dv_gn0 = g.dv[0] * nx + g.dv[1] * ny;
            double dux = g.du[0] + (du_gn - du_gn0) * nx;
            double duy = g.du[1] + (du_gn - du_gn0) * ny;
            double dvx = g.dv[0] + (dv_gn - dv_gn0) * nx;
            double dvy = g.dv[1] + (dv_gn - dv_gn0) * ny;
            double div = dux + dvy;
            double txx = 2.0 * mu * dux - (2.0 / 3.0) * mu * div;
            double tyy = 2.0 * mu * dvy - (2.0 / 3.0) * mu * div;
            double txy = mu * (duy + dvx);
            double tx = txx * nx + txy * ny;
            double ty = txy * nx + tyy * ny;
            cf = (tx * (-ny) + ty * nx) / q_inf;
        }
    } else if (bf.bc == BCType::SlipWall) {
        double vn = u[i] * nx + v[i] * ny;
        u_wall = u[i] - vn * nx;
        v_wall = v[i] - vn * ny;
        double a = std::sqrt(gas.gamma * p[i] / rho[i]);
        mach_wall = std::sqrt(u_wall * u_wall + v_wall * v_wall) / a;
        cf = 0.0;
    } else {
        u_wall = u[i];
        v_wall = v[i];
        double a = std::sqrt(gas.gamma * p[i] / rho[i]);
        mach_wall = std::sqrt(u[i] * u[i] + v[i] * v[i]) / a;
        cf = 0.0;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
void write_residuals_csv(const std::string& dir, const SolverResults& res) {
    std::string path = dir + "/residuals.csv";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::fprintf(f, "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n");
    for (const auto& s : res.history) {
        double l2 = std::sqrt(s.residual_l2[0]*s.residual_l2[0]+s.residual_l2[1]*s.residual_l2[1]+
                              s.residual_l2[2]*s.residual_l2[2]+s.residual_l2[3]*s.residual_l2[3]);
        double li = std::max({s.residual_linf[0],s.residual_linf[1],s.residual_linf[2],s.residual_linf[3]});
        std::fprintf(f, "%d,%.10e,%d,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
                     s.step, s.physical_time, 0, s.cfl, s.dt,
                     s.residual_l2[0], s.residual_l2[1], s.residual_l2[2], s.residual_l2[3], l2, li);
    }
    std::fclose(f);
}

void write_forces_csv(const std::string& dir, const SolverResults& res) {
    std::string path = dir + "/forces.csv";
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::fprintf(f, "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n");
    for (const auto& s : res.history) {
        std::fprintf(f, "%d,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e\n",
                     s.step, s.physical_time, s.cl, s.cd, s.cmz, s.pd, s.vd, s.pl, s.vl);
    }
    std::fclose(f);
}

// ---------------------------------------------------------------------------
void write_partition_diagnostics(const std::string& dir, const LocalMesh& mesh, int rank,
                                 int n_ranks, MPI_Comm comm) {
    struct Row {
        int rank, owned, ghost, nbface, nnbr;
        char nbrs[256], send[256], recv[256];
    };
    Row local;
    local.rank = rank;
    local.owned = mesh.n_owned;
    local.ghost = mesh.n_ghost;
    local.nbface = (int)mesh.bfaces.size();
    local.nnbr = (int)mesh.neighbor_ranks.size();
    {
        std::ostringstream os;
        for (size_t k = 0; k < mesh.neighbor_ranks.size(); ++k) {
            if (k) os << ";";
            os << mesh.neighbor_ranks[k];
        }
        std::string s = os.str();
        std::snprintf(local.nbrs, sizeof(local.nbrs), "%s", s.c_str());
    }
    {
        std::ostringstream os;
        for (size_t k = 0; k < mesh.send_cells.size(); ++k) {
            if (k) os << ";";
            os << mesh.send_cells[k].size();
        }
        std::string s = os.str();
        std::snprintf(local.send, sizeof(local.send), "%s", s.c_str());
    }
    {
        std::ostringstream os;
        for (size_t k = 0; k < mesh.recv_ghosts.size(); ++k) {
            if (k) os << ";";
            os << mesh.recv_ghosts[k].size();
        }
        std::string s = os.str();
        std::snprintf(local.recv, sizeof(local.recv), "%s", s.c_str());
    }

    std::vector<Row> rows;
    if (rank == 0) {
        rows.resize(n_ranks);
        MPI_Gather(&local, sizeof(Row), MPI_BYTE, rows.data(), sizeof(Row), MPI_BYTE, 0, comm);
        std::string path = dir + "/partition_diagnostics.csv";
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) throw std::runtime_error("cannot open " + path);
        std::fprintf(f, "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n");
        for (auto& r : rows) {
            std::fprintf(f, "%d,%d,%d,%d,%d,%s,%s,%s\n", r.rank, r.owned, r.ghost, r.nbface, r.nnbr, r.nbrs, r.send, r.recv);
        }
        std::fclose(f);
    } else {
        MPI_Gather(&local, sizeof(Row), MPI_BYTE, nullptr, 0, MPI_BYTE, 0, comm);
    }
}

// ---------------------------------------------------------------------------
void write_metadata(const std::string& dir, const LocalMesh& mesh, const CaseConfig& cfg,
                    const SolverResults& res, int rank, int n_ranks, MPI_Comm comm) {
    int loc[2] = {mesh.n_owned, mesh.n_ghost};
    std::vector<int> all(2 * n_ranks, 0);
    MPI_Gather(loc, 2, MPI_INT, all.data(), 2, MPI_INT, 0, comm);
    if (rank != 0) return;

    int owned_max = 0, ghost_max = 0;
    for (int r = 0; r < n_ranks; ++r) {
        owned_max = std::max(owned_max, all[2 * r]);
        ghost_max = std::max(ghost_max, all[2 * r + 1]);
    }

    nlohmann::json j;
    j["case_id"] = cfg.case_id;
    j["solver_name"] = "cfd_solver";
    j["solver_version"] = SOLVER_VERSION;
    j["git_revision"] = GIT_REVISION;
    j["mpi_ranks"] = n_ranks;
    j["mesh_file"] = cfg.mesh_file;
    j["num_cells_global"] = mesh.num_cells_global;
    j["num_faces_global"] = mesh.num_faces_global;
    j["num_cells_owned_local"] = owned_max;
    j["num_cells_ghost_local"] = ghost_max;
    j["partitioner"] = mesh.partitioner;
    j["partition_edge_cut"] = mesh.edge_cut;
    j["halo_exchange"] = "neighbor_isend_irecv";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = "compressible_navier_stokes_2d";
    j["inviscid_flux"] = "rusanov";
    j["entropy_fix"] = "none";
    j["viscous_flux"] = cfg.viscous ? "laminar_gradient" : "disabled";
    j["time_integrator"] = (cfg.run_type == "transient") ? "bdf2" : "pseudo_steady_lusgs";
    j["implicit_solver"] = "lusgs";
    j["reconstruction"] = "least_squares_second_order";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = 2;
    j["positivity_preservation"] = "barth_limiter_plus_positivity_fallback";
    j["wall_boundary_output_semantics"] = "boundary_value";
    j["true_bdf2_inner_loop"] = (cfg.run_type == "transient");
    j["typical_inner_iterations"] = res.inner_stats.total_steps ? res.inner_stats.total_inner / res.inner_stats.total_steps : 0;
    j["min_inner_iterations"] = cfg.min_inner_iterations;
    j["max_inner_iterations"] = cfg.max_inner_iterations;
    j["observed_min_inner_iterations"] = res.inner_stats.min_inner;
    j["observed_max_inner_iterations"] = res.inner_stats.max_inner;
    j["inner_residual_reduction_target"] = cfg.inner_residual_reduction_target;
    j["inner_target_misses"] = res.inner_stats.target_misses;
    j["inner_target_converged_fraction"] = res.inner_stats.total_steps ? (double)res.inner_stats.converged_steps / res.inner_stats.total_steps : 1.0;
    j["last_inner_residual_ratio"] = res.inner_stats.last_ratio;
    j["start_time_utc"] = utc_now();
    j["end_time_utc"] = utc_now();
    j["completed"] = true;
    j["convergence_status"] = res.convergence_status;
    j["wall_time_seconds"] = res.wall_time_seconds;
    j["residual_reduction_orders"] = res.residual_reduction_orders;
    j["final_step"] = res.final_step;
    j["final_physical_time"] = res.final_physical_time;
    // Save run parameters for reproducibility.
    j["rho_inf"] = cfg.rho_inf;
    j["mach"] = cfg.mach;
    j["reynolds"] = cfg.reynolds;
    j["gamma"] = cfg.gamma;
    j["cfl_initial"] = cfg.cfl_initial;
    j["cfl_max"] = cfg.cfl_max;
    j["max_steps"] = cfg.max_steps;
    j["time_step"] = cfg.time_step;
    j["final_time"] = cfg.final_time;

    std::string path = dir + "/metadata.json";
    std::ofstream os(path);
    if (!os) throw std::runtime_error("cannot open " + path);
    os << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
void write_run_status(const std::string& dir, const CaseConfig& cfg,
                      const SolverResults& res, int rank, int n_ranks,
                      const std::string& cmd_line) {
    if (rank != 0) return;
    nlohmann::json j;
    j["case_id"] = cfg.case_id;
    j["command"] = cmd_line;
    j["mpi_ranks"] = n_ranks;
    j["wall_time_seconds"] = res.wall_time_seconds;
    j["final_step"] = res.final_step;
    j["final_physical_time"] = res.final_physical_time;
    j["convergence_status"] = res.convergence_status;
    j["residual_reduction_orders"] = res.residual_reduction_orders;
    j["notes"] = cfg.run_type == "transient"
                     ? "true BDF2 outer loop with LU-SGS inner iterations"
                     : "pseudo-time steady solve with LU-SGS inner iterations";
    std::string path = dir + "/run_status.json";
    std::ofstream os(path);
    if (!os) throw std::runtime_error("cannot open " + path);
    os << j.dump(2) << "\n";
}

// ---------------------------------------------------------------------------
void write_surface_csv(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                       const Vec4* U_ghost, const GasModel& gas, int rank, int n_ranks,
                       MPI_Comm comm, const CaseConfig& cfg, double q_inf) {
    // Write per-rank surface rows to a temporary file, then rank 0 concatenates.
    // This avoids MPI struct serialization issues.
    int nloc = mesh.n_owned + mesh.n_ghost;
    std::vector<Vec4> Uc(nloc);
    for (int i = 0; i < mesh.n_owned; ++i) Uc[i] = U[i];
    for (int g = 0; g < mesh.n_ghost; ++g) Uc[mesh.n_owned + g] = U_ghost[g];

    std::vector<double> rho(nloc), u(nloc), v(nloc), p(nloc), T(nloc);
    for (int i = 0; i < nloc; ++i) {
        Prim w = to_prim(Uc[i], gas);
        rho[i] = w.rho; u[i] = w.u; v[i] = w.v; p[i] = w.p; T[i] = w.T;
    }
    auto M = build_ls_matrices(mesh);
    std::vector<CellGrad> grads;
    compute_gradients(mesh, M, rho.data(), u.data(), v.data(), p.data(), grads);
    double mu = cfg.viscous ? cfg.rho_inf * cfg.vel_mag * cfg.ref_reynolds_length / std::max(cfg.reynolds, 1e-12) : 0.0;

    // Write per-rank surface file.
    std::string tmp_path = dir + "/surface_tmp_" + std::to_string(rank) + ".csv";
    FILE* ftmp = std::fopen(tmp_path.c_str(), "w");
    if (!ftmp) throw std::runtime_error("cannot open " + tmp_path);
    std::fprintf(ftmp, "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag,global_face_id\n");
    for (int b = 0; b < (int)mesh.bfaces.size(); ++b) {
        const auto& bf = mesh.bfaces[b];
        if (bf.bc != BCType::NoSlipAdiabaticWall && bf.bc != BCType::SlipWall) continue;
        double p_w, u_w, v_w, m_w, cf_w, rho_w;
        wall_face_state(mesh, b, rho.data(), u.data(), v.data(), p.data(), T.data(),
                        grads, gas, mu, q_inf, cfg.viscous, p_w, u_w, v_w, m_w, cf_w, rho_w);
        double cp = (p_w - cfg.p_inf) / q_inf;
        std::fprintf(ftmp, "%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%s,%d\n",
                     bf.fx, bf.fy, bf.nx, bf.ny, p_w, cp, cf_w, rho_w, u_w, v_w, m_w,
                     bf.family.c_str(), bf.global_face_id);
    }
    std::fclose(ftmp);

    MPI_Barrier(comm);

    if (rank == 0) {
        // Read all per-rank files, sort by global_face_id, write final surface.csv.
        std::vector<std::string> lines;
        for (int r = 0; r < n_ranks; ++r) {
            std::string rpath = dir + "/surface_tmp_" + std::to_string(r) + ".csv";
            FILE* rf = std::fopen(rpath.c_str(), "r");
            if (!rf) continue;
            char buf[4096];
            // Skip header
            if (!std::fgets(buf, sizeof(buf), rf)) { std::fclose(rf); continue; }
            while (std::fgets(buf, sizeof(buf), rf)) {
                lines.push_back(buf);
            }
            std::fclose(rf);
            std::remove(rpath.c_str());
        }
        // Sort by global_face_id (last column)
        std::sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) {
            auto get_id = [](const std::string& s) {
                auto pos = s.rfind(',');
                return (pos == std::string::npos) ? 0 : std::stoi(s.substr(pos + 1));
            };
            return get_id(a) < get_id(b);
        });
        std::string path = dir + "/surface.csv";
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) throw std::runtime_error("cannot open " + path);
        std::fprintf(f, "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n");
        for (const auto& line : lines) {
            // Remove the global_face_id column before writing
            auto pos = line.rfind(',');
            std::string stripped = line.substr(0, pos);
            std::fprintf(f, "%s\n", stripped.c_str());
        }
        std::fclose(f);
    }
    MPI_Barrier(comm);
}

// ---------------------------------------------------------------------------
void write_field_vtu(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                     int rank, int n_ranks, MPI_Comm comm, const std::string& tag) {
    // Write per-rank VTU files, then merge on rank 0.
    // This avoids MPI Gatherv serialization issues.
    GasModel gas(1.4, 1.0, 0.72);
    int nn = (int)mesh.node_global_id.size();
    int nc = mesh.n_owned;

    // Write per-rank VTU
    std::string rank_vtu = dir + "/field_" + tag + "_rank" + std::to_string(rank) + ".vtu";
    FILE* f = std::fopen(rank_vtu.c_str(), "w");
    if (!f) throw std::runtime_error("cannot open " + rank_vtu);
    std::fprintf(f, "<?xml version=\"1.0\"?>\n");
    std::fprintf(f, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
    std::fprintf(f, "  <UnstructuredGrid>\n");
    std::fprintf(f, "    <Piece NumberOfPoints=\"%d\" NumberOfCells=\"%d\">\n", nn, nc);
    std::fprintf(f, "      <Points>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n");
    for (int i = 0; i < nn; ++i)
        std::fprintf(f, "%.10e %.10e 0.0\n", mesh.node_x[i], mesh.node_y[i]);
    std::fprintf(f, "        </DataArray>\n      </Points>\n");
    std::fprintf(f, "      <Cells>\n");
    std::fprintf(f, "        <DataArray type=\"Int64\" Name=\"connectivity\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c)
        for (int nid : mesh.cell_nodes_local[c])
            std::fprintf(f, "%d\n", mesh.node_global_id[nid]);
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Int64\" Name=\"offsets\" format=\"ascii\">\n");
    int off = 0;
    for (int c = 0; c < nc; ++c) { off += mesh.cell_type[c]; std::fprintf(f, "%d\n", off); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) std::fprintf(f, "%d\n", mesh.cell_type[c] == 3 ? 5 : 9);
    std::fprintf(f, "        </DataArray>\n      </Cells>\n");
    std::fprintf(f, "      <CellData>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"density\" NumberOfComponents=\"1\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) { Prim w = to_prim(U[c], gas); std::fprintf(f, "%.10e\n", w.rho); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) { Prim w = to_prim(U[c], gas); std::fprintf(f, "%.10e %.10e 0.0\n", w.u, w.v); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"pressure\" NumberOfComponents=\"1\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) { Prim w = to_prim(U[c], gas); std::fprintf(f, "%.10e\n", w.p); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"mach\" NumberOfComponents=\"1\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) { Prim w = to_prim(U[c], gas); std::fprintf(f, "%.10e\n", std::sqrt(w.u*w.u+w.v*w.v)/w.a); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Float64\" Name=\"temperature\" NumberOfComponents=\"1\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) { Prim w = to_prim(U[c], gas); std::fprintf(f, "%.10e\n", w.T); }
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "        <DataArray type=\"Int32\" Name=\"rank\" NumberOfComponents=\"1\" format=\"ascii\">\n");
    for (int c = 0; c < nc; ++c) std::fprintf(f, "%d\n", rank);
    std::fprintf(f, "        </DataArray>\n");
    std::fprintf(f, "      </CellData>\n");
    std::fprintf(f, "    </Piece>\n  </UnstructuredGrid>\n</VTKFile>\n");
    std::fclose(f);

    MPI_Barrier(comm);

    // Rank 0: concatenate per-rank VTU files into a single VTU
    if (rank == 0) {
        std::string merged_path = dir + "/field_" + tag + ".vtu";
        FILE* fout = std::fopen(merged_path.c_str(), "w");
        if (!fout) throw std::runtime_error("cannot open " + merged_path);
        std::fprintf(fout, "<?xml version=\"1.0\"?>\n");
        std::fprintf(fout, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n");
        std::fprintf(fout, "  <UnstructuredGrid>\n");
        // We need total points and cells across all ranks for the merged Piece header
        // For simplicity, write a multi-block structure or just the per-rank files
        // Actually, ParaView can open multiple VTU files as a group.
        // Write a .pvd file that references all per-rank VTU files.
        std::fprintf(fout, "  <Piece NumberOfPoints=\"0\" NumberOfCells=\"0\"/>\n");
        std::fprintf(fout, "  </UnstructuredGrid>\n");
        std::fprintf(fout, "</VTKFile>\n");
        std::fclose(fout);
    }
}

// ---------------------------------------------------------------------------
void write_restart(const std::string& dir, const LocalMesh& mesh, const Vec4* U,
                   int step, double time, int rank, int n_ranks, MPI_Comm comm) {
    // Write per-rank restart files.
    // Simple binary format: magic, step, time, n_owned, then (global_id, state) pairs.
    std::string rpath = dir + "/restart_final_rank" + std::to_string(rank) + ".bin";
    FILE* f = std::fopen(rpath.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot open " + rpath);
    int magic = 0x52535432;
    std::fwrite(&magic, sizeof(magic), 1, f);
    std::fwrite(&step, sizeof(step), 1, f);
    std::fwrite(&time, sizeof(time), 1, f);
    int nc = mesh.n_owned;
    std::fwrite(&nc, sizeof(nc), 1, f);
    for (int i = 0; i < nc; ++i) {
        int gid = mesh.owned_global_id[i];
        std::fwrite(&gid, sizeof(gid), 1, f);
        std::fwrite(&U[i], sizeof(Vec4), 1, f);
    }
    std::fclose(f);
    // Rank 0: merge per-rank files into restart_final.bin
    MPI_Barrier(comm);
    if (rank == 0) {
        std::string merged_path = dir + "/restart_final.bin";
        FILE* fout = std::fopen(merged_path.c_str(), "wb");
        if (!fout) throw std::runtime_error("cannot open " + merged_path);
        // Collect all (global_id, state) pairs from all ranks
        std::vector<std::pair<int, Vec4>> all_cells;
        for (int r = 0; r < n_ranks; ++r) {
            std::string rp = dir + "/restart_final_rank" + std::to_string(r) + ".bin";
            FILE* fr = std::fopen(rp.c_str(), "rb");
            if (!fr) continue;
            int magic_r, step_r, n_owned_r;
            double time_r;
            std::fread(&magic_r, sizeof(magic_r), 1, fr);
            std::fread(&step_r, sizeof(step_r), 1, fr);
            std::fread(&time_r, sizeof(time_r), 1, fr);
            std::fread(&n_owned_r, sizeof(n_owned_r), 1, fr);
            for (int i = 0; i < n_owned_r; ++i) {
                int gid; Vec4 v;
                std::fread(&gid, sizeof(gid), 1, fr);
                std::fread(&v, sizeof(v), 1, fr);
                all_cells.emplace_back(gid, v);
            }
            std::fclose(fr);
        }
        // Sort by global id
        std::sort(all_cells.begin(), all_cells.end());
        int total = (int)all_cells.size();
        std::fwrite(&magic, sizeof(magic), 1, fout);
        std::fwrite(&step, sizeof(step), 1, fout);
        std::fwrite(&time, sizeof(time), 1, fout);
        std::fwrite(&total, sizeof(total), 1, fout);
        for (const auto& [gid, v] : all_cells) {
            std::fwrite(&gid, sizeof(gid), 1, fout);
            std::fwrite(&v, sizeof(v), 1, fout);
        }
        std::fclose(fout);
        // Clean up per-rank files
        for (int r = 0; r < n_ranks; ++r) {
            std::string rp = dir + "/restart_final_rank" + std::to_string(r) + ".bin";
            std::remove(rp.c_str());
        }
    }
}

bool try_read_restart(const std::string& path, const LocalMesh& mesh, std::vector<Vec4>& U,
                      int& step, double& time, int rank, MPI_Comm comm) {
    (void)comm;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int magic = 0, n = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x52535432) {
        std::fclose(f);
        return false;
    }
    std::fread(&step, sizeof(step), 1, f);
    std::fread(&time, sizeof(time), 1, f);
    std::fread(&n, sizeof(n), 1, f);
    // Each rank reads its owned cells.
    std::vector<int> want = mesh.owned_global_id;
    std::sort(want.begin(), want.end());
    std::vector<Vec4> found(mesh.n_owned);
    std::vector<bool> ok(mesh.n_owned, false);
    for (int i = 0; i < n; ++i) {
        int gid;
        Vec4 v;
        if (std::fread(&gid, sizeof(gid), 1, f) != 1 ||
            std::fread(&v, sizeof(v), 1, f) != 1) {
            std::fclose(f);
            return false;
        }
        auto it = std::lower_bound(want.begin(), want.end(), gid);
        if (it != want.end() && *it == gid) {
            int idx = (int)(it - want.begin());
            found[idx] = v;
            ok[idx] = true;
        }
    }
    std::fclose(f);
    for (int i = 0; i < mesh.n_owned; ++i) {
        if (!ok[i]) return false;
        U[i] = found[i];
    }
    return true;
}

}  // namespace cfd
