#include "cfd/config.hpp"
#include "cfd/mesh.hpp"
#include "cfd/output.hpp"
#include "cfd/partition.hpp"
#include "cfd/solver.hpp"

#include <nlohmann/json.hpp>

#include <mpi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void bcast_string(std::string& value, int root, MPI_Comm communicator) {
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  std::uint64_t size = rank == root ? static_cast<std::uint64_t>(value.size()) : 0U;
  MPI_Bcast(&size, 1, MPI_UINT64_T, root, communicator);
  if (size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
    throw std::runtime_error("collective message exceeds MPI count range");
  }
  if (rank != root) value.resize(static_cast<std::size_t>(size));
  MPI_Bcast(value.data(), static_cast<int>(size), MPI_CHAR, root, communicator);
}

void collective_failure(const std::string& local_error, MPI_Comm communicator) {
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator, &rank);
  MPI_Comm_size(communicator, &size);
  const int candidate = local_error.empty() ? size : rank;
  int failed_rank = size;
  MPI_Allreduce(&candidate, &failed_rank, 1, MPI_INT, MPI_MIN, communicator);
  if (failed_rank == size) return;
  std::string message = rank == failed_rank ? local_error : std::string{};
  bcast_string(message, failed_rank, communicator);
  throw std::runtime_error("rank " + std::to_string(failed_rank) + ": " + message);
}

template <class Function>
void collective_stage(Function&& function, MPI_Comm communicator) {
  std::string error;
  try { function(); }
  catch (const std::exception& exception) { error = exception.what(); }
  catch (...) { error = "unknown failure"; }
  collective_failure(error, communicator);
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open case file '" + path.string() + "'");
  std::ostringstream text;
  text << input.rdbuf();
  if (input.bad()) throw std::runtime_error("failed reading case file '" + path.string() + "'");
  return text.str();
}

struct ResumeBudgetEvidence {
  int max_steps{};
  std::string case_fingerprint;
};

ResumeBudgetEvidence read_resume_budget_evidence(
    const std::filesystem::path& directory, const cfd::CaseConfig& current_config,
    int ranks, const std::string& executable_sha256) {
  const nlohmann::json metadata = nlohmann::json::parse(
      read_file(directory / "metadata.json"));
  if (!metadata.is_object() ||
      metadata.at("case_id").get<std::string>() != current_config.case_id ||
      metadata.at("mpi_ranks").get<int>() != ranks ||
      !metadata.at("history_complete").get<bool>() ||
      metadata.at("executable_sha256").get<std::string>() !=
          executable_sha256) {
    throw std::runtime_error(
        "resume metadata case/rank/history/executable provenance mismatch");
  }
  const nlohmann::json& overrides = metadata.at("diagnostic_overrides");
  if (!overrides.is_object() || !overrides.contains("max_steps") ||
      !overrides.at("max_steps").is_string()) {
    throw std::runtime_error(
        "resume metadata does not identify its max_steps budget");
  }
  const std::string encoded_max_steps =
      overrides.at("max_steps").get<std::string>();
  std::size_t used = 0;
  long parsed_max_steps = 0;
  try {
    parsed_max_steps = std::stol(encoded_max_steps, &used);
  } catch (const std::exception&) {
    throw std::runtime_error("resume metadata max_steps is invalid");
  }
  if (used != encoded_max_steps.size() || parsed_max_steps < 1 ||
      parsed_max_steps > std::numeric_limits<int>::max()) {
    throw std::runtime_error("resume metadata max_steps is invalid");
  }
  const int prior_max_steps = static_cast<int>(parsed_max_steps);
  const int current_max_steps = current_config.run_control.max_steps.value_or(0);
  if (current_max_steps < prior_max_steps) {
    throw std::runtime_error(
        "resume max_steps cannot be lower than the recorded budget");
  }
  cfd::CaseConfig prior_config = current_config;
  prior_config.run_control.max_steps = prior_max_steps;
  const std::string prior_fingerprint = cfd::case_fingerprint(prior_config);
  if (metadata.at("case_config_fingerprint").get<std::string>() !=
      prior_fingerprint) {
    throw std::runtime_error(
        "resume metadata differs from the current case beyond max_steps");
  }
  return {prior_max_steps, prior_fingerprint};
}

