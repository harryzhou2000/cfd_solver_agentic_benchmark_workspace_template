// Output writers implementing the benchmark output contract:
// metadata.json, residuals.csv, forces.csv, surface.csv, field_final.vtk,
// restart_final.bin, run_status.json, partition_diagnostics.csv.

#include "output.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "boundary.hpp"
#include "gas.hpp"

namespace cfd {

namespace {

// Ensures the output directory exists.
void ensure_dir(const std::string& dir) {
    std::filesystem::create_directories(dir);
}

std::string path(const std::string& dir, const char* name) {
    return dir + "/" + name;
}

// Writes a JSON value only when the string is non-empty; otherwise null.
void write_string_or_null(nlohmann::json& j, const char* key,
                          const std::string& value) {
    j[key] = value.empty() ? nlohmann::json(nullptr) : nlohmann::json(value);
}

}  // namespace

void OutputWriter::write_metadata(const std::string& output_dir,
                                  const MetadataParams& params) {
    ensure_dir(output_dir);
    nlohmann::json j;
    j["case_id"] = params.case_id;
    j["description"] = params.description;
    j["solver_name"] = params.solver_name;
    j["solver_version"] = params.solver_version;
    write_string_or_null(j, "git_revision", params.git_revision);
    j["mpi_ranks"] = params.n_ranks;
    j["mesh_file"] = params.mesh_file;
    j["num_cells_global"] = params.num_cells_global;
    j["num_faces_global"] = params.num_faces_global;
    j["num_cells_owned_local"] = params.num_cells_owned_local;
    j["num_cells_ghost_local"] = params.num_cells_ghost_local;
    j["partitioner"] = params.partitioner;
    j["partition_edge_cut"] = params.partition_edge_cut;
    j["halo_exchange"] = params.halo_exchange;
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = params.equation_set;
    j["inviscid_flux"] = params.inviscid_flux;
    write_string_or_null(j, "entropy_fix", params.entropy_fix);
    j["viscous_flux"] = params.viscous_flux;
    j["time_integrator"] = params.time_integrator;
    j["implicit_solver"] = params.implicit_solver;
    j["reconstruction"] = params.reconstruction;
    j["limiter"] = params.limiter;
    j["spatial_order_claimed"] = params.spatial_order;
    j["positivity_preservation"] = params.positivity_preservation;
    j["wall_boundary_output_semantics"] = params.wall_boundary_output_semantics;
    j["true_bdf2_inner_loop"] = params.true_bdf2_inner_loop;
    j["typical_inner_iterations"] = params.typical_inner_iterations;
    j["min_inner_iterations"] = params.min_inner_iterations;
    j["max_inner_iterations"] = params.max_inner_iterations;
    j["observed_min_inner_iterations"] = params.observed_min_inner_iterations;
    j["observed_max_inner_iterations"] = params.observed_max_inner_iterations;
    j["inner_residual_reduction_target"] =
        params.inner_residual_reduction_target;
    j["inner_target_misses"] = params.inner_target_misses;
    j["inner_target_converged_fraction"] =
        params.inner_target_converged_fraction;
    j["last_inner_residual_ratio"] = params.last_inner_residual_ratio;
    j["start_time_utc"] = params.start_time_utc;
    j["end_time_utc"] = params.end_time_utc;
    j["completed"] = params.completed;
    j["convergence_status"] = params.convergence_status;

    // Extra descriptive fields (nested; not part of the flat contract).
    j["physics"]["mode"] = params.mode;
    j["physics"]["reynolds"] = params.reynolds;
    j["physics"]["viscosity_model"] = params.viscosity_model;
    j["freestream"]["mach"] = params.mach;
    j["freestream"]["alpha_deg"] = params.alpha;
    j["gas"]["gamma"] = params.gamma;
    j["gas"]["R"] = params.gas_R;
    j["gas"]["prandtl"] = params.prandtl;
    j["run_control"]["type"] = params.run_type;
    j["run_control"]["time_step"] = params.time_step;
    j["run_control"]["final_time"] = params.final_time;
    j["run_control"]["max_steps"] = params.max_steps;
    j["numerics"]["inviscid_flux"] = params.inviscid_flux;
    j["numerics"]["limiter"] = params.limiter;

    std::ofstream out(path(output_dir, "metadata.json"));
    if (out.is_open()) out << j.dump(2) << "\n";
}

void OutputWriter::write_residuals(const std::string& output_dir,
                                   const std::vector<ResidualRow>& rows) {
    ensure_dir(output_dir);
    struct RowPrinter {
        static void print(const ResidualRow& r, std::ostream& out) {
            out << r.step << "," << r.physical_time << "," << r.inner_iter
                << "," << r.cfl << "," << r.dt << "," << r.rho << ","
                << r.rhou << "," << r.rhov << "," << r.rhoE << ","
                << r.residual_l2 << "," << r.residual_linf;
        }
    };
    const char* header =
        "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
        "residual_l2,residual_linf";
    const bool is_new =
        !std::filesystem::exists(path(output_dir, "residuals.csv"));
    std::ofstream out(path(output_dir, "residuals.csv"), std::ios::app);
    if (!out.is_open()) return;
    if (is_new) out << header << "\n";
    out << std::setprecision(12);
    for (const ResidualRow& r : rows) {
        RowPrinter::print(r, out);
        out << "\n";
    }
}

void OutputWriter::write_forces(const std::string& output_dir,
                                const std::vector<ForceRow>& rows) {
    ensure_dir(output_dir);
    const char* header =
        "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
        "pressure_lift,viscous_lift";
    const bool is_new =
        !std::filesystem::exists(path(output_dir, "forces.csv"));
    std::ofstream out(path(output_dir, "forces.csv"), std::ios::app);
    if (!out.is_open()) return;
    if (is_new) out << header << "\n";
    out << std::setprecision(12);
    for (const ForceRow& r : rows) {
        out << r.step << "," << r.physical_time << "," << r.cl << ","
            << r.cd << "," << r.cmz << "," << r.pressure_drag << ","
            << r.viscous_drag << "," << r.pressure_lift << ","
            << r.viscous_lift << "\n";
    }
}

void OutputWriter::write_surface(const std::string& output_dir,
                                 const std::vector<SurfaceRow>& rows) {
    ensure_dir(output_dir);
    const char* header =
        "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag";
    std::ofstream out(path(output_dir, "surface.csv"));
    if (!out.is_open()) return;
    out << header << "\n";
    out << std::setprecision(12);
    for (const SurfaceRow& r : rows) {
        out << r.x << "," << r.y << "," << r.nx << "," << r.ny << ","
            << r.pressure << "," << r.cp << "," << r.cf << "," << r.rho
            << "," << r.u << "," << r.v << "," << r.mach << ","
            << r.tag << "\n";
    }
}

void OutputWriter::write_field_vtk(
    const std::string& output_dir, const Mesh& mesh,
    const std::vector<std::vector<cgsize_t>>& cell_nodes,
    const std::vector<Vector4>& U_global, const std::vector<int>& cell_owner,
    const GasParams& gas, int rank) {
    if (rank != 0) return;
    ensure_dir(output_dir);
    const size_t n_cells = static_cast<size_t>(mesh.n_cells);
    const size_t n_nodes = static_cast<size_t>(mesh.n_nodes);
    if (U_global.size() != n_cells || cell_nodes.size() != n_cells) return;

    std::ofstream out(path(output_dir, "field_final.vtk"));
    if (!out.is_open()) return;
    out << "# vtk DataFile Version 3.0\n"
        << "cfd_solver field output\n"
        << "ASCII\n"
        << "DATASET UNSTRUCTURED_GRID\n";
    out << std::setprecision(10);

    // Points: 2-D mesh written with z = 0.
    out << "POINTS " << n_nodes << " double\n";
    for (size_t i = 0; i < n_nodes; ++i) {
        out << mesh.x[i] << " " << mesh.y[i] << " 0.0\n";
    }

    // Cells: connectivity with per-cell node counts.
    size_t total_size = 0;
    for (const auto& nodes : cell_nodes) total_size += nodes.size() + 1;
    out << "CELLS " << n_cells << " " << total_size << "\n";
    for (const auto& nodes : cell_nodes) {
        out << nodes.size();
        for (cgsize_t n : nodes) out << " " << n;
        out << "\n";
    }

    // Cell types: VTK_TRIANGLE = 5, VTK_QUAD = 9, VTK_POLYGON = 7.
    out << "CELL_TYPES " << n_cells << "\n";
    for (const auto& nodes : cell_nodes) {
        switch (nodes.size()) {
            case 3: out << "5\n"; break;
            case 4: out << "9\n"; break;
            default: out << "7\n"; break;
        }
    }

    // Cell data.
    out << "CELL_DATA " << n_cells << "\n";

    out << "SCALARS density double 1\nLOOKUP_TABLE default\n";
    for (const Vector4& U : U_global) out << U.r << "\n";

    out << "VECTORS velocity double\n";
    for (const Vector4& U : U_global) {
        out << velocity_x(U) << " " << velocity_y(U) << " 0.0\n";
    }

    out << "SCALARS pressure double 1\nLOOKUP_TABLE default\n";
    for (const Vector4& U : U_global) out << pressure(U, gas) << "\n";

    out << "SCALARS mach double 1\nLOOKUP_TABLE default\n";
    for (const Vector4& U : U_global) out << mach_number(U, gas) << "\n";

    out << "SCALARS temperature double 1\nLOOKUP_TABLE default\n";
    for (const Vector4& U : U_global) out << temperature(U, gas) << "\n";

    out << "SCALARS rank_id int 1\nLOOKUP_TABLE default\n";
    for (size_t i = 0; i < n_cells; ++i) {
        const int owner = (i < cell_owner.size()) ? cell_owner[i] : 0;
        out << owner << "\n";
    }
}

void OutputWriter::write_restart(const std::string& output_dir,
                                 const std::vector<Vector4>& U_local,
                                 const LocalMesh& local_mesh, int step,
                                 double physical_time, MPI_Comm comm) {
    ensure_dir(output_dir);
    int rank = 0, n_ranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);

