#include "solver/output_writer.hpp"

#include "solver/boundary.hpp"        // apply_boundary_condition
#include "solver/flux.hpp"            // conservative_to_primitive, speed_of_sound
#include "solver/limiter.hpp"         // apply_barth_jespersen_limiter
#include "solver/reconstruction.hpp"  // compute_gradients

#include <nlohmann/json.hpp>

#include <mpi.h>
#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace solver {

namespace {

using json = nlohmann::json;

std::string fmt_double(double v, int prec = 10) {
    std::ostringstream os;
    os << std::setprecision(prec) << v;
    return os.str();
}

// Gather a variable-length vector of T from all ranks to `root`.
template <typename T>
std::vector<T> gather_variable(const std::vector<T>& local, MPI_Comm comm,
                               int root, MPI_Datatype dtype) {
    int rank = 0, nranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);
    const int n = static_cast<int>(local.size());
    std::vector<int> counts(nranks, 0), displs(nranks, 0);
    MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
    std::vector<T> all;
    if (rank == root) {
        for (int r = 1; r < nranks; ++r) displs[r] = displs[r - 1] + counts[r - 1];
        all.resize(static_cast<size_t>(displs[nranks - 1]) + counts[nranks - 1]);
    }
    MPI_Gatherv(local.data(), n, dtype, all.data(), counts.data(),
                displs.data(), dtype, root, comm);
    return all;
}

// Gather one std::string per rank to `root` (concatenated, rank order).
std::string gather_string(const std::string& local, MPI_Comm comm, int root) {
    int rank = 0, nranks = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nranks);
    const int n = static_cast<int>(local.size());
    std::vector<int> counts(nranks, 0), displs(nranks, 0);
    MPI_Gather(&n, 1, MPI_INT, counts.data(), 1, MPI_INT, root, comm);
    std::string all;
    if (rank == root) {
        for (int r = 1; r < nranks; ++r) displs[r] = displs[r - 1] + counts[r - 1];
        all.resize(static_cast<size_t>(displs[nranks - 1]) + counts[nranks - 1]);
    }
    MPI_Gatherv(local.data(), n, MPI_CHAR, all.data(), counts.data(),
                displs.data(), MPI_CHAR, root, comm);
    return all;
}

// BC type of a local face (empty string if untagged).
std::string bc_type_of_face(const DistributedMesh& mesh, int f) {
    for (const auto& bc : mesh.bc_families) {
        for (int bf : bc.face_ids) {
            if (bf == f) return bc.bc_type;
        }
    }
    return "";
}

std::string bc_name_of_face(const DistributedMesh& mesh, int f) {
    for (const auto& bc : mesh.bc_families) {
        for (int bf : bc.face_ids) {
            if (bf == f) return bc.name;
        }
    }
    return "untagged";
}

// Order the vertices of a (convex) cell counter-clockwise about their mean.
std::vector<Vec2> order_ccw(std::vector<Vec2> pts) {
    if (pts.size() < 3) return pts;
    Vec2 center = Vec2::Zero();
    for (const Vec2& p : pts) center += p;
    center /= static_cast<Scalar>(pts.size());
    std::sort(pts.begin(), pts.end(), [&center](const Vec2& a, const Vec2& b) {
        return std::atan2(a.y() - center.y(), a.x() - center.x()) <
               std::atan2(b.y() - center.y(), b.x() - center.x());
    });
    return pts;
}

// Distinct vertices of a cell (from its faces), ordered counter-clockwise.
std::vector<Vec2> cell_vertices_ccw(const DistributedMesh& mesh, int c) {
    const Cell& cell = mesh.owned_cells[c];
    std::vector<Vec2> pts;
    for (int fid : cell.face_ids) {
        for (const Vec2& n : mesh.local_faces[fid].nodes) {
            const double tol = 1e-12 * (1.0 + n.norm());
            bool dup = false;
            for (const Vec2& p : pts) {
                if ((p - n).norm() < tol) {
                    dup = true;
                    break;
                }
            }
            if (!dup) pts.push_back(n);
        }
    }
    return order_ccw(std::move(pts));
}

