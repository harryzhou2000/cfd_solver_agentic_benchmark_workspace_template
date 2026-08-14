#include "metadata.hpp"
#include <fstream>

void write_metadata_json(const std::string& path,
                         const CaseConfig& cfg,
                         const RankMesh& rm,
                         int mpi_ranks,
                         int n_cells_global,
                         int n_faces_global,
                         const std::string& solver_name,
                         const std::string& git_revision,
                         const std::string& start_time,
                         const std::string& end_time,
                         bool completed,
                         const std::string& conv_status) {
    json j;
    j["case_id"] = cfg.case_id;
    j["solver_name"] = solver_name;
    j["solver_version"] = "0.1.0";
    j["git_revision"] = git_revision;
    j["mpi_ranks"] = mpi_ranks;
    j["mesh_file"] = cfg.mesh_file;
    j["num_cells_global"] = n_cells_global;
    j["num_faces_global"] = n_faces_global;
    j["num_cells_owned_local"] = (int)rm.owned_cells.size();
    j["num_cells_ghost_local"] = (int)rm.ghost_cells.size();
    j["partitioner"] = "metis_kway";
    j["partition_edge_cut"] = 0;
    j["halo_exchange"] = "neighbor_isend_irecv";
    j["full_state_replication_during_iterations"] = false;
    j["full_mesh_replication_during_iterations"] = false;
    j["equation_set"] = "compressible_navier_stokes_2d";
    j["inviscid_flux"] = "rusanov";
    j["entropy_fix"] = nullptr;
    j["viscous_flux"] = "gradient_based";
    j["time_integrator"] = (cfg.run_type == RunType::Transient) ? "bdf2" : "pseudo_steady";
    j["implicit_solver"] = "lu_sgs";
    j["reconstruction"] = "least_squares_linear";
    j["limiter"] = "barth_jespersen";
    j["spatial_order_claimed"] = 2;
    j["positivity_preservation"] = "density_pressure_clamp";
    j["wall_boundary_output_semantics"] = "boundary_value";
    j["true_bdf2_inner_loop"] = (cfg.run_type == RunType::Transient);
    j["start_time_utc"] = start_time;
    j["end_time_utc"] = end_time;
    j["completed"] = completed;
    j["convergence_status"] = conv_status;

    std::ofstream f(path);
    f << j.dump(2) << "\n";
}

void write_run_status_json(const std::string& path,
                           const std::string& case_id,
                           const std::string& command,
                           int mpi_ranks,
                           Real wall_time_sec,
                           int final_step,
                           Real final_time,
                           const std::string& conv_status,
                           Real residual_reduction_orders,
                           const std::string& notes) {
    json j;
    j["case_id"] = case_id;
    j["command"] = command;
    j["mpi_ranks"] = mpi_ranks;
    j["wall_time_seconds"] = wall_time_sec;
    j["final_step"] = final_step;
    j["final_physical_time"] = final_time;
    j["convergence_status"] = conv_status;
    j["residual_reduction_orders"] = residual_reduction_orders;
    j["notes"] = notes;

    std::ofstream f(path);
    f << j.dump(2) << "\n";
}

void write_partition_csv(const std::string& path,
                         const std::vector<int>& ranks,
                         const std::vector<int>& n_owned,
                         const std::vector<int>& n_ghost,
                         const std::vector<int>& n_bnd,
                         const std::vector<int>& n_neighbors,
                         int edge_cut) {
    std::ofstream f(path);
    f << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    size_t n = ranks.size();
    for (size_t i = 0; i < n; i++) {
        f << ranks[i] << "," << n_owned[i] << "," << n_ghost[i]
          << "," << n_bnd[i] << "," << n_neighbors[i]
          << ",\"[]\",\"[]\",\"[]\"\n";
    }
}
