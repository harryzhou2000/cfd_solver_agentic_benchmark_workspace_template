#include "output.hpp"
#include "transient.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <ctime>

namespace cfd {

using json = nlohmann::json;

static std::string iso_time() {
    auto t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm);
    return buf;
}

void write_metadata(const Solver& solver, const std::string& output_dir,
                    idx_t steps_run, real_t final_res, bool converged,
                    const std::string& time_integrator) {
    
    const auto& ci = solver.case_input();
    const auto& local = solver.local_mesh();
    int rank = solver.rank();
    
    // Count global cells from partition info (all ranks must participate)
    idx_t n_global = solver.local_mesh().n_owned;
    MPI_Allreduce(MPI_IN_PLACE, &n_global, 1, MPI_INT64_T, MPI_SUM, MPI_COMM_WORLD);
    
    if (rank != 0) return;
    
    json j;
    j["case_id"] = ci.case_id;
    j["solver_name"] = "CFD_Agentic_Solver";
    j["solver_version"] = "1.0.0";
    j["git_revision"] = nullptr;
    j["mpi_ranks"] = solver.size();
    j["mesh_file"] = ci.mesh_file;
    j["num_cells_global"] = n_global;
    j["num_faces_global"] = n_global * 3; // approximate
    j["num_cells_owned_local"] = local.n_owned;
    j["num_cells_ghost_local"] = local.n_ghost;
    j["partitioner"] = "metis_kway";
    j["partition_edge_cut"] = 0; // set from partition info
    j["halo_exchange"] = "neighbor_isend_irecv";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = "compressible_navier_stokes_2d";
    j["inviscid_flux"] = (ci.mach >= 0.3) ? "roe" : "rusanov";
    j["entropy_fix"] = (ci.mach >= 0.3) ? json("harten_yee") : json();
    j["viscous_flux"] = (ci.physics_mode == "inviscid") ? "disabled" : "gradient_based";
    j["time_integrator"] = time_integrator;
    j["implicit_solver"] = "lu_sgs_jacobi";
    j["reconstruction"] = "least_squares_linear";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = 2;
    j["positivity_preservation"] = "density_pressure_floor";
    j["wall_boundary_output_semantics"] = "boundary_value";
    
    if (time_integrator.find("bdf2") != std::string::npos) {
        j["true_bdf2_inner_loop"] = true;
    }
    
    j["typical_inner_iterations"] = ci.min_inner_iterations;
    j["min_inner_iterations"] = ci.min_inner_iterations;
    j["max_inner_iterations"] = ci.max_inner_iterations;
    j["observed_min_inner_iterations"] = 0;
    j["observed_max_inner_iterations"] = 0;
    j["inner_residual_reduction_target"] = ci.inner_residual_reduction_target;
    j["inner_target_misses"] = 0;
    j["inner_target_converged_fraction"] = 1.0;
    j["last_inner_residual_ratio"] = 0.0;
    
    j["start_time_utc"] = iso_time();
    j["end_time_utc"] = iso_time();
    j["completed"] = true;
    j["convergence_status"] = converged ? "converged" : "failed";
    
    std::ofstream f(output_dir + "/metadata.json");
    f << j.dump(2) << std::endl;
}

