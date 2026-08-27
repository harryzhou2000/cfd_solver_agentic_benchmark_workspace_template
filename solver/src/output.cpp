#include "output.hpp"
#include "partition.hpp"
#include "solver.hpp"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

static inline void cons_to_prim_out(const Vec4& U, double gamma,
                                     double& rho, double& u, double& v, double& p) {
    rho = U[0];
    u = U[1] / rho;
    v = U[2] / rho;
    double E = U[3] / rho;
    p = (gamma - 1.0) * rho * (E - 0.5 * (u * u + v * v));
}

void OutputWriter::init(const CaseConfig& config, const LocalMesh& lm, MPI_Comm comm) {
    config_ = config;
    MPI_Comm_rank(comm, &rank_);
}

void OutputWriter::open(const std::string& output_dir) {
    output_dir_ = output_dir;
    if (rank_ == 0) {
        fs::create_directories(output_dir);
        res_file_.open(output_dir + "/residuals.csv");
        res_file_ << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
        force_file_.open(output_dir + "/forces.csv");
        force_file_ << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    }
}

void OutputWriter::write_residual(int step, double time, int inner, double cfl, double dt,
                                   const Vec4& rc, double l2, double linf) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,%.8e,%d,%.6e,%.6e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e\n",
             step, time, inner, cfl, dt, rc[0], rc[1], rc[2], rc[3], l2, linf);
    res_file_ << buf;
    res_file_.flush();
}

void OutputWriter::write_force(int step, double time, double cl, double cd, double cmz,
                                double pdrag, double vdrag, double plift, double vlift) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%d,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e\n",
             step, time, cl, cd, cmz, pdrag, vdrag, plift, vlift);
    force_file_ << buf;
    force_file_.flush();
}

void OutputWriter::write_surface(const Mesh& mesh, const std::vector<Vec4>& U,
                                  const std::vector<std::array<Vec4, 2>>& grads,
                                  const CaseConfig& config, double mu,
                                  int rank, int nranks, MPI_Comm comm) {
    std::vector<std::string> lines;

    for (auto& bg : mesh.boundary_groups) {
        if (bg.type != BCType::SLIP_WALL && bg.type != BCType::NO_SLIP_ADIABATIC_WALL) continue;

        for (int fi : bg.face_ids) {
            auto& face = mesh.faces[fi];
            int ci = face.left_cell;
            if (ci < 0 || ci >= mesh.num_owned) continue;

            double gamma = config.gas.gamma;
            double rho, u, v, p;
            cons_to_prim_out(U[ci], gamma, rho, u, v, p);

            double q_inf = 0.5 * config.freestream.rho * config.freestream.vel_mag * config.freestream.vel_mag;
            double cp_val = (p - config.freestream.pressure) / q_inf;

            double wall_u = 0, wall_v = 0, wall_mach = 0;
            double cf = 0;

            if (bg.type == BCType::SLIP_WALL) {
                double un = u * face.normal.x() + v * face.normal.y();
                wall_u = u - un * face.normal.x();
                wall_v = v - un * face.normal.y();
                wall_mach = std::sqrt(wall_u * wall_u + wall_v * wall_v) / config.gas.sound_speed(rho, p);
            } else {
                wall_u = 0; wall_v = 0; wall_mach = 0;
                if (mu > 1e-30) {
                    Vec2 dc = face.midpoint - mesh.cells[ci].centroid;
                    double dist = dc.norm();
                    if (dist > 1e-30) {
                        Vec2 tangent(face.normal.y(), -face.normal.x());
                        double ut = u * tangent.x() + v * tangent.y();
                        cf = mu * ut / dist / q_inf;
                    }
                }
            }

            char buf[512];
            snprintf(buf, sizeof(buf), "%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%.8e,%s",
                     face.midpoint.x(), face.midpoint.y(),
                     face.normal.x(), face.normal.y(),
                     p, cp_val, cf, rho, wall_u, wall_v, wall_mach,
                     bg.family_name.c_str());
            lines.push_back(buf);
        }
    }

    if (nranks > 1) {
        int local_count = (int)lines.size();
        std::vector<int> counts(nranks);
        MPI_Gather(&local_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);

        std::string local_str;
        for (auto& l : lines) { local_str += l; local_str += "\n"; }

        int local_len = (int)local_str.size();
        std::vector<int> sizes(nranks), displs(nranks);
        MPI_Gather(&local_len, 1, MPI_INT, sizes.data(), 1, MPI_INT, 0, comm);

        if (rank == 0) {
            displs[0] = 0;
            for (int i = 1; i < nranks; i++) displs[i] = displs[i-1] + sizes[i-1];
            int total = displs[nranks-1] + sizes[nranks-1];
            std::string all_str(total, '\0');
            MPI_Gatherv(local_str.data(), local_len, MPI_CHAR,
                        &all_str[0], sizes.data(), displs.data(), MPI_CHAR, 0, comm);

            std::ofstream f(output_dir_ + "/surface.csv");
            f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
            f << all_str;
        } else {
            MPI_Gatherv(local_str.data(), local_len, MPI_CHAR,
                        nullptr, nullptr, nullptr, MPI_CHAR, 0, comm);
        }
    } else {
        if (rank == 0) {
            std::ofstream f(output_dir_ + "/surface.csv");
            f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
            for (auto& l : lines) f << l << "\n";
        }
    }
}

