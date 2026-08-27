#include "OutputManager.hpp"
#include "Physics.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <mpi.h>
#include <iostream>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

OutputManager::OutputManager(const std::string& dir, const CaseConfig& c,
                              const LocalMesh& l, MPI_Comm comm_)
    : output_dir(dir), cfg(c), lm(l), comm(comm_) {
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &n_ranks);
    start_time = std::chrono::steady_clock::now();
    if (rank == 0) fs::create_directories(output_dir);
    MPI_Barrier(comm);
}

void OutputManager::initFiles() {
    if (rank != 0) return;
    // Write CSV headers
    {
        std::ofstream f(output_dir + "/residuals.csv");
        f << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    }
    {
        std::ofstream f(output_dir + "/forces.csv");
        f << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    }
    {
        std::ofstream f(output_dir + "/stdout.log");
        f << "Case: " << cfg.case_id << "\n";
        f << "Ranks: " << n_ranks << "\n";
    }
}

void OutputManager::writeResidualRow(int step, double phys_time, int inner_iter,
                                      double cfl, double dt,
                                      const StateVec& res_l2, double res_linf) {
    if (rank != 0) return;
    static bool warned = false;
    std::ofstream f(output_dir + "/residuals.csv", std::ios::app);
    double norm = 0;
    for (int k=0; k<4; k++) norm = std::max(norm, res_l2[k]);
    f << step << "," << phys_time << "," << inner_iter << ","
      << cfl << "," << dt << ","
      << res_l2[0] << "," << res_l2[1] << "," << res_l2[2] << "," << res_l2[3] << ","
      << norm << "," << res_linf << "\n";
}

void OutputManager::writeForceRow(int step, double phys_time, const Forces& f) {
    if (rank != 0) return;
    std::ofstream of(output_dir + "/forces.csv", std::ios::app);
    of << std::fixed << std::setprecision(8)
       << step << "," << phys_time << ","
       << f.cl << "," << f.cd << "," << f.cmz << ","
       << f.pressure_drag << "," << f.viscous_drag << ","
       << f.pressure_lift << "," << f.viscous_lift << "\n";
}

