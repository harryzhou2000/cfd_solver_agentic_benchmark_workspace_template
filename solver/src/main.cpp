#include "case_config.hpp"
#include "cli.hpp"
#include "mesh.hpp"
#include "output.hpp"
#include "partition.hpp"
#include "solver.hpp"

#include <mpi.h>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void mpi_check(int status, const char *operation) {
  if (status == MPI_SUCCESS) {
    return;
  }
  char message[MPI_MAX_ERROR_STRING]{};
  int length = 0;
  MPI_Error_string(status, message, &length);
  throw std::runtime_error(std::string(operation) + ": " +
                           std::string(message, static_cast<std::size_t>(length)));
}

std::string shell_quote(const std::string &value) {
  if (value.find_first_of(" \t\n'\"\\$`;&|<>()[]{}*?!") ==
      std::string::npos) {
    return value;
  }
  std::string result{"'"};
  for (char character : value) {
    if (character == '\'') {
      result += "'\\''";
    } else {
      result += character;
    }
  }
  return result + "'";
}

std::string command_line(int argc, char **argv) {
  std::ostringstream stream;
  for (int index = 0; index < argc; ++index) {
    if (index != 0) {
      stream << ' ';
    }
    stream << shell_quote(argv[index] == nullptr ? "" : argv[index]);
  }
  return stream.str();
}

void broadcast_root_error(int rank, int root_ok, std::string root_error,
                          MPI_Comm communicator) {
  mpi_check(MPI_Bcast(&root_ok, 1, MPI_INT, 0, communicator),
            "MPI_Bcast(initialization status)");
  std::uint64_t size = rank == 0 ? root_error.size() : 0U;
  mpi_check(MPI_Bcast(&size, 1, MPI_UINT64_T, 0, communicator),
            "MPI_Bcast(initialization error size)");
  if (rank != 0) {
    root_error.resize(static_cast<std::size_t>(size));
  }
  if (size > 0) {
    mpi_check(MPI_Bcast(root_error.data(), static_cast<int>(size), MPI_CHAR, 0,
                        communicator),
              "MPI_Bcast(initialization error)");
  }
  if (root_ok == 0) {
    throw std::runtime_error(root_error);
  }
}

} // namespace

int main(int argc, char **argv) {
  int provided = MPI_THREAD_SINGLE;
  if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) !=
      MPI_SUCCESS) {
    std::cerr << "AeroFV: MPI initialization failed\n";
    return 2;
  }
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  try {
    const aerofv::CliOptions options = aerofv::parse_cli(argc, argv);
    if (options.action == aerofv::CliAction::help) {
      if (rank == 0) {
        std::cout << aerofv::cli_usage(argc > 0 ? argv[0] : "aerofv") << '\n';
      }
      MPI_Finalize();
      return 0;
    }
    const aerofv::CaseConfig config =
        aerofv::load_case_config(options.solve.case_file);

    std::optional<aerofv::GlobalMesh> global_mesh;
    int root_ok = 1;
    std::string root_error;
    if (rank == 0) {
      try {
        if (std::filesystem::exists(options.solve.output_dir) &&
            !std::filesystem::is_empty(options.solve.output_dir)) {
          throw std::runtime_error(
              "output directory is not empty; choose a new directory: " +
              options.solve.output_dir.string());
        }
        global_mesh.emplace(
            aerofv::read_cgns_unstructured_2d(config.mesh.file.string()));
      } catch (const std::exception &error) {
        root_ok = 0;
        root_error = error.what();
      }
    }
    broadcast_root_error(rank, root_ok, root_error, MPI_COMM_WORLD);
    aerofv::LocalMesh local_mesh = aerofv::partition_and_distribute(
        rank == 0 ? &*global_mesh : nullptr, MPI_COMM_WORLD);
    global_mesh.reset(); // no solve rank retains the complete mesh

    aerofv::FlowSolver solver(config, local_mesh, MPI_COMM_WORLD);
    aerofv::OutputWriter output(config, local_mesh, options.solve.output_dir,
                                MPI_COMM_WORLD, solver.method_metadata());
    output.prepare();
    output.write_partition_diagnostics();
    const std::string command = command_line(argc, argv);
    const aerofv::RunSummary summary =
        solver.run(output, options.solve.restart_file, command);
    if (rank == 0) {
      std::cout << "AeroFV completed " << config.case_id << " at step "
                << summary.final_step << " with status "
                << summary.convergence_status << '\n';
    }
    mpi_check(MPI_Finalize(), "MPI_Finalize");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "AeroFV rank " << rank << " error: " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 2);
    return 2;
  }
}
