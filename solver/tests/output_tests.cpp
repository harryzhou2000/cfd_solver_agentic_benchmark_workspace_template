#include "cfd/config.hpp"
#include "cfd/gas.hpp"
#include "cfd/output.hpp"
#include "cfd/solver.hpp"

#include <nlohmann/json.hpp>

#include <mpi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error("test assertion failed: " + message);
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "open " + path.string());
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

std::string first_line(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string line;
  std::getline(input, line);
  return line;
}

cfd::CaseConfig test_config() {
  cfd::CaseConfig config;
  config.case_id = "output-roundtrip";
  config.mesh.file = "synthetic-memory-mesh.cgns";
  config.physics.mode = cfd::PhysicsMode::inviscid;
  config.gas = {"calorically_perfect", 1.4, 1.0, 0.72};
  config.freestream = {0.2, 0.0, 1.0, 1.0, 1.0 / (1.4 * 0.2 * 0.2)};
  config.reference.length = 1.0;
  config.reference.area = 1.0;
  config.reference.reynolds_length = 1.0;
  config.boundary_conditions["FAR"] = cfd::BoundaryCondition::farfield;
  config.boundary_conditions["WALL"] = cfd::BoundaryCondition::no_slip_adiabatic_wall;
  config.numerics_required.spatial_order = 2;
  config.numerics_required.inviscid_flux = "rusanov";
  config.numerics_required.viscous_flux = "corrected_gradient";
  config.numerics_required.main_time_method = "implicit_pseudo_time";
  config.numerics_required.implicit_solver = "point_jacobi";
  config.run_control.type = cfd::RunType::steady;
  config.run_control.max_steps = 2;
  config.run_control.residual_reduction_target = 1.0;
  config.run_control.min_inner_iterations = 1;
  config.run_control.max_inner_iterations = 3;
  config.run_control.inner_residual_reduction_target = 0.1;
  config.run_control.cfl_initial = 1.0;
  config.run_control.cfl_max = 1.0;
  config.run_control.pseudo_cfl_ramp_steps = 8;
  config.outputs.write_forces_every = 1;
  config.outputs.write_residuals_every = 1;
  return config;
}

cfd::DistributedMesh test_mesh(int rank, int size) {
  cfd::DistributedMesh mesh;
  mesh.rank = rank;
  mesh.size = size;
  mesh.global_vertex_count = static_cast<std::size_t>(4 * size);
  mesh.global_cell_count = static_cast<std::size_t>(size);
  mesh.global_face_count = static_cast<std::size_t>(4 * size);
  mesh.owned_cell_count = 1;
  const double x = static_cast<double>(rank) * 2.0;
  for (int i = 0; i < 4; ++i) {
    const std::array<cfd::Vec2, 4> points{{{x, 0.0}, {x + 1.0, 0.0},
                                           {x + 1.0, 1.0}, {x, 1.0}}};
    mesh.vertices.push_back({4 * rank + i, points[static_cast<std::size_t>(i)]});
  }
  cfd::LocalCell cell;
  cell.global_id = rank;
  cell.owner = rank;
  cell.owned = true;
  cell.vertices = {0, 1, 2, 3};
  cell.faces = {0, 1, 2, 3};
  cell.center = {x + 0.5, 0.5};
  cell.area = 1.0;
  mesh.cells.push_back(cell);
  const std::array<std::array<cfd::LocalIndex, 2>, 4> vertices{{{{0, 1}}, {{1, 2}},
                                                                {{2, 3}}, {{3, 0}}}};
  const std::array<cfd::Vec2, 4> centers{{{x + 0.5, 0.0}, {x + 1.0, 0.5},
                                          {x + 0.5, 1.0}, {x, 0.5}}};
  const std::array<cfd::Vec2, 4> normals{{{0.0, -1.0}, {1.0, 0.0},
                                          {0.0, 1.0}, {-1.0, 0.0}}};
  for (int i = 0; i < 4; ++i) {
    cfd::LocalFace face;
    face.global_id = 4 * rank + i;
    face.vertices = vertices[static_cast<std::size_t>(i)];
    face.left_cell = 0;
    face.center = centers[static_cast<std::size_t>(i)];
    face.normal = normals[static_cast<std::size_t>(i)];
    face.length = 1.0;
    face.boundary = i == 2 ? "WALL" : "FAR";
    mesh.faces.push_back(face);
  }
  mesh.diagnostics.rank = rank;
  mesh.diagnostics.owned_cells = 1;
  mesh.global_diagnostics.total_owned_cells = static_cast<std::size_t>(size);
  mesh.global_diagnostics.ranks.resize(static_cast<std::size_t>(size));
  for (int i = 0; i < size; ++i) {
    mesh.global_diagnostics.ranks[static_cast<std::size_t>(i)].rank = i;
    mesh.global_diagnostics.ranks[static_cast<std::size_t>(i)].owned_cells = 1;
  }
  return mesh;
}

void test_cli_and_missing_mesh(int rank) {
  char program[] = "cfd_solver";
  char solve[] = "solve";
  char case_option[] = "--case";
  char case_value[] = "case.json";
  char output_option[] = "--output";
  char output_value[] = "result";
  char report_option[] = "--report-level";
  char report_value[] = "brief";
  char* argv[] = {program, solve, case_option, case_value, output_option, output_value,
                  report_option, report_value};
  const cfd::CliOptions parsed = cfd::parse_cli(8, argv);
  require(parsed.case_file == "case.json" && parsed.output_directory == "result",
          "exact required CLI parses");
  bool rejected = false;
  try { (void)cfd::parse_cli(1, argv); }
  catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("usage:") != std::string::npos;
  }
  require(rejected, "malformed CLI has clear usage error");

  if (rank == 0) {
    const std::filesystem::path cases =
        std::filesystem::path(BENCHMARK_ROOT) / "inputs" / "cases";
    nlohmann::json fixture = nlohmann::json::parse(
        read_text(cases / "naca0012_m015_inviscid.json"));
    fixture["mesh"]["file"] = "definitely-missing-mesh.cgns";
    rejected = false;
    try {
      (void)cfd::parse_case_json(fixture.dump(), cases, "missing-mesh-test.json");
    } catch (const std::runtime_error& error) {
      rejected = std::string(error.what()).find("resolved mesh does not exist") !=
                 std::string::npos;
    }
    require(rejected, "missing mesh has a clear input error");
  }
}