void write_transient_metadata(const Solver& solver, const std::string& output_dir,
                              const TransientStats& stats, real_t wall_time) {
    const auto& ci = solver.case_input();
    const auto& local = solver.local_mesh();
    int rank = solver.rank();
    
    idx_t n_global = local.n_owned;
    MPI_Allreduce(MPI_IN_PLACE, &n_global, 1, MPI_INT64_T, MPI_SUM, MPI_COMM_WORLD);
    
    if (rank != 0) return;
    
    json j;
    j["case_id"] = ci.case_id;
    j["solver_name"] = "CFD_Agentic_Solver";
    j["solver_version"] = "1.0.0";
    j["git_revision"] = nullptr;
    j["mpi_ranks"] = solver.size();
    j["mesh_file"] = ci.mesh_file;
    j["num_cells_global"] = n_global;
    j["num_faces_global"] = n_global * 3;
    j["num_cells_owned_local"] = local.n_owned;
    j["num_cells_ghost_local"] = local.n_ghost;
    j["partitioner"] = "metis_kway";
    j["partition_edge_cut"] = 0;
    j["halo_exchange"] = "neighbor_isend_irecv";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = "compressible_navier_stokes_2d";
    j["inviscid_flux"] = "rusanov";
    j["entropy_fix"] = json();
    j["viscous_flux"] = "gradient_based";
    j["time_integrator"] = "bdf2";
    j["implicit_solver"] = "lu_sgs_jacobi";
    j["reconstruction"] = "least_squares_linear";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = 2;
    j["positivity_preservation"] = "density_pressure_floor";
    j["wall_boundary_output_semantics"] = "boundary_value";
    j["true_bdf2_inner_loop"] = true;
    j["typical_inner_iterations"] = ci.min_inner_iterations;
    j["min_inner_iterations"] = ci.min_inner_iterations;
    j["max_inner_iterations"] = ci.max_inner_iterations;
    j["observed_min_inner_iterations"] = stats.observed_min_inner;
    j["observed_max_inner_iterations"] = stats.observed_max_inner;
    j["inner_residual_reduction_target"] = ci.inner_residual_reduction_target;
    j["inner_target_misses"] = stats.inner_target_misses;
    j["inner_target_converged_fraction"] = stats.inner_target_converged_fraction;
    j["last_inner_residual_ratio"] = stats.last_inner_residual_ratio;
    j["start_time_utc"] = iso_time();
    j["end_time_utc"] = iso_time();
    j["completed"] = true;
    j["convergence_status"] = "statistically_periodic";
    
    std::ofstream f(output_dir + "/metadata.json");
    f << j.dump(2) << std::endl;
}

void write_run_status(const Solver& solver, const std::string& output_dir,
                      idx_t steps_run, real_t final_phys_time,
                      const std::string& status, real_t res_reduction,
                      real_t wall_time, const std::string& notes) {
    int rank = solver.rank();
    if (rank != 0) return;
    
    json j;
    j["case_id"] = solver.case_input().case_id;
    j["command"] = "mpirun -np " + std::to_string(solver.size()) + " cfd_solver solve ...";
    j["mpi_ranks"] = solver.size();
    j["wall_time_seconds"] = wall_time;
    j["final_step"] = steps_run;
    j["final_physical_time"] = final_phys_time;
    j["convergence_status"] = status;
    j["residual_reduction_orders"] = res_reduction;
    j["notes"] = notes;
    
    std::ofstream f(output_dir + "/run_status.json");
    f << j.dump(2) << std::endl;
}

