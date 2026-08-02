#include "cfd/case_config.hpp"
#include "cfd/mesh.hpp"
#include "cfd/output.hpp"
#include "cfd/partition.hpp"
#include "cfd/physics.hpp"
#include "cfd/solver.hpp"

#include <mpi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct CommandLine {
    std::filesystem::path case_file;
    std::filesystem::path output_directory;
    std::optional<std::filesystem::path> restart_file;
    std::optional<std::filesystem::path> resume_file;
    bool restart_perturbation{};
    std::string report_level{"full"};
    std::optional<int> diagnostic_steps;
    std::optional<int> diagnostic_order;
    std::optional<cfd::Real> diagnostic_cfl;
    std::optional<int> diagnostic_max_inner_iterations;
    std::optional<cfd::Real> pseudo_cfl;
    std::optional<cfd::Real> rusanov_dissipation_scale;
    bool diagnostic_uniform{};
    std::string command;
};

[[nodiscard]] std::string shell_quote(const std::string& value) {
    if (value.find_first_of(" \t\n'\"") == std::string::npos) return value;
    std::string result{"'"};
    for (const char character : value) {
        if (character == '\'') result += "'\\''";
        else result += character;
    }
    result += '\'';
    return result;
}

[[nodiscard]] CommandLine parse_command_line(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) != "solve") {
        throw std::invalid_argument(
            "usage: cfd_solver solve --case <case.json> --output <directory> "
            "[--restart <restart-file>] [--resume <transient-checkpoint>] [--report-level brief|full]");
    }
    CommandLine result{};
    std::ostringstream command;
    for (int index = 0; index < argc; ++index) {
        if (index != 0) command << ' ';
        command << shell_quote(argv[index]);
    }
    result.command = command.str();
    for (int index = 2; index < argc; ++index) {
        const std::string option = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value after command-line option " + option);
        }
        const std::string value = argv[++index];
        if (option == "--case") result.case_file = value;
        else if (option == "--output") result.output_directory = value;
        else if (option == "--restart") result.restart_file = value;
        else if (option == "--resume") result.resume_file = value;
        else if (option == "--restart-perturbation") {
            if (value != "true" && value != "false") {
                throw std::invalid_argument("--restart-perturbation must be true or false");
            }
            result.restart_perturbation = value == "true";
        }
        else if (option == "--report-level") result.report_level = value;
        else if (option == "--diagnostic-steps") {
            try {
                std::size_t consumed = 0;
                const int steps = std::stoi(value, &consumed);
                if (consumed != value.size() || steps <= 0) throw std::invalid_argument("range");
                result.diagnostic_steps = steps;
            } catch (const std::exception&) {
                throw std::invalid_argument("--diagnostic-steps must be a positive integer");
            }
        }
        else if (option == "--diagnostic-order") {
            if (value != "1" && value != "2") {
                throw std::invalid_argument("--diagnostic-order must be 1 or 2");
            }
            result.diagnostic_order = std::stoi(value);
        }
        else if (option == "--diagnostic-cfl") {
            try {
                std::size_t consumed = 0;
                const cfd::Real cfl = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(cfl) || !(cfl > 0.0)) {
                    throw std::invalid_argument("range");
                }
                result.diagnostic_cfl = cfl;
            } catch (const std::exception&) {
                throw std::invalid_argument("--diagnostic-cfl must be a positive finite number");
            }
        }
        else if (option == "--diagnostic-max-inner") {
            try {
                std::size_t consumed = 0;
                const int iterations = std::stoi(value, &consumed);
                if (consumed != value.size() || iterations <= 0) {
                    throw std::invalid_argument("range");
                }
                result.diagnostic_max_inner_iterations = iterations;
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "--diagnostic-max-inner must be a positive integer");
            }
        }
        else if (option == "--pseudo-cfl") {
            try {
                std::size_t consumed = 0;
                const cfd::Real cfl = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(cfl) || !(cfl > 0.0)) {
                    throw std::invalid_argument("range");
                }
                result.pseudo_cfl = cfl;
            } catch (const std::exception&) {
                throw std::invalid_argument("--pseudo-cfl must be a positive finite number");
            }
        }
        else if (option == "--rusanov-dissipation-scale") {
            try {
                std::size_t consumed = 0;
                const cfd::Real scale = std::stod(value, &consumed);
                if (consumed != value.size() || !std::isfinite(scale) ||
                    !(scale > 0.0)) {
                    throw std::invalid_argument("range");
                }
                result.rusanov_dissipation_scale = scale;
            } catch (const std::exception&) {
                throw std::invalid_argument(
                    "--rusanov-dissipation-scale must be a positive finite number");
            }
        }
        else if (option == "--diagnostic-uniform") {
            if (value != "true" && value != "false") {
                throw std::invalid_argument("--diagnostic-uniform must be true or false");
            }
            result.diagnostic_uniform = value == "true";
        }
        else throw std::invalid_argument("unsupported command-line option " + option);
    }
    if (result.case_file.empty() || result.output_directory.empty()) {
        throw std::invalid_argument("both --case and --output are required");
    }
    if (result.report_level != "brief" && result.report_level != "full") {
        throw std::invalid_argument("--report-level must be 'brief' or 'full'");
    }
    if (result.restart_perturbation && !result.restart_file) {
        throw std::invalid_argument("--restart-perturbation=true requires --restart");
    }
    if (result.restart_file && result.resume_file) {
        throw std::invalid_argument("--restart and --resume are mutually exclusive");
    }
    if (result.restart_perturbation && result.resume_file) {
        throw std::invalid_argument("--restart-perturbation cannot be used with --resume");
    }
    if (result.pseudo_cfl && result.diagnostic_cfl) {
        throw std::invalid_argument("--pseudo-cfl and --diagnostic-cfl are mutually exclusive");
    }
    return result;
}