// Wall shear coefficient estimate for a no-slip wall face: one-sided
// tangential velocity gradient from the adjacent cell.
double wall_cf(const Vec4& W_cell, const Vec2& normal, const Vec2& face_centroid,
               const Vec2& cell_centroid, double mu, double q_inf) {
    if (mu <= 0.0 || q_inf <= 0.0) return 0.0;
    const double un = W_cell(1) * normal.x() + W_cell(2) * normal.y();
    const double ut_x = W_cell(1) - un * normal.x();
    const double ut_y = W_cell(2) - un * normal.y();
    const double ut = std::sqrt(ut_x * ut_x + ut_y * ut_y);
    const double dist = (face_centroid - cell_centroid).norm();
    if (dist <= 0.0) return 0.0;
    return mu * ut / (q_inf * dist);
}

} // namespace

std::string iso8601_utc_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

StepLogFiles open_step_logs(const std::string& output_dir, int rank) {
    StepLogFiles logs;
    if (rank != 0) return logs;
    logs.active = true;
    logs.residuals.open(output_dir + "/residuals.csv", std::ios::out);
    logs.residuals
        << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,"
           "residual_l2,residual_linf\n";
    logs.forces.open(output_dir + "/forces.csv", std::ios::out);
    logs.forces
        << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,"
           "pressure_lift,viscous_lift\n";
    return logs;
}

void close_step_logs(StepLogFiles& logs) {
    if (logs.residuals.is_open()) logs.residuals.close();
    if (logs.forces.is_open()) logs.forces.close();
    logs.active = false;
}

void write_residual_row(StepLogFiles& logs, int step, double phys_time,
                        int inner_iter, double cfl, double dt,
                        const double comp_l2[4], double l2, double linf) {
    if (!logs.active) return;
    logs.residuals << step << "," << fmt_double(phys_time) << ","
                   << inner_iter << "," << fmt_double(cfl) << ","
                   << fmt_double(dt) << "," << fmt_double(comp_l2[0]) << ","
                   << fmt_double(comp_l2[1]) << "," << fmt_double(comp_l2[2])
                   << "," << fmt_double(comp_l2[3]) << "," << fmt_double(l2)
                   << "," << fmt_double(linf) << "\n";
}

void write_force_row(StepLogFiles& logs, int step, double phys_time,
                     const ForceCoeffs& f) {
    if (!logs.active) return;
    logs.forces << step << "," << fmt_double(phys_time) << "," << fmt_double(f.cl)
                << "," << fmt_double(f.cd) << "," << fmt_double(f.cmz) << ","
                << fmt_double(f.pressure_drag) << ","
                << fmt_double(f.viscous_drag) << ","
                << fmt_double(f.pressure_lift) << ","
                << fmt_double(f.viscous_lift) << "\n";
}

void write_partition_diagnostics(const DistributedMesh& mesh,
                                 const std::string& output_dir, MPI_Comm comm,
                                 int rank) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_ghost = static_cast<int>(mesh.ghost_cells.size());
    int num_bnd = 0;
    for (int f = 0; f < static_cast<int>(mesh.local_faces.size()); ++f) {
        if (mesh.face_right[f] < 0) ++num_bnd;
    }
    int send_cells = 0, recv_cells = 0;
    std::string neighbors;
    for (size_t i = 0; i < mesh.neighbors.size(); ++i) {
        if (i > 0) neighbors += ";";
        neighbors += std::to_string(mesh.neighbors[i].rank);
        send_cells += static_cast<int>(mesh.neighbors[i].send_indices.size());
        recv_cells += static_cast<int>(mesh.neighbors[i].recv_indices.size());
    }
    if (neighbors.empty()) neighbors = "-";

    std::ostringstream row;
    row << rank << "," << num_owned << "," << num_ghost << "," << num_bnd
        << "," << mesh.neighbors.size() << "," << neighbors << ","
        << send_cells << "," << recv_cells << "\n";
    const std::string local = row.str();
    const std::string all = gather_string(local, comm, 0);
    if (rank == 0) {
        std::ofstream f(output_dir + "/partition_diagnostics.csv",
                        std::ios::out);
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,"
             "num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        f << all;
    }
}