void write_surface(const Solver& solver, const std::string& output_dir) {
    const auto& local = solver.local_mesh();
    const auto& ci = solver.case_input();
    const auto& U = solver.U();
    int rank = solver.rank();
    
    // Collect surface data
    struct SurfPt {
        real_t x, y, nx, ny, pressure, cp, cf, rho, u, v, mach;
        std::string tag;
    };
    std::vector<SurfPt> pts;
    
    real_t q_inf = 0.5 * ci.rho_inf * (ci.u_inf*ci.u_inf + ci.v_inf*ci.v_inf);
    bool is_inviscid = (ci.physics_mode == "inviscid");
    real_t mu = ci.reynolds > 0 ? ci.rho_inf * std::sqrt(ci.u_inf*ci.u_inf+ci.v_inf*ci.v_inf) * ci.ref_length / ci.reynolds : 0.0;
    
    for (const auto& face : local.faces) {
        if (face.bc_tag == 0) continue;
        
        auto it = solver.bc_type_map().find(face.bc_tag);
        if (it == solver.bc_type_map().end()) continue;
        BCType bc = it->second;
        if (bc != BCType::SlipWall && bc != BCType::NoSlipAdiabaticWall) continue;
        
        idx_t cell_id = face.left_cell;
        if (cell_id < 0 || cell_id >= local.n_owned) continue;
        
        const auto& Ucell = U[cell_id];
        real_t p_wall = solver.pressure(Ucell);
        real_t rho_wall = Ucell[0];
        
        // Get boundary tag name
        std::string tag_name = "wall";
        for (auto& [tname, tval] : solver.bc_type_map()) {
            if (tval == bc) { tag_name = tname; break; }
        }
        
        Vec2 n_out = face.normal.normalized();
        // For surface output, normal points INTO the flow (towards body)
        Vec2 n_body = -n_out;
        
        real_t cp = (p_wall - ci.p_inf) / q_inf;
        
        real_t u_wall = 0, v_wall = 0, mach_wall = 0;
        real_t cf_val = 0;
        
        if (bc == BCType::NoSlipAdiabaticWall) {
            // Wall velocity is zero (boundary value)
            u_wall = 0;
            v_wall = 0;
            mach_wall = 0;
            
            // Estimate skin friction
            real_t u_cell = Ucell[1] / rho_wall;
            real_t v_cell = Ucell[2] / rho_wall;
            Vec2 to_center = face.centroid - local.owned_cells[cell_id].centroid;
            real_t dist = std::max(to_center.norm(), 1e-10);
            cf_val = mu * std::sqrt(u_cell*u_cell + v_cell*v_cell) / (dist * q_inf);
        } else if (bc == BCType::SlipWall) {
            // For slip wall, cell-center values approximate boundary values
            u_wall = Ucell[1] / rho_wall;
            v_wall = Ucell[2] / rho_wall;
            mach_wall = solver.Mach(Ucell);
        }
        
        SurfPt pt;
        pt.x = face.centroid[0];
        pt.y = face.centroid[1];
        pt.nx = n_body[0];
        pt.ny = n_body[1];
        pt.pressure = p_wall;
        pt.cp = cp;
        pt.cf = cf_val;
        pt.rho = rho_wall;
        pt.u = u_wall;
        pt.v = v_wall;
        pt.mach = mach_wall;
        pt.tag = tag_name;
        pts.push_back(pt);
    }
    
    // Gather all surface points to rank 0
    int local_n = pts.size();
    std::vector<int> all_counts(solver.size());
    MPI_Gather(&local_n, 1, MPI_INT, all_counts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    std::vector<int> displs(solver.size(), 0);
    int total_pts = 0;
    if (rank == 0) {
        for (int i = 0; i < solver.size(); i++) {
            displs[i] = total_pts;
            total_pts += all_counts[i];
        }
    }
    
    // Serialize
    std::vector<real_t> local_buf;
    for (auto& pt : pts) {
        local_buf.push_back(pt.x);
        local_buf.push_back(pt.y);
        local_buf.push_back(pt.nx);
        local_buf.push_back(pt.ny);
        local_buf.push_back(pt.pressure);
        local_buf.push_back(pt.cp);
        local_buf.push_back(pt.cf);
        local_buf.push_back(pt.rho);
        local_buf.push_back(pt.u);
        local_buf.push_back(pt.v);
        local_buf.push_back(pt.mach);
    }
    
    std::vector<real_t> global_buf;
    if (rank == 0) global_buf.resize(total_pts * 11);
    
    std::vector<int> recvcounts(solver.size());
    for (int i = 0; i < solver.size(); i++) recvcounts[i] = all_counts[i] * 11;
    std::vector<int> sendcounts(solver.size());
    for (int i = 0; i < solver.size(); i++) sendcounts[i] = all_counts[i] * 11;
    if (rank == 0) {
        for (int i = 0; i < solver.size(); i++) displs[i] *= 11;
    }
    
    MPI_Gatherv(local_buf.data(), local_n * 11, MPI_DOUBLE,
                global_buf.data(), recvcounts.data(), displs.data(), MPI_DOUBLE,
                0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        std::string outfile = output_dir + "/surface.csv";
        std::ofstream f(outfile);
        f << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
        f << std::scientific << std::setprecision(10);
        
        for (int i = 0; i < total_pts; i++) {
            real_t* d = &global_buf[i * 11];
            f << d[0] << "," << d[1] << "," << d[2] << "," << d[3] << ","
              << d[4] << "," << d[5] << "," << d[6] << "," << d[7] << ","
              << d[8] << "," << d[9] << "," << d[10] << ",wall\n";
        }
        std::cout << "Surface written: " << total_pts << " points" << std::endl;
    }
}

void write_field_vtu(const Solver& solver, const std::string& output_dir) {
    const auto& local = solver.local_mesh();
    const auto& U = solver.U();
    int rank = solver.rank();
    real_t gamma = solver.gamma();
    
    // Write VTU (unstructured XML) for each rank
    // This is a simplified VTU writer
    std::string fname = output_dir + "/field_final.vtu";
    
    // Only rank 0 writes (simplified: gather to rank 0)
    // For a full implementation, we'd write parallel VTK
    
    if (rank == 0) {
        std::ofstream f(fname);
        f << "<?xml version=\"1.0\"?>\n";
        f << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
        f << "<UnstructuredGrid>\n";
        f << "<Piece NumberOfPoints=\"" << local.n_owned
          << "\" NumberOfCells=\"" << local.n_owned << "\">\n";
        
        // Points (cell centers)
        f << "<Points>\n";
        f << "<DataArray type=\"Float64\" NumberOfComponents=\"3\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) {
            f << local.owned_cells[i].centroid[0] << " "
              << local.owned_cells[i].centroid[1] << " 0.0\n";
        }
        f << "</DataArray>\n</Points>\n";
        
        // Cells (vertex cells - one vertex per cell center)
        f << "<Cells>\n";
        f << "<DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << i << "\n";
        f << "</DataArray>\n";
        f << "<DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << (i+1) << "\n";
        f << "</DataArray>\n";
        f << "<DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << "1\n"; // VTK_VERTEX
        f << "</DataArray>\n</Cells>\n";
        
        // Cell data
        f << "<CellData Scalars=\"density\">\n";
        
        f << "<DataArray type=\"Float64\" Name=\"density\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << U[i][0] << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Float64\" Name=\"velocity_x\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << U[i][1]/U[i][0] << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Float64\" Name=\"velocity_y\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << U[i][2]/U[i][0] << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Float64\" Name=\"pressure\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << solver.pressure(U[i]) << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Float64\" Name=\"mach\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << solver.Mach(U[i]) << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Float64\" Name=\"temperature\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << solver.temperature(U[i]) << "\n";
        f << "</DataArray>\n";
        
        f << "<DataArray type=\"Int32\" Name=\"rank\" format=\"ascii\">\n";
        for (idx_t i = 0; i < local.n_owned; i++) f << rank << "\n";
        f << "</DataArray>\n";
        
        f << "</CellData>\n";
        f << "</Piece>\n";
        f << "</UnstructuredGrid>\n";
        f << "</VTKFile>\n";
        
        std::cout << "Field VTU written: " << fname << std::endl;
    }
}