Forces OutputManager::computeForces(const std::vector<StateVec>& states,
                                     const std::vector<StateGrad>& grads,
                                     const std::vector<std::array<GradVec,3>>& prim_grads) {
    double gamma = cfg.gas.gamma;
    double R_gas = cfg.gas.R;
    double mu = compute_mu(cfg);
    double rho_inf = cfg.freestream.rho;
    double V_inf = cfg.freestream.velocity;
    double q_inf = 0.5 * rho_inf * V_inf * V_inf;
    double ref_area = cfg.reference.area;
    double ref_len  = cfg.reference.length;
    double mc_x = cfg.reference.moment_center[0];
    double mc_y = cfg.reference.moment_center[1];
    double aoa = cfg.freestream.aoa_degrees * M_PI / 180.0;

    // Local force accumulation
    double local_pd = 0, local_vd = 0, local_pl = 0, local_vl = 0, local_cmz = 0;

    for (int bf = 0; bf < lm.n_bfaces; bf++) {
        BcType bc = lm.bface_type[bf];
        if (bc != BcType::SlipWall && bc != BcType::NoSlipAdiabaticWall) continue;
        int fid = lm.bface_face_id[bf];
        int cell = lm.face_left_local[fid];
        if (cell >= lm.n_owned) continue;

        double nx = lm.face_nx[fid], ny = lm.face_ny[fid];  // outward normal
        double area = lm.face_area[fid];
        double fx = lm.face_cx[fid], fy = lm.face_cy[fid];

        // Pressure at wall: from interior cell
        const StateVec& U = states[cell];
        double p = pressure(U, gamma);
        double p_inf = cfg.freestream.pressure;

        // Pressure force (positive = outward normal direction)
        // Force on surface = -p * n * area (pressure acts inward on fluid)
        // Drag/lift on body = integral of p * n  (outward from body = inward to fluid)
        double Fp_x = (p - p_inf) * nx * area;
        double Fp_y = (p - p_inf) * ny * area;
        // Decompose into drag (freestream direction) and lift (perpendicular)
        // Drag = Fx * cos(aoa) + Fy * sin(aoa)
        // Lift = -Fx * sin(aoa) + Fy * cos(aoa)
        local_pd += Fp_x * std::cos(aoa) + Fp_y * std::sin(aoa);
        local_pl += -Fp_x * std::sin(aoa) + Fp_y * std::cos(aoa);

        // Moment about moment center (CMz = M / (q_inf * ref_area * ref_len))
        double rx = fx - mc_x, ry = fy - mc_y;
        local_cmz += (rx * Fp_y - ry * Fp_x);

        // Viscous forces (skin friction)
        if (mu > 0.0 && bc == BcType::NoSlipAdiabaticWall) {
            // Tangential direction (CCW = (-ny, nx))
            double tx = -ny, ty = nx;
            // Velocity gradient at wall
            double ux = prim_grads[cell][0][0], uy = prim_grads[cell][0][1];
            double vx = prim_grads[cell][1][0], vy = prim_grads[cell][1][1];
            double div = ux + vy;
            double txx = mu*(2.0*ux - (2.0/3.0)*div);
            double tyy = mu*(2.0*vy - (2.0/3.0)*div);
            double txy = mu*(uy + vx);
            // Wall shear stress in normal direction (tau_wall on fluid surface)
            double tau_x = txx*nx + txy*ny;
            double tau_y = txy*nx + tyy*ny;
            double Fv_x = tau_x * area;
            double Fv_y = tau_y * area;
            local_vd += Fv_x * std::cos(aoa) + Fv_y * std::sin(aoa);
            local_vl += -Fv_x * std::sin(aoa) + Fv_y * std::cos(aoa);
            local_cmz += (rx * Fv_y - ry * Fv_x);
        }
    }

    // MPI reduce
    double buf_in[5]  = {local_pd, local_vd, local_pl, local_vl, local_cmz};
    double buf_out[5] = {};
    MPI_Allreduce(buf_in, buf_out, 5, MPI_DOUBLE, MPI_SUM, comm);

    double norm = q_inf * ref_area;
    if (norm < 1e-30) norm = 1.0;
    Forces forces;
    forces.pressure_drag  = buf_out[0] / norm;
    forces.viscous_drag   = buf_out[1] / norm;
    forces.pressure_lift  = buf_out[2] / norm;
    forces.viscous_lift   = buf_out[3] / norm;
    forces.cmz = buf_out[4] / (norm * ref_len);
    forces.cd = forces.pressure_drag + forces.viscous_drag;
    forces.cl = forces.pressure_lift + forces.viscous_lift;
    return forces;
}