    // Each rank packs its owned cells; rank 0 gathers in rank order.
    const int n_owned_local = static_cast<int>(local_mesh.n_owned);
    std::vector<int> counts(static_cast<size_t>(n_ranks), 0);
    std::vector<int> displs(static_cast<size_t>(n_ranks), 0);
    MPI_Gather(&n_owned_local, 1, MPI_INT, counts.data(), 1, MPI_INT, 0,
               comm);
    std::vector<double> send_buf(static_cast<size_t>(4 * n_owned_local));
    for (int i = 0; i < n_owned_local; ++i) {
        const Vector4& U = U_local[static_cast<size_t>(i)];
        send_buf[static_cast<size_t>(4 * i + 0)] = U.r;
        send_buf[static_cast<size_t>(4 * i + 1)] = U.u;
        send_buf[static_cast<size_t>(4 * i + 2)] = U.v;
        send_buf[static_cast<size_t>(4 * i + 3)] = U.e;
    }
    // Recv counts/displacements are in doubles: 4 doubles per owned cell.
    std::vector<int> recvcounts(static_cast<size_t>(n_ranks), 0);
    for (int r = 0; r < n_ranks; ++r) {
        recvcounts[static_cast<size_t>(r)] = 4 * counts[static_cast<size_t>(r)];
    }
    std::vector<double> recv_buf;
    int64_t total = 0;
    if (rank == 0) {
        int offset = 0;
        for (int r = 0; r < n_ranks; ++r) {
            displs[static_cast<size_t>(r)] = offset;
            offset += recvcounts[static_cast<size_t>(r)];
            total += counts[static_cast<size_t>(r)];
        }
        recv_buf.resize(static_cast<size_t>(offset));
    }
    MPI_Gatherv(send_buf.data(), 4 * n_owned_local, MPI_DOUBLE,
                recv_buf.data(), recvcounts.data(), displs.data(), MPI_DOUBLE,
                0, comm);

