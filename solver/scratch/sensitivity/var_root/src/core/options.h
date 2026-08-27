// cns2d -- command-line interface.
//
// Contract (OUTPUT_CONTRACT.md):
//   mpirun -np <ranks> cns2d solve --case <case-json> --output <output-dir>
//                                 [--restart <restart-file>]
//                                 [--report-level brief|full]
#pragma once

#include <string>
#include <vector>

#include "core/types.h"

namespace cns2d {

enum class ReportLevel { kBrief, kFull };

struct CommandLineOptions {
  std::string command;       // "solve"
  std::string case_path;
  std::string output_dir;
  std::string restart_path;  // empty when starting from freestream
  ReportLevel report_level{ReportLevel::kFull};

  // Optional overrides, all defaulting to "use the case file".  These exist
  // for debugging and rank-count studies; production runs use the case values.
  int override_max_steps{-1};
  Real override_final_time{-1.0};
  int log_every{100};
  bool write_intermediate_fields{true};

  // Verification overrides.  They exist so the order of accuracy, the effect of
  // each limiter and the effect of each Riemann solver can be measured with the
  // same binary; production runs leave them unset and use the scheme selected
  // from the case file.
  int override_spatial_order{-1};        // 1 or 2
  std::string override_flux;             // roe | hllc | rusanov
  std::string override_limiter;          // barth_jespersen | venkatakrishnan | none
  Real override_venkat_k{-1.0};

  // Verbatim command line, recorded in run_status.json.
  std::string command_line;
};

// Parse argv.  Throws CnsError with a usage message on malformed input.
CommandLineOptions parseCommandLine(int argc, char **argv);
std::string usageText();

}  // namespace cns2d