[[nodiscard]] std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm timestamp{};
    gmtime_r(&seconds, &timestamp);
    std::ostringstream output;
    output << std::put_time(&timestamp, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

template <typename Value>
[[nodiscard]] std::string join_values(const std::vector<Value>& values) {
    std::ostringstream result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result << ';';
        result << values[index];
    }
    return result.str();
}

struct PartitionWire {
    int rank{};
    std::int64_t owned{};
    std::int64_t ghost{};
    std::int64_t boundary{};
    int neighbors{};
    std::array<char, 512> neighbor_ranks{};
    std::array<char, 512> send_cells{};
    std::array<char, 512> recv_cells{};
};

void copy_string(const std::string& source, std::array<char, 512>& destination) {
    if (source.size() >= destination.size()) {
        throw std::overflow_error("partition diagnostic string exceeds fixed MPI packet");
    }
    std::memcpy(destination.data(), source.data(), source.size());
    destination[source.size()] = '\0';
}

[[nodiscard]] std::vector<cfd::PartitionDiagnosticsRecord> gather_partition_diagnostics(
    const cfd::DistributedMesh& mesh, MPI_Comm communicator) {
    PartitionWire local{};
    local.rank = mesh.diagnostics.rank;
    local.owned = mesh.diagnostics.num_cells_owned;
    local.ghost = mesh.diagnostics.num_cells_ghost;
    local.boundary = mesh.diagnostics.num_boundary_faces;
    local.neighbors = static_cast<int>(mesh.diagnostics.neighbor_ranks.size());
    copy_string(join_values(mesh.diagnostics.neighbor_ranks), local.neighbor_ranks);
    copy_string(join_values(mesh.diagnostics.send_cells), local.send_cells);
    copy_string(join_values(mesh.diagnostics.recv_cells), local.recv_cells);
    int rank = 0;
    int size = 1;
    MPI_Comm_rank(communicator, &rank);
    MPI_Comm_size(communicator, &size);
    std::vector<PartitionWire> gathered(rank == 0 ? static_cast<std::size_t>(size) : 0U);
    MPI_Gather(&local, static_cast<int>(sizeof(local)), MPI_BYTE,
               rank == 0 ? gathered.data() : nullptr, static_cast<int>(sizeof(local)),
               MPI_BYTE, 0, communicator);
    std::vector<cfd::PartitionDiagnosticsRecord> records;
    if (rank == 0) {
        records.reserve(gathered.size());
        for (const PartitionWire& wire : gathered) {
            records.push_back({wire.rank, wire.owned, wire.ghost, wire.boundary,
                               wire.neighbors, wire.neighbor_ranks.data(),
                               wire.send_cells.data(), wire.recv_cells.data()});
        }
        std::sort(records.begin(), records.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.rank < rhs.rank; });
    }
    return records;
}