    if (rank != 0) return;
    std::ofstream out(path(output_dir, "restart_final.bin"),
                      std::ios::binary);
    if (!out.is_open()) return;
    const char magic[4] = {'C', 'F', 'D', 'R'};
    const uint32_t version = 2;
    const int32_t step32 = static_cast<int32_t>(step);
    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&step32), sizeof(step32));
    out.write(reinterpret_cast<const char*>(&physical_time),
              sizeof(physical_time));
    out.write(reinterpret_cast<const char*>(&total), sizeof(total));
    out.write(reinterpret_cast<const char*>(recv_buf.data()),
              static_cast<std::streamsize>(recv_buf.size() *
                                           sizeof(double)));
}

void OutputWriter::write_run_status(const std::string& output_dir,
                                    const RunStatusParams& params) {
    ensure_dir(output_dir);
    nlohmann::json j;
    j["case_id"] = params.case_id;
    j["command"] = params.command;
    j["mpi_ranks"] = params.n_ranks;
    j["wall_time_seconds"] = params.wall_time_seconds;
    j["final_step"] = params.final_step;
    j["final_physical_time"] = params.final_physical_time;
    j["convergence_status"] = params.convergence_status;
    j["residual_reduction_orders"] = params.residual_reduction_orders;
    j["notes"] = params.notes;
    std::ofstream out(path(output_dir, "run_status.json"));
    if (out.is_open()) out << j.dump(2) << "\n";
}