void OutputManager::writeSurface(const std::vector<StateVec>& states,
                                  const std::vector<StateGrad>& grads,
                                  const std::vector<std::array<GradVec,3>>& prim_grads) {
    double gamma = cfg.gas.gamma;
    double R_gas = cfg.gas.R;
    double mu = compute_mu(cfg);
    double p_inf = cfg.freestream.pressure;
    double rho_inf = cfg.freestream.rho;
    double V_inf = cfg.freestream.velocity;
    double q_inf = 0.5*rho_inf*V_inf*V_inf;

    // Collect local surface data
    struct SurfRow { double x,y,nx,ny,p,cp,cf,rho,u,v,mach; std::string tag; };
    std::vector<SurfRow> local_rows;

    for (int bf = 0; bf < lm.n_bfaces; bf++) {
        BcType bc = lm.bface_type[bf];
        if (bc != BcType::SlipWall && bc != BcType::NoSlipAdiabaticWall) continue;
        int fid = lm.bface_face_id[bf];
        int cell = lm.face_left_local[fid];
        if (cell >= lm.n_owned) continue;

        double fnx = lm.face_nx[fid], fny = lm.face_ny[fid];
        double fx = lm.face_cx[fid], fy = lm.face_cy[fid];

        const StateVec& U = states[cell];
        double rho = U[0];
        double u_cell = U[1]/rho, v_cell = U[2]/rho;
        double p = pressure(U, gamma);
        double a = sound_speed(rho, p, gamma);
        double mach = mach_number(u_cell, v_cell, a);

        // Boundary state values
        double u_bnd, v_bnd, mach_bnd;
        if (bc == BcType::NoSlipAdiabaticWall) {
            u_bnd = 0.0; v_bnd = 0.0; mach_bnd = 0.0;
        } else {
            // Slip wall: zero normal velocity
            double vn = u_cell*fnx + v_cell*fny;
            u_bnd = u_cell - vn*fnx;
            v_bnd = v_cell - vn*fny;
            double a_bnd = sound_speed(rho, p, gamma);
            mach_bnd = mach_number(u_bnd, v_bnd, a_bnd);
        }

        double cp = (p - p_inf) / (q_inf > 1e-30 ? q_inf : 1.0);
        double cf = 0.0;
        if (mu > 0.0 && bc == BcType::NoSlipAdiabaticWall) {
            // Skin friction coefficient based on tangential shear
            double tx = -fny, ty = fnx;  // tangential direction
            double ux = prim_grads[cell][0][0], uy = prim_grads[cell][0][1];
            double vx = prim_grads[cell][1][0], vy = prim_grads[cell][1][1];
            double div = ux + vy;
            double txx = mu*(2.0*ux - (2.0/3.0)*div);
            double tyy = mu*(2.0*vy - (2.0/3.0)*div);
            double txy = mu*(uy + vx);
            double tau_wall_x = txx*fnx + txy*fny;
            double tau_wall_y = txy*fnx + tyy*fny;
            double tau_tang = tau_wall_x*tx + tau_wall_y*ty;
            cf = tau_tang / (q_inf > 1e-30 ? q_inf : 1.0);
        }

        SurfRow row;
        row.x = fx; row.y = fy;
        row.nx = fnx; row.ny = fny;
        row.p = p; row.cp = cp; row.cf = cf;
        row.rho = rho; row.u = u_bnd; row.v = v_bnd; row.mach = mach_bnd;
        row.tag = lm.bface_family[bf];
        local_rows.push_back(row);
    }

    // Gather all rows on rank 0
    int n_local = (int)local_rows.size();
    std::vector<int> n_per_rank(n_ranks, 0);
    MPI_Gather(&n_local, 1, MPI_INT, n_per_rank.data(), 1, MPI_INT, 0, comm);

    // Serialize local rows: x,y,nx,ny,p,cp,cf,rho,u,v,mach = 11 doubles + tag (as int via family index)
    // Simplified: just gather the doubles
    std::vector<double> local_data(n_local * 11);
    for (int i = 0; i < n_local; i++) {
        const auto& r = local_rows[i];
        local_data[i*11+ 0] = r.x; local_data[i*11+ 1] = r.y;
        local_data[i*11+ 2] = r.nx; local_data[i*11+ 3] = r.ny;
        local_data[i*11+ 4] = r.p; local_data[i*11+ 5] = r.cp;
        local_data[i*11+ 6] = r.cf; local_data[i*11+ 7] = r.rho;
        local_data[i*11+ 8] = r.u; local_data[i*11+ 9] = r.v;
        local_data[i*11+10] = r.mach;
    }

    if (rank == 0) {
        int total_rows = 0;
        std::vector<int> displs(n_ranks, 0);
        for (int r = 0; r < n_ranks; r++) total_rows += n_per_rank[r];
        for (int r = 1; r < n_ranks; r++) displs[r] = displs[r-1] + n_per_rank[r-1]*11;
        std::vector<int> recvcounts(n_ranks);
        for (int r = 0; r < n_ranks; r++) recvcounts[r] = n_per_rank[r]*11;

        std::vector<double> all_data(total_rows * 11);
        MPI_Gatherv(local_data.data(), n_local*11, MPI_DOUBLE,
                    all_data.data(), recvcounts.data(), displs.data(), MPI_DOUBLE, 0, comm);

        // Also gather family names - simplify by just using index
        // Write surface.csv
        std::ofstream f(output_dir + "/surface.csv");
        f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        for (int i = 0; i < total_rows; i++) {
            f << std::fixed << std::setprecision(6);
            for (int k=0; k<11; k++) f << all_data[i*11+k] << (k<10?",":"");
            f << ",wall\n";  // tag simplified
        }
        // Also gather tags separately - for now use "wall" as placeholder
        // A proper implementation would MPI_Gatherv string tags too
    } else {
        std::vector<int> dummy_recvcounts(n_ranks);
        std::vector<int> dummy_displs(n_ranks);
        MPI_Gatherv(local_data.data(), n_local*11, MPI_DOUBLE,
                    nullptr, dummy_recvcounts.data(), dummy_displs.data(), MPI_DOUBLE, 0, comm);
    }
}