std::string progress_line(std::size_t step, double time, const cfd::StepResult& row,
                          bool transient) {
  std::ostringstream text;
  text << std::setprecision(6) << "step=" << step << " time=" << time
       << " accepted=" << (row.accepted ? "yes" : "no")
       << " residual=" << row.residual.total_l2 << " cfl=" << row.cfl
       << " Cl=" << row.forces.cl << " Cd=" << row.forces.cd
       << " line_scale=" << row.line_search_scale;
  if (transient) {
    text << " nonlinear_inner=" << row.inner.nonlinear_iterations
         << " linear_solver=" << row.inner.linear_solver
         << " linear_iterations=" << row.inner.total_linear_sweeps
         << " last_linear_ratio=" << row.inner.defect_ratio
         << " inner_ratio=" << row.inner.nonlinear_ratio
         << " target=" << (row.target_met ? "met" : "miss")
         << " step_wall_seconds=" << row.wall_time_seconds;
  } else {
    text << " linear_solver=" << row.inner.linear_solver
         << " linear_iterations=" << row.inner.total_linear_sweeps
         << " linear_ratio=" << row.inner.defect_ratio
          << " acceptance=" << cfd::to_string(row.steady_acceptance)
          << " jfnk_attempted=" << (row.jfnk_attempted ? "yes" : "no")
           << " jfnk_iterations=" << row.jfnk_iterations
           << " jfnk_epsilon_reference="
           << row.jfnk_epsilon_reference_residual
           << " jfnk_epsilon_multiplier=" << row.jfnk_epsilon_multiplier
           << " jfnk_last_epsilon=" << row.jfnk_last_epsilon
           << " jfnk_last_epsilon_halvings="
           << row.jfnk_last_epsilon_halvings
           << " rescue_attempted=" << (row.rescue_attempted ? "yes" : "no")
           << " rescue_accepted=" << (row.rescue_accepted ? "yes" : "no")
           << " fallback_mode=" << (row.fallback_mode ? "yes" : "no")
          << " reconstruction_blend=" << row.reconstruction_blend
           << " full_order_steps=" << row.full_order_accepted_steps
           << " limiter_active=" << (row.limiter_active ? "yes" : "no")
           << " nonmonotone_attempted="
           << (row.nonmonotone_bridge_attempted ? "yes" : "no")
            << " nonmonotone_accepted="
            << (row.nonmonotone_bridge_accepted ? "yes" : "no")
            << " nonmonotone_direction_descent="
            << (row.nonmonotone_direction_descent ? "yes" : "no")
            << " nonmonotone_bypass_attempted="
            << (row.nonmonotone_descent_bypass_attempted ? "yes" : "no")
            << " nonmonotone_bypass_accepted="
            << (row.nonmonotone_descent_bypass_accepted ? "yes" : "no")
            << " nonmonotone_bypass_gmres_ratio="
            << row.nonmonotone_bypass_gmres_ratio
            << " nonmonotone_envelope_seeded="
            << (row.nonmonotone_envelope_seeded ? "yes" : "no")
            << " nonmonotone_envelope_accepted="
            << (row.nonmonotone_envelope_accepted ? "yes" : "no")
            << " nonmonotone_envelope_reference="
            << row.nonmonotone_envelope_reference
            << " nonmonotone_envelope_relative_increase="
            << row.nonmonotone_envelope_relative_increase
            << " implicit_bridge_attempted="
            << (row.implicit_bridge_attempted ? "yes" : "no")
            << " implicit_bridge_accepted="
            << (row.implicit_bridge_accepted ? "yes" : "no")
            << " implicit_bridge_cfl=" << row.implicit_bridge_cfl
            << " implicit_bridge_line_scale="
            << row.implicit_bridge_line_scale
            << " implicit_bridge_initial_residual="
            << row.implicit_bridge_initial_residual
            << " implicit_bridge_final_residual="
            << row.implicit_bridge_final_residual
            << " implicit_bridge_relative_growth="
            << row.implicit_bridge_relative_growth
            << " nonmonotone_trial_evaluations="
            << row.nonmonotone_trial_evaluations
            << " nonmonotone_best_trial_residual="
            << row.nonmonotone_best_trial_residual
            << " nonmonotone_reference="
            << row.nonmonotone_reference_residual
            << " nonmonotone_relative_increase="
            << row.nonmonotone_relative_increase
            << " meaningful_strict_best="
            << (row.meaningful_strict_best_improvement ? "yes" : "no")
            << " noise_scale_strict_best="
            << (row.noise_scale_strict_best_improvement ? "yes" : "no")
            << " steady_target=" << (row.target_met ? "met" : "miss");
    if (row.fallback_attempted) {
      text << " fallback_cfl=" << row.fallback_cfl
           << " fallback_sweeps=" << row.fallback_sweeps
           << " fallback_cfl_halvings=" << row.fallback_cfl_halvings;
    }
    if (row.rescue_attempted) {
      text << " rescue_cfl=" << row.rescue_cfl
           << " rescue_gmres_iterations=" << row.rescue_gmres_iterations
           << " rescue_gmres_ratio=" << row.rescue_gmres_ratio
           << " rescue_initial_residual=" << row.rescue_initial_residual
           << " rescue_final_residual=" << row.rescue_final_residual;
    }
    if (row.trust_region_retry_attempted) {
      text << " trust_retry_candidates=" << row.trust_region_retry_candidates
           << " trust_retry_accepted="
           << (row.trust_region_retry_accepted ? "yes" : "no")
           << " trust_retry_accepted_cfl="
           << row.trust_region_retry_accepted_cfl
           << " trust_retry_total_gmres_iterations="
           << row.trust_region_retry_total_gmres_iterations
           << " trust_retry_accepted_gmres_iterations="
           << row.trust_region_retry_accepted_gmres_iterations
           << " trust_retry_initial_residual="
           << row.trust_region_retry_initial_residual
           << " trust_retry_final_residual="
           << row.trust_region_retry_final_residual;
      for (const cfd::ImplicitTrustRegionCandidateDiagnostics& candidate :
           row.trust_region_retry_diagnostics) {
        text << " trust_candidate[cfl=" << candidate.cfl
             << ",gmres_iterations=" << candidate.gmres_iterations
             << ",gmres_ratio=" << candidate.gmres_ratio
             << ",gmres_converged="
             << (candidate.gmres_converged ? "yes" : "no")
             << ",line_evaluations=" << candidate.line_search_evaluations
             << ",best_scale=" << candidate.best_line_scale
             << ",best_residual=" << candidate.best_trial_residual
              << ",update_norm=" << candidate.best_update_norm
              << ",epsilon_reference="
              << candidate.epsilon_reference_residual
              << ",epsilon_multiplier=" << candidate.epsilon_multiplier
              << ",last_epsilon=" << candidate.last_epsilon
              << ",epsilon_halvings=" << candidate.last_epsilon_halvings
             << ",initial_components="
             << candidate.initial_component_l2[0] << ':'
             << candidate.initial_component_l2[1] << ':'
             << candidate.initial_component_l2[2] << ':'
             << candidate.initial_component_l2[3]
             << ",best_components=" << candidate.best_component_l2[0] << ':'
             << candidate.best_component_l2[1] << ':'
             << candidate.best_component_l2[2] << ':'
             << candidate.best_component_l2[3] << ']';
      }
    }
  }
  return text.str();
}