void OutputWriter::write_partition_diagnostics(
    const std::string& output_dir, int rank, int n_ranks, cgsize_t n_owned,
    cgsize_t n_ghost, cgsize_t n_boundary_faces_owned,
    const std::vector<int>& neighbor_ranks, const std::vector<int>& send_cells,
    const std::vector<int>& recv_cells) {
    ensure_dir(output_dir);
    const char* header =
        "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
        "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells";
    const bool is_new =
        !std::filesystem::exists(path(output_dir, "partition_diagnostics.csv"));
    std::ofstream out(path(output_dir, "partition_diagnostics.csv"),
                      std::ios::app);
    if (!out.is_open()) return;
    if (is_new && rank == 0) out << header << "\n";

    auto list = [](std::ostream& os, const std::vector<int>& v) {
        for (size_t k = 0; k < v.size(); ++k) {
            if (k) os << " ";
            os << v[k];
        }
    };
    out << rank << "," << n_owned << "," << n_ghost << ","
        << n_boundary_faces_owned << "," << neighbor_ranks.size() << ",";
    list(out, neighbor_ranks);
    out << ",";
    list(out, send_cells);
    out << ",";
    list(out, recv_cells);
    out << "\n";
    (void)n_ranks;
}

// ---------------------------------------------------------------------------
// Field gathering and surface-row construction (rank 0 post-processing)
// ---------------------------------------------------------------------------

