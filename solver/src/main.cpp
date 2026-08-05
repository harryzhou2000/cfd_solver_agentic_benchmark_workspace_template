#include "cfd/config.hpp"
#include "cfd/io.hpp"
#include "cfd/partition.hpp"
#include "cfd/solver.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mpi.h>

namespace {

struct CommandLine {
  std::filesystem::path case_file;
  std::filesystem::path output_directory;
  std::filesystem::path restart_file;
  std::string report_level{"brief"};
  bool steady_newton_only{false};
};

std::string usage() {
  return "Usage: cfd_solver solve --case <case.json> --output <output-dir> "
         "[--restart <restart_final.manifest.json>] [--report-level brief|full] [--steady-newton-only]";
}

CommandLine parse_command_line(const int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) != "solve") {
    throw std::runtime_error(usage());
  }
  CommandLine command;
  for (int index = 2; index < argc; ++index) {
    const std::string argument(argv[index]);
    const auto require_value = [&]() -> std::string {
      if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + argument + "\n" + usage());
      }
      return argv[++index];
    };
    if (argument == "--case") {
      command.case_file = require_value();
    } else if (argument == "--output") {
      command.output_directory = require_value();
    } else if (argument == "--restart") {
      command.restart_file = require_value();
    } else if (argument == "--report-level") {
      command.report_level = require_value();
    } else if (argument == "--steady-newton-only") {
      command.steady_newton_only = true;
    } else if (argument == "--help" || argument == "-h") {
      throw std::runtime_error(usage());
    } else {
      throw std::runtime_error("unrecognized argument: " + argument + "\n" + usage());
    }
  }
  if (command.case_file.empty() || command.output_directory.empty()) {
    throw std::runtime_error("--case and --output are required\n" + usage());
  }
  if (command.report_level != "brief" && command.report_level != "full") {
    throw std::runtime_error("--report-level must be brief or full");
  }
  return command;
}

std::string join_command(const int argc, char** argv) {
  std::ostringstream command;
  for (int index = 0; index < argc; ++index) {
    if (index != 0) {
      command << ' ';
    }
    command << argv[index];
  }
  return command.str();
}

std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm value{};
#if defined(_WIN32)
  gmtime_s(&value, &time);
#else
  gmtime_r(&time, &value);
#endif
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

std::string git_revision() {
  const char* value = std::getenv("CFD_SOLVER_GIT_REVISION");
  return value == nullptr ? std::string{} : std::string(value);
}