void test_periodicity() {
  std::vector<std::pair<double, double>> forces;
  for (int i = 0; i < 1000; ++i) {
    const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / 40.0;
    forces.emplace_back(std::sin(phase), 1.0 + 0.05 * std::cos(2.0 * phase));
  }
  const cfd::PeriodicityResult result = cfd::test_force_periodicity(forces);
  require(result.passed, "transparent periodic force gate accepts stable periodic data");
  std::vector<std::pair<double, double>> tiny;
  std::vector<std::pair<double, double>> negative;
  for (int i = 0; i < 1000; ++i) {
    const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(i) / 40.0;
    tiny.emplace_back(1.0e-6 * std::sin(phase), 1.0);
    negative.emplace_back(std::sin(phase), -0.2);
  }
  require(!cfd::test_force_periodicity(tiny).passed,
          "periodicity rejects physically meaningless tiny lift");
  require(!cfd::test_force_periodicity(negative).passed,
          "periodicity rejects negative mean drag");
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  try {
    if (argc == 3 && std::string(argv[1]) == "--verify-cli-smoke") {
      if (rank == 0) {
        const std::filesystem::path directory = argv[2];
        for (const char* file : {"metadata.json", "partition_diagnostics.csv",
                                 "residuals.csv", "forces.csv", "surface.csv",
                                 "field_final.vtu", "restart_final.bin",
                                 "restart_checkpoint.bin", "stdout.log",
                                 "run_status.json"}) {
          require(std::filesystem::is_regular_file(directory / file),
                  std::string("CLI smoke output ") + file);
        }
        const nlohmann::json metadata =
            nlohmann::json::parse(read_text(directory / "metadata.json"));
        const nlohmann::json status =
            nlohmann::json::parse(read_text(directory / "run_status.json"));
        require(metadata.at("completed") == false &&
                    metadata.at("convergence_status") == "failed" &&
                    metadata.at("diagnostic_overrides").at("max_steps") == "2" &&
                    metadata.at("diagnostic_overrides")
                            .at("resume_source_max_steps") == "1",
                "CLI diagnostic resume records old and extended attempt budgets");
        require(status.at("final_step") == 2 &&
                    status.at("convergence_status") == "failed",
                "CLI smoke reaches the extended cumulative attempt budget");
        const std::string cli_log = read_text(directory / "stdout.log");
        const std::string accepted_marker = "accepted=yes";
        const auto accepted_position = cli_log.find(accepted_marker);
        const auto second_accepted_position =
            accepted_position == std::string::npos
                ? std::string::npos
                : cli_log.find(
                      accepted_marker,
                      accepted_position + accepted_marker.size());
        require(accepted_position != std::string::npos &&
                    second_accepted_position != std::string::npos &&
                    cli_log.find(accepted_marker,
                                 second_accepted_position + accepted_marker.size()) ==
                        std::string::npos &&
                    cli_log.find("resume attempt budget: previous_max_steps=1 "
                                 "current_max_steps=2") != std::string::npos,
                "resume appends exactly one attempt and records budget continuity");
        require(metadata.at("git_revision").get<std::string>().size() == 40U &&
                    metadata.at("executable_sha256") == cfd::sha256_file(CFD_SOLVER_PATH) &&
                    metadata.at("source_dirty").is_boolean(),
                 "CLI provenance identifies build and exact running executable");
        for (const char* restart_name : {"restart_checkpoint.bin", "restart_final.bin"}) {
          std::ifstream restart(directory / restart_name, std::ios::binary);
          std::array<char, 8> magic{};
          std::uint32_t version = 0;
          restart.read(magic.data(), static_cast<std::streamsize>(magic.size()));
          restart.read(reinterpret_cast<char*>(&version), sizeof(version));
          require(restart &&
                      magic == std::array<char, 8>{{'C','F','D','R','S','T','1','5'}} &&
                        version == 15U,
                    std::string("CLI ") + restart_name + " uses v15 binary framing");
        }
        const std::string cli_residuals = read_text(directory / "residuals.csv");
        require(std::count(cli_residuals.begin(), cli_residuals.end(), '\n') == 3,
                "CLI history contains one unique residual row per cumulative attempt");
        const std::string cli_forces = read_text(directory / "forces.csv");
        require(std::count(cli_forces.begin(), cli_forces.end(), '\n') == 3,
                "CLI history contains one unique force row per cumulative attempt");
      }
      MPI_Barrier(MPI_COMM_WORLD);
      MPI_Finalize();
      return 0;
    }
    test_cli_and_missing_mesh(rank);
    test_periodicity();
    cfd::CaseConfig config = test_config();
    cfd::DistributedMesh mesh = test_mesh(rank, size);
    const cfd::CaloricallyPerfectGas gas(config.gas);
    const cfd::Conservative base = gas.freestream(config.freestream);
    cfd::RestartableSolution solution;
    solution.U.resize(4U);
    solution.U_n.resize(4U);
    solution.U_nm1.resize(4U);
    solution.U_best.resize(4U);
    for (std::size_t k = 0; k < 4U; ++k) {
      solution.U[k] = base[k] + (k == 3U ? 0.1 * rank : 0.0);
      solution.U_n[k] = solution.U[k];
      solution.U_nm1[k] = solution.U[k];
      solution.U_best[k] = solution.U[k] + (k == 0U ? 0.01 : 0.0);
    }
    solution.physical_step = 35;
    solution.time = 0.07;

    const std::filesystem::path directory =
        std::filesystem::current_path() / ("output_test_np" + std::to_string(size));
    if (rank == 0) {
      std::error_code error;
      std::filesystem::remove_all(directory, error);
      std::filesystem::create_directories(directory);
    }
    MPI_Barrier(MPI_COMM_WORLD);

    const std::string mesh_hash = cfd::mesh_fingerprint(mesh, MPI_COMM_WORLD);
    const std::string config_hash = cfd::case_fingerprint(config);
    cfd::CaseConfig changed_bc = config;
    changed_bc.boundary_conditions["WALL"] = cfd::BoundaryCondition::slip_wall;
    require(cfd::case_fingerprint(changed_bc) != config_hash,
            "case fingerprint changes with boundary controls");
    cfd::CaseConfig changed_control = config;
    changed_control.run_control.cfl_max += 1.0;
    require(cfd::case_fingerprint(changed_control) != config_hash,
            "case fingerprint changes with run controls");
    mesh.faces[0].boundary = "ALTERED";
    const std::string altered_mesh_hash = cfd::mesh_fingerprint(mesh, MPI_COMM_WORLD);
    require(altered_mesh_hash != mesh_hash, "mesh fingerprint covers boundary tags");
    mesh.faces[0].boundary = "FAR";
    const std::string fingerprint = mesh_hash + "|" + config_hash;
    cfd::ContinuationState saved_context;
    saved_context.steady_residual_baseline = 2.5;
    saved_context.last_residual = 0.25;
    saved_context.inner.observe(7, true, 1.0e-3);
    saved_context.force_window = {{0.1, 1.2}, {-0.1, 1.1}};
    saved_context.original_start_time_utc = "2026-01-02T03:04:05Z";
    saved_context.all_accepted_transient_targets = true;
    saved_context.history_complete = true;
    saved_context.rollbacks = 3;
    saved_context.total_attempted_steps = 35;
    saved_context.last_inner_iterations = 7;
    saved_context.residual_output_rows = 1;
    saved_context.force_output_rows = 1;
    saved_context.last_residual_output_step = 1;
    saved_context.last_force_output_step = 1;
    saved_context.solver.cfl = 1.0;
    saved_context.solver.nonlinear_steps = 35;
    saved_context.solver.steady_previous_residual = 0.25;
    saved_context.solver.steady_best_residual = 0.2;
    saved_context.solver.steady_trend_reference_residual = 0.24;
    saved_context.solver.steady_trend_samples = 2;
    saved_context.solver.steady_rejected_attempts = 2;
    saved_context.solver.steady_fallback_accepted_steps = 1;
    saved_context.solver.steady_fallback_attempts = 2;
    saved_context.solver.steady_fallback_rejected_steps = 1;
    saved_context.solver.steady_fallback_cfl_halvings = 3;
    saved_context.solver.steady_last_fallback_cfl = 0.05;
    saved_context.solver.steady_fallback_mode = false;
    saved_context.solver.steady_fallback_cfl = 0.04;
    saved_context.solver.steady_fallback_residual_window = {0.25, 0.24};
    saved_context.solver.steady_fallback_steps_since_jfnk = 7;
    saved_context.solver.steady_jfnk_failure_streak = 2;
    saved_context.solver.steady_jfnk_attempts = 2;
    saved_context.solver.steady_initial_residual_scale = 2.5;
    saved_context.solver.steady_jfnk_epsilon_reference_residual = 2.5;
    saved_context.solver.steady_jfnk_epsilon_multiplier = 0.25;
    saved_context.solver.steady_jfnk_last_epsilon = 1.25e-8;
    saved_context.solver.steady_jfnk_last_epsilon_halvings = 2;
    saved_context.solver.steady_reconstruction_blend = 1.0;
    saved_context.solver.steady_first_order_accepted_steps = 2;
    saved_context.solver.steady_order_ramp_accepted_steps = 2;
    saved_context.solver.steady_full_order_accepted_steps = 32;
    saved_context.solver.steady_full_order_initial_residual = 2.5;
    saved_context.solver.steady_full_order_best_residual = 0.2;
    saved_context.solver.steady_rescue_attempts = 2;
    saved_context.solver.steady_rescue_accepted_steps = 1;
    saved_context.solver.steady_rescue_total_gmres_iterations = 45;
    saved_context.solver.steady_rescue_last_gmres_iterations = 20;
    saved_context.solver.steady_rescue_max_gmres_iterations = 25;
    saved_context.solver.steady_rescue_last_gmres_ratio = 0.005;
    saved_context.solver.steady_rescue_last_cfl = 1.0;
    saved_context.solver.steady_rescue_last_line_scale = 0.5;
    saved_context.solver.steady_rescue_reference_residual = 0.2;
    saved_context.solver.steady_rescue_stagnation_count = 3;
    saved_context.solver.steady_rescue_cooldown_attempts = 17;
    saved_context.solver.steady_trust_region_retry_batches = 2;
    saved_context.solver.steady_trust_region_retry_candidates = 6;
    saved_context.solver.steady_trust_region_retry_accepted_steps = 1;
    saved_context.solver.steady_trust_region_retry_total_gmres_iterations = 95;
    saved_context.solver.steady_trust_region_retry_last_candidate_count = 3;
    saved_context.solver.steady_trust_region_retry_last_total_gmres_iterations = 48;
    saved_context.solver.steady_trust_region_retry_last_accepted_gmres_iterations = 20;
    saved_context.solver.steady_trust_region_retry_last_accepted_cfl = 0.5;
    saved_context.solver.steady_trust_region_retry_last_line_scale = 0.5;
    saved_context.solver.steady_trust_region_retry_last_initial_residual = 0.2;
    saved_context.solver.steady_trust_region_retry_last_final_residual = 0.19;
    saved_context.solver.steady_trust_region_retry_cooldown_attempts = 0;
    saved_context.solver.steady_fallback_disabled = true;
    saved_context.solver.steady_fallback_consecutive_accepted_steps = 1;
    saved_context.solver.steady_fallback_growth_disables = 1;
    saved_context.solver.steady_lusgs_preconditioner_applications = 4;
    saved_context.solver.steady_lusgs_preconditioner_sweeps = 12;
    saved_context.solver.steady_lusgs_last_defect_ratio = 0.07;
    saved_context.solver.steady_nonmonotone_residual_window =
        {0.202, 0.201, 0.2};
    saved_context.solver.steady_strict_decrease_stagnation_streak = 3;
    saved_context.solver.steady_nonmonotone_bridge_active = true;
    saved_context.solver.steady_nonmonotone_steps_since_strict_best = 4;
    saved_context.solver.steady_nonmonotone_accepted_steps = 5;
    saved_context.solver.steady_nonmonotone_max_relative_increase = 0.005;
    saved_context.solver.steady_nonmonotone_strict_best_improvements = 2;
    saved_context.solver.steady_nonmonotone_watchdog_resets = 1;
    saved_context.solver.steady_nonmonotone_bypass_attempts = 7;
    saved_context.solver.steady_nonmonotone_bypass_accepted_steps = 2;
    saved_context.solver.steady_nonmonotone_bypass_trial_evaluations = 41;
    saved_context.solver.steady_nonmonotone_bypass_last_actual_trial_residual =
        0.201;
    saved_context.solver.steady_nonmonotone_bypass_last_gmres_ratio = 0.03;
    saved_context.solver.steady_nonmonotone_envelope_reference = 0.20002;
    saved_context.solver.steady_nonmonotone_envelope_accepted_steps = 3;
    saved_context.solver.steady_nonmonotone_envelope_max_relative_increase =
        8.0e-5;
    saved_context.solver.steady_implicit_bridge.active = true;
    saved_context.solver.steady_implicit_bridge.cfl = 0.08;
    saved_context.solver.steady_implicit_bridge.entry_best_residual = 0.2;
    saved_context.solver.steady_implicit_bridge.attempts = 6;
    saved_context.solver.steady_implicit_bridge.accepted_steps = 5;
    saved_context.solver.steady_implicit_bridge.rejected_steps = 1;
    saved_context.solver.steady_implicit_bridge.accepted_steps_since_best = 4;
    saved_context.solver.steady_implicit_bridge.residual_minimum = 0.19;
    saved_context.solver.steady_implicit_bridge.residual_maximum = 0.21;
    saved_context.solver.steady_implicit_bridge.maximum_relative_growth = 0.03;
    saved_context.solver.steady_implicit_bridge.meaningful_best_improvements = 1;
    saved_context.solver.steady_implicit_bridge.linear_sweeps = 18;
    saved_context.solver.transient_stats.observed_min = 5;
    saved_context.solver.transient_stats.observed_mean = 6.0;
    saved_context.solver.transient_stats.observed_max = 7;
    saved_context.solver.transient_stats.target_met_fraction = 1.0;
    saved_context.solver.transient_stats.last_ratio = 1.0e-3;
    saved_context.solver.transient_samples = 2;
    saved_context.solver.transient_iteration_sum = 12.0;
    const auto checkpoint_path = directory / "restart_checkpoint.bin";
    const std::string executable_hash = cfd::sha256_file(CFD_SOLVER_PATH);
    cfd::write_restart(checkpoint_path, fingerprint, executable_hash, config, mesh, solution,
                       saved_context, MPI_COMM_WORLD);
    cfd::write_restart(checkpoint_path, fingerprint, executable_hash, config, mesh, solution,
                       saved_context, MPI_COMM_WORLD);
    if (rank == 0) {
      std::ifstream binary(checkpoint_path, std::ios::binary);
      auto read_exact = [&](void* data, std::size_t bytes) {
        binary.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
        require(static_cast<bool>(binary), "independent v15 restart header read");
      };
      auto read_string = [&] {
        std::uint64_t length = 0;
        read_exact(&length, sizeof(length));
        std::string value(static_cast<std::size_t>(length), '\0');
        read_exact(value.data(), value.size());
        return value;
      };
      std::array<char, 8> magic{};
      std::uint32_t version = 0;
      read_exact(magic.data(), magic.size());
      read_exact(&version, sizeof(version));
      require(magic == std::array<char, 8>{{'C','F','D','R','S','T','1','5'}} &&
                   version == 15U,
                     "independent restart parser sees CFDRST15 v15");
      require(read_string() == fingerprint && read_string() == config.case_id &&
                  read_string() == executable_hash,
                   "v15 fixed header field order and provenance");
      std::uint64_t count = 0;
      std::uint64_t accepted_step = 0;
      double physical_time = 0.0;
      std::uint64_t continuation_size = 0;
      read_exact(&count, sizeof(count));
      read_exact(&accepted_step, sizeof(accepted_step));
      read_exact(&physical_time, sizeof(physical_time));
      read_exact(&continuation_size, sizeof(continuation_size));
      require(count == static_cast<std::uint64_t>(size) && accepted_step == 35U &&
                  physical_time == solution.time && continuation_size > 0U,
                    "v15 fixed count/step/time/continuation order");
      const auto record_start = static_cast<std::uintmax_t>(binary.tellg()) +
                                continuation_size;
      const std::uintmax_t expected_size = record_start + count *
          (sizeof(std::int64_t) + 16U * sizeof(double));
      require(std::filesystem::file_size(checkpoint_path) == expected_size,
                    "v15 cell records include current, BDF, and trusted-best states");
    }
    bool provenance_rejected = false;
    try {
      (void)cfd::read_restart(checkpoint_path, fingerprint, std::string(64U, '0'),
                              config, mesh, MPI_COMM_WORLD);
    } catch (const std::runtime_error& error) {
      provenance_rejected = std::string(error.what()).find("provenance mismatch") !=
                            std::string::npos;
    }
    require(provenance_rejected, "restart rejects executable provenance mismatch");
    const cfd::RestartData restored_data = cfd::read_restart(
        checkpoint_path, fingerprint, executable_hash, config, mesh, MPI_COMM_WORLD);
    const cfd::RestartableSolution& restored = restored_data.solution;
    require(restored.physical_step == solution.physical_step && restored.time == solution.time,
            "restart step/time round trip");
    require(restored.U == solution.U && restored.U_n == solution.U_n &&
                 restored.U_nm1 == solution.U_nm1 &&
                 restored.U_best == solution.U_best,
             "restart current, BDF, and trusted-best states round trip by global ID");
    require(restored_data.continuation.steady_residual_baseline == 2.5 &&
                restored_data.continuation.last_residual == 0.25 &&
                restored_data.continuation.inner.samples == 1U &&
                restored_data.continuation.force_window == saved_context.force_window &&
                restored_data.continuation.original_start_time_utc ==
                    saved_context.original_start_time_utc &&
                restored_data.continuation.rollbacks == 3U &&
                restored_data.continuation.total_attempted_steps == 35U &&
                restored_data.continuation.residual_output_rows == 1U &&
                restored_data.continuation.force_output_rows == 1U &&
                restored_data.continuation.last_residual_output_step == 1U &&
                 restored_data.continuation.last_force_output_step == 1U &&
                 restored_data.continuation.solver.cfl == 1.0 &&
                  restored_data.continuation.solver.nonlinear_steps == 35U &&
                 restored_data.continuation.solver.steady_previous_residual == 0.25 &&
                 restored_data.continuation.solver.steady_best_residual == 0.2 &&
                 restored_data.continuation.solver.steady_trend_reference_residual == 0.24 &&
                  restored_data.continuation.solver.steady_trend_samples == 2 &&
                   !restored_data.continuation.solver.steady_probe_active &&
                   restored_data.continuation.solver.steady_rejected_attempts == 2 &&
                     restored_data.continuation.solver.steady_recovery_probe_cfl == 0.0 &&
                     !restored_data.continuation.solver.steady_recovery_restore_pending &&
                    restored_data.continuation.solver.steady_fallback_accepted_steps == 1U &&
                    restored_data.continuation.solver.steady_fallback_attempts == 2U &&
                    restored_data.continuation.solver.steady_fallback_rejected_steps == 1U &&
                    restored_data.continuation.solver.steady_fallback_cfl_halvings == 3U &&
                     restored_data.continuation.solver.steady_last_fallback_cfl == 0.05 &&
                      !restored_data.continuation.solver.steady_fallback_mode &&
                     restored_data.continuation.solver.steady_fallback_cfl == 0.04 &&
                     restored_data.continuation.solver.steady_fallback_residual_window ==
                         saved_context.solver.steady_fallback_residual_window &&
                     restored_data.continuation.solver.steady_fallback_steps_since_jfnk == 7U &&
                     restored_data.continuation.solver.steady_jfnk_failure_streak == 2 &&
                      restored_data.continuation.solver.steady_jfnk_attempts == 2U &&
                       restored_data.continuation.solver.steady_initial_residual_scale == 2.5 &&
                       restored_data.continuation.solver.steady_jfnk_epsilon_reference_residual == 2.5 &&
                       restored_data.continuation.solver.steady_jfnk_epsilon_multiplier == 0.25 &&
                       restored_data.continuation.solver.steady_jfnk_last_epsilon == 1.25e-8 &&
                       restored_data.continuation.solver.steady_jfnk_last_epsilon_halvings == 2 &&
                      restored_data.continuation.solver.steady_reconstruction_blend == 1.0 &&
                      restored_data.continuation.solver.steady_first_order_accepted_steps == 2U &&
                      restored_data.continuation.solver.steady_order_ramp_accepted_steps == 2U &&
                      restored_data.continuation.solver.steady_full_order_accepted_steps == 32U &&
                      restored_data.continuation.solver.steady_full_order_initial_residual == 2.5 &&
                       restored_data.continuation.solver.steady_full_order_best_residual == 0.2 &&
                       restored_data.continuation.solver.steady_rescue_attempts == 2U &&
                       restored_data.continuation.solver.steady_rescue_accepted_steps == 1U &&
                       restored_data.continuation.solver.steady_rescue_total_gmres_iterations == 45U &&
                       restored_data.continuation.solver.steady_rescue_last_gmres_iterations == 20 &&
                       restored_data.continuation.solver.steady_rescue_max_gmres_iterations == 25 &&
                       restored_data.continuation.solver.steady_rescue_last_gmres_ratio == 0.005 &&
                       restored_data.continuation.solver.steady_rescue_last_cfl == 1.0 &&
                       restored_data.continuation.solver.steady_rescue_last_line_scale == 0.5 &&
                       restored_data.continuation.solver.steady_rescue_reference_residual == 0.2 &&
                        restored_data.continuation.solver.steady_rescue_stagnation_count == 3U &&
                        restored_data.continuation.solver.steady_rescue_cooldown_attempts == 17U &&
                        restored_data.continuation.solver.steady_trust_region_retry_batches == 2U &&
                        restored_data.continuation.solver.steady_trust_region_retry_candidates == 6U &&
                        restored_data.continuation.solver.steady_trust_region_retry_accepted_steps == 1U &&
                        restored_data.continuation.solver.steady_trust_region_retry_total_gmres_iterations == 95U &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_candidate_count == 3U &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_total_gmres_iterations == 48 &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_accepted_gmres_iterations == 20 &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_accepted_cfl == 0.5 &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_line_scale == 0.5 &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_initial_residual == 0.2 &&
                        restored_data.continuation.solver.steady_trust_region_retry_last_final_residual == 0.19 &&
                        restored_data.continuation.solver.steady_fallback_disabled &&
                       restored_data.continuation.solver.steady_fallback_consecutive_accepted_steps == 1U &&
                        restored_data.continuation.solver.steady_fallback_growth_disables == 1U &&
                        restored_data.continuation.solver.steady_lusgs_preconditioner_applications == 4U &&
                         restored_data.continuation.solver.steady_lusgs_preconditioner_sweeps == 12U &&
                         restored_data.continuation.solver.steady_lusgs_last_defect_ratio == 0.07 &&
                         restored_data.continuation.solver.steady_nonmonotone_residual_window ==
                             saved_context.solver.steady_nonmonotone_residual_window &&
                         restored_data.continuation.solver.steady_strict_decrease_stagnation_streak == 3U &&
                         restored_data.continuation.solver.steady_nonmonotone_bridge_active &&
                         !restored_data.continuation.solver.steady_nonmonotone_bridge_disabled &&
                         restored_data.continuation.solver.steady_nonmonotone_steps_since_strict_best == 4U &&
                         restored_data.continuation.solver.steady_nonmonotone_accepted_steps == 5U &&
                         restored_data.continuation.solver.steady_nonmonotone_max_relative_increase == 0.005 &&
                         restored_data.continuation.solver.steady_nonmonotone_strict_best_improvements == 2U &&
                           restored_data.continuation.solver.steady_nonmonotone_watchdog_resets == 1U &&
                          restored_data.continuation.solver.steady_nonmonotone_bypass_attempts == 7U &&
                          restored_data.continuation.solver.steady_nonmonotone_bypass_accepted_steps == 2U &&
                          restored_data.continuation.solver.steady_nonmonotone_bypass_trial_evaluations == 41U &&
                          restored_data.continuation.solver.steady_nonmonotone_bypass_last_actual_trial_residual == 0.201 &&
                           restored_data.continuation.solver.steady_nonmonotone_bypass_last_gmres_ratio == 0.03 &&
                           restored_data.continuation.solver.steady_nonmonotone_envelope_reference == 0.20002 &&
                           restored_data.continuation.solver.steady_nonmonotone_envelope_accepted_steps == 3U &&
                           restored_data.continuation.solver.steady_nonmonotone_envelope_max_relative_increase == 8.0e-5 &&
                           restored_data.continuation.solver.steady_implicit_bridge.active &&
                           restored_data.continuation.solver.steady_implicit_bridge.cfl == 0.08 &&
                           restored_data.continuation.solver.steady_implicit_bridge.entry_best_residual == 0.2 &&
                           restored_data.continuation.solver.steady_implicit_bridge.attempts == 6U &&
                           restored_data.continuation.solver.steady_implicit_bridge.accepted_steps == 5U &&
                           restored_data.continuation.solver.steady_implicit_bridge.rejected_steps == 1U &&
                           restored_data.continuation.solver.steady_implicit_bridge.residual_minimum == 0.19 &&
                           restored_data.continuation.solver.steady_implicit_bridge.residual_maximum == 0.21 &&
                           restored_data.continuation.solver.steady_implicit_bridge.meaningful_best_improvements == 1U &&
                           restored_data.continuation.solver.steady_implicit_bridge.linear_sweeps == 18U,
               "continuation round trip restores baseline, statistics, force window and solver state");

    const auto v14_checkpoint_path = directory / "restart_v14_migration.bin";
    if (rank == 0) {
      std::ifstream input(checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v15 checkpoint for v14 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      const std::size_t implicit_bridge_tail_bytes =
          2U * sizeof(std::uint8_t) + 5U * sizeof(double) +
          7U * sizeof(std::uint64_t);
      bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size -
                      implicit_bridge_tail_bytes),
                  bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size));
      continuation_size -= implicit_bridge_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v14_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '1', '4'}};
      std::copy(v14_magic.begin(), v14_magic.end(), bytes.begin());
      const std::uint32_t v14_version = 14U;
      std::memcpy(bytes.data() + 8U, &v14_version, sizeof(v14_version));
      std::ofstream output(v14_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v14 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v14 = cfd::read_restart(
        v14_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(!migrated_v14.continuation.solver.steady_implicit_bridge.active &&
                !migrated_v14.continuation.solver.steady_implicit_bridge.disabled &&
                migrated_v14.continuation.solver.steady_implicit_bridge.attempts == 0U &&
                migrated_v14.continuation.solver.steady_implicit_bridge.accepted_steps == 0U,
            "CFDRST14 defaults implicit pseudo-transient bridge state");

    const auto v13_checkpoint_path = directory / "restart_v13_migration.bin";
    if (rank == 0) {
      std::ifstream input(v14_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v14 checkpoint for v13 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      const std::size_t envelope_tail_bytes =
          sizeof(std::uint64_t) + 2U * sizeof(double);
      bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size - envelope_tail_bytes),
                  bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size));
      continuation_size -= envelope_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v13_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '1', '3'}};
      std::copy(v13_magic.begin(), v13_magic.end(), bytes.begin());
      const std::uint32_t v13_version = 13U;
      std::memcpy(bytes.data() + 8U, &v13_version, sizeof(v13_version));
      std::ofstream output(v13_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v13 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v13 = cfd::read_restart(
        v13_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(migrated_v13.continuation.solver
                    .steady_nonmonotone_envelope_reference == -1.0 &&
                migrated_v13.continuation.solver
                    .steady_nonmonotone_envelope_accepted_steps == 0U &&
                migrated_v13.continuation.solver
                    .steady_nonmonotone_envelope_max_relative_increase == 0.0,
            "CFDRST13 defaults nonmonotone activation-envelope state");

    const auto v12_checkpoint_path = directory / "restart_v12_migration.bin";
    if (rank == 0) {
      std::ifstream input(v13_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v13 checkpoint for v12 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      const std::size_t bypass_tail_bytes =
          3U * sizeof(std::uint64_t) + 2U * sizeof(double);
      bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size - bypass_tail_bytes),
                  bytes.begin() + static_cast<std::ptrdiff_t>(
                      position + continuation_size));
      continuation_size -= bypass_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v12_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '1', '2'}};
      std::copy(v12_magic.begin(), v12_magic.end(), bytes.begin());
      const std::uint32_t v12_version = 12U;
      std::memcpy(bytes.data() + 8U, &v12_version, sizeof(v12_version));
      std::ofstream output(v12_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v12 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v12 = cfd::read_restart(
        v12_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(migrated_v12.continuation.solver
                    .steady_nonmonotone_bypass_attempts == 0U &&
                migrated_v12.continuation.solver
                    .steady_nonmonotone_bypass_accepted_steps == 0U &&
                migrated_v12.continuation.solver
                    .steady_nonmonotone_bypass_last_actual_trial_residual == -1.0,
            "CFDRST12 defaults nonmonotone descent-bypass statistics");

    const auto v11_checkpoint_path = directory / "restart_v11_migration.bin";
    if (rank == 0) {
      std::ifstream input(v12_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v12 checkpoint for v11 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      const std::size_t nonmonotone_tail_bytes =
          7U * sizeof(std::uint64_t) + 2U * sizeof(std::uint8_t) +
          saved_context.solver.steady_nonmonotone_residual_window.size() *
              sizeof(double);
      require(continuation_size > nonmonotone_tail_bytes,
              "v12 continuation includes nonmonotone extension");
      const auto erase_begin = bytes.begin() + static_cast<std::ptrdiff_t>(
          position + continuation_size - nonmonotone_tail_bytes);
      bytes.erase(erase_begin,
                  erase_begin +
                      static_cast<std::ptrdiff_t>(nonmonotone_tail_bytes));
      continuation_size -= nonmonotone_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v11_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '1', '1'}};
      std::copy(v11_magic.begin(), v11_magic.end(), bytes.begin());
      const std::uint32_t v11_version = 11U;
      std::memcpy(bytes.data() + 8U, &v11_version, sizeof(v11_version));
      std::ofstream output(v11_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v11 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v11 = cfd::read_restart(
        v11_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(migrated_v11.continuation.solver
                .steady_nonmonotone_residual_window.empty() &&
                migrated_v11.continuation.solver
                        .steady_nonmonotone_accepted_steps == 0U &&
                !migrated_v11.continuation.solver
                     .steady_nonmonotone_bridge_active,
            "CFDRST11 defaults nonmonotone continuation state");

    const auto v10_checkpoint_path = directory / "restart_v10_migration.bin";
    if (rank == 0) {
      std::ifstream input(v11_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v11 checkpoint for v10 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      constexpr std::size_t semantic_tail_bytes = sizeof(std::uint8_t);
      require(continuation_size > semantic_tail_bytes,
              "v11 continuation includes residual-baseline semantic marker");
      const auto erase_begin = bytes.begin() + static_cast<std::ptrdiff_t>(
          position + continuation_size - semantic_tail_bytes);
      bytes.erase(erase_begin,
                  erase_begin + static_cast<std::ptrdiff_t>(semantic_tail_bytes));
      continuation_size -= semantic_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v10_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '1', '0'}};
      std::copy(v10_magic.begin(), v10_magic.end(), bytes.begin());
      const std::uint32_t v10_version = 10U;
      std::memcpy(bytes.data() + 8U, &v10_version, sizeof(v10_version));
      std::ofstream output(v10_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v10 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v10 = cfd::read_restart(
        v10_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(!migrated_v10.continuation.solver
                 .steady_initial_residual_is_original_run,
            "CFDRST10 marks the overloaded residual baseline for migration");
    cfd::FlowSolver migrated_v10_solver(mesh, config, MPI_COMM_WORLD);
    migrated_v10_solver.restore_continuation_state(
        migrated_v10.continuation.solver);
    const cfd::FlowSolverContinuation migrated_v10_state =
        migrated_v10_solver.continuation_state();
    require(migrated_v10_state.steady_initial_residual_is_original_run &&
                migrated_v10_state.steady_initial_residual_scale >= 0.0 &&
                migrated_v10_state.steady_full_order_initial_residual == 2.5,
            "CFDRST10 migration reconstructs original baseline and preserves full-order diagnostic");

    const auto v9_checkpoint_path = directory / "restart_v9_migration.bin";
    if (rank == 0) {
      std::ifstream input(v10_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
              "read v10 checkpoint for v9 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      constexpr std::size_t epsilon_tail_bytes =
          3U * sizeof(double) + sizeof(int);
      require(continuation_size > epsilon_tail_bytes,
              "v10 continuation includes adaptive epsilon extension");
      const auto erase_begin = bytes.begin() + static_cast<std::ptrdiff_t>(
          position + continuation_size - epsilon_tail_bytes);
      bytes.erase(erase_begin,
                  erase_begin + static_cast<std::ptrdiff_t>(epsilon_tail_bytes));
      continuation_size -= epsilon_tail_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> v9_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '9', '\0'}};
      std::copy(v9_magic.begin(), v9_magic.end(), bytes.begin());
      const std::uint32_t v9_version = 9U;
      std::memcpy(bytes.data() + 8U, &v9_version, sizeof(v9_version));
      std::ofstream output(v9_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v9 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v9 = cfd::read_restart(
        v9_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(migrated_v9.solution.U == solution.U &&
                migrated_v9.continuation.solver
                        .steady_trust_region_retry_batches == 2U &&
                migrated_v9.continuation.solver
                        .steady_jfnk_epsilon_reference_residual == -1.0 &&
                migrated_v9.continuation.solver
                        .steady_jfnk_epsilon_multiplier == 1.0 &&
                migrated_v9.continuation.solver.steady_jfnk_last_epsilon == 0.0 &&
                migrated_v9.continuation.solver
                        .steady_jfnk_last_epsilon_halvings == 0,
            "CFDRST9 migrates retry state and defaults adaptive epsilon diagnostics");

    const auto legacy_checkpoint_path = directory / "restart_v8_migration.bin";
    if (rank == 0) {
      std::ifstream input(v11_checkpoint_path, std::ios::binary);
      std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(static_cast<bool>(input) || input.eof(),
               "read v10 checkpoint for v8 migration fixture");
      std::size_t position = 12U;
      for (int field = 0; field < 3; ++field) {
        std::uint64_t length = 0;
        std::memcpy(&length, bytes.data() + position, sizeof(length));
        position += sizeof(length) + static_cast<std::size_t>(length);
      }
      position += 2U * sizeof(std::uint64_t) + sizeof(double);
      const std::size_t continuation_size_position = position;
      std::uint64_t continuation_size = 0;
      std::memcpy(&continuation_size, bytes.data() + position,
                  sizeof(continuation_size));
      position += sizeof(continuation_size);
      constexpr std::size_t v9_retry_tail_bytes =
          5U * sizeof(std::uint64_t) + 2U * sizeof(int) +
          4U * sizeof(double) + sizeof(std::uint64_t);
      constexpr std::size_t v10_epsilon_tail_bytes =
          3U * sizeof(double) + sizeof(int);
      const std::size_t extension_bytes =
          v9_retry_tail_bytes + v10_epsilon_tail_bytes +
          sizeof(std::uint8_t);
      require(continuation_size > extension_bytes,
               "v11 continuation includes retry, epsilon, and semantic extensions");
      const auto erase_begin = bytes.begin() + static_cast<std::ptrdiff_t>(
           position + continuation_size - extension_bytes);
      bytes.erase(erase_begin,
                   erase_begin + static_cast<std::ptrdiff_t>(extension_bytes));
      continuation_size -= extension_bytes;
      std::memcpy(bytes.data() + continuation_size_position, &continuation_size,
                  sizeof(continuation_size));
      const std::array<char, 8> legacy_magic{
          {'C', 'F', 'D', 'R', 'S', 'T', '8', '\0'}};
      std::copy(legacy_magic.begin(), legacy_magic.end(), bytes.begin());
      const std::uint32_t legacy_version = 8U;
      std::memcpy(bytes.data() + 8U, &legacy_version, sizeof(legacy_version));
      std::ofstream output(legacy_checkpoint_path,
                           std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      require(static_cast<bool>(output), "write v8 migration fixture");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    const cfd::RestartData migrated_v8 = cfd::read_restart(
        legacy_checkpoint_path, fingerprint, executable_hash, config, mesh,
        MPI_COMM_WORLD);
    require(migrated_v8.solution.U == solution.U &&
                migrated_v8.continuation.solver
                        .steady_trust_region_retry_batches == 0U &&
                migrated_v8.continuation.solver
                        .steady_trust_region_retry_last_initial_residual == -1.0 &&
                migrated_v8.continuation.solver
                        .steady_trust_region_retry_cooldown_attempts == 0U,
            "CFDRST8 migrates with zero/default trust-retry continuation state");

    cfd::FlowSolver continued_solver(mesh, config, MPI_COMM_WORLD);
    continued_solver.restore_continuation_state(restored_data.continuation.solver);
    require(continued_solver.continuation_state().cfl == saved_context.solver.cfl &&
                 continued_solver.continuation_state().nonlinear_steps ==
                     saved_context.solver.nonlinear_steps &&
                 continued_solver.continuation_state().steady_previous_residual ==
                     saved_context.solver.steady_previous_residual &&
                 continued_solver.continuation_state().steady_best_residual ==
                     saved_context.solver.steady_best_residual &&
                 continued_solver.continuation_state().steady_trend_reference_residual ==
                     saved_context.solver.steady_trend_reference_residual &&
                 continued_solver.continuation_state().steady_trend_samples ==
                     saved_context.solver.steady_trend_samples &&
                  continued_solver.continuation_state().steady_probe_active ==
                      saved_context.solver.steady_probe_active &&
                  continued_solver.continuation_state().steady_rejected_attempts ==
                      saved_context.solver.steady_rejected_attempts &&
                  continued_solver.continuation_state().steady_recovery_probe_cfl ==
                      saved_context.solver.steady_recovery_probe_cfl &&
                   continued_solver.continuation_state().steady_recovery_restore_pending ==
                       saved_context.solver.steady_recovery_restore_pending &&
                   continued_solver.continuation_state().steady_fallback_accepted_steps ==
                       saved_context.solver.steady_fallback_accepted_steps &&
                   continued_solver.continuation_state().steady_fallback_attempts ==
                       saved_context.solver.steady_fallback_attempts &&
                   continued_solver.continuation_state().steady_fallback_rejected_steps ==
                       saved_context.solver.steady_fallback_rejected_steps &&
                   continued_solver.continuation_state().steady_fallback_cfl_halvings ==
                       saved_context.solver.steady_fallback_cfl_halvings &&
                    continued_solver.continuation_state().steady_last_fallback_cfl ==
                        saved_context.solver.steady_last_fallback_cfl &&
                    continued_solver.continuation_state().steady_fallback_mode ==
                        saved_context.solver.steady_fallback_mode &&
                    continued_solver.continuation_state().steady_fallback_cfl ==
                        saved_context.solver.steady_fallback_cfl &&
                    continued_solver.continuation_state().steady_fallback_residual_window ==
                        saved_context.solver.steady_fallback_residual_window &&
                    continued_solver.continuation_state().steady_fallback_steps_since_jfnk ==
                        saved_context.solver.steady_fallback_steps_since_jfnk &&
                    continued_solver.continuation_state().steady_jfnk_failure_streak ==
                        saved_context.solver.steady_jfnk_failure_streak &&
                    continued_solver.continuation_state().steady_jfnk_attempts ==
                        saved_context.solver.steady_jfnk_attempts &&
                     continued_solver.continuation_state().steady_initial_residual_scale ==
                      saved_context.solver.steady_initial_residual_scale &&
                      continued_solver.continuation_state().steady_jfnk_epsilon_reference_residual ==
                          saved_context.solver.steady_jfnk_epsilon_reference_residual &&
                      continued_solver.continuation_state().steady_jfnk_epsilon_multiplier ==
                          saved_context.solver.steady_jfnk_epsilon_multiplier &&
                      continued_solver.continuation_state().steady_jfnk_last_epsilon ==
                          saved_context.solver.steady_jfnk_last_epsilon &&
                      continued_solver.continuation_state().steady_jfnk_last_epsilon_halvings ==
                          saved_context.solver.steady_jfnk_last_epsilon_halvings &&
                     continued_solver.continuation_state().steady_reconstruction_blend ==
                         saved_context.solver.steady_reconstruction_blend &&
                     continued_solver.continuation_state().steady_first_order_accepted_steps ==
                         saved_context.solver.steady_first_order_accepted_steps &&
                     continued_solver.continuation_state().steady_order_ramp_accepted_steps ==
                         saved_context.solver.steady_order_ramp_accepted_steps &&
                     continued_solver.continuation_state().steady_full_order_accepted_steps ==
                         saved_context.solver.steady_full_order_accepted_steps &&
                     continued_solver.continuation_state().steady_full_order_initial_residual ==
                         saved_context.solver.steady_full_order_initial_residual &&
                      continued_solver.continuation_state().steady_full_order_best_residual ==
                          saved_context.solver.steady_full_order_best_residual &&
                      continued_solver.continuation_state().steady_rescue_attempts ==
                          saved_context.solver.steady_rescue_attempts &&
                      continued_solver.continuation_state().steady_rescue_accepted_steps ==
                          saved_context.solver.steady_rescue_accepted_steps &&
                      continued_solver.continuation_state().steady_rescue_total_gmres_iterations ==
                          saved_context.solver.steady_rescue_total_gmres_iterations &&
                      continued_solver.continuation_state().steady_rescue_last_gmres_iterations ==
                          saved_context.solver.steady_rescue_last_gmres_iterations &&
                      continued_solver.continuation_state().steady_rescue_last_gmres_ratio ==
                          saved_context.solver.steady_rescue_last_gmres_ratio &&
                      continued_solver.continuation_state().steady_rescue_last_cfl ==
                          saved_context.solver.steady_rescue_last_cfl &&
                       continued_solver.continuation_state().steady_rescue_cooldown_attempts ==
                           saved_context.solver.steady_rescue_cooldown_attempts &&
                       continued_solver.continuation_state().steady_trust_region_retry_batches ==
                           saved_context.solver.steady_trust_region_retry_batches &&
                       continued_solver.continuation_state().steady_trust_region_retry_candidates ==
                           saved_context.solver.steady_trust_region_retry_candidates &&
                       continued_solver.continuation_state().steady_trust_region_retry_accepted_steps ==
                           saved_context.solver.steady_trust_region_retry_accepted_steps &&
                       continued_solver.continuation_state().steady_trust_region_retry_last_accepted_cfl ==
                           saved_context.solver.steady_trust_region_retry_last_accepted_cfl &&
                       continued_solver.continuation_state().steady_trust_region_retry_last_initial_residual ==
                           saved_context.solver.steady_trust_region_retry_last_initial_residual &&
                       continued_solver.continuation_state().steady_trust_region_retry_last_final_residual ==
                           saved_context.solver.steady_trust_region_retry_last_final_residual &&
                       continued_solver.continuation_state().steady_fallback_disabled ==
                          saved_context.solver.steady_fallback_disabled &&
                      continued_solver.continuation_state().steady_fallback_growth_disables ==
                          saved_context.solver.steady_fallback_growth_disables,
            "FlowSolver continuation control state restores before the next step");
    if (rank == 0) {
      require(std::filesystem::is_regular_file(checkpoint_path) &&
                  !std::filesystem::exists(checkpoint_path.string() + ".tmp"),
              "checkpoint is atomically published without temporary residue");
    }

    // Run a real numerical step; this is only a diagnostic smoke run and is not
    // represented as a production completion claim.
    cfd::FlowSolver solver(mesh, config, MPI_COMM_WORLD);
    cfd::RestartableSolution smoke = solver.uniform_initial_solution();
    const cfd::StepResult step = solver.steady_step(smoke);
    require(step.accepted, "short actual FlowSolver smoke step accepted");
    cfd::FinalEvaluation final =
        cfd::evaluate_final_state(mesh, config, smoke, step.cfl, MPI_COMM_WORLD);

    {
      cfd::RunOutput output(directory, rank);
      output.log("diagnostic smoke only; no production completion claim");
      output.write_residual(1, 0.0, step.inner.total_linear_sweeps, step.cfl, 0.0,
                            step.residual);
      output.write_force(1, 0.0, final.forces);
      output.flush();
    }
    cfd::write_partition_diagnostics(directory / "partition_diagnostics.csv", mesh,
                                     MPI_COMM_WORLD);
    cfd::write_surface(directory / "surface.csv", config, mesh, final.residual,
                       MPI_COMM_WORLD);
    cfd::write_vtu(directory / "field_final.vtu", config, mesh, smoke, MPI_COMM_WORLD);

    cfd::RunReport report;
    report.command = "output_tests diagnostic smoke";
    report.start_time_utc = cfd::utc_timestamp();
    report.end_time_utc = report.start_time_utc;
    report.wall_time_seconds = 0.01;
    report.final_step = 1;
    report.convergence_status = "failed";
    report.completed = false;
    report.residual_reduction_orders = 1.0;
    report.full_order_residual_reduction_orders = 0.5;
    report.notes = "diagnostic smoke only";
    report.git_revision = cfd::build_git_revision();
    report.source_dirty = cfd::build_source_dirty();
    report.executable_sha256 = cfd::sha256_file(CFD_SOLVER_PATH);
    report.mesh_fingerprint = mesh_hash;
    report.case_fingerprint = config_hash;
    report.physics_gates = cfd::evaluate_physics_gates(
        mesh, config, smoke, final.residual, final.forces, MPI_COMM_WORLD);
    report.final_reconstruction_diagnostics = final.residual.diagnostics;
    report.steady_spatial_order.first_order_target_steps = 2;
    report.steady_spatial_order.ramp_target_steps = 2;
    report.steady_spatial_order.first_order_accepted_steps = 2;
    report.steady_spatial_order.ramp_accepted_steps = 2;
    report.steady_spatial_order.full_order_accepted_steps = 32;
    report.steady_spatial_order.minimum_full_order_steps =
        cfd::steady_full_order_minimum_steps(
            config.run_control.pseudo_cfl_ramp_steps);
    report.steady_spatial_order.final_blend = 1.0;
    report.steady_spatial_order.original_initial_residual = 2.5;
    report.steady_spatial_order.full_order_initial_residual = 2.5;
    report.steady_spatial_order.full_order_best_residual = 0.2;
    report.inner.observe(step.inner.total_linear_sweeps, step.inner.converged,
                         step.inner.defect_ratio);
    cfd::RunReport nonfinite = report;
    nonfinite.wall_time_seconds = std::numeric_limits<double>::quiet_NaN();
    int finite_rejected = 0;
    if (rank == 0) {
      try { cfd::write_metadata_and_status(directory, config, mesh, nonfinite); }
      catch (const std::runtime_error&) { finite_rejected = 1; }
    }
    MPI_Bcast(&finite_rejected, 1, MPI_INT, 0, MPI_COMM_WORLD);
    require(finite_rejected == 1, "JSON writer rejects NaN/Inf");
    cfd::write_metadata_and_status(directory, config, mesh, report);
    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
      require(first_line(directory / "residuals.csv") == cfd::residuals_csv_header,
              "exact residual CSV schema");
      require(first_line(directory / "forces.csv") == cfd::forces_csv_header,
              "exact force CSV schema");
      require(first_line(directory / "surface.csv") == cfd::surface_csv_header,
              "exact surface CSV schema");
      require(first_line(directory / "partition_diagnostics.csv") ==
                  cfd::partition_csv_header,
              "exact partition CSV schema");

      const nlohmann::json metadata = nlohmann::json::parse(
          read_text(directory / "metadata.json"));
      for (const char* field : {"case_id", "solver_name", "solver_version", "git_revision",
                                "source_dirty", "executable_sha256", "mesh_fingerprint",
                                "case_config_fingerprint", "physics_gates",
                                "mpi_ranks", "mesh_file", "num_cells_global",
                                "num_faces_global", "num_cells_owned_local",
                                "num_cells_ghost_local", "partitioner", "partition_edge_cut",
                                "halo_exchange", "full_state_replication_during_iterations",
                                "full_mesh_replication_during_iterations", "equation_set",
                                "inviscid_flux", "entropy_fix", "viscous_flux",
                                 "time_integrator", "implicit_solver", "reconstruction",
                                  "steady_acceptance", "steady_convergence_gate",
                                  "spatial_order_continuation",
                                  "steady_initial_residual_scale",
                                  "steady_full_order_initial_residual",
                                  "residual_reduction_target",
                                  "residual_reduction_orders",
                                  "full_order_residual_reduction_orders",
                                "limiter", "limiter_diagnostics", "spatial_order_claimed",
                                "positivity_preservation",
                                "wall_boundary_output_semantics", "true_bdf2_inner_loop",
                                "typical_inner_iterations", "min_inner_iterations",
                                "max_inner_iterations", "observed_min_inner_iterations",
                                "observed_max_inner_iterations",
                                "inner_residual_reduction_target", "inner_target_misses",
                                "inner_target_converged_fraction", "last_inner_residual_ratio",
                                "start_time_utc", "end_time_utc", "completed",
                                "convergence_status"}) {
        require(metadata.contains(field), std::string("metadata required field ") + field);
      }
      const std::string metadata_text = read_text(directory / "metadata.json");
      require(metadata_text.find("NaN") == std::string::npos &&
                  metadata_text.find("Infinity") == std::string::npos,
              "JSON serialization is finite");
      require(metadata.at("completed") == false &&
                   metadata.at("convergence_status") == "failed",
               "smoke output does not claim production completion");
      require(metadata.at("implicit_solver") ==
                    cfd::steady_linear_solver_name,
                "steady metadata identifies matrix-free GMRES and its preconditioner");
      require(metadata.at("steady_acceptance").at("primary_method") ==
                     cfd::steady_linear_solver_name &&
                   metadata.at("steady_acceptance").at("fallback_method") ==
                     cfd::steady_fallback_solver_name &&
                   metadata.at("steady_acceptance").at("preconditioner") ==
                     "frozen_first_order_full_block_rusanov_lu_sgs" &&
                   metadata.at("steady_acceptance").at("fallback_maximum_cfl") ==
                     0.1 &&
                   metadata.at("steady_acceptance").at("fallback_acceptance") ==
                     "positive_strict_actual_residual_decrease" &&
                   metadata.at("steady_acceptance").contains(
                     "jfnk_accepted_steps") &&
                    metadata.at("steady_acceptance").contains(
                      "fallback_accepted_steps") &&
                    metadata.at("steady_acceptance").contains(
                      "lusgs_preconditioner_sweeps") &&
                    metadata.at("steady_acceptance").at(
                      "trust_region_retry_scope") ==
                      "all_steady_spatial_order_phases_after_primary_jfnk_failure",
               "metadata reports primary/fallback acceptance methods and counts");
      require(metadata.at("limiter") ==
                  "Venkatakrishnan with Barth-Jespersen positivity/shock fallback" &&
                  metadata.at("limiter_diagnostics").contains(
                      "venkatakrishnan_limited_face_components") &&
                  metadata.at("limiter_diagnostics").contains(
                      "shock_fallback_cells") &&
                  metadata.at("limiter_diagnostics").contains(
                      "positivity_barth_fallbacks"),
               "metadata identifies smooth limiter and hard fallback diagnostics");
      require(metadata.at("spatial_order_continuation").at(
                      "first_order_accepted_steps") == 2U &&
                  metadata.at("spatial_order_continuation").at(
                      "ramp_accepted_steps") == 2U &&
                  metadata.at("spatial_order_continuation").at(
                      "full_order_accepted_steps") == 32U &&
                  metadata.at("spatial_order_continuation").at(
                      "final_reconstruction_blend") == 1.0 &&
                   metadata.at("spatial_order_continuation").at(
                       "completion_requires_sustained_full_order") == true &&
                   metadata.at("spatial_order_continuation").at(
                       "minimum_full_order_steps") == 50U &&
                   metadata.at("spatial_order_continuation").at(
                       "original_initial_residual") == 2.5 &&
                   metadata.at("spatial_order_continuation").at(
                       "full_order_initial_residual_role") == "diagnostic_only" &&
                   metadata.at("steady_convergence_gate").at(
                       "residual_baseline") ==
                       "original_run_global_initial_residual" &&
                   metadata.at("steady_convergence_gate").at(
                       "required_full_order_accepted_steps") == 50U &&
                   metadata.at("steady_convergence_gate").at(
                       "rejected_attempts_count_toward_hold") == false &&
                   metadata.at("spatial_order_continuation").at(
                       "final_outputs_use_full_order_reconstruction") == true,
              "metadata reports exact convergence gate and both residual baselines");
      const nlohmann::json status = nlohmann::json::parse(
          read_text(directory / "run_status.json"));
      require(status.at("steady_initial_residual_scale") == 2.5 &&
                  status.at("steady_full_order_initial_residual") == 2.5 &&
                  status.at("residual_reduction_orders") == 1.0 &&
                  status.at("full_order_residual_reduction_orders") == 0.5,
              "run status reports both baselines and both reduction metrics consistently");
      const std::string revision = metadata.at("git_revision");
      const std::string metadata_executable_hash = metadata.at("executable_sha256");
      require(revision.size() == 40U && metadata_executable_hash.size() == 64U &&
                   revision.find_first_not_of("0123456789abcdef") == std::string::npos &&
                   metadata_executable_hash.find_first_not_of("0123456789abcdef") ==
                       std::string::npos,
              "provenance revision and running executable SHA-256 have exact shapes");

      const std::string vtu = read_text(directory / "field_final.vtu");
      require(vtu.find("<VTKFile type=\"UnstructuredGrid\"") != std::string::npos &&
                  vtu.find("</VTKFile>") != std::string::npos,
              "VTU parses as a complete basic XML VTK document");
      require(vtu.find("NumberOfCells=\"" + std::to_string(size) + "\"") !=
                  std::string::npos,
              "VTU actual global cell count");
      for (const char* array : {"rho", "u", "v", "pressure", "mach", "rhoE",
                                "temperature", "owner_rank"}) {
        require(vtu.find("Name=\"" + std::string(array) + "\"") != std::string::npos,
                std::string("VTU array ") + array);
      }
    }

    const std::filesystem::path resume_directory =
        std::filesystem::current_path() / ("resume_test_np" + std::to_string(size));
    if (rank == 0) {
      std::error_code error;
      std::filesystem::remove_all(resume_directory, error);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    {
      cfd::RunOutput stream(resume_directory, rank);
      stream.log("complete log line");
      stream.write_residual(1, 0.0, 3, 1.0, 0.0, step.residual);
      stream.write_force(1, 0.0, final.forces);
      stream.flush();
      stream.write_residual(2, 0.0, 3, 1.0, 0.0, step.residual);
      stream.write_force(2, 0.0, final.forces);
      stream.flush();
    }
    if (rank == 0) {
      std::ofstream partial(resume_directory / "residuals.csv", std::ios::app);
      partial << "3,partial";
      std::ofstream partial_log(resume_directory / "stdout.log", std::ios::app);
      partial_log << "partial";
    }
    MPI_Barrier(MPI_COMM_WORLD);
    {
      cfd::RunOutput resumed(resume_directory, rank, true, 1U, 1U, 1U);
      resumed.write_residual(2, 0.0, 3, 1.0, 0.0, step.residual);
      resumed.write_force(2, 0.0, final.forces);
      resumed.write_residual(2, 0.0, 3, 1.0, 0.0, step.residual);
      resumed.write_force(2, 0.0, final.forces);
      resumed.flush();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) {
      const std::string residual_text = read_text(resume_directory / "residuals.csv");
      const std::string force_text = read_text(resume_directory / "forces.csv");
      require(std::count(residual_text.begin(), residual_text.end(), '\n') == 3 &&
                  std::count(force_text.begin(), force_text.end(), '\n') == 3,
              "resume truncates partial rows and appends exactly one row per step");
      require(residual_text.find(cfd::residuals_csv_header) == 0U &&
                  residual_text.find(cfd::residuals_csv_header, 1U) == std::string::npos &&
                  force_text.find(cfd::forces_csv_header, 1U) == std::string::npos,
              "resume output has no duplicate headers");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) std::cout << "output tests passed with " << size << " rank(s)\n";
    MPI_Finalize();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rank " << rank << " output test failure: " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