[[nodiscard]] cfd::GasModel gas_model(const cfd::CaseConfig& config) {
    cfd::GasModel gas{};
    gas.gamma = config.gas.gamma;
    gas.gas_constant = config.gas.gas_constant;
    gas.prandtl = config.gas.prandtl;
    return gas;
}

[[nodiscard]] std::vector<cfd::GatheredPolygonRecord> make_polygons(
    const std::vector<cfd::SolverFieldCell>& cells, const cfd::CaseConfig& config) {
    const cfd::GasModel gas = gas_model(config);
    std::vector<cfd::GatheredPolygonRecord> records;
    records.reserve(cells.size());
    for (const cfd::SolverFieldCell& cell : cells) {
        const cfd::ThermodynamicState primitive = cfd::decode_state(cell.state, gas);
        cfd::GatheredPolygonRecord record{};
        record.global_cell_id = cell.global_id;
        record.points.assign(cell.vertices.begin(),
                             cell.vertices.begin() + cell.vertex_count);
        record.density = primitive.density;
        record.velocity = {primitive.velocity_x, primitive.velocity_y};
        record.pressure = primitive.pressure;
        record.mach = std::hypot(primitive.velocity_x, primitive.velocity_y) /
                      primitive.sound_speed;
        record.total_energy = cell.state[3];
        record.temperature = primitive.temperature;
        record.owner_rank = cell.owner_rank;
        records.push_back(std::move(record));
    }
    return records;
}

[[nodiscard]] std::vector<cfd::RestartStateRecord> make_restart_records(
    const std::vector<cfd::SolverFieldCell>& cells) {
    std::vector<cfd::RestartStateRecord> records;
    records.reserve(cells.size());
    for (const auto& cell : cells) records.push_back({cell.global_id, cell.state});
    return records;
}

[[nodiscard]] std::vector<cfd::SurfaceRecord> make_surface_records(
    const std::vector<cfd::SolverSurfaceSample>& samples) {
    std::vector<cfd::SurfaceRecord> records;
    records.reserve(samples.size());
    for (const auto& sample : samples) {
        records.push_back({sample.center[0], sample.center[1], sample.outward_normal[0],
                           sample.outward_normal[1], sample.pressure,
                           sample.pressure_coefficient,
                           sample.skin_friction_coefficient, sample.density,
                           sample.velocity_x, sample.velocity_y, sample.mach,
                           sample.tag.data()});
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.tag != rhs.tag) return lhs.tag < rhs.tag;
        const double lhs_angle = std::atan2(lhs.y, lhs.x);
        const double rhs_angle = std::atan2(rhs.y, rhs.x);
        if (lhs_angle != rhs_angle) return lhs_angle < rhs_angle;
        return lhs.x < rhs.x;
    });
    return records;
}