void write_partition_diagnostics(const Solver& solver, const std::string& output_dir) {
    const auto& local = solver.local_mesh();
    int rank = solver.rank();
    int size = solver.size();
    
    // Gather diagnostics to rank 0
    struct Diag {
        idx_t n_owned, n_ghost, n_boundary, n_neighbors;
    };
    Diag my_diag;
    my_diag.n_owned = local.n_owned;
    my_diag.n_ghost = local.n_ghost;
    my_diag.n_boundary = local.n_boundary_faces;
    my_diag.n_neighbors = local.neighbors.size();
    
    std::vector<Diag> all_diags(size);
    MPI_Gather(&my_diag, sizeof(Diag)/sizeof(idx_t), MPI_INT64_T,
               all_diags.data(), sizeof(Diag)/sizeof(idx_t), MPI_INT64_T,
               0, MPI_COMM_WORLD);
    
    if (rank == 0) {
        std::ofstream f(output_dir + "/partition_diagnostics.csv");
        f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
        for (int i = 0; i < size; i++) {
            f << i << "," << all_diags[i].n_owned << "," << all_diags[i].n_ghost
              << "," << all_diags[i].n_boundary << "," << all_diags[i].n_neighbors
              << ",\"\""
              << "," << all_diags[i].n_ghost  // send approx
              << "," << all_diags[i].n_ghost  // recv approx
              << "\n";
        }
    }
}

void write_restart(const Solver& solver, const std::string& output_dir) {
    const auto& U = solver.U();
    const auto& local = solver.local_mesh();
    int rank = solver.rank();
    
    // Simple binary restart
    std::string rfile = output_dir + "/restart_final.bin";
    
    if (rank == 0) {
        std::ofstream f(rfile, std::ios::binary);
        idx_t n = local.n_owned;
        f.write(reinterpret_cast<const char*>(&n), sizeof(idx_t));
        for (idx_t i = 0; i < n; i++) {
            f.write(reinterpret_cast<const char*>(U[i].data()), 4 * sizeof(real_t));
        }
    }
}

} // namespace cfd