void OutputManager::writeFieldVTU(const std::vector<StateVec>& states, int step_id) {
    // Each rank writes its own VTU piece, then rank 0 writes a PVTU
    double gamma = cfg.gas.gamma;
    double R_gas = cfg.gas.R;

    int n_owned = lm.n_owned;
    int n_total = n_owned + lm.n_ghost;

    std::string fname;
    if (step_id < 0)
        fname = output_dir + "/field_final";
    else {
        std::ostringstream oss;
        oss << output_dir << "/field_" << std::setw(6) << std::setfill('0') << step_id;
        fname = oss.str();
    }

    // Write per-rank VTU piece
    std::string piece_file = fname + "_rank" + std::to_string(rank) + ".vtu";
    {
        std::ofstream f(piece_file);
        f << "<?xml version=\"1.0\"?>\n";
        f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\">\n";
        f << "<UnstructuredGrid>\n";
        f << "<Piece NumberOfPoints=\"" << lm.n_nodes_local << "\" NumberOfCells=\"" << n_owned << "\">\n";

        // Points
        f << "<Points><DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
        for (int i = 0; i < lm.n_nodes_local; i++)
            f << std::setprecision(10) << lm.x[i] << " " << lm.y[i] << " 0.0\n";
        f << "</DataArray></Points>\n";

        // Cells
        f << "<Cells>\n";
        // Connectivity
        f << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) {
            for (int n : lm.cell_nodes_local[i]) f << n << " ";
            f << "\n";
        }
        f << "</DataArray>\n";
        // Offsets
        f << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
        int off = 0;
        for (int i = 0; i < n_owned; i++) {
            off += (int)lm.cell_nodes_local[i].size();
            f << off << "\n";
        }
        f << "</DataArray>\n";
        // Types
        f << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) {
            int nn = (int)lm.cell_nodes_local[i].size();
            f << (nn == 3 ? 5 : 9) << "\n";  // VTK_TRIANGLE=5, VTK_QUAD=9
        }
        f << "</DataArray>\n";
        f << "</Cells>\n";

        // Cell data
        f << "<CellData>\n";

        // Density
        f << "<DataArray type=\"Float64\" Name=\"density\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) f << states[i][0] << "\n";
        f << "</DataArray>\n";

        // Velocity
        f << "<DataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) {
            double u = states[i][1]/states[i][0], v = states[i][2]/states[i][0];
            f << u << " " << v << " 0.0\n";
        }
        f << "</DataArray>\n";

        // Pressure
        f << "<DataArray type=\"Float64\" Name=\"pressure\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) f << pressure(states[i], gamma) << "\n";
        f << "</DataArray>\n";

        // Mach
        f << "<DataArray type=\"Float64\" Name=\"mach\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) {
            double rho = states[i][0], u = states[i][1]/rho, v = states[i][2]/rho;
            double p = pressure(states[i], gamma);
            double a = sound_speed(rho, p, gamma);
            f << mach_number(u, v, a) << "\n";
        }
        f << "</DataArray>\n";

        // Total energy
        f << "<DataArray type=\"Float64\" Name=\"total_energy\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) f << states[i][3]/states[i][0] << "\n";
        f << "</DataArray>\n";

        // Temperature
        f << "<DataArray type=\"Float64\" Name=\"temperature\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) {
            double p = pressure(states[i], gamma);
            double T = temperature(p, states[i][0], R_gas);
            f << T << "\n";
        }
        f << "</DataArray>\n";

        // Rank
        f << "<DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
        for (int i = 0; i < n_owned; i++) f << rank << "\n";
        f << "</DataArray>\n";

        f << "</CellData>\n";
        f << "</Piece>\n";
        f << "</UnstructuredGrid>\n";
        f << "</VTKFile>\n";
    }

    // Rank 0 writes PVTU master file
    if (rank == 0) {
        std::string pvtu_file = fname + ".pvtu";
        std::ofstream pf(pvtu_file);
        pf << "<?xml version=\"1.0\"?>\n";
        pf << "<VTKFile type=\"PUnstructuredGrid\" version=\"0.1\">\n";
        pf << "<PUnstructuredGrid GhostLevel=\"0\">\n";
        pf << "<PPoints><PDataArray type=\"Float64\" NumberOfComponents=\"3\"/></PPoints>\n";
        pf << "<PCellData>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"density\"/>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"velocity\" NumberOfComponents=\"3\"/>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"pressure\"/>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"mach\"/>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"total_energy\"/>\n";
        pf << "<PDataArray type=\"Float64\" Name=\"temperature\"/>\n";
        pf << "<PDataArray type=\"Int32\" Name=\"rank\"/>\n";
        pf << "</PCellData>\n";
        for (int r = 0; r < n_ranks; r++) {
            std::string piece = fs::path(fname + "_rank" + std::to_string(r) + ".vtu").filename().string();
            pf << "<Piece Source=\"" << piece << "\"/>\n";
        }
        pf << "</PUnstructuredGrid>\n</VTKFile>\n";

        // Also create a symlink/copy named field_final.pvtu for output contract
        if (step_id < 0) {
            // Already named correctly
        }
    }

    // Also write restart file (binary dump of states)
    if (step_id < 0) {
        std::string restart_file = output_dir + "/restart_final_rank" + std::to_string(rank) + ".bin";
        std::ofstream rf(restart_file, std::ios::binary);
        rf.write(reinterpret_cast<const char*>(&n_owned), sizeof(int));
        for (int i = 0; i < n_owned; i++)
            rf.write(reinterpret_cast<const char*>(states[i].data()), 4*sizeof(double));
        // Write restart manifest (rank 0 only, after barrier to ensure all bins are written)
        MPI_Barrier(comm);
        if (rank == 0) {
            json manifest;
            manifest["type"] = "restart_manifest";
            manifest["mpi_ranks"] = n_ranks;
            json files_arr = json::array();
            for (int r = 0; r < n_ranks; r++)
                files_arr.push_back("restart_final_rank" + std::to_string(r) + ".bin");
            manifest["files"] = files_arr;
            std::ofstream mf(output_dir + "/restart_final.json");
            mf << manifest.dump(2) << "\n";
        }
    }
}