[[nodiscard]] cfd::OutputMetadata make_metadata(const cfd::CaseConfig& config,
                                                const cfd::DistributedMesh& mesh) {
    cfd::OutputMetadata metadata{};
    metadata.solver_name = "AsteriaFV";
    metadata.solver_version = "0.1.0";
    metadata.git_revision = std::nullopt;
    metadata.mpi_ranks = mesh.size;
    metadata.num_cells_global = mesh.global_cell_count;
    metadata.num_faces_global = mesh.global_face_count;
    metadata.num_cells_owned_local = static_cast<std::int64_t>(mesh.owned_count);
    metadata.num_cells_ghost_local =
        static_cast<std::int64_t>(mesh.cells.size() - mesh.owned_count);
    metadata.partitioner = "metis_kway";
    metadata.partition_edge_cut = mesh.partition_edge_cut;
    metadata.halo_exchange = "neighbor_isend_irecv";
    metadata.equation_set = "compressible_navier_stokes_2d";
    if (config.physics.mode == cfd::PhysicsMode::inviscid) {
        std::ostringstream flux;
        flux << "Rusanov_local_Lax_Friedrichs_with_upwind_total_enthalpy_energy_flux_scale_"
             << std::setprecision(8)
             << config.run_control.rusanov_dissipation_scale.value_or(1.0)
             << "_with_exact_stationary_wall_flux";
        metadata.inviscid_flux = flux.str();
    } else {
        metadata.inviscid_flux =
            "HLLC_with_admissibility_checked_Rusanov_fallback_and_exact_stationary_wall_flux";
    }
    metadata.entropy_fix = std::nullopt;
    metadata.viscous_flux = config.physics.mode == cfd::PhysicsMode::laminar
                                ? "corrected_central_Newtonian_Fourier"
                                : "disabled_zero_transport";
    metadata.time_integrator = config.run_control.type == cfd::RunType::transient
                                   ? "true_dual_time_BDF2"
                                   : "local_pseudo_time_defect_correction";
    metadata.implicit_solver =
        "analytic_4x4_block_Jacobi_with_exact_stationary_wall_pressure_block";
    if (config.numerics_required.spatial_order >= 2) {
        metadata.reconstruction =
            "weighted_least_squares_piecewise_linear_with_pressure_jump_shock_flattening_0.01_to_0.03";
        if (config.run_control.type == cfd::RunType::steady &&
            config.physics.mode == cfd::PhysicsMode::inviscid) {
            metadata.reconstruction +=
                "_with_freestream_total_enthalpy_consistent_face_states";
        }
        metadata.limiter = "Barth_Jespersen_active";
    } else {
        metadata.reconstruction = "piecewise_constant";
        metadata.limiter = "disabled_for_first_order_diagnostic";
    }
    metadata.spatial_order_claimed = config.numerics_required.spatial_order;
    metadata.positivity_preservation =
        config.run_control.type == cfd::RunType::steady &&
                config.physics.mode == cfd::PhysicsMode::inviscid
            ? "face_increment_scaling_global_update_backtracking_and_reconstructed_face_only_0.1_freestream_relative_joint_density_pressure_rarefaction_guard"
            : "face_increment_scaling_and_global_update_backtracking";
    metadata.wall_boundary_output_semantics = "boundary_value";
    metadata.true_bdf2_inner_loop = config.run_control.type == cfd::RunType::transient;
    metadata.transient_statistics.typical_inner_iterations =
        config.run_control.min_inner_iterations;
    metadata.start_time_utc = utc_now();
    return metadata;
}

[[nodiscard]] cfd::ConvergenceStatus parse_status(const std::string& value) {
    if (value == "converged") return cfd::ConvergenceStatus::converged;
    if (value == "statistically_periodic") return cfd::ConvergenceStatus::statistically_periodic;
    return cfd::ConvergenceStatus::failed;
}

void apply_restart(cfd::FlowSolver& solver, const std::filesystem::path& restart_file) {
    const auto records = cfd::read_restart_file(restart_file);
    std::unordered_map<cfd::GlobalIndex, cfd::State> state_by_id;
    state_by_id.reserve(records.size());
    for (const auto& record : records) state_by_id.emplace(record.global_cell_id, record.state);
    std::vector<cfd::State> owned;
    owned.reserve(solver.mesh().owned_count);
    for (std::size_t local = 0; local < solver.mesh().owned_count; ++local) {
        const auto global_id = solver.mesh().cells[local].cell.global_id;
        const auto found = state_by_id.find(global_id);
        if (found == state_by_id.end()) {
            throw std::runtime_error("restart is missing global cell " +
                                     std::to_string(global_id));
        }
        owned.push_back(found->second);
    }
    solver.set_initial_owned_states(owned);
}