void write_surface_csv(DistributedMesh& mesh, std::vector<double>& state,
                       const CaseConfig& config, const std::string& output_dir,
                       MPI_Comm comm, int rank) {
    const double gamma = config.gas.gamma;
    const double p_inf = config.freestream.pressure;
    const double q_inf =
        0.5 * config.freestream.rho *
        (config.freestream.u_inf * config.freestream.u_inf +
         config.freestream.v_inf * config.freestream.v_inf);
    const double mu = config.freestream.viscosity;
    const bool viscous = (config.physics.mode != "inviscid") && mu > 0.0;
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    const int num_total =
        num_owned + static_cast<int>(mesh.ghost_cells.size());

    // Refresh ghost state, then compute limited gradients for the
    // reconstructed face (boundary) state.
    HaloBuffers bufs;
    init_halo_buffers(mesh, bufs);
    start_halo_exchange(state, mesh, bufs);
    finish_halo_exchange(state, mesh, bufs);
    std::vector<Vec2> grads = compute_gradients(mesh, state, gamma);
    apply_barth_jespersen_limiter(mesh, state, grads, gamma);

    std::ostringstream rows;
    const int num_faces = static_cast<int>(mesh.local_faces.size());
    for (int f = 0; f < num_faces; ++f) {
        if (mesh.face_right[f] >= 0) continue;  // interior face
        const std::string bc_type = bc_type_of_face(mesh, f);
        if (bc_type != "slip_wall" && bc_type != "no_slip_adiabatic_wall") {
            continue;
        }
        const int left = mesh.face_left[f];
        const Face& face = mesh.local_faces[f];
        const Vec2 fc = face.centroid;
        const Vec2 xc =
            (left < num_owned) ? mesh.owned_cells[left].centroid
                               : mesh.ghost_cells[left - num_owned].centroid;

        // Reconstructed interior state at the face (cell average + limited
        // gradient).
        const size_t base = static_cast<size_t>(left) * kStateSize;
        Vec4 U_face;
        for (int v = 0; v < kStateSize; ++v) {
            U_face[v] = state[base + v] +
                        grads[left + v * num_total].dot(fc - xc);
        }
        const Vec4 W_face = conservative_to_primitive(U_face, gamma);

        // Boundary state through the wall BC: slip walls get zero normal
        // velocity, no-slip walls get u = v = 0.
        const Vec4 W_b = conservative_to_primitive(
            apply_boundary_condition(U_face, face.normal, bc_type, config),
            gamma);

        const double p = W_face(3);
        const double rho = W_b(0);
        const double u = W_b(1);
        const double v = W_b(2);
        const double a = speed_of_sound(rho, p, gamma);
        const double mach = a > 0.0 ? std::sqrt(u * u + v * v) / a : 0.0;
        const double cp = q_inf != 0.0 ? (p - p_inf) / q_inf : 0.0;
        const double cf =
            viscous ? wall_cf(W_face, face.normal, fc, xc, mu, q_inf) : 0.0;

        rows << fmt_double(fc.x()) << "," << fmt_double(fc.y()) << ","
             << fmt_double(face.normal.x()) << "," << fmt_double(face.normal.y())
             << "," << fmt_double(p) << "," << fmt_double(cp) << ","
             << fmt_double(cf) << "," << fmt_double(rho) << ","
             << fmt_double(u) << "," << fmt_double(v) << "," << fmt_double(mach)
             << "," << bc_name_of_face(mesh, f) << "\n";
    }

    const std::string local = rows.str();
    fprintf(stderr, "S r%d rows %zu, gathering...\n", rank, local.size());
    const std::string all = gather_string(local, comm, 0);
    if (rank == 0) {
        std::ofstream f(output_dir + "/surface.csv", std::ios::out);
        f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        f << all;
    }
}