std::vector<Vector4> gather_global_field(const LocalMesh& local_mesh,
                                         const std::vector<Vector4>& U_local,
                                         cgsize_t n_cells_global,
                                         MPI_Comm comm) {
    int rank = 0, n_ranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);

    const size_t n_owned = static_cast<size_t>(local_mesh.n_owned);
    // Pack (global_id, rho, rhou, rhov, rhoE) per owned cell.
    std::vector<double> send_buf(5 * n_owned);
    for (size_t i = 0; i < n_owned; ++i) {
        send_buf[5 * i + 0] =
            static_cast<double>(local_mesh.owned_cells[i]);
        const Vector4& U = U_local[i];
        send_buf[5 * i + 1] = U.r;
        send_buf[5 * i + 2] = U.u;
        send_buf[5 * i + 3] = U.v;
        send_buf[5 * i + 4] = U.e;
    }
    const int send_count = static_cast<int>(send_buf.size());
    std::vector<int> counts(static_cast<size_t>(n_ranks), 0);
    std::vector<int> displs(static_cast<size_t>(n_ranks), 0);
    MPI_Gather(&send_count, 1, MPI_INT, counts.data(), 1, MPI_INT, 0, comm);
    std::vector<double> recv_buf;
    if (rank == 0) {
        size_t total = 0;
        for (int r = 0; r < n_ranks; ++r) {
            displs[static_cast<size_t>(r)] = static_cast<int>(total);
            total += static_cast<size_t>(counts[static_cast<size_t>(r)]);
        }
        recv_buf.resize(total);
    }
    MPI_Gatherv(send_buf.data(), send_count, MPI_DOUBLE, recv_buf.data(),
                counts.data(), displs.data(), MPI_DOUBLE, 0, comm);

    std::vector<Vector4> U_global;
    if (rank != 0) return U_global;
    U_global.assign(static_cast<size_t>(n_cells_global), Vector4{});
    const size_t n_pairs = recv_buf.size() / 5;
    for (size_t k = 0; k < n_pairs; ++k) {
        const size_t gid = static_cast<size_t>(recv_buf[5 * k + 0]);
        if (gid >= U_global.size()) continue;
        U_global[gid] = {recv_buf[5 * k + 1], recv_buf[5 * k + 2],
                         recv_buf[5 * k + 3], recv_buf[5 * k + 4]};
    }
    return U_global;
}

std::vector<SurfaceRow> build_surface_rows(
    const Mesh& mesh, const std::vector<Vector4>& U_global,
    const GasParams& gas, const FreestreamParams& freestream,
    double viscosity, MPI_Comm comm) {
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    std::vector<SurfaceRow> rows;
    if (rank != 0) return rows;

    const double q_inf = 0.5 * freestream.density * freestream.velocity *
                         freestream.velocity;
    const double p_inf = freestream.pressure;

    for (cgsize_t bf = 0; bf < mesh.n_boundary_faces; ++bf) {
        const size_t bi = static_cast<size_t>(bf);
        const int bc = mesh.bface_bc_type[bi];
        if (bc != static_cast<int>(BCType::SlipWall) &&
            bc != static_cast<int>(BCType::NoSlipAdiabaticWall)) {
            continue;  // wall faces only
        }
        const cgsize_t gc = mesh.bface_cell[bi];
        if (static_cast<size_t>(gc) >= U_global.size()) continue;
        const Vector4& U = U_global[static_cast<size_t>(gc)];
        const PrimitiveState prim = conservative_to_primitive(U, gas);

        // Unit outward normal.
        const double nx = mesh.bface_nx[bi];
        const double ny = mesh.bface_ny[bi];
        const double len = std::hypot(nx, ny);
        if (!(len > 0.0)) continue;
        const double n_x = nx / len;
        const double n_y = ny / len;

        SurfaceRow row;
        row.x = mesh.bface_center_x[bi];
        row.y = mesh.bface_center_y[bi];
        row.nx = n_x;
        row.ny = n_y;
        row.tag = mesh.bface_tag[bi];

        // Boundary-state semantics: the wall pressure/temperature use the
        // zero-normal-gradient extrapolation from the interior (the wall
        // value for adiabatic walls); the velocity satisfies the wall BC.
        const double p_wall = prim.p;
        const double T_wall = prim.p / (gas.R * prim.rho);
        double u_wall = 0.0, v_wall = 0.0;
        double cf = 0.0;
        if (bc == static_cast<int>(BCType::NoSlipAdiabaticWall)) {
            // No-slip: wall velocity is zero; the skin friction uses the
            // one-sided tangential velocity gradient (u_t_cell / d_cell).
            row.rho = p_wall / (gas.R * T_wall);
            if (viscosity > 0.0) {
                const double gx =
                    mesh.cell_center_x[static_cast<size_t>(gc)];
                const double gy =
                    mesh.cell_center_y[static_cast<size_t>(gc)];
                const double dist =
                    std::hypot(row.x - gx, row.y - gy);
                const double t_x = -n_y, t_y = n_x;  // tangential unit
                const double u_t = prim.u * t_x + prim.v * t_y;
                if (dist > 0.0) {
                    cf = viscosity * u_t / dist / q_inf;
                }
            }
        } else {
            // Slip wall: zero normal velocity, tangential velocity
            // preserved from the interior.
            const double vn = prim.u * n_x + prim.v * n_y;
            u_wall = prim.u - vn * n_x;
            v_wall = prim.v - vn * n_y;
            row.rho = prim.rho;
        }
        row.pressure = p_wall;
        row.cp = (p_wall - p_inf) / q_inf;
        row.cf = cf;
        row.u = u_wall;
        row.v = v_wall;
        const double a_wall = std::sqrt(gas.gamma * p_wall / row.rho);
        row.mach = (a_wall > 0.0)
                       ? std::sqrt(u_wall * u_wall + v_wall * v_wall) / a_wall
                       : 0.0;
        rows.push_back(row);
    }
    return rows;
}

