#pragma once

#include <string>
#include <vector>

#include "case.hpp"
#include "flux.hpp"
#include "gradient.hpp"
#include "partition.hpp"

namespace cfd {

struct SolveStats {
  long finalStep = 0;
  double finalTime = 0.0;
  double wallSeconds = 0.0;
  std::string convergenceStatus = "failed";  // converged | statistically_periodic | failed
  double residualReductionOrders = 0.0;
  long innerMin = 0, innerMax = 0, innerSum = 0, innerCount = 0, innerMisses = 0;
  double innerLastRatio = 1.0;
  double innerTarget = 1e-3;
  double convergedFraction = 0.0;
  double residual0 = 0.0, residualFinal = 0.0;
  double meanDrag = 0.0, meanLift = 0.0;
};

struct SolveResult {
  int exitCode = 0;
  SolveStats stats;
};

// Run one case to completion and write the full output package.
SolveResult runSolver(const Case& c, const LocalMesh& mesh, int rank, int nRanks,
                      const std::string& outDir, const std::string& restartPath,
                      const std::string& commandLine, const std::string& reportLevel);

}  // namespace cfd
