#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cfd/case_config.hpp"
#include "cfd/fvm.hpp"
#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"

namespace cfd {

struct RunSummary {
  int final_step{0};
  Real final_physical_time{0.0};
  Real wall_time_seconds{0.0};
  Real residual_reduction_orders{0.0};
  std::string convergence_status{"failed"};
  std::string command;
  int mpi_ranks{1};
  int edge_cut{0};
  int min_inner_iterations{0};
  int max_inner_iterations{0};
  int observed_min_inner_iterations{0};
  int observed_max_inner_iterations{0};
  Real mean_inner_iterations{0.0};
  int inner_target_misses{0};
  Real inner_target_converged_fraction{0.0};
  Real last_inner_residual_ratio{1.0};
  std::string notes;
};

class CsvHistory {
 public:
  CsvHistory() = default;
  explicit CsvHistory(const std::filesystem::path& path);
  void open(const std::filesystem::path& path);
  std::ofstream& stream() { return out_; }

 private:
  std::ofstream out_;
};

void ensure_output_dir(const std::filesystem::path& output_dir);
void write_metadata(const std::filesystem::path& output_dir, const CaseConfig& cfg,
                    const LocalMesh& local, const RunSummary& summary,
                    const PartitionDiagnostics& diag);
void write_run_status(const std::filesystem::path& output_dir, const CaseConfig& cfg,
                      const RunSummary& summary);
void write_partition_diagnostics(const std::filesystem::path& output_dir,
                                 const std::vector<PartitionDiagnostics>& diagnostics);
void write_surface_csv(const std::filesystem::path& output_dir, const std::vector<std::string>& rows);
void write_restart(const std::filesystem::path& output_dir, const LocalMesh& local,
                   const std::vector<Conserved>& state);
void write_vtu(const std::filesystem::path& output_dir, const LocalMesh& local,
               const CaseConfig& cfg, const std::vector<Conserved>& state, int rank);
std::string json_escape(const std::string& value);

}  // namespace cfd
