#pragma once
// Output contract implementation: CSV histories, surface files, VTU fields,
// restart files, metadata/run_status JSON, partition diagnostics, logging.

#include <mpi.h>
#include <fstream>
#include <string>
#include <vector>
#include "case_file.hpp"
#include "partition.hpp"
#include "spatial.hpp"

namespace cfd {

struct OutputContext {
  MPI_Comm comm = MPI_COMM_WORLD;
  int rank = 0, n_ranks = 1;
  std::string dir;
  std::ofstream log_file;
  std::ofstream res_file;
  std::ofstream force_file;
  bool brief = false;

  void log(const std::string& msg);  // rank 0 writes to stdout + stdout.log
};

void open_output(OutputContext& oc, const std::string& dir);

// CSV history rows (rank 0 writes; values must already be globally reduced).
void write_residual_row(OutputContext& oc, long step, double time, int inner_iter,
                        double cfl, double dt, const double per_eq[4], double l2,
                        double linf);
void write_force_row(OutputContext& oc, long step, double time, double cl, double cd,
                     double cmz, double pdrag, double vdrag, double plift, double vlift);

// Gathered outputs (call on all ranks; rank 0 writes).
void write_surface_csv(OutputContext& oc, const LocalMesh& m,
                       const std::vector<SurfaceRow>& local_rows);

void write_field_vtu(OutputContext& oc, const LocalMesh& m, const CaseFile& cs,
                     const FlowState& s, const std::string& filename);

// Restart: single file in global-cell-id order; n_states = 1 or 3.
void write_restart(OutputContext& oc, const LocalMesh& m, const std::string& filename,
                   long step, double time, const std::vector<Vec4>& U,
                   const std::vector<Vec4>& U_n, const std::vector<Vec4>& U_nm1,
                   int n_states);
void write_partition_diagnostics(OutputContext& oc, const LocalMesh& m,
                                 const PartitionInfo& info);

void write_json_file(const std::string& path, const std::string& json_text);

}  // namespace cfd
