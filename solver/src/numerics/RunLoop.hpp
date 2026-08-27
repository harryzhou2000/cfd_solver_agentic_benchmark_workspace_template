// Steady pseudo-time and transient dual-time drivers.
#pragma once

#include <string>
#include <vector>

#include "core/CaseConfig.hpp"
#include "io/OutputWriter.hpp"
#include "numerics/ImplicitSolver.hpp"
#include "numerics/SpatialOperator.hpp"
#include "post/Forces.hpp"

namespace cfd {

struct RunResult {
  long long final_step = 0;
  Real final_physical_time = 0.0;
  std::string convergence_status = "failed";
  Real residual_reduction_orders = 0.0;
  Real initial_residual = 0.0;
  Real final_residual = 0.0;
  ForceReport final_forces;
  InnerStats inner;
  std::string notes;
  long long field_snapshots = 0;
};

RunResult runSteady(SpatialOperator& op, ImplicitSolver& solver, const std::string& outdir);
RunResult runTransient(SpatialOperator& op, ImplicitSolver& solver, const std::string& outdir,
                       Real start_time, long long start_step, std::vector<Real>& un,
                       std::vector<Real>& unm1, bool have_history);

}  // namespace cfd