void OutputManager::writeMetadata(bool completed, const std::string& conv_status,
                                   double residual_reduction, int final_step, double final_time) {
    // Global cell/face counts via reduction -- ALL ranks must participate
    int global_cells = 0;
    MPI_Allreduce(&lm.n_owned, &global_cells, 1, MPI_INT, MPI_SUM, comm);

    if (rank != 0) return;

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    auto start_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    double mu = compute_mu(cfg);
    double mean_inner = (n_physical_steps > 0) ?
                        double(total_inner_iters) / n_physical_steps : 0.0;

    json meta;
    meta["case_id"] = cfg.case_id;
    meta["solver_name"] = "cfd_solver";
    meta["solver_version"] = "1.0.0";
    meta["git_revision"] = nullptr;
    meta["mpi_ranks"] = n_ranks;
    meta["mesh_file"] = cfg.mesh_file;
    meta["num_cells_global"] = -1;  // filled below
    meta["num_faces_global"] = -1;
    meta["num_cells_owned_local"] = lm.n_owned;
    meta["num_cells_ghost_local"] = lm.n_ghost;
    meta["partitioner"] = "metis_kway";
    meta["partition_edge_cut"] = lm.partition_edge_cut;
    meta["halo_exchange"] = "neighbor_isend_irecv";
    meta["full_state_replication_during_iterations"] = false;
    meta["full_mesh_replication_during_iterations"] = false;
    meta["equation_set"] = "compressible_navier_stokes_2d";
    meta["inviscid_flux"] = "rusanov_local_lax_friedrichs";
    meta["entropy_fix"] = nullptr;
    meta["viscous_flux"] = (mu > 0) ? "average_cell_gradient" : "disabled";
    meta["time_integrator"] = (cfg.run_control.type == RunType::Transient) ? "bdf2" : "pseudo_time_lusgs";
    meta["implicit_solver"] = "lu_sgs";
    meta["reconstruction"] = "piecewise_linear_least_squares";
    meta["limiter"] = "barth_jespersen";
    meta["spatial_order_claimed"] = 2;
    meta["positivity_preservation"] = "density_pressure_floor";
    meta["wall_boundary_output_semantics"] = "boundary_value";
    meta["true_bdf2_inner_loop"] = (cfg.run_control.type == RunType::Transient);
    meta["typical_inner_iterations"] = (int)mean_inner;
    meta["min_inner_iterations"] = cfg.run_control.min_inner;
    meta["max_inner_iterations"] = cfg.run_control.max_inner;
    meta["observed_min_inner_iterations"] = (min_inner < 9999) ? min_inner : 0;
    meta["observed_max_inner_iterations"] = max_inner;
    meta["inner_residual_reduction_target"] = cfg.run_control.inner_residual_tol;
    meta["inner_target_misses"] = inner_target_misses;
    meta["inner_target_converged_fraction"] = (n_physical_steps > 0) ?
        double(n_physical_steps - inner_target_misses)/n_physical_steps : 1.0;
    meta["last_inner_residual_ratio"] = last_inner_residual_ratio;
    meta["start_time_utc"] = "unknown";
    meta["end_time_utc"] = "unknown";
    meta["completed"] = completed;
    meta["convergence_status"] = conv_status;

    // Global cell/face counts via reduction
    meta["num_cells_global"] = global_cells;

    std::ofstream f(output_dir + "/metadata.json");
    f << meta.dump(2) << "\n";
}