bool same_path(const std::filesystem::path& left, const std::filesystem::path& right) {
  std::error_code error;
  const auto a = std::filesystem::weakly_canonical(left, error);
  if (error) return false;
  const auto b = std::filesystem::weakly_canonical(right, error);
  return !error && a == b;
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int ranks = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &ranks);
  const double wall_start = MPI_Wtime();
  const std::string invocation_start = cfd::utc_timestamp();
  std::unique_ptr<cfd::RunOutput> output;

  try {
    cfd::CliOptions cli;
    collective_stage([&] { cli = cfd::parse_cli(argc, argv); }, MPI_COMM_WORLD);
    if (cli.resume_output) {
      const auto required = cli.output_directory / "restart_checkpoint.bin";
      if (!same_path(*cli.restart_file, required)) {
        throw std::invalid_argument(
            "--resume-output requires --restart <output>/restart_checkpoint.bin");
      }
    }

    const std::filesystem::path absolute_case =
        std::filesystem::absolute(cli.case_file).lexically_normal();
    std::string case_text;
    std::string root_error;
    if (rank == 0) {
      try { case_text = read_file(absolute_case); }
      catch (const std::exception& exception) { root_error = exception.what(); }
    }
    collective_failure(root_error, MPI_COMM_WORLD);
    bcast_string(case_text, 0, MPI_COMM_WORLD);

    cfd::CaseConfig config;
    cfd::CaseConfig original_config;
    collective_stage([&] {
      config = cfd::parse_case_json(case_text, absolute_case.parent_path(),
                                    absolute_case.string());
      original_config = config;
      if (cli.max_steps_override.has_value()) {
        if (config.run_control.type != cfd::RunType::steady) {
          throw std::invalid_argument("--max-steps is valid only for steady cases");
        }
        config.run_control.max_steps = *cli.max_steps_override;
      }
      if (cli.final_time_override.has_value()) {
        if (config.run_control.type != cfd::RunType::transient) {
          throw std::invalid_argument("--final-time is valid only for transient cases");
        }
        config.run_control.final_time = *cli.final_time_override;
      }
      if (config.run_control.type == cfd::RunType::transient) {
        const double steps = *config.run_control.final_time / *config.run_control.time_step;
        if (std::abs(steps - std::round(steps)) >
            1.0e-10 * std::max(1.0, std::abs(steps))) {
          throw std::invalid_argument("final_time must be an integral multiple of time_step");
        }
      }
    }, MPI_COMM_WORLD);

    std::unique_ptr<cfd::Mesh> global_mesh;
    root_error.clear();
    if (rank == 0) {
      try {
        global_mesh = std::make_unique<cfd::Mesh>(cfd::read_cgns_mesh(config.mesh.file));
        cfd::validate_mesh(*global_mesh);
        (void)cfd::partition_cells(*global_mesh, ranks);
      } catch (const std::exception& exception) { root_error = exception.what(); }
    }
    collective_failure(root_error, MPI_COMM_WORLD);

    cfd::DistributedMesh mesh;
    collective_stage([&] {
      mesh = cfd::partition_and_distribute(std::move(global_mesh), MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);
    std::string mesh_fingerprint;
    collective_stage([&] { mesh_fingerprint = cfd::mesh_fingerprint(mesh, MPI_COMM_WORLD); },
                     MPI_COMM_WORLD);
    const std::string config_fingerprint = cfd::case_fingerprint(config);
    const std::string compatibility_fingerprint =
        mesh_fingerprint + "|" + config_fingerprint;
    const std::string executable_hash =
        cfd::sha256_file(cfd::running_executable_path(argv[0]));
    std::optional<ResumeBudgetEvidence> resume_budget;
    std::string restart_compatibility_fingerprint = compatibility_fingerprint;
    if (cli.resume_output && cli.max_steps_override.has_value()) {
      std::string prior_case_fingerprint;
      int prior_max_steps = 0;
      root_error.clear();
      if (rank == 0) {
        try {
          const ResumeBudgetEvidence evidence = read_resume_budget_evidence(
              cli.output_directory, config, ranks, executable_hash);
          prior_max_steps = evidence.max_steps;
          prior_case_fingerprint = evidence.case_fingerprint;
        } catch (const std::exception& exception) {
          root_error = exception.what();
        }
      }
      collective_failure(root_error, MPI_COMM_WORLD);
      MPI_Bcast(&prior_max_steps, 1, MPI_INT, 0, MPI_COMM_WORLD);
      bcast_string(prior_case_fingerprint, 0, MPI_COMM_WORLD);
      resume_budget = ResumeBudgetEvidence{prior_max_steps,
                                           prior_case_fingerprint};
      restart_compatibility_fingerprint =
          mesh_fingerprint + "|" + prior_case_fingerprint;
    }

    std::optional<cfd::FlowSolver> solver;
    collective_stage([&] { solver.emplace(mesh, config, MPI_COMM_WORLD); }, MPI_COMM_WORLD);
    cfd::RestartableSolution solution;
    cfd::ContinuationState continuation;
    if (cli.restart_file.has_value()) {
      cfd::RestartData restart;
      collective_stage([&] {
        restart = cfd::read_restart(*cli.restart_file,
                                    restart_compatibility_fingerprint,
                                    executable_hash, config, mesh, MPI_COMM_WORLD);
      }, MPI_COMM_WORLD);
      solution = std::move(restart.solution);
      continuation = std::move(restart.continuation);
      if (cli.resume_output) {
        if (!continuation.history_complete) {
          throw std::runtime_error("checkpoint cannot prove complete output history");
        }
      } else {
        continuation.history_complete = false;
        continuation.original_start_time_utc = invocation_start;
      }
      collective_stage([&] { solver->restore_continuation_state(continuation.solver); },
                       MPI_COMM_WORLD);
      continuation.solver = solver->continuation_state();
      if (config.run_control.type == cfd::RunType::steady) {
        continuation.steady_residual_baseline =
            continuation.solver.steady_initial_residual_scale;
      }
    } else {
      collective_stage([&] { solution = solver->uniform_initial_solution(); }, MPI_COMM_WORLD);
      continuation.original_start_time_utc = invocation_start;
      continuation.solver = solver->continuation_state();
    }

    collective_stage([&] {
      output = std::make_unique<cfd::RunOutput>(cli.output_directory, rank,
                                               cli.resume_output, solution.physical_step,
                                               continuation.last_residual_output_step,
                                               continuation.last_force_output_step);
    }, MPI_COMM_WORLD);
    if (cli.resume_output) {
      collective_stage([&] {
        if (rank == 0 &&
            (output->residual_rows() != continuation.residual_output_rows ||
            output->force_rows() != continuation.force_output_rows ||
            output->last_residual_step() != continuation.last_residual_output_step ||
            output->last_force_step() != continuation.last_force_output_step)) {
          throw std::runtime_error(
              "resume streams do not match checkpoint row-count/last-step evidence");
        }
      }, MPI_COMM_WORLD);
    }
    output->log("cfd_solver 3.1 starting: " + cfd::shell_join(argc, argv));
    output->log("MPI ranks: " + std::to_string(ranks) + " case=" + config.case_id +
                " run_type=" + cfd::to_string(config.run_control.type));
    output->log("mesh: global_cells=" + std::to_string(mesh.global_cell_count) +
                " global_faces=" + std::to_string(mesh.global_face_count) +
                " edge_cut=" + std::to_string(mesh.global_diagnostics.edge_cut));
    output->log("mesh fingerprint=" + mesh_fingerprint +
                " case fingerprint=" + config_fingerprint);
    if (cli.restart_file.has_value()) {
      output->log(std::string(cli.resume_output ? "in-place resume" : "diagnostic restart") +
                  ": step=" + std::to_string(solution.physical_step) +
                  " time=" + std::to_string(solution.time));
      if (resume_budget.has_value()) {
        output->log(
            "resume attempt budget: previous_max_steps=" +
            std::to_string(resume_budget->max_steps) + " current_max_steps=" +
            std::to_string(config.run_control.max_steps.value_or(0)));
      }
    }
    collective_stage([&] {
      cfd::write_partition_diagnostics(cli.output_directory / "partition_diagnostics.csv",
                                       mesh, MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);

    cfd::RunReport report;
    report.command = cfd::shell_join(argc, argv);
    report.start_time_utc = continuation.original_start_time_utc;
    report.resumed_output = cli.resume_output;
    report.history_complete = continuation.history_complete;
    report.git_revision = cfd::build_git_revision();
    report.source_dirty = cfd::build_source_dirty();
    report.mesh_fingerprint = mesh_fingerprint;
    report.case_fingerprint = config_fingerprint;
    report.executable_sha256 = executable_hash;
    if (cli.max_steps_override.has_value()) {
      report.diagnostic_overrides.emplace_back("max_steps",
                                               std::to_string(*cli.max_steps_override));
      if (resume_budget.has_value()) {
        report.diagnostic_overrides.emplace_back(
            "resume_source_max_steps",
            std::to_string(resume_budget->max_steps));
      }
    }
    if (cli.final_time_override.has_value()) {
      std::ostringstream value;
      value << std::setprecision(17) << *cli.final_time_override;
      report.diagnostic_overrides.emplace_back("final_time", value.str());
    }

    std::size_t attempts = continuation.total_attempted_steps;
    double initial_residual = continuation.steady_residual_baseline;
    double final_residual = continuation.last_residual;
    double final_cfl = continuation.solver.cfl > 0.0
                           ? continuation.solver.cfl : config.run_control.cfl_initial;
    bool steady_met = continuation.solver.steady_target_met;
    bool all_accepted_transient_targets = continuation.all_accepted_transient_targets;
    std::size_t rollbacks = continuation.rollbacks;
    int last_inner_iterations = continuation.last_inner_iterations;
    report.inner = continuation.inner;
    std::deque<std::pair<double, double>> force_window(
        continuation.force_window.begin(), continuation.force_window.end());
    constexpr std::size_t force_window_limit = 4096U;
    const std::size_t progress_every = static_cast<std::size_t>(cli.progress_every.value_or(
        cli.report_level == "full" ? 1 :
        (config.run_control.type == cfd::RunType::steady ? 50 : 100)));
    const std::size_t flush_every = static_cast<std::size_t>(cli.flush_every.value_or(100));
    const std::size_t checkpoint_every =
        config.run_control.type == cfd::RunType::steady ? 500U : 100U;

    auto capture_continuation = [&] {
      continuation.last_residual = final_residual;
      continuation.inner = report.inner;
      continuation.force_window.assign(force_window.begin(), force_window.end());
      continuation.all_accepted_transient_targets = all_accepted_transient_targets;
      continuation.rollbacks = rollbacks;
      continuation.total_attempted_steps = attempts;
      continuation.last_inner_iterations = last_inner_iterations;
      continuation.residual_output_rows = output->residual_rows();
      continuation.force_output_rows = output->force_rows();
      continuation.last_residual_output_step = output->last_residual_step();
      continuation.last_force_output_step = output->last_force_step();
      continuation.solver = solver->continuation_state();
      continuation.solver.steady_target_met = steady_met;
      continuation.steady_residual_baseline =
          config.run_control.type == cfd::RunType::steady
              ? continuation.solver.steady_initial_residual_scale
              : initial_residual;
    };
    auto checkpoint = [&] {
      capture_continuation();
      collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
      collective_stage([&] {
        cfd::write_restart(cli.output_directory / "restart_checkpoint.bin",
                           compatibility_fingerprint, executable_hash, config, mesh, solution,
                           continuation, MPI_COMM_WORLD);
      }, MPI_COMM_WORLD);
    };

    if (config.run_control.type == cfd::RunType::steady) {
      const std::size_t max_steps = static_cast<std::size_t>(*config.run_control.max_steps);
      while (attempts < max_steps && !steady_met) {
        cfd::StepResult row;
        collective_stage([&] { row = solver->steady_step(solution); }, MPI_COMM_WORLD);
        ++attempts;
        final_cfl = row.cfl;
        if (!row.accepted) {
          // For steady runs the externally visible step is the attempt index;
          // the solver's separate nonlinear_steps counter tracks accepted
          // updates. Advancing this index on rejection keeps restart/output
          // evidence honest and reserves a unique final row at the attempt cap.
          solution.physical_step = attempts;
          ++rollbacks;
          output->log(progress_line(attempts, solution.time, row, false));
          // Rejection streaks are precisely when a recoverable checkpoint is
          // most valuable.  Do not let the early continue bypass periodic
          // stream flushes and restart capture.
          if (attempts % flush_every == 0U) {
            collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
          }
          if (attempts % checkpoint_every == 0U && attempts != max_steps) {
            checkpoint();
          }
          continue;
        }
        solution.physical_step = attempts;
        last_inner_iterations = row.inner.total_linear_sweeps;
        if (row.steady_initial_residual_baseline >= 0.0) {
          initial_residual = row.steady_initial_residual_baseline;
        }
        final_residual = row.residual.total_l2;
        steady_met = row.target_met;
        report.inner.observe(row.inner.total_linear_sweeps, row.inner.converged,
                             row.inner.defect_ratio);
        const bool final_iteration = steady_met || attempts == max_steps;
        if (!final_iteration && attempts %
                static_cast<std::size_t>(config.outputs.write_residuals_every) == 0U) {
          output->write_residual(attempts, solution.time, row.inner.total_linear_sweeps,
                                 row.cfl, 0.0, row.residual);
        }
        if (!final_iteration && attempts %
                static_cast<std::size_t>(config.outputs.write_forces_every) == 0U) {
          output->write_force(attempts, solution.time, row.forces);
        }
        if (attempts == 1U || attempts % progress_every == 0U || final_iteration ||
            row.trust_region_retry_attempted) {
          output->log(progress_line(attempts, solution.time, row, false));
        }
        if (attempts % flush_every == 0U) collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
        if (attempts % checkpoint_every == 0U && !final_iteration) checkpoint();
      }
    } else {
      const double dt = *config.run_control.time_step;
      const double target_time = *config.run_control.final_time;
      while (solution.time + 0.5 * dt < target_time) {
        cfd::StepResult row;
        const double step_wall_start = MPI_Wtime();
        collective_stage([&] { row = solver->transient_step(solution); }, MPI_COMM_WORLD);
        const double local_step_wall = MPI_Wtime() - step_wall_start;
        MPI_Allreduce(&local_step_wall, &row.wall_time_seconds, 1, MPI_DOUBLE,
                      MPI_MAX, MPI_COMM_WORLD);
        ++attempts;
        final_cfl = row.cfl;
        if (!row.accepted) {
          ++rollbacks;
          last_inner_iterations = row.inner.nonlinear_iterations;
          output->log(progress_line(solution.physical_step + 1U, solution.time, row, true));
          checkpoint();  // Histories were rolled back by FlowSolver; preserve recoverable state.
          break;
        }
        all_accepted_transient_targets = all_accepted_transient_targets && row.target_met;
        last_inner_iterations = row.inner.nonlinear_iterations;
        report.inner.observe(row.inner.nonlinear_iterations, row.target_met,
                             row.inner.nonlinear_ratio);
        if (initial_residual < 0.0) initial_residual = row.residual.total_l2;
        final_residual = row.residual.total_l2;
        force_window.emplace_back(row.forces.cl, row.forces.cd);
        if (force_window.size() > force_window_limit) force_window.pop_front();
        const bool final_iteration =
            solution.time + 0.5 * dt >= target_time;
        if (!final_iteration && solution.physical_step %
                static_cast<std::size_t>(config.outputs.write_residuals_every) == 0U) {
          output->write_residual(solution.physical_step, solution.time,
                                 row.inner.nonlinear_iterations, row.cfl, dt, row.residual);
        }
        if (!final_iteration && solution.physical_step %
                static_cast<std::size_t>(config.outputs.write_forces_every) == 0U) {
          output->write_force(solution.physical_step, solution.time, row.forces);
        }
        if (solution.physical_step == 1U || solution.physical_step % progress_every == 0U ||
            final_iteration) {
          output->log(progress_line(solution.physical_step, solution.time, row, true));
        }
        if (solution.physical_step % flush_every == 0U) {
          collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
        }
        if (solution.physical_step % checkpoint_every == 0U && !final_iteration) checkpoint();
      }
      if (std::abs(solution.time - target_time) <=
          1.0e-11 * std::max(1.0, std::abs(target_time))) solution.time = target_time;
      report.periodicity = cfd::test_force_periodicity(
          std::vector<std::pair<double, double>>(force_window.begin(), force_window.end()));
    }

    const cfd::FlowSolverContinuation final_solver_continuation =
        solver->continuation_state();
    if (config.run_control.type == cfd::RunType::steady) {
      initial_residual =
          final_solver_continuation.steady_initial_residual_scale;
    }
    report.steady_acceptance.jfnk_accepted_steps =
        final_solver_continuation.steady_jfnk_accepted_steps;
    report.steady_acceptance.jfnk_attempts =
        final_solver_continuation.steady_jfnk_attempts;
    report.steady_acceptance.fallback_accepted_steps =
        final_solver_continuation.steady_fallback_accepted_steps;
    report.steady_acceptance.fallback_attempts =
        final_solver_continuation.steady_fallback_attempts;
    report.steady_acceptance.fallback_rejected_steps =
        final_solver_continuation.steady_fallback_rejected_steps;
    report.steady_acceptance.fallback_cfl_halvings =
        final_solver_continuation.steady_fallback_cfl_halvings;
    report.steady_acceptance.last_fallback_cfl =
        final_solver_continuation.steady_last_fallback_cfl;
    report.steady_acceptance.fallback_mode =
        final_solver_continuation.steady_fallback_mode;
    report.steady_acceptance.operating_fallback_cfl =
        final_solver_continuation.steady_fallback_cfl;
    report.steady_acceptance.fallback_window_samples =
        final_solver_continuation.steady_fallback_residual_window.size();
    report.steady_acceptance.fallback_steps_since_jfnk =
        final_solver_continuation.steady_fallback_steps_since_jfnk;
    report.steady_acceptance.rescue_attempts =
        final_solver_continuation.steady_rescue_attempts;
    report.steady_acceptance.rescue_accepted_steps =
        final_solver_continuation.steady_rescue_accepted_steps;
    report.steady_acceptance.rescue_total_gmres_iterations =
        final_solver_continuation.steady_rescue_total_gmres_iterations;
    report.steady_acceptance.rescue_last_gmres_iterations =
        final_solver_continuation.steady_rescue_last_gmres_iterations;
    report.steady_acceptance.rescue_max_gmres_iterations =
        final_solver_continuation.steady_rescue_max_gmres_iterations;
    report.steady_acceptance.rescue_last_gmres_ratio =
        final_solver_continuation.steady_rescue_last_gmres_ratio;
    report.steady_acceptance.rescue_last_cfl =
        final_solver_continuation.steady_rescue_last_cfl;
    report.steady_acceptance.rescue_last_line_scale =
        final_solver_continuation.steady_rescue_last_line_scale;
    report.steady_acceptance.rescue_cooldown_attempts =
        final_solver_continuation.steady_rescue_cooldown_attempts;
    report.steady_acceptance.trust_region_retry_batches =
        final_solver_continuation.steady_trust_region_retry_batches;
    report.steady_acceptance.trust_region_retry_candidates =
        final_solver_continuation.steady_trust_region_retry_candidates;
    report.steady_acceptance.trust_region_retry_accepted_steps =
        final_solver_continuation.steady_trust_region_retry_accepted_steps;
    report.steady_acceptance.trust_region_retry_total_gmres_iterations =
        final_solver_continuation
            .steady_trust_region_retry_total_gmres_iterations;
    report.steady_acceptance.trust_region_retry_last_candidate_count =
        final_solver_continuation
            .steady_trust_region_retry_last_candidate_count;
    report.steady_acceptance.trust_region_retry_last_total_gmres_iterations =
        final_solver_continuation
            .steady_trust_region_retry_last_total_gmres_iterations;
    report.steady_acceptance
        .trust_region_retry_last_accepted_gmres_iterations =
        final_solver_continuation
            .steady_trust_region_retry_last_accepted_gmres_iterations;
    report.steady_acceptance.trust_region_retry_last_accepted_cfl =
        final_solver_continuation
            .steady_trust_region_retry_last_accepted_cfl;
    report.steady_acceptance.trust_region_retry_last_line_scale =
        final_solver_continuation.steady_trust_region_retry_last_line_scale;
    report.steady_acceptance.trust_region_retry_last_initial_residual =
        final_solver_continuation
            .steady_trust_region_retry_last_initial_residual;
    report.steady_acceptance.trust_region_retry_last_final_residual =
        final_solver_continuation
            .steady_trust_region_retry_last_final_residual;
    report.steady_acceptance.trust_region_retry_cooldown_attempts =
        final_solver_continuation
            .steady_trust_region_retry_cooldown_attempts;
    report.steady_acceptance.fallback_disabled =
        final_solver_continuation.steady_fallback_disabled;
    report.steady_acceptance.fallback_growth_disables =
        final_solver_continuation.steady_fallback_growth_disables;
    report.steady_acceptance.lusgs_preconditioner_applications =
        final_solver_continuation.steady_lusgs_preconditioner_applications;
    report.steady_acceptance.lusgs_preconditioner_sweeps =
        final_solver_continuation.steady_lusgs_preconditioner_sweeps;
    report.steady_acceptance.lusgs_last_defect_ratio =
        final_solver_continuation.steady_lusgs_last_defect_ratio;
    report.steady_acceptance.jfnk_epsilon_reference_residual =
        final_solver_continuation.steady_jfnk_epsilon_reference_residual;
    report.steady_acceptance.jfnk_epsilon_multiplier =
        final_solver_continuation.steady_jfnk_epsilon_multiplier;
    report.steady_acceptance.jfnk_last_epsilon =
        final_solver_continuation.steady_jfnk_last_epsilon;
    report.steady_acceptance.jfnk_last_epsilon_halvings =
        final_solver_continuation.steady_jfnk_last_epsilon_halvings;
    report.steady_acceptance.nonmonotone_window_samples =
        final_solver_continuation.steady_nonmonotone_residual_window.size();
    report.steady_acceptance.strict_decrease_stagnation_streak =
        final_solver_continuation
            .steady_strict_decrease_stagnation_streak;
    report.steady_acceptance.nonmonotone_bridge_active =
        final_solver_continuation.steady_nonmonotone_bridge_active;
    report.steady_acceptance.nonmonotone_bridge_disabled =
        final_solver_continuation.steady_nonmonotone_bridge_disabled;
    report.steady_acceptance.nonmonotone_steps_since_strict_best =
        final_solver_continuation
            .steady_nonmonotone_steps_since_strict_best;
    report.steady_acceptance.nonmonotone_accepted_steps =
        final_solver_continuation.steady_nonmonotone_accepted_steps;
    report.steady_acceptance.nonmonotone_max_relative_increase =
        final_solver_continuation
            .steady_nonmonotone_max_relative_increase;
    report.steady_acceptance.nonmonotone_strict_best_improvements =
        final_solver_continuation
            .steady_nonmonotone_strict_best_improvements;
    report.steady_acceptance.nonmonotone_watchdog_resets =
        final_solver_continuation.steady_nonmonotone_watchdog_resets;
    report.steady_acceptance.nonmonotone_bypass_attempts =
        final_solver_continuation.steady_nonmonotone_bypass_attempts;
    report.steady_acceptance.nonmonotone_bypass_accepted_steps =
        final_solver_continuation.steady_nonmonotone_bypass_accepted_steps;
    report.steady_acceptance.nonmonotone_bypass_trial_evaluations =
        final_solver_continuation.steady_nonmonotone_bypass_trial_evaluations;
    report.steady_acceptance.nonmonotone_bypass_last_actual_trial_residual =
        final_solver_continuation
            .steady_nonmonotone_bypass_last_actual_trial_residual;
    report.steady_acceptance.nonmonotone_bypass_last_gmres_ratio =
        final_solver_continuation.steady_nonmonotone_bypass_last_gmres_ratio;
    report.steady_acceptance.nonmonotone_envelope_reference =
        final_solver_continuation.steady_nonmonotone_envelope_reference;
    report.steady_acceptance.nonmonotone_envelope_accepted_steps =
        final_solver_continuation.steady_nonmonotone_envelope_accepted_steps;
    report.steady_acceptance.nonmonotone_envelope_max_relative_increase =
        final_solver_continuation
            .steady_nonmonotone_envelope_max_relative_increase;
    report.steady_acceptance.implicit_bridge =
        final_solver_continuation.steady_implicit_bridge;
    const cfd::SteadySpatialOrderSchedule spatial_schedule =
        cfd::steady_spatial_order_schedule(
            config.run_control.pseudo_cfl_ramp_steps);
    report.steady_spatial_order.first_order_target_steps =
        spatial_schedule.first_order_steps;
    report.steady_spatial_order.ramp_target_steps =
        spatial_schedule.ramp_steps;
    report.steady_spatial_order.first_order_accepted_steps =
        final_solver_continuation.steady_first_order_accepted_steps;
    report.steady_spatial_order.ramp_accepted_steps =
        final_solver_continuation.steady_order_ramp_accepted_steps;
    report.steady_spatial_order.full_order_accepted_steps =
        final_solver_continuation.steady_full_order_accepted_steps;
    report.steady_spatial_order.minimum_full_order_steps =
        cfd::steady_full_order_minimum_steps(
            config.run_control.pseudo_cfl_ramp_steps);
    report.steady_spatial_order.final_blend =
        final_solver_continuation.steady_reconstruction_blend;
    report.steady_spatial_order.original_initial_residual =
        final_solver_continuation.steady_initial_residual_scale;
    report.steady_spatial_order.full_order_initial_residual =
        final_solver_continuation.steady_full_order_initial_residual;
    report.steady_spatial_order.full_order_best_residual =
        final_solver_continuation.steady_full_order_best_residual;
    report.steady_spatial_order.promoted_by_newton_rescue =
        final_solver_continuation.steady_order_rescue_promoted;

    cfd::FinalEvaluation final_evaluation;
    collective_stage([&] {
      final_evaluation = cfd::evaluate_final_state(mesh, config, solution, final_cfl,
                                                   MPI_COMM_WORLD);
      report.physics_gates = cfd::evaluate_physics_gates(
          mesh, config, solution, final_evaluation.residual, final_evaluation.forces,
          MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);
    final_residual = final_evaluation.residual.norms.total_l2;
    report.final_reconstruction_diagnostics = final_evaluation.residual.diagnostics;
    if (config.run_control.type == cfd::RunType::transient &&
        initial_residual < 0.0) {
      initial_residual = final_residual;
    }
    const std::size_t final_step = solution.physical_step;
    output->write_residual(final_step, solution.time, last_inner_iterations, final_cfl,
                           config.run_control.time_step.value_or(0.0),
                           final_evaluation.residual.norms);
    output->write_force(final_step, solution.time, final_evaluation.forces);
    if (initial_residual > 0.0 && final_residual > 0.0) {
      report.residual_reduction_orders = std::log10(initial_residual / final_residual);
    }
    if (report.steady_spatial_order.full_order_initial_residual > 0.0 &&
        final_residual > 0.0) {
      report.full_order_residual_reduction_orders = std::log10(
          report.steady_spatial_order.full_order_initial_residual /
          final_residual);
    }
    report.final_step = final_step;
    report.final_physical_time = solution.time;

    const bool diagnostic = !report.diagnostic_overrides.empty() ||
                            (cli.restart_file.has_value() && !cli.resume_output) ||
                            !continuation.history_complete;
    if (config.run_control.type == cfd::RunType::steady) {
      const bool sustained_full_order =
          final_solver_continuation.steady_reconstruction_blend == 1.0 &&
          final_solver_continuation.steady_full_order_accepted_steps >=
              report.steady_spatial_order.minimum_full_order_steps;
      const bool corrected_residual_gate = cfd::steady_convergence_gate(
          final_solver_continuation.steady_reconstruction_blend,
          final_solver_continuation.steady_full_order_accepted_steps,
          config.run_control.pseudo_cfl_ramp_steps,
          final_solver_continuation.steady_initial_residual_scale,
          final_residual, *config.run_control.residual_reduction_target);
      report.completed = steady_met && corrected_residual_gate &&
                          sustained_full_order &&
                          report.physics_gates.passed && !diagnostic;
      report.convergence_status = report.completed ? "converged" : "failed";
      report.notes = report.completed
          ? "original-run global residual reduction, accepted full-order hold, and physics/positivity gates passed"
          : "steady gate failed or run is diagnostic/incomplete";
    } else {
      const double target_time = *config.run_control.final_time;
      const bool exact_time = solution.time == target_time;
      const bool fraction = report.inner.samples > 0U &&
                            report.inner.target_converged_fraction >= 0.95;
      bool long_horizon_controls = true;
      const bool re200_like = original_config.run_control.type == cfd::RunType::transient &&
          original_config.run_control.final_time.value_or(0.0) >= 300.0;
      if (re200_like) {
        long_horizon_controls =
            std::abs(original_config.run_control.time_step.value_or(0.0) - 0.01) <= 1.0e-14 &&
            original_config.run_control.final_time.value_or(0.0) >= 300.0 &&
            solution.physical_step >= 30000U &&
            original_config.run_control.min_inner_iterations >= 5 &&
            original_config.run_control.inner_residual_reduction_target <= 1.0e-3 &&
            fraction && report.periodicity.mean_drag > 0.0 &&
            report.periodicity.lift_rms >= 1.0e-4;
      }
      report.completed = exact_time && all_accepted_transient_targets && fraction &&
                         report.periodicity.passed && report.physics_gates.passed &&
                         long_horizon_controls && !diagnostic;
      report.convergence_status = report.completed ? "statistically_periodic" : "failed";
      report.notes = report.completed
          ? "exact final time, cumulative inner convergence, periodicity, production controls, and physics gates passed"
          : "transient completion, production-control, physics, history, or diagnostic gate failed";
    }

    output->log("final residual=" + std::to_string(final_residual) +
                " original_reduction_orders=" +
                std::to_string(report.residual_reduction_orders) +
                " full_order_diagnostic_reduction_orders=" +
                std::to_string(report.full_order_residual_reduction_orders) +
                " Cl=" + std::to_string(final_evaluation.forces.cl) +
                " Cd=" + std::to_string(final_evaluation.forces.cd));
    output->log("physics gates: passed=" + std::string(report.physics_gates.passed ? "yes" : "no") +
                " min_rho=" + std::to_string(report.physics_gates.minimum_owned_rho) +
                " min_p=" + std::to_string(report.physics_gates.minimum_owned_pressure) +
                " cp_range=" + std::to_string(report.physics_gates.wall_cp_range) +
                " max_no_slip_speed=" +
                std::to_string(report.physics_gates.maximum_no_slip_speed));
    output->log("inner stats: min=" + std::to_string(report.inner.minimum) +
                " mean=" + std::to_string(report.inner.mean) +
                " max=" + std::to_string(report.inner.maximum) +
                 " fraction=" + std::to_string(report.inner.target_converged_fraction) +
                 " rollbacks=" + std::to_string(rollbacks));
    if (config.run_control.type == cfd::RunType::steady) {
      output->log(
          "steady acceptance stats: jfnk_accepted=" +
          std::to_string(report.steady_acceptance.jfnk_accepted_steps) +
          " jfnk_attempts=" +
          std::to_string(report.steady_acceptance.jfnk_attempts) +
          " rescue_accepted=" +
          std::to_string(report.steady_acceptance.rescue_accepted_steps) +
          " rescue_attempts=" +
          std::to_string(report.steady_acceptance.rescue_attempts) +
          " rescue_total_gmres_iterations=" +
          std::to_string(
              report.steady_acceptance.rescue_total_gmres_iterations) +
          " rescue_last_gmres_iterations=" +
          std::to_string(report.steady_acceptance.rescue_last_gmres_iterations) +
          " rescue_last_gmres_ratio=" +
          std::to_string(report.steady_acceptance.rescue_last_gmres_ratio) +
           " rescue_last_cfl=" +
           std::to_string(report.steady_acceptance.rescue_last_cfl) +
           " trust_retry_accepted=" +
           std::to_string(
               report.steady_acceptance.trust_region_retry_accepted_steps) +
           " trust_retry_batches=" +
           std::to_string(
               report.steady_acceptance.trust_region_retry_batches) +
           " trust_retry_candidates=" +
           std::to_string(
               report.steady_acceptance.trust_region_retry_candidates) +
           " trust_retry_total_gmres_iterations=" +
           std::to_string(report.steady_acceptance
                              .trust_region_retry_total_gmres_iterations) +
           " trust_retry_last_accepted_cfl=" +
           std::to_string(report.steady_acceptance
                              .trust_region_retry_last_accepted_cfl) +
           " fallback_accepted=" +
          std::to_string(report.steady_acceptance.fallback_accepted_steps) +
          " fallback_attempts=" +
          std::to_string(report.steady_acceptance.fallback_attempts) +
          " fallback_rejected=" +
          std::to_string(report.steady_acceptance.fallback_rejected_steps) +
          " fallback_cfl_halvings=" +
          std::to_string(report.steady_acceptance.fallback_cfl_halvings) +
          " final_fallback_cfl=" +
          std::to_string(report.steady_acceptance.last_fallback_cfl) +
          " operating_fallback_cfl=" +
          std::to_string(report.steady_acceptance.operating_fallback_cfl) +
          " fallback_mode=" +
          std::string(report.steady_acceptance.fallback_mode ? "yes" : "no") +
           " fallback_disabled=" +
           std::string(report.steady_acceptance.fallback_disabled ? "yes" : "no") +
           " lusgs_applications=" +
           std::to_string(
               report.steady_acceptance.lusgs_preconditioner_applications) +
           " lusgs_sweeps=" +
           std::to_string(report.steady_acceptance.lusgs_preconditioner_sweeps) +
           " lusgs_last_defect_ratio=" +
            std::to_string(report.steady_acceptance.lusgs_last_defect_ratio) +
            " jfnk_epsilon_reference=" +
            std::to_string(report.steady_acceptance
                               .jfnk_epsilon_reference_residual) +
            " jfnk_epsilon_multiplier=" +
            std::to_string(report.steady_acceptance
                               .jfnk_epsilon_multiplier) +
            " jfnk_last_epsilon=" +
            std::to_string(report.steady_acceptance.jfnk_last_epsilon) +
             " jfnk_last_epsilon_halvings=" +
             std::to_string(report.steady_acceptance
                                .jfnk_last_epsilon_halvings) +
             " nonmonotone_accepted=" +
             std::to_string(report.steady_acceptance
                                .nonmonotone_accepted_steps) +
             " nonmonotone_max_relative_increase=" +
             std::to_string(report.steady_acceptance
                                .nonmonotone_max_relative_increase) +
             " nonmonotone_strict_best_improvements=" +
             std::to_string(report.steady_acceptance
                                .nonmonotone_strict_best_improvements) +
              " nonmonotone_watchdog_resets=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_watchdog_resets) +
              " nonmonotone_bypass_attempts=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_bypass_attempts) +
              " nonmonotone_bypass_accepted=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_bypass_accepted_steps) +
              " nonmonotone_bypass_trial_evaluations=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_bypass_trial_evaluations) +
              " nonmonotone_bypass_last_actual_trial_residual=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_bypass_last_actual_trial_residual) +
              " nonmonotone_bypass_last_gmres_ratio=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_bypass_last_gmres_ratio) +
              " nonmonotone_envelope_reference=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_envelope_reference) +
              " nonmonotone_envelope_accepted=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_envelope_accepted_steps) +
              " nonmonotone_envelope_max_relative_increase=" +
              std::to_string(report.steady_acceptance
                                 .nonmonotone_envelope_max_relative_increase) +
              " implicit_bridge_accepted=" +
              std::to_string(report.steady_acceptance
                                 .implicit_bridge.accepted_steps) +
              " implicit_bridge_cfl=" +
              std::to_string(report.steady_acceptance.implicit_bridge.cfl) +
              " implicit_bridge_residual_min=" +
              std::to_string(report.steady_acceptance
                                 .implicit_bridge.residual_minimum) +
              " implicit_bridge_residual_max=" +
              std::to_string(report.steady_acceptance
                                 .implicit_bridge.residual_maximum) +
              " implicit_bridge_best_improvements=" +
              std::to_string(report.steady_acceptance
                                 .implicit_bridge.meaningful_best_improvements) +
              " implicit_bridge_watchdog_stops=" +
              std::to_string(report.steady_acceptance
                                 .implicit_bridge.watchdog_stops) +
             " nonmonotone_bridge_active=" +
             std::string(report.steady_acceptance.nonmonotone_bridge_active
                             ? "yes"
                             : "no") +
             " nonmonotone_bridge_disabled=" +
             std::string(report.steady_acceptance.nonmonotone_bridge_disabled
                             ? "yes"
                             : "no"));
      output->log(
          "spatial order continuation: first_order=" +
          std::to_string(report.steady_spatial_order.first_order_accepted_steps) +
          "/" +
          std::to_string(report.steady_spatial_order.first_order_target_steps) +
          " ramp=" +
          std::to_string(report.steady_spatial_order.ramp_accepted_steps) +
          "/" + std::to_string(report.steady_spatial_order.ramp_target_steps) +
          " full_order=" +
          std::to_string(report.steady_spatial_order.full_order_accepted_steps) +
          " final_blend=" +
          std::to_string(report.steady_spatial_order.final_blend) +
          " full_order_baseline=" +
          std::to_string(report.steady_spatial_order.full_order_initial_residual) +
          " full_order_best=" +
          std::to_string(report.steady_spatial_order.full_order_best_residual) +
          " final_outputs_full_order=yes");
    }

    capture_continuation();
    checkpoint();
    collective_stage([&] {
      cfd::write_surface(cli.output_directory / "surface.csv", config, mesh,
                         final_evaluation.residual, MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);
    collective_stage([&] {
      cfd::write_vtu(cli.output_directory / "field_final.vtu", config, mesh, solution,
                     MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);
    collective_stage([&] {
      cfd::write_restart(cli.output_directory / "restart_final.bin",
                         compatibility_fingerprint, executable_hash, config, mesh, solution,
                         continuation, MPI_COMM_WORLD);
    }, MPI_COMM_WORLD);
    collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    report.wall_time_seconds = MPI_Wtime() - wall_start;
    report.end_time_utc = cfd::utc_timestamp();
    collective_stage([&] {
      cfd::write_metadata_and_status(cli.output_directory, config, mesh, report);
    }, MPI_COMM_WORLD);
    output->log("final status: " + report.convergence_status + "; " + report.notes);
    output->log("wall_time_seconds=" + std::to_string(report.wall_time_seconds) +
                " start_utc=" + report.start_time_utc + " end_utc=" + report.end_time_utc);
    collective_stage([&] { output->flush(); }, MPI_COMM_WORLD);
    const int exit_code = report.completed ? 0 : 2;
    MPI_Finalize();
    return exit_code;
  } catch (const std::exception& exception) {
    if (rank == 0) {
      const std::string message = std::string("cfd_solver error: ") + exception.what();
      if (output) output->log(message);
      else std::cerr << message << '\n';
    }
    MPI_Finalize();
    return 1;
  }
}
