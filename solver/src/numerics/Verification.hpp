// Solver verification.
//
// Two independent checks are provided:
//
//  * an order-of-accuracy study of the spatial operator on a sequence of
//    internally generated mixed triangle/quadrilateral meshes obtained by a
//    smooth analytic distortion of a Cartesian grid, using a smooth
//    manufactured state.  Both the truncation error of the residual and the
//    discretisation error of the converged manufactured solution are measured;
//
//  * freestream preservation and linear-reconstruction exactness on the actual
//    benchmark mesh.
#pragma once

#include <mpi.h>

#include <string>
#include <vector>

#include "core/CaseConfig.hpp"
#include "core/Types.hpp"
#include "numerics/SolverOptions.hpp"

namespace cfd {

struct OrderLevel {
  int n = 0;              // cells per side of the generated mesh
  Index num_cells = 0;
  Real h = 0.0;           // mean cell size of the sampled cells
  Real err_l1 = 0.0;
  Real err_l2 = 0.0;
  Real err_linf = 0.0;
  Index num_sampled = 0;
};

struct MmsLevel {
  int n = 0;
  Index num_cells = 0;
  Real h = 0.0;
  Real err_l1 = 0.0;
  Real err_l2 = 0.0;
  Real err_linf = 0.0;
  long long steps = 0;
  Real residual_orders = 0.0;
};

struct VerificationReport {
  // Truncation-error study (smooth mesh sequence).
  std::vector<OrderLevel> second_order;
  std::vector<OrderLevel> first_order;
  Real observed_order_l1_second = 0.0;
  Real observed_order_l2_second = 0.0;
  Real observed_order_l1_first = 0.0;
  // Manufactured-solution study: discretisation error of the converged steady
  // solution, which is the quantity the formal order refers to.
  std::vector<MmsLevel> mms_second_order;
  std::vector<MmsLevel> mms_second_order_limited;
  std::vector<MmsLevel> mms_first_order;
  Real mms_order_second = 0.0;
  Real mms_order_second_limited = 0.0;
  Real mms_order_first = 0.0;
  // Checks on the real mesh (filled only when a case file is given).
  bool have_case_checks = false;
  std::string case_id;
  Real freestream_residual_linf = 0.0;
  Real linear_gradient_max_error = 0.0;
  Real linear_reconstruction_max_error = 0.0;
  Real mesh_jitter = 0.0;
};

VerificationReport runVerification(int levels, int base, const std::string* case_path,
                                   const SolverOptions& base_opts, MPI_Comm comm,
                                   Real mesh_jitter = 0.0);

void writeVerificationJson(const VerificationReport& r, const std::string& path);

}  // namespace cfd