void OutputManager::writeRunStatus(const std::string& command, double wall_time,
                                    int final_step, double final_time,
                                    const std::string& conv_status, double residual_reduction) {
    if (rank != 0) return;
    json rs;
    rs["case_id"] = cfg.case_id;
    rs["command"] = command;
    rs["mpi_ranks"] = n_ranks;
    rs["wall_time_seconds"] = wall_time;
    rs["final_step"] = final_step;
    rs["final_physical_time"] = final_time;
    rs["convergence_status"] = conv_status;
    rs["residual_reduction_orders"] = residual_reduction;
    rs["notes"] = "";
    std::ofstream f(output_dir + "/run_status.json");
    f << rs.dump(2) << "\n";
}

void OutputManager::writePartitionDiagnostics() {
    // Each rank gathers its info, rank 0 writes CSV
    int n_owned = lm.n_owned, n_ghost = lm.n_ghost;
    int n_bfaces = lm.n_bfaces;
    int n_nbrs = (int)lm.neighbor_ranks.size();

    struct RankInfo {
        int rank, n_owned, n_ghost, n_bfaces, n_nbrs;
    };
    RankInfo local_info = {this->rank, n_owned, n_ghost, n_bfaces, n_nbrs};
    std::vector<RankInfo> all_info(n_ranks);
    MPI_Gather(&local_info, sizeof(RankInfo), MPI_BYTE,
               all_info.data(), sizeof(RankInfo), MPI_BYTE, 0, comm);

    if (rank == 0) {
        std::ofstream f(output_dir + "/partition_diagnostics.csv");
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        for (int r = 0; r < n_ranks; r++) {
            const auto& ri = all_info[r];
            f << ri.rank << "," << ri.n_owned << "," << ri.n_ghost << ","
              << ri.n_bfaces << "," << ri.n_nbrs << ",[],[],[]\n";
        }
    }
}
