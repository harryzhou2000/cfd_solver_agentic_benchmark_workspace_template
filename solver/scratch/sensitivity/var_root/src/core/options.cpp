#include "core/options.h"

#include <sstream>

#include "core/exceptions.h"
#include "core/path_utils.h"

namespace cns2d {

std::string usageText() {
  std::ostringstream os;
  os << "cns2d -- 2-D unstructured compressible Navier-Stokes finite-volume solver\n"
     << "\n"
     << "usage:\n"
     << "  mpirun -np <ranks> cns2d solve --case <case.json> --output <dir>\n"
     << "         [--restart <restart-file>] [--report-level brief|full]\n"
     << "         [--max-steps N] [--final-time T] [--log-every N]\n"
     << "         [--no-intermediate-fields]\n"
     << "\n"
     << "arguments:\n"
     << "  solve                  run a case to completion (only supported command)\n"
     << "  --case <path>          benchmark case JSON file (required)\n"
     << "  --output <dir>         output directory, created if absent (required)\n"
     << "  --restart <path>       restart from a previously written restart file\n"
     << "  --report-level <lvl>   brief | full (default full)\n"
     << "  --max-steps N          debug override of run_control.max_steps\n"
     << "  --final-time T         debug override of run_control.final_time\n"
     << "  --log-every N          progress log cadence in steps (default 100)\n"
     << "  --no-intermediate-fields  suppress transient intermediate field dumps\n";
  os << "\n"
     << "verification overrides (production runs omit these):\n"
     << "  --spatial-order 1|2    force first- or second-order reconstruction\n"
     << "  --flux roe|hllc|rusanov   force the inviscid Riemann solver\n"
     << "  --limiter barth_jespersen|venkatakrishnan|none\n"
     << "  --venkat-k K           Venkatakrishnan smoothing constant\n";
  return os.str();
}

namespace {

std::string requireValue(int argc, char **argv, int &i, const std::string &flag) {
  if (i + 1 >= argc) {
    throw CnsError("option " + flag + " requires a value\n\n" + usageText());
  }
  return std::string(argv[++i]);
}

}  // namespace

CommandLineOptions parseCommandLine(int argc, char **argv) {
  CommandLineOptions opt;
  {
    std::ostringstream os;
    for (int i = 0; i < argc; ++i) {
      if (i > 0) os << ' ';
      os << argv[i];
    }
    opt.command_line = os.str();
  }

  if (argc < 2) {
    throw CnsError("missing command\n\n" + usageText());
  }

  const std::string first(argv[1]);
  if (first == "-h" || first == "--help") {
    opt.command = "help";
    return opt;
  }
  if (first != "solve") {
    throw CnsError("unsupported command '" + first + "' (expected 'solve')\n\n" + usageText());
  }
  opt.command = "solve";

  for (int i = 2; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--case") {
      opt.case_path = requireValue(argc, argv, i, a);
    } else if (a == "--output") {
      opt.output_dir = requireValue(argc, argv, i, a);
    } else if (a == "--restart") {
      opt.restart_path = requireValue(argc, argv, i, a);
    } else if (a == "--report-level") {
      const std::string v = requireValue(argc, argv, i, a);
      if (v == "brief") {
        opt.report_level = ReportLevel::kBrief;
      } else if (v == "full") {
        opt.report_level = ReportLevel::kFull;
      } else {
        throw CnsError("--report-level must be 'brief' or 'full', got '" + v + "'");
      }
    } else if (a == "--max-steps") {
      const std::string v = requireValue(argc, argv, i, a);
      try {
        opt.override_max_steps = std::stoi(v);
      } catch (const std::exception &) {
        throw CnsError("--max-steps expects an integer, got '" + v + "'");
      }
      if (opt.override_max_steps <= 0) throw CnsError("--max-steps must be positive");
    } else if (a == "--final-time") {
      const std::string v = requireValue(argc, argv, i, a);
      try {
        opt.override_final_time = std::stod(v);
      } catch (const std::exception &) {
        throw CnsError("--final-time expects a number, got '" + v + "'");
      }
      if (opt.override_final_time <= 0.0) throw CnsError("--final-time must be positive");
    } else if (a == "--log-every") {
      const std::string v = requireValue(argc, argv, i, a);
      try {
        opt.log_every = std::stoi(v);
      } catch (const std::exception &) {
        throw CnsError("--log-every expects an integer, got '" + v + "'");
      }
      if (opt.log_every <= 0) throw CnsError("--log-every must be positive");
    } else if (a == "--no-intermediate-fields") {
      opt.write_intermediate_fields = false;
    } else if (a == "--spatial-order") {
      const std::string v = requireValue(argc, argv, i, a);
      if (v != "1" && v != "2") throw CnsError("--spatial-order must be 1 or 2, got '" + v + "'");
      opt.override_spatial_order = std::stoi(v);
    } else if (a == "--flux") {
      opt.override_flux = requireValue(argc, argv, i, a);
    } else if (a == "--limiter") {
      opt.override_limiter = requireValue(argc, argv, i, a);
    } else if (a == "--venkat-k") {
      const std::string v = requireValue(argc, argv, i, a);
      try {
        opt.override_venkat_k = std::stod(v);
      } catch (const std::exception &) {
        throw CnsError("--venkat-k expects a number, got '" + v + "'");
      }
      if (opt.override_venkat_k < 0.0) throw CnsError("--venkat-k must be nonnegative");
    } else if (a == "-h" || a == "--help") {
      opt.command = "help";
      return opt;
    } else {
      throw CnsError("unrecognized option '" + a + "'\n\n" + usageText());
    }
  }

  if (opt.case_path.empty()) throw CnsError("--case is required\n\n" + usageText());
  if (opt.output_dir.empty()) throw CnsError("--output is required\n\n" + usageText());
  if (!fileExists(opt.case_path)) throw CnsError("case file does not exist: " + opt.case_path);
  if (!opt.restart_path.empty() && !fileExists(opt.restart_path)) {
    throw CnsError("restart file does not exist: " + opt.restart_path);
  }
  return opt;
}

}  // namespace cns2d
