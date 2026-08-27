// Numerical options that are not part of the case file.  Every one of them has
// a documented command-line switch so that a run can be reproduced without
// editing sources or inputs; the defaults below are the production settings
// used for all eight benchmark cases.
#pragma once

#include <string>

#include "core/Types.hpp"
#include "physics/FluxInviscid.hpp"

namespace cfd {

enum class LimiterType { kNone, kBarthJespersen, kVenkatakrishnan };

struct SolverOptions {
  // HLLC is the production default: it is positivity friendly and free of the
  // Roe shock instability that appears at the Mach 2 bow shock on this mesh.
  RiemannScheme flux = RiemannScheme::kHllc;
  Real entropy_fix = 0.10;
  LimiterType limiter = LimiterType::kVenkatakrishnan;
  Real venkatakrishnan_k = 5.0;
  bool second_order = true;
  int inner_sweeps = 2;              // symmetric Gauss-Seidel sweeps per inner iteration
  Real cfl_scale = 1.0;              // multiplies the case CFL schedule
  // Residual-based CFL safeguard.  The case CFL schedule is an *upper*
  // envelope; if the residual grows the factor is cut, so the effective CFL is
  // never larger than the value requested by the case file.
  bool adaptive_cfl = true;
  Real cfl_backoff = 0.5;
  Real cfl_recover = 1.02;
  Real cfl_growth_trigger = 2.0;     // residual/best ratio that triggers a back-off
  Real cfl_min_factor = 0.02;
  Real viscous_dt_factor = 4.0;      // weight of the viscous spectral radius in dtau
  Real positivity_floor = 1.0e-10;   // relative to freestream rho / p
  int max_update_backtracks = 8;
  int limiter_freeze_step = -1;      // freeze the limiter from this step on (-1 = never)
  std::string field_format = "vtu";
  int field_precision = 32;          // 32 or 64 bit floats in the VTU payload
  bool write_intermediate_fields = true;
  int progress_every = 100;
};

inline const char* toString(LimiterType t) {
  switch (t) {
    case LimiterType::kNone: return "none_first_order";
    case LimiterType::kBarthJespersen: return "barth_jespersen";
    case LimiterType::kVenkatakrishnan: return "venkatakrishnan";
  }
  return "unknown";
}

}  // namespace cfd
