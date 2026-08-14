#pragma once
#include "common/types.hpp"
#include "mesh/mesh.hpp"
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Write metadata.json
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
                         const std::string& conv_status);

// Write run_status.json
void write_run_status_json(const std::string& path,
                           const std::string& case_id,
                           const std::string& command,
                           int mpi_ranks,
                           Real wall_time_sec,
                           int final_step,
                           Real final_time,
                           const std::string& conv_status,
                           Real residual_reduction_orders,
                           const std::string& notes);

// Write partition_diagnostics.csv
void write_partition_csv(const std::string& path,
                         const std::vector<int>& ranks,
                         const std::vector<int>& n_owned,
                         const std::vector<int>& n_ghost,
                         const std::vector<int>& n_bnd,
                         const std::vector<int>& n_neighbors,
                         int edge_cut);