void apply_transient_checkpoint(cfd::FlowSolver& solver,
                                const cfd::TransientCheckpoint& checkpoint) {
    if (solver.config().run_control.type != cfd::RunType::transient ||
        checkpoint.case_id != solver.config().case_id ||
        checkpoint.global_cell_count != solver.mesh().global_cell_count ||
        std::abs(checkpoint.time_step - solver.config().run_control.time_step.value()) > 1.0e-12 ||
        std::abs(checkpoint.physical_time - static_cast<cfd::Real>(checkpoint.step) *
                                            checkpoint.time_step) > 1.0e-12) {
        throw std::runtime_error("transient checkpoint is incompatible with the requested case");
    }
    std::unordered_map<cfd::GlobalIndex, const cfd::TransientRestartStateRecord*> by_id;
    by_id.reserve(checkpoint.states.size());
    for (const auto& record : checkpoint.states) by_id.emplace(record.global_cell_id, &record);
    std::vector<cfd::State> previous;
    std::vector<cfd::State> older;
    previous.reserve(solver.mesh().owned_count);
    older.reserve(solver.mesh().owned_count);
    for (std::size_t local = 0; local < solver.mesh().owned_count; ++local) {
        const auto global_id = solver.mesh().cells[local].cell.global_id;
        const auto found = by_id.find(global_id);
        if (found == by_id.end()) {
            throw std::runtime_error("transient checkpoint is missing global cell " +
                                     std::to_string(global_id));
        }
        previous.push_back(found->second->previous);
        older.push_back(found->second->older);
    }
    std::vector<cfd::SolverForceSample> history;
    history.reserve(checkpoint.force_history.size());
    for (const auto& sample : checkpoint.force_history) {
        history.push_back({sample.step, sample.physical_time, sample.lift, sample.drag});
    }
    solver.set_transient_owned_states(static_cast<int>(checkpoint.step),
                                      checkpoint.initial_global_residual,
                                      checkpoint.initial_symmetry_seed_applied, previous,
                                      older, history, checkpoint.inner_iterations);
}

}  // namespace

