#include "cli.hpp"

#include <utility>

namespace aerofv {
namespace {

std::string require_value(int argc, char *const argv[], int &index,
                          const std::string &option) {
  if (++index >= argc) {
    throw CliError("missing value for " + option);
  }
  const std::string value = argv[index] == nullptr ? "" : argv[index];
  if (value.empty() || value.rfind("--", 0) == 0) {
    throw CliError("missing value for " + option);
  }
  return value;
}

} // namespace

const char *to_string(ReportLevel level) noexcept {
  return level == ReportLevel::brief ? "brief" : "full";
}

std::string cli_usage(const std::string &program_name) {
  return "Usage: " + program_name +
         " solve --case <case-json> --output <output-dir>"
         " [--restart <restart-file>] [--report-level brief|full]";
}

CliOptions parse_cli(int argc, char *const argv[]) {
  if (argc <= 0 || argv == nullptr) {
    throw CliError("invalid argv");
  }
  if (argc == 1) {
    throw CliError("missing command; " + cli_usage(argv[0] == nullptr ? "aerofv" : argv[0]));
  }

  const std::string command = argv[1] == nullptr ? "" : argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    if (argc != 2) {
      throw CliError("help does not accept additional arguments");
    }
    return {};
  }
  if (command != "solve") {
    throw CliError("unknown command '" + command + "'; expected 'solve'");
  }
  if (argc == 3 && (std::string(argv[2] == nullptr ? "" : argv[2]) == "--help" ||
                    std::string(argv[2] == nullptr ? "" : argv[2]) == "-h")) {
    return {};
  }

  CliOptions result;
  result.action = CliAction::solve;
  bool saw_case = false;
  bool saw_output = false;
  bool saw_restart = false;
  bool saw_report_level = false;

  for (int index = 2; index < argc; ++index) {
    const std::string option = argv[index] == nullptr ? "" : argv[index];
    if (option == "--case") {
      if (saw_case) {
        throw CliError("--case may be specified only once");
      }
      result.solve.case_file = require_value(argc, argv, index, option);
      saw_case = true;
    } else if (option == "--output") {
      if (saw_output) {
        throw CliError("--output may be specified only once");
      }
      result.solve.output_dir = require_value(argc, argv, index, option);
      saw_output = true;
    } else if (option == "--restart") {
      if (saw_restart) {
        throw CliError("--restart may be specified only once");
      }
      result.solve.restart_file = require_value(argc, argv, index, option);
      saw_restart = true;
    } else if (option == "--report-level") {
      if (saw_report_level) {
        throw CliError("--report-level may be specified only once");
      }
      const std::string level = require_value(argc, argv, index, option);
      if (level == "brief") {
        result.solve.report_level = ReportLevel::brief;
      } else if (level == "full") {
        result.solve.report_level = ReportLevel::full;
      } else {
        throw CliError("invalid --report-level '" + level + "'; expected brief or full");
      }
      saw_report_level = true;
    } else if (option == "--help" || option == "-h") {
      throw CliError("--help cannot be combined with solve options");
    } else {
      throw CliError("unknown solve option '" + option + "'");
    }
  }

  if (!saw_case || !saw_output) {
    throw CliError("solve requires both --case and --output");
  }
  return result;
}

} // namespace aerofv