void write_field_vtu(const DistributedMesh& mesh,
                     const std::vector<double>& state,
                     const CaseConfig& config, const std::string& output_dir,
                     MPI_Comm comm, int rank) {
    const double gamma = config.gas.gamma;
    const double R = config.gas.R;
    const int num_owned = static_cast<int>(mesh.owned_cells.size());

    // Local piece: points (3 doubles per vertex), connectivity, offsets,
    // types, and cell data.
    std::vector<double> pts;
    std::vector<int> conn, offs;
    std::vector<unsigned char> types;
    std::vector<double> cell_d;  // density,u,v,0,pressure,mach,temp per cell
    std::vector<int> cell_rank;
    cell_d.reserve(static_cast<size_t>(num_owned) * 7);
    cell_rank.reserve(static_cast<size_t>(num_owned));

    for (int c = 0; c < num_owned; ++c) {
        const std::vector<Vec2> verts = cell_vertices_ccw(mesh, c);
        const int base_pt = static_cast<int>(pts.size() / 3);
        for (const Vec2& p : verts) {
            pts.push_back(p.x());
            pts.push_back(p.y());
            pts.push_back(0.0);
        }
        const int nv = static_cast<int>(verts.size());
        for (int k = 0; k < nv; ++k) conn.push_back(base_pt + k);
        offs.push_back(static_cast<int>(conn.size()));
        types.push_back(nv == 3 ? 5 : (nv == 4 ? 9 : 7));  // TRI, QUAD, POLYGON

        const size_t b = static_cast<size_t>(c) * kStateSize;
        const Vec4 W = conservative_to_primitive(
            Vec4(state[b], state[b + 1], state[b + 2], state[b + 3]), gamma);
        const double a = speed_of_sound(W(0), W(3), gamma);
        const double mag = std::sqrt(W(1) * W(1) + W(2) * W(2));
        cell_d.push_back(W(0));   // density
        cell_d.push_back(W(1));   // u
        cell_d.push_back(W(2));   // v
        cell_d.push_back(0.0);    // z-velocity
        cell_d.push_back(W(3));   // pressure
        cell_d.push_back(a > 0.0 ? mag / a : 0.0);  // mach
        cell_d.push_back(R > 0.0 ? W(3) / (W(0) * R) : 0.0);  // temperature
        cell_rank.push_back(rank);
    }

    // Gather everything to rank 0.
    const std::vector<double> all_pts = gather_variable(pts, comm, 0, MPI_DOUBLE);
    const std::vector<int> all_conn = gather_variable(conn, comm, 0, MPI_INT);
    const std::vector<int> all_offs = gather_variable(offs, comm, 0, MPI_INT);
    const std::vector<unsigned char> all_types =
        gather_variable(types, comm, 0, MPI_UNSIGNED_CHAR);
    const std::vector<double> all_cell_d =
        gather_variable(cell_d, comm, 0, MPI_DOUBLE);
    const std::vector<int> all_rank = gather_variable(cell_rank, comm, 0, MPI_INT);

    // Per-rank counts needed to renumber the concatenated arrays (collective;
    // all ranks must participate).
    int nranks = 1;
    MPI_Comm_size(comm, &nranks);
    const int npts_local = static_cast<int>(pts.size() / 3);
    const int nconn_local = static_cast<int>(conn.size());
    const int noff_local = static_cast<int>(offs.size());
    std::vector<int> pt_counts(nranks), conn_counts(nranks), off_counts(nranks);
    MPI_Gather(&npts_local, 1, MPI_INT, pt_counts.data(), 1, MPI_INT, 0, comm);
    MPI_Gather(&nconn_local, 1, MPI_INT, conn_counts.data(), 1, MPI_INT, 0,
               comm);
    MPI_Gather(&noff_local, 1, MPI_INT, off_counts.data(), 1, MPI_INT, 0,
               comm);

    if (rank != 0) return;

    std::vector<int> pt_start(nranks, 0), conn_start(nranks, 0);
    for (int r = 1; r < nranks; ++r) {
        pt_start[r] = pt_start[r - 1] + pt_counts[r - 1];
        conn_start[r] = conn_start[r - 1] + conn_counts[r - 1];
    }

    std::ostringstream os;
    os << "<?xml version=\"1.0\"?>\n";
    os << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" "
          "byte_order=\"LittleEndian\">\n";
    os << "  <UnstructuredGrid>\n";
    os << "    <Piece NumberOfPoints=\"" << all_pts.size() / 3
       << "\" NumberOfCells=\"" << all_offs.size() << "\">\n";
    os << "      <Points>\n";
    os << "        <DataArray type=\"Float64\" NumberOfComponents=\"3\" "
          "format=\"ascii\">\n";
    for (size_t i = 0; i < all_pts.size(); i += 3) {
        os << fmt_double(all_pts[i]) << " " << fmt_double(all_pts[i + 1])
           << " " << fmt_double(all_pts[i + 2]) << "\n";
    }
    os << "        </DataArray>\n";
    os << "      </Points>\n";
    os << "      <Cells>\n";
    os << "        <DataArray type=\"Int32\" Name=\"connectivity\" "
          "format=\"ascii\">\n";
    // Renumber rank r's point indices by pt_start[r].
    for (int r = 0; r < nranks; ++r) {
        const int begin = conn_start[r];
        const int end = begin + conn_counts[r];
        for (int i = begin; i < end; ++i) {
            os << (all_conn[i] + pt_start[r]) << " ";
            if ((i - begin + 1) % 12 == 0) os << "\n";
        }
        os << "\n";
    }
    os << "        </DataArray>\n";
    os << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    // Offsets are positions in the connectivity array; shift rank r's local
    // offsets by the number of connectivity entries of the previous ranks.
    {
        std::vector<int> off_start(nranks, 0);
        for (int r = 1; r < nranks; ++r) {
            off_start[r] = off_start[r - 1] + off_counts[r - 1];
        }
        for (int r = 0; r < nranks; ++r) {
            const int begin = off_start[r];
            const int end = begin + off_counts[r];
            for (int i = begin; i < end; ++i) {
                os << (all_offs[i] + conn_start[r]) << " ";
                if ((i - begin + 1) % 12 == 0) os << "\n";
            }
            os << "\n";
        }
    }
    os << "        </DataArray>\n";
    os << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    for (size_t i = 0; i < all_types.size(); ++i) {
        os << static_cast<int>(all_types[i]) << " ";
        if ((i + 1) % 20 == 0) os << "\n";
    }
    os << "\n        </DataArray>\n";
    os << "      </Cells>\n";
    os << "      <CellData>\n";
    // cell_d layout per cell: density,u,v,0,pressure,mach,temperature.
    {
        auto write_array = [&os](const std::vector<double>& vals,
                                 const char* name, int comps) {
            os << "        <DataArray type=\"Float64\" Name=\"" << name
               << "\" NumberOfComponents=\"" << comps
               << "\" format=\"ascii\">\n";
            for (size_t i = 0; i < vals.size(); ++i) {
                os << fmt_double(vals[i]) << " ";
                if ((i + 1) % 8 == 0) os << "\n";
            }
            os << "\n        </DataArray>\n";
        };
        const size_t ncells = all_offs.size();
        std::vector<double> density, vel, pressure, mach, temp;
        density.reserve(ncells);
        vel.reserve(ncells * 3);
        pressure.reserve(ncells);
        mach.reserve(ncells);
        temp.reserve(ncells);
        for (size_t c = 0; c < ncells; ++c) {
            density.push_back(all_cell_d[7 * c + 0]);
            vel.push_back(all_cell_d[7 * c + 1]);
            vel.push_back(all_cell_d[7 * c + 2]);
            vel.push_back(all_cell_d[7 * c + 3]);
            pressure.push_back(all_cell_d[7 * c + 4]);
            mach.push_back(all_cell_d[7 * c + 5]);
            temp.push_back(all_cell_d[7 * c + 6]);
        }
        write_array(density, "density", 1);
        write_array(vel, "velocity", 3);
        write_array(pressure, "pressure", 1);
        write_array(mach, "mach", 1);
        write_array(temp, "temperature", 1);
    }
    os << "        <DataArray type=\"Int32\" Name=\"partition_rank\" "
          "format=\"ascii\">\n";
    for (size_t i = 0; i < all_rank.size(); ++i) {
        os << all_rank[i] << " ";
        if ((i + 1) % 20 == 0) os << "\n";
    }
    os << "\n        </DataArray>\n";
    os << "      </CellData>\n";
    os << "    </Piece>\n";
    os << "  </UnstructuredGrid>\n";
    os << "</VTKFile>\n";

    std::ofstream f(output_dir + "/field_final.vtu", std::ios::out);
    f << os.str();
}