std::string utc_now_iso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

std::vector<Vector4> read_restart(const std::string& file,
                                  cgsize_t n_owned_local, int* step,
                                  double* physical_time, MPI_Comm comm) {
    int rank = 0, n_ranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);

    // File layout: 28-byte header (magic 4, version 4, step 4, time 8,
    // total cells 8) followed by the owned cells of every rank in rank
    // order, 32 bytes (4 doubles) per cell. This rank's segment starts at
    // the prefix sum of the owned counts of the lower ranks.
    const int my_n = static_cast<int>(n_owned_local);
    int offset = 0;
    MPI_Exscan(&my_n, &offset, 1, MPI_INT, MPI_SUM, comm);

    std::vector<Vector4> U(static_cast<size_t>(n_owned_local), Vector4{});
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("read_restart: cannot open " + file);
    }
    char magic[4] = {0, 0, 0, 0};
    uint32_t version = 0;
    int32_t step32 = 0;
    double time = 0.0;
    int64_t total = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&step32), sizeof(step32));
    in.read(reinterpret_cast<char*>(&time), sizeof(time));
    in.read(reinterpret_cast<char*>(&total), sizeof(total));
    if (in.gcount() == 0 || std::string(magic, 4) != "CFDR") {
        throw std::runtime_error("read_restart: bad magic in " + file);
    }
    if (version < 2) {
        throw std::runtime_error("read_restart: unsupported version " +
                                 std::to_string(version));
    }
    const int64_t header_bytes = 28;
    const int64_t seg_start = header_bytes + 32LL * offset;
    if (seg_start + 32LL * my_n > header_bytes + 32LL * total) {
        throw std::runtime_error("read_restart: file is truncated for rank " +
                                 std::to_string(rank));
    }
    in.seekg(static_cast<std::streamoff>(seg_start));
    std::vector<double> buf(4 * static_cast<size_t>(my_n));
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(buf.size() * sizeof(double)));
    for (size_t i = 0; i < static_cast<size_t>(my_n); ++i) {
        U[i] = {buf[4 * i + 0], buf[4 * i + 1], buf[4 * i + 2],
                buf[4 * i + 3]};
    }
    if (step) *step = static_cast<int>(step32);
    if (physical_time) *physical_time = time;
    return U;
}

}  // namespace cfd
