#pragma once

#include <filesystem>
#include <string>

#include <mpi.h>

#include "cfd/case_config.hpp"
#include "cfd/output.hpp"

namespace cfd {

struct SolveOptions {
  std::filesystem::path case_path;
  std::filesystem::path output_dir;
  std::filesystem::path restart_file;
  std::string report_level{"brief"};
  std::string command_line;
};

RunSummary solve_case(const SolveOptions& options, MPI_Comm comm);

}  // namespace cfd