void write_restart(const DistributedMesh& mesh,
                   const std::vector<double>& state,
                   const std::string& output_dir, MPI_Comm comm, int rank) {
    const int num_owned = static_cast<int>(mesh.owned_cells.size());
    std::vector<double> local(
        state.begin(),
        state.begin() + static_cast<size_t>(num_owned) * kStateSize);
    const std::vector<double> all =
        gather_variable(local, comm, 0, MPI_DOUBLE);

    int num_cells_global = 0;
    MPI_Allreduce(&num_owned, &num_cells_global, 1, MPI_INT, MPI_SUM, comm);

    if (rank != 0) return;
    std::ofstream f(output_dir + "/restart_final.bin", std::ios::binary);
    const char magic[12] = "CFDRESTART1";
    f.write(magic, 12);
    f.write(reinterpret_cast<const char*>(&num_cells_global), sizeof(int));
    f.write(reinterpret_cast<const char*>(&kStateSize), sizeof(int));
    f.write(reinterpret_cast<const char*>(all.data()),
            static_cast<std::streamsize>(all.size() * sizeof(double)));
}

void write_metadata_json(const DistributedMesh& mesh, const CaseConfig& config,
                         const SolverStats& stats,
                         const std::string& flux_type,
                         const std::string& output_dir, int num_cells_global,
                         int num_faces_global, int edge_cut,
                         const std::string& start_utc,
                         const std::string& end_utc) {
    int mpi_ranks = 1;
    MPI_Comm_size(mesh.comm, &mpi_ranks);
    const bool transient = (config.run_control.type == "transient");

    json j;
    j["case_id"] = config.case_id;
    j["solver_name"] = "cfd_solver";
    j["solver_version"] = "0.5.0";
    j["git_revision"] = nullptr;
    j["mpi_ranks"] = mpi_ranks;
    j["mesh_file"] = config.mesh.file;
    j["num_cells_global"] = num_cells_global;
    j["num_faces_global"] = num_faces_global;
    j["num_cells_owned_local"] =
        static_cast<int>(mesh.owned_cells.size());
    j["num_cells_ghost_local"] =
        static_cast<int>(mesh.ghost_cells.size());
    j["partitioner"] = "metis_kway";
    j["partition_edge_cut"] = edge_cut;
    j["halo_exchange"] = "neighbor_isend_irecv";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = "compressible_navier_stokes_2d";
    j["inviscid_flux"] = flux_type;
    j["entropy_fix"] =
        (flux_type == "roe") ? json("harten_yee") : json(nullptr);
    j["viscous_flux"] =
        (config.physics.mode == "laminar") ? "laminar_constant_viscosity"
                                           : "disabled";
    j["time_integrator"] =
        transient ? "bdf2_dual_time" : "implicit_lusgs_pseudo_time";
    j["implicit_solver"] = "block_lusgs";
    j["reconstruction"] = "linear_least_squares";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = 2;
    j["positivity_preservation"] = "density_clamp_pressure_freestream_reset";
    j["wall_boundary_output_semantics"] = "boundary_value";
    j["true_bdf2_inner_loop"] = transient;
    j["typical_inner_iterations"] =
        static_cast<int>(std::lround(stats.mean_inner_its));
    j["min_inner_iterations"] = config.run_control.min_inner_iterations;
    j["max_inner_iterations"] = config.run_control.max_inner_iterations;
    j["observed_min_inner_iterations"] = stats.min_inner_its;
    j["observed_max_inner_iterations"] = stats.max_inner_its;
    j["inner_residual_reduction_target"] =
        config.run_control.inner_residual_reduction_target;
    j["inner_target_misses"] = stats.inner_target_misses;
    j["inner_target_converged_fraction"] = stats.converged_fraction;
    j["last_inner_residual_ratio"] = stats.last_inner_residual_ratio;
    j["start_time_utc"] = start_utc;
    j["end_time_utc"] = end_utc;
    j["completed"] = true;
    j["convergence_status"] = stats.convergence_status;

    std::ofstream f(output_dir + "/metadata.json", std::ios::out);
    f << j.dump(2) << "\n";
}

void write_run_status_json(const CaseConfig& config, const SolverStats& stats,
                           int final_step, const std::string& command,
                           const std::string& output_dir, int mpi_ranks) {
    const bool transient = (config.run_control.type == "transient");
    const double final_phys_time =
        transient ? stats.total_steps * config.run_control.time_step : 0.0;

    json j;
    j["case_id"] = config.case_id;
    j["command"] = command;
    j["mpi_ranks"] = mpi_ranks;
    j["wall_time_seconds"] = stats.wall_time_seconds;
    j["final_step"] = final_step;
    j["final_physical_time"] = final_phys_time;
    j["convergence_status"] = stats.convergence_status;
    j["residual_reduction_orders"] = stats.residual_reduction_orders;
    j["notes"] = (stats.convergence_status == "converged")
                     ? "residual reduction target met"
                     : (stats.convergence_status == "statistically_periodic"
                            ? "transient run completed to final_time"
                            : "run completed but convergence target not met");

    std::ofstream f(output_dir + "/run_status.json", std::ios::out);
    f << j.dump(2) << "\n";
}

} // namespace solver
