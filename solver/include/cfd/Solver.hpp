#pragma once

#include "cfd/CaseConfig.hpp"
#include "cfd/DistributedMesh.hpp"
#include "cfd/HaloExchange.hpp"
#include "cfd/Output.hpp"
#include "cfd/Physics.hpp"
#include "cfd/SpatialOperator.hpp"

#include <mpi.h>

#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace cfd {

/// Checks Cd and Cl over the trailing force history.  Histories shorter than
/// 1000 samples use all available samples; longer histories use the final
/// 1000 samples.  The accepted range of each coefficient is strictly below
/// 0.02, matching the established steady plateau criterion.
bool has_stable_terminal_force_window(const std::vector<double>& drag_history,
                                      const std::vector<double>& lift_history);

class Solver {
 public:
  Solver(const CaseConfig& config, const LocalMesh& mesh, MPI_Comm communicator,
         OutputWriter& output);

  RunSummary run(const std::string& command, const std::string& start_time_utc);
  void load_restart(const std::filesystem::path& path);

 private:
  struct Norms {
    Conserved component_l2{};
    double l2 = 0.0;
    double linf = 0.0;
  };

  Norms global_norms(const std::vector<Conserved>& residual) const;
  ForceRow reduce_forces(const ForceComponents& local, int step, double physical_time) const;
  std::vector<SurfaceRow> surface_rows(const std::vector<ReconstructionData>& reconstruction) const;
  double cfl_for_step(int step) const;
  void initialize_state();
  void update_owned(const std::vector<Conserved>& residual, const std::vector<double>& spectral,
                    double cfl, double physical_diagonal);
  void log_progress(int step, double physical_time, const Norms& norms, const ForceRow& force,
                    int inner_iterations, double ratio);

  const CaseConfig& config_;
  const LocalMesh& mesh_;
  MPI_Comm communicator_;
  int rank_ = 0;
  int ranks_ = 1;
  GasProperties gas_;
  HaloExchange halo_;
  SpatialOperator spatial_;
  OutputWriter& output_;
  std::vector<Conserved> state_;
  std::vector<std::vector<std::pair<int, double>>> implicit_coupling_;
  std::ofstream log_;
};

}  // namespace cfd