int main(int argc, char** argv) {
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) != MPI_SUCCESS) {
        std::cerr << "cfd_solver: MPI initialization failed\n";
        return 1;
    }
    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    try {
        const CommandLine command = parse_command_line(argc, argv);
        cfd::CaseConfig config = cfd::load_case_config(command.case_file);
        std::optional<cfd::TransientCheckpoint> resume_checkpoint;
        if (command.resume_file) {
            resume_checkpoint = cfd::read_transient_checkpoint_file(*command.resume_file);
            if (config.run_control.type != cfd::RunType::transient ||
                resume_checkpoint->case_id != config.case_id ||
                std::abs(resume_checkpoint->time_step - config.run_control.time_step.value()) > 1.0e-12) {
                throw std::runtime_error("--resume checkpoint does not match this transient case");
            }
        }
        if (command.diagnostic_steps) {
            if (config.run_control.type == cfd::RunType::steady) {
                config.run_control.max_steps = *command.diagnostic_steps;
            } else {
                config.run_control.final_time =
                    static_cast<cfd::Real>(*command.diagnostic_steps) *
                    config.run_control.time_step.value();
            }
        }
        if (command.diagnostic_order) {
            config.numerics_required.spatial_order = *command.diagnostic_order;
        }
        if (command.pseudo_cfl) {
            config.run_control.cfl_initial = *command.pseudo_cfl;
            config.run_control.cfl_max = *command.pseudo_cfl;
        }
        if (command.diagnostic_cfl) {
            config.run_control.cfl_initial = *command.diagnostic_cfl;
            config.run_control.cfl_max = *command.diagnostic_cfl;
        }
        if (command.diagnostic_max_inner_iterations) {
            if (*command.diagnostic_max_inner_iterations <
                config.run_control.min_inner_iterations) {
                throw std::invalid_argument(
                    "--diagnostic-max-inner cannot be below the supplied minimum inner iterations");
            }
            config.run_control.max_inner_iterations =
                *command.diagnostic_max_inner_iterations;
        }
        if (command.rusanov_dissipation_scale) {
            config.run_control.rusanov_dissipation_scale =
                *command.rusanov_dissipation_scale;
        }
        if (command.diagnostic_uniform) {
            for (auto& [tag, type] : config.boundary_conditions) {
                static_cast<void>(tag);
                type = cfd::BoundaryType::farfield;
            }
        }
        cfd::Mesh global_mesh;
        cfd::PartitionResult partition;
        if (rank == 0) {
            global_mesh = cfd::read_cgns_mesh(config.mesh.resolved_file);
            cfd::validate_mesh(global_mesh);
            partition = cfd::partition_metis(global_mesh, size);
        }
        cfd::DistributedMesh distributed =
            cfd::distribute_mesh(global_mesh, partition, MPI_COMM_WORLD);
        const auto diagnostics = gather_partition_diagnostics(distributed, MPI_COMM_WORLD);
        std::unique_ptr<cfd::OutputSession> output;
        if (rank == 0) {
            std::optional<cfd::OutputResumeState> output_resume;
            if (resume_checkpoint) {
                output_resume = {resume_checkpoint->step, resume_checkpoint->physical_time,
                                 *command.resume_file};
            }
            output = std::make_unique<cfd::OutputSession>(
                config, command.output_directory, make_metadata(config, distributed), rank, output_resume);
            if (!resume_checkpoint) output->write_partition_diagnostics(diagnostics);
            output->log("command: " + command.command);
            output->log("distributed METIS mesh: global cells=" +
                        std::to_string(distributed.global_cell_count) +
                        " global faces=" + std::to_string(distributed.global_face_count) +
                        " edge cut=" + std::to_string(distributed.partition_edge_cut));
        }
        cfd::FlowSolver solver(config, std::move(distributed), MPI_COMM_WORLD);
        if (resume_checkpoint) apply_transient_checkpoint(solver, *resume_checkpoint);
        else if (command.restart_file) apply_restart(solver, *command.restart_file);
        if (command.restart_perturbation) solver.apply_transient_symmetry_seed();

        cfd::SolverCallbacks callbacks{};
        callbacks.residual = [&](const cfd::SolverResidualSample& sample) {
            if (rank != 0) return;
            output->append_residual({sample.step, sample.physical_time,
                                     sample.inner_iteration, sample.cfl,
                                     sample.physical_dt, sample.component_l2[0],
                                     sample.component_l2[1], sample.component_l2[2],
                                     sample.component_l2[3], sample.residual_l2,
                                     sample.residual_linf});
        };
        callbacks.force = [&](const cfd::SolverForceSample& sample) {
            if (rank != 0) return;
            output->append_force({sample.step, sample.physical_time, sample.lift,
                                  sample.drag, sample.moment_z, sample.pressure_drag,
                                  sample.viscous_drag, sample.pressure_lift,
                                  sample.viscous_lift});
        };
        callbacks.log = [&](const std::string& message) {
            if (rank != 0) return;
            output->log(message);
            std::cout << message << '\n' << std::flush;
        };
        callbacks.checkpoint = [&](
                                   int step,
                                   const std::vector<cfd::SolverFieldCell>& local) {
            const auto gathered =
                cfd::gather_field_cells(local, MPI_COMM_WORLD);
            if (rank == 0) {
                output->write_final_restart(make_restart_records(gathered));
                output->log("wrote durable steady restart checkpoint step=" +
                            std::to_string(step));
            }
        };
        callbacks.transient_checkpoint = [&](const cfd::SolverTransientCheckpoint& checkpoint) {
            const auto gathered = cfd::gather_transient_states(checkpoint.states, MPI_COMM_WORLD);
            if (rank == 0) {
                cfd::TransientCheckpoint durable{};
                durable.case_id = config.case_id;
                durable.step = checkpoint.step;
                durable.physical_time = checkpoint.physical_time;
                durable.time_step = config.run_control.time_step.value();
                durable.initial_global_residual = checkpoint.initial_global_residual;
                durable.initial_symmetry_seed_applied =
                    checkpoint.initial_symmetry_seed_applied;
                durable.global_cell_count = solver.mesh().global_cell_count;
                durable.inner_iterations = checkpoint.inner_iterations;
                durable.states.reserve(gathered.size());
                for (const auto& state : gathered) {
                    durable.states.push_back({state.global_id, state.previous, state.older});
                }
                durable.force_history.reserve(checkpoint.force_history.size());
                for (const auto& sample : checkpoint.force_history) {
                    durable.force_history.push_back(
                        {sample.step, sample.physical_time, sample.lift, sample.drag});
                }
                output->write_transient_checkpoint(durable);
                output->log("wrote durable transient BDF2 checkpoint step=" +
                            std::to_string(checkpoint.step));
            }
        };
        // A physical-time snapshot cadence is part of the case output contract,
        // not merely a verbosity choice.  Honor it for every production run;
        // report_level controls presentation only.
        if (config.outputs.write_field_every_time) {
            callbacks.snapshot = [&](int step, cfd::Real time,
                                     const std::vector<cfd::SolverFieldCell>& local) {
                const auto gathered = cfd::gather_field_cells(local, MPI_COMM_WORLD);
                if (rank == 0) {
                    std::ostringstream filename;
                    filename << "field_step_" << std::setw(6) << std::setfill('0') << step
                             << ".vtk";
                    output->write_field_snapshot_vtk(make_polygons(gathered, config),
                                                     filename.str());
                    output->log("wrote physical-time snapshot t=" + std::to_string(time));
                }
            };
        }

        const double start = MPI_Wtime();
        cfd::SolverSummary summary = solver.solve(callbacks);
        if (command.diagnostic_steps || command.diagnostic_order ||
            command.diagnostic_cfl || command.diagnostic_max_inner_iterations ||
            command.diagnostic_uniform) {
            summary.convergence_status = "failed";
            summary.notes += "; diagnostic numerical override was used; this is not a final result";
        }
        const double local_wall_time = MPI_Wtime() - start;
        double wall_time = 0.0;
        MPI_Allreduce(&local_wall_time, &wall_time, 1, MPI_DOUBLE, MPI_MAX,
                      MPI_COMM_WORLD);

        const auto gathered_field =
            cfd::gather_field_cells(solver.local_field_cells(), MPI_COMM_WORLD);
        const auto gathered_surface =
            cfd::gather_surface_samples(solver.local_surface_samples(), MPI_COMM_WORLD);
        if (rank == 0) {
            const cfd::FinalStateDescriptor final_state{summary.final_step,
                                                        summary.final_physical_time};
            const auto polygons = make_polygons(gathered_field, config);
            output->write_final_surface(make_surface_records(gathered_surface), final_state);
            output->write_final_field_vtk(polygons, final_state);
            output->write_final_restart(make_restart_records(gathered_field));
            cfd::TransientStatistics statistics{};
            statistics.typical_inner_iterations =
                static_cast<int>(std::llround(summary.inner_statistics.observed_mean));
            statistics.observed_min_inner_iterations = summary.inner_statistics.observed_min;
            statistics.observed_max_inner_iterations = summary.inner_statistics.observed_max;
            statistics.observed_mean_inner_iterations = summary.inner_statistics.observed_mean;
            statistics.inner_target_misses = summary.inner_statistics.target_misses;
            statistics.inner_target_converged_fraction =
                summary.inner_statistics.converged_fraction;
            statistics.last_inner_residual_ratio =
                summary.inner_statistics.last_residual_ratio;
            output->update_transient_statistics(statistics);
            cfd::RunStatus status{};
            status.command = command.command;
            status.mpi_ranks = size;
            status.wall_time_seconds = wall_time;
            status.final_state = final_state;
            status.convergence_status = parse_status(summary.convergence_status);
            status.residual_reduction_orders = summary.residual_reduction_orders;
            status.notes = summary.notes + "; positivity fallbacks=" +
                           std::to_string(summary.positivity_backtracks) +
                           "; Riemann fallbacks=" +
                           std::to_string(summary.hllc_fallback_faces);
            if (status.convergence_status == cfd::ConvergenceStatus::failed) {
                output->record_failure(status);
            } else {
                output->complete(status);
            }
            std::cout << "case " << config.case_id << " finished with status "
                      << cfd::to_string(status.convergence_status) << " in "
                      << wall_time << " s\n";
        }
        MPI_Finalize();
        return summary.convergence_status == "failed" ? 2 : 0;
    } catch (const std::exception& error) {
        if (rank == 0) std::cerr << "cfd_solver: " << error.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