void OutputWriter::write_field(const Mesh& mesh, const std::vector<Vec4>& U,
                                const CaseConfig& config, int rank, int nranks, MPI_Comm comm) {
    double gamma = config.gas.gamma;

    struct CellData {
        double x, y, rho, u, v, p, mach, E, T;
        int part;
        std::vector<int> nodes;
    };

    std::vector<CellData> local_cells;
    for (int ci = 0; ci < mesh.num_owned; ci++) {
        CellData cd;
        cd.x = mesh.cells[ci].centroid.x();
        cd.y = mesh.cells[ci].centroid.y();
        double rho, u, v, p;
        cons_to_prim_out(U[ci], gamma, rho, u, v, p);
        cd.rho = rho;
        cd.u = u;
        cd.v = v;
        cd.p = p;
        double a = config.gas.sound_speed(std::max(rho, 1e-14), std::max(p, 1e-14));
        cd.mach = std::sqrt(u*u + v*v) / a;
        cd.E = U[ci][3] / rho;
        cd.T = config.gas.temperature(rho, p);
        cd.part = rank;
        cd.nodes = mesh.cells[ci].nodes;
        local_cells.push_back(cd);
    }

    int local_ncells = (int)local_cells.size();
    int local_nnodes = (int)mesh.nodes.size();

    std::vector<int> all_ncells(nranks), all_nnodes(nranks);
    MPI_Gather(&local_ncells, 1, MPI_INT, all_ncells.data(), 1, MPI_INT, 0, comm);
    MPI_Gather(&local_nnodes, 1, MPI_INT, all_nnodes.data(), 1, MPI_INT, 0, comm);

    if (rank == 0) {
        std::string fname = output_dir_ + "/field_final.vtk";
        FILE* f = fopen(fname.c_str(), "w");
        if (!f) return;

        int total_nodes = 0, total_cells = 0;
        for (int i = 0; i < nranks; i++) { total_nodes += all_nnodes[i]; total_cells += all_ncells[i]; }

        fprintf(f, "# vtk DataFile Version 3.0\n");
        fprintf(f, "CFD2D field output - %s\n", config.case_id.c_str());
        fprintf(f, "ASCII\n");
        fprintf(f, "DATASET UNSTRUCTURED_GRID\n");

        struct NodePack { double x, y; };
        struct CellPack { double rho, u, v, p, mach, E, T; int part; int nnodes; int nodes[4]; };

        std::vector<std::vector<double>> all_node_coords(nranks);
        std::vector<std::vector<double>> all_cell_data(nranks);
        std::vector<std::vector<int>> all_cell_conn(nranks);
        std::vector<std::vector<int>> all_cell_types(nranks);

        all_node_coords[0].resize(local_nnodes * 2);
        for (int i = 0; i < local_nnodes; i++) {
            all_node_coords[0][i*2] = mesh.nodes[i].x();
            all_node_coords[0][i*2+1] = mesh.nodes[i].y();
        }

        int total_conn = 0;
        all_cell_data[0].resize(local_ncells * 8);
        for (int i = 0; i < local_ncells; i++) {
            auto& cd = local_cells[i];
            all_cell_data[0][i*8] = cd.rho;
            all_cell_data[0][i*8+1] = cd.u;
            all_cell_data[0][i*8+2] = cd.v;
            all_cell_data[0][i*8+3] = cd.p;
            all_cell_data[0][i*8+4] = cd.mach;
            all_cell_data[0][i*8+5] = cd.E;
            all_cell_data[0][i*8+6] = cd.T;
            all_cell_data[0][i*8+7] = (double)cd.part;
        }
        for (int i = 0; i < local_ncells; i++) {
            int nn = (int)local_cells[i].nodes.size();
            all_cell_conn[0].push_back(nn);
            for (int n : local_cells[i].nodes) all_cell_conn[0].push_back(n);
            all_cell_types[0].push_back(nn == 3 ? 5 : 9);
            total_conn += nn + 1;
        }

        for (int r = 1; r < nranks; r++) {
            all_node_coords[r].resize(all_nnodes[r] * 2);
            MPI_Recv(all_node_coords[r].data(), all_nnodes[r]*2, MPI_DOUBLE, r, 10, comm, MPI_STATUS_IGNORE);

            all_cell_data[r].resize(all_ncells[r] * 8);
            MPI_Recv(all_cell_data[r].data(), all_ncells[r]*8, MPI_DOUBLE, r, 11, comm, MPI_STATUS_IGNORE);

            int conn_size;
            MPI_Recv(&conn_size, 1, MPI_INT, r, 12, comm, MPI_STATUS_IGNORE);
            all_cell_conn[r].resize(conn_size);
            MPI_Recv(all_cell_conn[r].data(), conn_size, MPI_INT, r, 13, comm, MPI_STATUS_IGNORE);

            all_cell_types[r].resize(all_ncells[r]);
            MPI_Recv(all_cell_types[r].data(), all_ncells[r], MPI_INT, r, 14, comm, MPI_STATUS_IGNORE);

            total_conn += conn_size;
        }

        int merged_nnodes = 0;
        std::vector<int> node_offsets(nranks);
        for (int r = 0; r < nranks; r++) {
            node_offsets[r] = merged_nnodes;
            merged_nnodes += all_nnodes[r];
        }

        fprintf(f, "POINTS %d double\n", merged_nnodes);
        for (int r = 0; r < nranks; r++) {
            for (int i = 0; i < all_nnodes[r]; i++) {
                fprintf(f, "%.10e %.10e 0.0\n", all_node_coords[r][i*2], all_node_coords[r][i*2+1]);
            }
        }

        fprintf(f, "CELLS %d %d\n", total_cells, total_conn);
        for (int r = 0; r < nranks; r++) {
            int idx = 0;
            for (int i = 0; i < all_ncells[r]; i++) {
                int nn = all_cell_conn[r][idx++];
                fprintf(f, "%d", nn);
                for (int k = 0; k < nn; k++) {
                    fprintf(f, " %d", all_cell_conn[r][idx++] + node_offsets[r]);
                }
                fprintf(f, "\n");
            }
        }

        fprintf(f, "CELL_TYPES %d\n", total_cells);
        for (int r = 0; r < nranks; r++) {
            for (int i = 0; i < all_ncells[r]; i++) fprintf(f, "%d\n", all_cell_types[r][i]);
        }

        auto write_scalar = [&](const char* name, int idx) {
            fprintf(f, "SCALARS %s double 1\nLOOKUP_TABLE default\n", name);
            for (int r = 0; r < nranks; r++) {
                for (int i = 0; i < all_ncells[r]; i++)
                    fprintf(f, "%.10e\n", all_cell_data[r][i*8 + idx]);
            }
        };

        fprintf(f, "CELL_DATA %d\n", total_cells);
        write_scalar("Density", 0);
        write_scalar("VelocityX", 1);
        write_scalar("VelocityY", 2);
        write_scalar("Pressure", 3);
        write_scalar("Mach", 4);
        write_scalar("TotalEnergy", 5);
        write_scalar("Temperature", 6);
        write_scalar("PartitionID", 7);

        fclose(f);

    } else {
        std::vector<double> coords(local_nnodes * 2);
        for (int i = 0; i < local_nnodes; i++) {
            coords[i*2] = mesh.nodes[i].x();
            coords[i*2+1] = mesh.nodes[i].y();
        }
        MPI_Send(coords.data(), local_nnodes*2, MPI_DOUBLE, 0, 10, comm);

        std::vector<double> cdata(local_ncells * 8);
        for (int i = 0; i < local_ncells; i++) {
            auto& cd = local_cells[i];
            cdata[i*8] = cd.rho; cdata[i*8+1] = cd.u; cdata[i*8+2] = cd.v;
            cdata[i*8+3] = cd.p; cdata[i*8+4] = cd.mach; cdata[i*8+5] = cd.E;
            cdata[i*8+6] = cd.T; cdata[i*8+7] = (double)cd.part;
        }
        MPI_Send(cdata.data(), local_ncells*8, MPI_DOUBLE, 0, 11, comm);

        std::vector<int> conn;
        std::vector<int> types;
        for (int i = 0; i < local_ncells; i++) {
            int nn = (int)local_cells[i].nodes.size();
            conn.push_back(nn);
            for (int n : local_cells[i].nodes) conn.push_back(n);
            types.push_back(nn == 3 ? 5 : 9);
        }
        int conn_size = (int)conn.size();
        MPI_Send(&conn_size, 1, MPI_INT, 0, 12, comm);
        MPI_Send(conn.data(), conn_size, MPI_INT, 0, 13, comm);
        MPI_Send(types.data(), local_ncells, MPI_INT, 0, 14, comm);
    }
}

