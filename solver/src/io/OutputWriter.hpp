// All file output required by OUTPUT_CONTRACT.md.
//
// Field/surface/restart output gathers rank-local data onto rank 0 *after* the
// iteration loop (or at a snapshot point); the solver never keeps a global
// mesh or global state while iterating.
#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "mesh/MeshDistributor.hpp"
#include "numerics/SpatialOperator.hpp"
#include "post/Forces.hpp"

namespace cfd {

struct InnerStats {
  long long steps = 0;
  long long sum_inner = 0;
  int min_inner = 1 << 30;
  int max_inner = 0;
  long long target_misses = 0;
  Real last_ratio = 0.0;
  Real mean() const { return steps > 0 ? static_cast<Real>(sum_inner) / steps : 0.0; }
  Real convergedFraction() const {
    return steps > 0 ? 1.0 - static_cast<Real>(target_misses) / steps : 1.0;
  }
  void add(int inner, bool converged, Real ratio) {
    ++steps;
    sum_inner += inner;
    min_inner = std::min(min_inner, inner);
    max_inner = std::max(max_inner, inner);
    if (!converged) ++target_misses;
    last_ratio = ratio;
  }
};

// Streaming CSV writers used during the run (rank 0 only).
class CsvStream {
 public:
  void open(const std::string& path, const std::string& header, int rank);
  void row(const std::string& line);
  void flush();
  void close();
  bool active() const { return static_cast<bool>(file_); }

 private:
  std::unique_ptr<std::ofstream> file_;
};

void writeVtu(const std::string& path, const SpatialOperator& op, int precision_bits);

void writeSurfaceCsv(const std::string& base_path, const SpatialOperator& op);

void writeRestart(const std::string& path, const SpatialOperator& op, Real time, long long step,
                  const std::vector<Real>* un, const std::vector<Real>* unm1);

// Returns the physical time / step stored in the restart file.
struct RestartInfo {
  Real time = 0.0;
  long long step = 0;
  int levels = 1;
};
RestartInfo readRestart(const std::string& path, SpatialOperator& op, std::vector<Real>* un,
                        std::vector<Real>* unm1);

void writePartitionDiagnostics(const std::string& dir,
                               const std::vector<PartitionDiagnostics>& diags,
                               GlobalIndex edge_cut, int rank);

}  // namespace cfd
