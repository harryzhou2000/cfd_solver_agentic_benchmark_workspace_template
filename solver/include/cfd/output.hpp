#pragma once

#include "cfd/case_config.hpp"
#include "cfd/partition.hpp"
#include "cfd/physics.hpp"
#include "cfd/solver.hpp"

#include <mpi.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace cfd {

class OutputManager final : public RunObserver {
 public:
  OutputManager(const std::filesystem::path& output_directory, const CaseConfig& config,
                const LocalMesh& mesh, MPI_Comm communicator);
  ~OutputManager() override = default;

  void residual(const ResidualRow& row) override;
  void force(const ForceRow& row) override;
  void field(int step, double physical_time, const std::vector<Conserved>& state,
             const std::vector<double>& vorticity, bool final) override;
  void progress(const std::string& line) override;

  void finalize(const RunResult& result, const std::string& command, double wall_time_seconds,
                const std::string& start_time_utc, const std::string& end_time_utc);

  [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

 private:
  void write_vtu_piece(const std::filesystem::path& path, const std::vector<Conserved>& state,
                       const std::vector<double>& vorticity) const;
  void write_pvtu(const std::filesystem::path& path, const std::string& piece_pattern) const;
  void write_partition_diagnostics() const;
  void write_surface(const std::vector<SurfaceRow>& local_surface) const;
  void write_restart(const RunResult& result) const;
  void write_metadata(const RunResult& result, const std::string& start_time_utc,
                      const std::string& end_time_utc) const;
  void write_run_status(const RunResult& result, const std::string& command,
                        double wall_time_seconds) const;

  std::filesystem::path directory_;
  const CaseConfig& config_;
  const LocalMesh& mesh_;
  MPI_Comm communicator_;
  PerfectGasPhysics physics_;
  int rank_ = 0;
  int rank_count_ = 1;
  std::ofstream residual_stream_;
  std::ofstream force_stream_;
  std::ofstream log_stream_;
};

InitialState load_restart(const std::filesystem::path& manifest_path, const LocalMesh& mesh,
                          MPI_Comm communicator);
std::string utc_now();

}  // namespace cfd