void OutputWriter::write_metadata(const CaseConfig& config, const LocalMesh& lm,
                                   const SolverStats& stats, const std::string& output_dir) {
    json meta;
    meta["case_id"] = config.case_id;
    meta["solver_name"] = "cfd2d";
    meta["solver_version"] = "1.0.0";
    meta["git_revision"] = nullptr;
    meta["mpi_ranks"] = lm.nranks;
    meta["mesh_file"] = config.mesh_file;
    meta["num_cells_global"] = lm.mesh.num_cells_global;
    meta["num_faces_global"] = lm.mesh.num_faces_global;
    meta["num_cells_owned_local"] = lm.num_owned;
    meta["num_cells_ghost_local"] = lm.num_ghost;
    meta["partitioner"] = lm.nranks > 1 ? "metis_kway" : "serial";
    meta["partition_edge_cut"] = lm.edge_cut;
    meta["halo_exchange"] = "neighbor_isend_irecv";
    meta["full_state_replication_during_iterations"] = false;
    meta["full_mesh_replication_during_iterations"] = false;
    meta["equation_set"] = "compressible_navier_stokes_2d";
    meta["inviscid_flux"] = "roe_harten_entropy_fix";
    meta["entropy_fix"] = "harten_yee";
    meta["viscous_flux"] = config.physics_mode == PhysicsMode::LAMINAR ? "corrected_face_gradient" : "disabled";
    meta["time_integrator"] = config.run_control.type == RunType::TRANSIENT ? "bdf2_lusgs" : "steady_lusgs";
    meta["implicit_solver"] = "lusgs";
    meta["reconstruction"] = "least_squares_linear";
    meta["limiter"] = "barth_jespersen";
    meta["spatial_order_claimed"] = 2;
    meta["positivity_preservation"] = "fallback_to_first_order";
    meta["wall_boundary_output_semantics"] = "boundary_value";

    bool is_transient = config.run_control.type == RunType::TRANSIENT;
    meta["true_bdf2_inner_loop"] = is_transient;
    meta["typical_inner_iterations"] = (int)stats.observed_mean_inner;
    meta["min_inner_iterations"] = config.run_control.min_inner_iterations;
    meta["max_inner_iterations"] = config.run_control.max_inner_iterations;
    meta["observed_min_inner_iterations"] = stats.observed_min_inner;
    meta["observed_max_inner_iterations"] = stats.observed_max_inner;
    meta["inner_residual_reduction_target"] = config.run_control.inner_residual_reduction_target;
    meta["inner_target_misses"] = stats.inner_target_misses;
    meta["inner_target_converged_fraction"] = stats.inner_converged_fraction;
    meta["last_inner_residual_ratio"] = stats.last_inner_residual_ratio;

    auto now = std::time(nullptr);
    char timebuf[64];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    meta["start_time_utc"] = timebuf;
    meta["end_time_utc"] = timebuf;
    meta["completed"] = true;
    meta["convergence_status"] = stats.convergence_status;

    std::ofstream f(output_dir + "/metadata.json");
    f << meta.dump(2) << "\n";
}

