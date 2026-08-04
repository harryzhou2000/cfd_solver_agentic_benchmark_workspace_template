#include "cfd/case_config.hpp"
#include "cfd/mesh.hpp"
#include "cfd/output.hpp"
#include "cfd/partition.hpp"
#include "cfd/solver.hpp"

#include <mpi.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Arguments {
  std::string command;
  std::filesystem::path case_path;
  std::filesystem::path output_path;
  std::optional<std::filesystem::path> restart_path;
  std::string report_level = "brief";
  cfd::RunOptions run_options;
};

std::string usage() {
  return "Usage:\n"
         "  saturn_cfd solve --case CASE.json --output DIR [--restart restart_final.json] "
         "[--report-level brief|full]\n"
         "  saturn_cfd inspect --case CASE.json\n";
}

Arguments parse_arguments(int argc, char** argv) {
  if (argc < 2) throw std::runtime_error(usage());
  Arguments arguments;
  arguments.command = argv[1];
  if (arguments.command != "solve" && arguments.command != "inspect") {
    throw std::runtime_error("unknown command '" + arguments.command + "'\n" + usage());
  }
  for (int i = 2; i < argc; ++i) {
    const std::string option = argv[i];
    auto value = [&]() -> std::string {
      if (++i >= argc) throw std::runtime_error("missing value after " + option);
      return argv[i];
    };
    if (option == "--case") arguments.case_path = value();
    else if (option == "--output") arguments.output_path = value();
    else if (option == "--restart") arguments.restart_path = value();
    else if (option == "--report-level") arguments.report_level = value();
    else if (option == "--debug-max-steps") arguments.run_options.debug_max_steps = std::stoi(value());
    else if (option == "--debug-final-time") arguments.run_options.debug_final_time = std::stod(value());
    else if (option == "--progress-interval") arguments.run_options.progress_interval = std::stoi(value());
    else if (option == "--help" || option == "-h") throw std::runtime_error(usage());
    else throw std::runtime_error("unknown option '" + option + "'");
  }
  if (arguments.case_path.empty()) throw std::runtime_error("--case is required");
  if (arguments.command == "solve" && arguments.output_path.empty()) throw std::runtime_error("--output is required for solve");
  if (arguments.report_level != "brief" && arguments.report_level != "full") {
    throw std::runtime_error("--report-level must be brief or full");
  }
  if (arguments.run_options.debug_max_steps < 0 || arguments.run_options.debug_final_time < 0.0 ||
      arguments.run_options.progress_interval < 0) {
    throw std::runtime_error("debug and progress controls must be nonnegative");
  }
  return arguments;
}

std::string command_line(int argc, char** argv) {
  std::ostringstream stream;
  for (int i = 0; i < argc; ++i) {
    if (i) stream << ' ';
    const std::string argument = argv[i];
    if (argument.find_first_of(" \t\"'") == std::string::npos) stream << argument;
    else {
      stream << '\'';
      for (char character : argument) {
        if (character == '\'') stream << "'\\''";
        else stream << character;
      }
      stream << '\'';
    }
  }
  return stream.str();
}

}  // namespace

int main(int argc, char** argv) {
  int provided = 0;
  if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) != MPI_SUCCESS) {
    std::cerr << "failed to initialize MPI\n";
    return 1;
  }
  int rank = 0;
  int rank_count = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &rank_count);
  try {
    const Arguments arguments = parse_arguments(argc, argv);
    const cfd::CaseConfig config = cfd::load_case_config(arguments.case_path);
    if (arguments.command == "inspect") {
      if (rank == 0) {
        const cfd::GlobalMesh mesh = cfd::read_cgns_mesh(config.mesh_path, config);
        std::size_t boundary_faces = 0;
        for (const cfd::GlobalFace& face : mesh.faces) if (face.right < 0) ++boundary_faces;
        std::cout << "case=" << config.case_id << " zones=" << mesh.source_zone_count
                  << " source_vertices=" << mesh.source_vertex_count << " merged_vertices=" << mesh.nodes.size()
                  << " interface_merges=" << mesh.merged_interface_vertices << " cells=" << mesh.cells.size()
                  << " faces=" << mesh.faces.size() << " boundary_faces=" << boundary_faces
                  << " max_abs_z=" << mesh.maximum_abs_z << '\n';
      }
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      return 0;
    }

    const std::string start_time = cfd::utc_now();
    const double wall_start = MPI_Wtime();
    std::optional<cfd::PartitionResult> partition;
    if (rank == 0) {
      cfd::GlobalMesh global = cfd::read_cgns_mesh(config.mesh_path, config);
      std::cout << "preprocess: " << global.cells.size() << " cells, " << global.faces.size()
                << " faces, " << global.source_zone_count << " zones\n";
      partition = cfd::partition_global_mesh(global, rank_count);
    }
    cfd::LocalMesh local = cfd::distribute_local_mesh(rank == 0 ? &partition->local_meshes : nullptr,
                                                       MPI_COMM_WORLD);
    partition.reset();  // no rank retains the global mesh or all partitions during solver iterations
    cfd::validate_distributed_mesh(local, MPI_COMM_WORLD);
    if (rank == 0) {
      std::cout << "partition: METIS edge cut " << local.partition_edge_cut << ", rank 0 owns "
                << local.owned_cell_count << " cells and " << local.ghost_cell_count() << " ghosts\n";
    }

    cfd::OutputManager output(arguments.output_path, config, local, MPI_COMM_WORLD);
    std::optional<cfd::InitialState> restart;
    if (arguments.restart_path) restart = cfd::load_restart(*arguments.restart_path, local, MPI_COMM_WORLD);
    cfd::RunResult result = cfd::run_solver(config, local, MPI_COMM_WORLD, output, arguments.run_options, restart);
    const double local_wall = MPI_Wtime() - wall_start;
    double wall_time = 0.0;
    MPI_Allreduce(&local_wall, &wall_time, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    output.finalize(result, command_line(argc, argv), wall_time, start_time, cfd::utc_now());

    int exit_code = result.convergence_status == "converged" || result.convergence_status == "statistically_periodic" ? 0 : 2;
    if (rank == 0) {
      std::cout << "SaturnCFD finished with status " << result.convergence_status << ": " << result.notes << '\n';
    }
    MPI_Finalize();
    return exit_code;
  } catch (const std::exception& error) {
    std::cerr << "[rank " << rank << "] SaturnCFD error: " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
