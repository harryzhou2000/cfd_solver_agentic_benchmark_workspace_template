#pragma once

#include <string>

#include "config.hpp"
#include "mesh.hpp"

namespace cfd {

// Runs the full solve for a case and writes the output contract files.
// Returns 0 on a normal completion (converged / statistically periodic),
// nonzero otherwise (caller exits with that code).
int run_solve(const CaseConfig& cfg, const std::string& output_dir,
              const std::string& restart_file, const std::string& command_line,
              bool report_full);

// Prints mesh/partition information for a case (debugging).
int run_info(const CaseConfig& cfg);

// Debug: verifies least-squares gradient accuracy on a manufactured linear
// field (used during development; not part of the production path).
int run_lsq_test(const CaseConfig& cfg);

}  // namespace cfd