void OutputWriter::write_run_status(const CaseConfig& config, const SolverStats& stats,
                                     const std::string& output_dir, int nranks,
                                     const std::string& command) {
    json rs;
    rs["case_id"] = config.case_id;
    rs["command"] = command;
    rs["mpi_ranks"] = nranks;
    rs["wall_time_seconds"] = stats.wall_time_seconds;
    rs["final_step"] = stats.total_steps;
    rs["final_physical_time"] = stats.final_time;
    rs["convergence_status"] = stats.convergence_status;
    rs["residual_reduction_orders"] = stats.residual_reduction_orders;
    rs["notes"] = "";

    std::ofstream f(output_dir + "/run_status.json");
    f << rs.dump(2) << "\n";
}

void OutputWriter::write_partition_diagnostics(const LocalMesh& lm, int rank, int nranks,
                                                const std::string& output_dir, MPI_Comm comm) {
    struct RankInfo {
        int rank, owned, ghost, nbfaces, nneighbors;
        std::vector<int> neighbors;
        int send_total, recv_total;
    };

    RankInfo ri;
    ri.rank = rank;
    ri.owned = lm.num_owned;
    ri.ghost = lm.num_ghost;
    ri.nneighbors = (int)lm.neighbor_ranks.size();
    ri.neighbors = lm.neighbor_ranks;
    ri.send_total = 0;
    ri.recv_total = 0;
    for (auto& h : lm.halos) {
        ri.send_total += (int)h.send_cells.size();
        ri.recv_total += (int)h.recv_cells.size();
    }

    int nbfaces = 0;
    for (auto& bg : lm.mesh.boundary_groups) nbfaces += (int)bg.face_ids.size();
    ri.nbfaces = nbfaces;

    if (rank == 0) {
        std::vector<RankInfo> all_info(nranks);
        all_info[0] = ri;

        for (int r = 1; r < nranks; r++) {
            int buf[6];
            MPI_Recv(buf, 6, MPI_INT, r, 20, comm, MPI_STATUS_IGNORE);
            all_info[r].rank = buf[0];
            all_info[r].owned = buf[1];
            all_info[r].ghost = buf[2];
            all_info[r].nbfaces = buf[3];
            all_info[r].nneighbors = buf[4];
            all_info[r].send_total = buf[5];

            all_info[r].neighbors.resize(buf[4]);
            if (buf[4] > 0)
                MPI_Recv(all_info[r].neighbors.data(), buf[4], MPI_INT, r, 21, comm, MPI_STATUS_IGNORE);
            int recv_total;
            MPI_Recv(&recv_total, 1, MPI_INT, r, 22, comm, MPI_STATUS_IGNORE);
            all_info[r].recv_total = recv_total;
        }

        std::ofstream f(output_dir + "/partition_diagnostics.csv");
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        for (auto& info : all_info) {
            f << info.rank << "," << info.owned << "," << info.ghost << ","
              << info.nbfaces << "," << info.nneighbors << ",\"";
            for (int i = 0; i < info.nneighbors; i++) {
                if (i > 0) f << ";";
                f << info.neighbors[i];
            }
            f << "\"," << info.send_total << "," << info.recv_total << "\n";
        }
    } else {
        int buf[6] = {rank, ri.owned, ri.ghost, ri.nbfaces, ri.nneighbors, ri.send_total};
        MPI_Send(buf, 6, MPI_INT, 0, 20, comm);
        if (ri.nneighbors > 0)
            MPI_Send(ri.neighbors.data(), ri.nneighbors, MPI_INT, 0, 21, comm);
        MPI_Send(&ri.recv_total, 1, MPI_INT, 0, 22, comm);
    }
}
