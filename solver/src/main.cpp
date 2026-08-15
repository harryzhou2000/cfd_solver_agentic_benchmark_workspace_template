#include "cfd/CaseConfig.hpp"
#include "cfd/DistributedMesh.hpp"
#include "cfd/Output.hpp"
#include "cfd/Solver.hpp"

#include <mpi.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

struct Arguments {
  std::filesystem::path case_file;
  std::filesystem::path output_directory;
  std::optional<std::filesystem::path> restart;
  std::string report_level = "brief";
};

[[noreturn]] void usage_error(const std::string& message) {
  throw std::runtime_error(
      message +
      "\nusage: cfd_solver solve --case CASE.json --output DIR [--restart FILE_OR_DIR] "
      "[--report-level brief|full]");
}

Arguments parse_arguments(int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) != "solve") usage_error("expected the 'solve' command");
  Arguments result;
  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    if (i + 1 >= argc) usage_error("missing value for " + option);
    const std::string value = argv[++i];
    if (option == "--case") result.case_file = value;
    else if (option == "--output") result.output_directory = value;
    else if (option == "--restart") result.restart = value;
    else if (option == "--report-level") result.report_level = value;
    else usage_error("unknown option: " + option);
  }
  if (result.case_file.empty()) usage_error("--case is required");
  if (result.output_directory.empty()) usage_error("--output is required");
  if (result.report_level != "brief" && result.report_level != "full") {
    usage_error("--report-level must be brief or full");
  }
  return result;
}

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream result;
  result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return result.str();
}

std::string command_line(int argc, char** argv) {
  std::ostringstream command;
  for (int i = 0; i < argc; ++i) {
    if (i != 0) command << ' ';
    command << argv[i];
  }
  return command.str();
}

void validate_boundaries(const cfd::CaseConfig& config, const cfd::LocalMesh& mesh,
                         MPI_Comm communicator) {
  int local_bad = 0;
  for (const cfd::LocalFace& face : mesh.faces) {
    if (face.right < 0 && config.boundary_conditions.count(face.boundary_tag) == 0) local_bad = 1;
  }
  int global_bad = 0;
  MPI_Allreduce(&local_bad, &global_bad, 1, MPI_INT, MPI_MAX, communicator);
  if (global_bad) throw std::runtime_error("mesh contains an exterior boundary without a case mapping");
  for (const auto& [tag, type] : config.boundary_conditions) {
    (void)type;
    int local_found = 0;
    for (const cfd::LocalFace& face : mesh.faces) {
      if (face.right < 0 && face.boundary_tag == tag) local_found = 1;
    }
    int global_found = 0;
    MPI_Allreduce(&local_found, &global_found, 1, MPI_INT, MPI_MAX, communicator);
    if (!global_found) throw std::runtime_error("case boundary family not present in mesh: " + tag);
  }
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int exit_code = 0;
  try {
    const Arguments arguments = parse_arguments(argc, argv);
    const cfd::CaseConfig config = cfd::CaseConfig::load(arguments.case_file);
    const cfd::LocalMesh mesh =
        cfd::MeshDistributor::load_partition(config.mesh.file.string(), MPI_COMM_WORLD);
    validate_boundaries(config, mesh, MPI_COMM_WORLD);
    cfd::OutputWriter output(arguments.output_directory, config, mesh, MPI_COMM_WORLD);
    cfd::Solver solver(config, mesh, MPI_COMM_WORLD, output);
    if (arguments.restart) solver.load_restart(*arguments.restart);
    cfd::RunSummary summary = solver.run(command_line(argc, argv), utc_now());
    summary.end_time_utc = utc_now();
    output.write_metadata_and_status(summary);
    if (rank == 0) {
      std::cout << "case=" << config.case_id << " status=" << summary.convergence_status
                << " steps=" << summary.final_step << " wall_seconds=" << summary.wall_time_seconds
                << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "rank " << rank << " error: " << error.what() << '\n';
    exit_code = 1;
  }
  int global_exit = 0;
  MPI_Allreduce(&exit_code, &global_exit, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Finalize();
  return global_exit;
}
