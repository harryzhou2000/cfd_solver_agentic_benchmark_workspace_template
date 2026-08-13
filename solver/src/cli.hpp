#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace aerofv {

class CliError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class ReportLevel { brief, full };
enum class CliAction { help, solve };

struct SolveCommand {
  std::filesystem::path case_file;
  std::filesystem::path output_dir;
  std::optional<std::filesystem::path> restart_file;
  ReportLevel report_level{ReportLevel::brief};
};

struct CliOptions {
  CliAction action{CliAction::help};
  SolveCommand solve;
};

/// Parses exactly: solve --case <case-json> --output <output-dir>
///                 [--restart <restart-file>] [--report-level brief|full]
/// `--help`, `help`, and `solve --help` return CliAction::help.
CliOptions parse_cli(int argc, char *const argv[]);
std::string cli_usage(const std::string &program_name = "aerofv");
const char *to_string(ReportLevel level) noexcept;

} // namespace aerofv