void write_stdout_log(const std::filesystem::path& directory, const cfd::CaseConfig& config,
                      const cfd::RunSummary& summary, const double wall_seconds,
                      const std::string& command, const std::string& status) {
  std::ofstream stream(directory / "stdout.log");
  if (!stream) {
    throw std::runtime_error("could not write stdout.log");
  }
  stream << std::setprecision(17)
         << "cfd_solver completed solve command\n"
         << "command: " << command << "\n"
         << "case_id: " << config.case_id << "\n"
         << "run_type: " << cfd::to_string(config.run.type) << "\n"
         << "final_step: " << summary.final_step << "\n"
         << "final_physical_time: " << summary.final_physical_time << "\n"
         << "residual_reduction_orders: " << summary.residual_reduction_orders << "\n"
         << "convergence_status: " << status << "\n"
         << "wall_time_seconds: " << wall_seconds << "\n"
         << "notes: " << summary.diagnostic << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  int exit_code = 1;
  try {
    const CommandLine command = parse_command_line(argc, argv);
    cfd::CaseConfig config = cfd::load_case_config(command.case_file);
    if (command.steady_newton_only) {
      if (config.run.type != cfd::RunType::Steady) {
        throw std::runtime_error("--steady-newton-only is available only for steady cases");
      }
      config.run.steady_newton_only = true;
      config.run.time_integrator = "steady_safeguarded_matrix_free_newton";
    }
    cfd::LocalMesh mesh = cfd::read_partition_distribute(config.mesh_file.string(), config.boundary_conditions,
                                                          MPI_COMM_WORLD);
    cfd::OutputWriter writer(command.output_directory, MPI_COMM_WORLD);
    cfd::FlowSolver solver(config, std::move(mesh), MPI_COMM_WORLD);

    if (!command.restart_file.empty()) {
      const std::filesystem::path restart_directory =
          std::filesystem::is_directory(command.restart_file) ? command.restart_file : command.restart_file.parent_path();
      cfd::OutputWriter restart_reader(restart_directory, MPI_COMM_WORLD);
      int restart_step = 0;
      double restart_time = 0.0;
      solver.restore_owned_state(restart_reader.read_restart_local(solver.mesh(), restart_step, restart_time));
      if (rank == 0) {
        std::cout << "Restarted owned conservative state from step " << restart_step << " at t=" << restart_time << '\n';
      }
    } else {
      solver.initialize();
    }

    const std::string start_time = utc_now();
    const double start_wall = MPI_Wtime();
    const cfd::RunSummary summary = solver.solve();
    const double wall_seconds = MPI_Wtime() - start_wall;
    const std::string end_time = utc_now();
    const std::string status = summary.statistically_periodic ? "statistically_periodic"
                               : summary.converged            ? "converged"
                                                              : "failed";
    const bool completed = status != "failed";

    cfd::OutputMetadata metadata;
    metadata.git_revision = git_revision();
    metadata.viscous_flux = config.physics_mode == cfd::PhysicsMode::Laminar
                                ? "unstructured_primitive_gradient_newtonian_fourier"
                                : "disabled";
    metadata.inviscid_flux = "rusanov_local_lax_friedrichs";
    metadata.implicit_solver = config.run.steady_newton_only
                                   ? "safeguarded_matrix_free_newton_with_block_lu_sgs_right_preconditioning"
                                   : "multi_sweep_rank_local_lu_sgs_rusanov_preconditioner";
    metadata.true_bdf2_inner_loop = config.run.type == cfd::RunType::Transient;
    metadata.typical_inner_iterations = static_cast<int>(std::llround(summary.inner_statistics.mean));
    metadata.min_inner_iterations = config.run.min_inner_iterations;
    metadata.max_inner_iterations = config.run.max_inner_iterations;
    metadata.observed_min_inner_iterations = summary.inner_statistics.minimum;
    metadata.observed_max_inner_iterations = summary.inner_statistics.maximum;
    metadata.inner_residual_reduction_target = config.run.inner_residual_reduction_target;
    metadata.inner_target_misses = summary.inner_statistics.target_misses;
    metadata.inner_target_converged_fraction = summary.inner_statistics.converged_fraction;
    metadata.last_inner_residual_ratio = summary.inner_statistics.last_ratio;
    metadata.start_time_utc = start_time;
    metadata.end_time_utc = end_time;
    metadata.completed = completed;
    metadata.convergence_status = status;

    writer.write_partition_diagnostics(solver.mesh());
    for (const cfd::ResidualRecord& record : summary.residuals) {
      writer.append_residual(record);
    }
    for (const cfd::ForceRecord& record : summary.forces) {
      writer.append_force(record);
    }
    writer.write_surface(summary.local_surface);
    writer.write_field_final(solver.mesh(), solver.state(), solver.gas());
    writer.write_restart_final(solver.mesh(), solver.state(), summary.final_step, summary.final_physical_time);
    writer.write_metadata(config, solver.mesh(), metadata);
    cfd::RunStatus run_status;
    run_status.command = join_command(argc, argv);
    run_status.wall_time_seconds = wall_seconds;
    run_status.final_step = summary.final_step;
    run_status.final_physical_time = summary.final_physical_time;
    run_status.convergence_status = status;
    run_status.residual_reduction_orders = summary.residual_reduction_orders;
    run_status.notes = summary.diagnostic;
    writer.write_run_status(config, run_status);
    if (rank == 0) {
      write_stdout_log(writer.directory(), config, summary, wall_seconds, run_status.command, status);
      std::cout << "case " << config.case_id << " completed with status " << status
                << " in " << wall_seconds << " seconds\n";
    }
    exit_code = completed ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "cfd_solver error on MPI rank " << rank << ": " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  return exit_code;
}
