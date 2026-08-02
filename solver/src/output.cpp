#include "cfd/output.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace cfd {
namespace {

using json = nlohmann::json;

constexpr std::array<char, 8> kRestartMagic{{'C', 'F', 'D', 'R', 'S', 'T', '0', '1'}};
constexpr std::uint32_t kRestartVersion = 1;
constexpr std::uint32_t kRestartStateWidth = 4;

[[noreturn]] void fail(const std::string& message) {
    throw OutputError(message);
}

bool finite(Real value) {
    return std::isfinite(value);
}

void require_finite(Real value, const char* name) {
    if (!finite(value)) {
        fail(std::string(name) + " must be finite");
    }
}

template <typename... Values>
void require_all_finite(const Values... values) {
    (require_finite(values, "output value"), ...);
}

void require_nonnegative(std::int64_t value, const char* name) {
    if (value < 0) {
        fail(std::string(name) + " must not be negative");
    }
}

std::string csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }
    std::string escaped{"\""};
    for (const char character : value) {
        if (character == '\"') {
            escaped += "\"\"";
        } else {
            escaped += character;
        }
    }
    escaped += '\"';
    return escaped;
}

bool same_final_state(const FinalStateDescriptor& lhs, const FinalStateDescriptor& rhs) {
    return lhs.step == rhs.step && std::abs(lhs.physical_time - rhs.physical_time) <= 1e-12;
}

void require_valid_final_state(const FinalStateDescriptor& state, const char* name) {
    if (state.step < 0) {
        fail(std::string(name) + ".step must not be negative");
    }
    require_finite(state.physical_time, name);
    if (state.physical_time < 0.0) {
        fail(std::string(name) + ".physical_time must not be negative");
    }
}

void require_open(const std::ofstream& stream, const std::filesystem::path& path) {
    if (!stream) {
        fail("cannot write '" + path.string() + "'");
    }
}

std::string now_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm timestamp{};
#if defined(_WIN32)
    gmtime_s(&timestamp, &seconds);
#else
    gmtime_r(&seconds, &timestamp);
#endif
    std::ostringstream output;
    output << std::put_time(&timestamp, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

void validate_metadata(const OutputMetadata& metadata) {
    const std::array<const std::string*, 13> strings{{
        &metadata.solver_name, &metadata.solver_version, &metadata.partitioner,
        &metadata.halo_exchange, &metadata.equation_set, &metadata.inviscid_flux,
        &metadata.viscous_flux, &metadata.time_integrator, &metadata.implicit_solver,
        &metadata.reconstruction, &metadata.limiter, &metadata.positivity_preservation,
        &metadata.wall_boundary_output_semantics,
    }};
    for (const auto* value : strings) {
        if (value->empty()) {
            fail("metadata contains a required empty string");
        }
    }
    if (metadata.mpi_ranks <= 0 || metadata.num_cells_global <= 0 ||
        metadata.num_faces_global < 0 || metadata.num_cells_owned_local < 0 ||
        metadata.num_cells_ghost_local < 0 || metadata.partition_edge_cut < 0 ||
        metadata.spatial_order_claimed < 1 || metadata.start_time_utc.empty() ||
        metadata.full_state_replication_during_iterations ||
        metadata.full_mesh_replication_during_iterations) {
        fail("metadata contains an invalid count, order, rank count, or start time");
    }
    const auto& statistics = metadata.transient_statistics;
    if (statistics.typical_inner_iterations < 0 || statistics.observed_min_inner_iterations < 0 ||
        statistics.observed_max_inner_iterations < statistics.observed_min_inner_iterations ||
        statistics.inner_target_misses < 0 || !finite(statistics.observed_mean_inner_iterations) ||
        !finite(statistics.inner_target_converged_fraction) ||
        !finite(statistics.last_inner_residual_ratio) ||
        statistics.inner_target_converged_fraction < 0.0 ||
        statistics.inner_target_converged_fraction > 1.0) {
        fail("metadata transient statistics are invalid");
    }
}

json metadata_json(const CaseConfig& config, const OutputMetadata& metadata,
                   ConvergenceStatus status, bool completed = true) {
    const auto& stats = metadata.transient_statistics;
    return json{{"case_id", config.case_id},
                {"solver_name", metadata.solver_name},
                {"solver_version", metadata.solver_version},
                {"git_revision", metadata.git_revision ? json(*metadata.git_revision) : json(nullptr)},
                {"mpi_ranks", metadata.mpi_ranks},
                {"mesh_file", config.mesh.resolved_file.string()},
                {"num_cells_global", metadata.num_cells_global},
                {"num_faces_global", metadata.num_faces_global},
                {"num_cells_owned_local", metadata.num_cells_owned_local},
                {"num_cells_ghost_local", metadata.num_cells_ghost_local},
                {"partitioner", metadata.partitioner},
                {"partition_edge_cut", metadata.partition_edge_cut},
                {"halo_exchange", metadata.halo_exchange},
                {"full_state_replication_during_iterations", metadata.full_state_replication_during_iterations},
                {"full_mesh_replication_during_iterations", metadata.full_mesh_replication_during_iterations},
                {"equation_set", metadata.equation_set},
                {"inviscid_flux", metadata.inviscid_flux},
                {"entropy_fix", metadata.entropy_fix ? json(*metadata.entropy_fix) : json(nullptr)},
                {"viscous_flux", metadata.viscous_flux},
                {"time_integrator", metadata.time_integrator},
                {"implicit_solver", metadata.implicit_solver},
                {"reconstruction", metadata.reconstruction},
                {"limiter", metadata.limiter},
                {"spatial_order_claimed", metadata.spatial_order_claimed},
                {"positivity_preservation", metadata.positivity_preservation},
                {"wall_boundary_output_semantics", metadata.wall_boundary_output_semantics},
                {"true_bdf2_inner_loop", metadata.true_bdf2_inner_loop},
                {"typical_inner_iterations", stats.typical_inner_iterations},
                {"min_inner_iterations", config.run_control.min_inner_iterations},
                {"max_inner_iterations", config.run_control.max_inner_iterations},
                {"observed_min_inner_iterations", stats.observed_min_inner_iterations},
                {"observed_max_inner_iterations", stats.observed_max_inner_iterations},
                {"observed_mean_inner_iterations", stats.observed_mean_inner_iterations},
                {"inner_residual_reduction_target", config.run_control.inner_residual_reduction_target},
                {"inner_target_misses", stats.inner_target_misses},
                {"inner_target_converged_fraction", stats.inner_target_converged_fraction},
                {"last_inner_residual_ratio", stats.last_inner_residual_ratio},
                {"start_time_utc", metadata.start_time_utc},
                {"end_time_utc", now_utc()},
                {"completed", completed},
                {"convergence_status", to_string(status)}};
}

void write_vtk_file(const std::filesystem::path& path,
                    const std::vector<GatheredPolygonRecord>& records,
                    const std::string& title) {
    if (records.empty()) fail("VTK field requires at least one polygon");
    std::map<std::pair<Real, Real>, std::size_t> point_index;
    std::vector<Vec2> unique_points;
    std::vector<std::vector<std::size_t>> connectivity;
    connectivity.reserve(records.size());
    std::size_t cell_list_size = 0;
    for (const auto& record : records) {
        if (record.global_cell_id < 0 || record.points.size() < 3U || record.owner_rank < 0) {
            fail("field polygon has invalid id, connectivity, or owner rank");
        }
        std::vector<std::size_t> cell;
        cell.reserve(record.points.size());
        for (const auto& point : record.points) {
            require_all_finite(point[0], point[1]);
            const auto key = std::make_pair(point[0], point[1]);
            const auto [found, inserted] = point_index.emplace(key, unique_points.size());
            if (inserted) unique_points.push_back(point);
            cell.push_back(found->second);
        }
        connectivity.push_back(std::move(cell));
        require_all_finite(record.density, record.velocity[0], record.velocity[1], record.pressure,
                           record.mach, record.total_energy, record.temperature);
        cell_list_size += record.points.size() + 1U;
    }
    std::ofstream output(path);
    require_open(output, path);
    output << "# vtk DataFile Version 3.0\n" << title
           << "\nASCII\nDATASET UNSTRUCTURED_GRID\n";
    output << "POINTS " << unique_points.size() << " double\n" << std::setprecision(17);
    for (const auto& point : unique_points) output << point[0] << ' ' << point[1] << " 0\n";
    output << "CELLS " << records.size() << ' ' << cell_list_size << '\n';
    for (const auto& cell : connectivity) {
        output << cell.size();
        for (const std::size_t index : cell) output << ' ' << index;
        output << '\n';
    }
    output << "CELL_TYPES " << records.size() << '\n';
    for (std::size_t index = 0; index < records.size(); ++index) output << "7\n";
    output << "CELL_DATA " << records.size() << '\n';
    const auto write_scalar = [&output, &records](const char* name, const auto getter) {
        output << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
        for (const auto& record : records) output << getter(record) << '\n';
    };
    write_scalar("density", [](const GatheredPolygonRecord& record) { return record.density; });
    output << "VECTORS velocity double\n";
    for (const auto& record : records) {
        output << record.velocity[0] << ' ' << record.velocity[1] << " 0\n";
    }
    write_scalar("pressure", [](const GatheredPolygonRecord& record) { return record.pressure; });
    write_scalar("mach", [](const GatheredPolygonRecord& record) { return record.mach; });
    write_scalar("total_energy", [](const GatheredPolygonRecord& record) { return record.total_energy; });
    write_scalar("temperature", [](const GatheredPolygonRecord& record) { return record.temperature; });
    output << "SCALARS owner_rank int 1\nLOOKUP_TABLE default\n";
    for (const auto& record : records) output << record.owner_rank << '\n';
    require_open(output, path);
}

void write_json_file(const std::filesystem::path& path, const json& value) {
    std::ofstream output(path);
    require_open(output, path);
    output << value.dump(2) << '\n';
    require_open(output, path);
}

}  // namespace

const char* to_string(ConvergenceStatus value) noexcept {
    switch (value) {
        case ConvergenceStatus::converged:
            return "converged";
        case ConvergenceStatus::statistically_periodic:
            return "statistically_periodic";
        case ConvergenceStatus::failed:
            return "failed";
    }
    return "failed";
}

struct OutputSession::Impl {
    CaseConfig config;
    std::filesystem::path directory;
    OutputMetadata metadata;
    std::ofstream stdout_log;
    std::ofstream residuals;
    std::ofstream forces;
    std::ofstream surface;
    std::ofstream partition_diagnostics;
    std::optional<ForceRecord> last_force;
    std::optional<FinalStateDescriptor> surface_state;
    std::optional<FinalStateDescriptor> field_state;
    bool has_residual{};
    bool has_partition_diagnostics{};
    bool has_restart{};
    bool completed{};
};

OutputSession::OutputSession(const CaseConfig& case_config, std::filesystem::path output_directory,
                             OutputMetadata metadata, int mpi_rank)
    : impl_(std::make_unique<Impl>()) {
    if (mpi_rank != 0) {
        fail("OutputSession may only be created on MPI rank 0");
    }
    validate_metadata(metadata);
    if (output_directory.empty()) {
        fail("output directory must not be empty");
    }
    std::filesystem::create_directories(output_directory);
    if (!std::filesystem::is_directory(output_directory)) {
        fail("output path is not a directory: '" + output_directory.string() + "'");
    }

    impl_->config = case_config;
    impl_->directory = std::move(output_directory);
    impl_->metadata = std::move(metadata);
    impl_->stdout_log.open(impl_->directory / "stdout.log");
    impl_->residuals.open(impl_->directory / "residuals.csv");
    impl_->forces.open(impl_->directory / "forces.csv");
    impl_->surface.open(impl_->directory / "surface.csv");
    impl_->partition_diagnostics.open(impl_->directory / "partition_diagnostics.csv");
    require_open(impl_->stdout_log, impl_->directory / "stdout.log");
    require_open(impl_->residuals, impl_->directory / "residuals.csv");
    require_open(impl_->forces, impl_->directory / "forces.csv");
    require_open(impl_->surface, impl_->directory / "surface.csv");
    require_open(impl_->partition_diagnostics, impl_->directory / "partition_diagnostics.csv");
    impl_->residuals << "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf\n";
    impl_->forces << "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift\n";
    impl_->surface << "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag\n";
    impl_->partition_diagnostics << "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells\n";
    log("OutputSession initialized for case " + impl_->config.case_id);
}

OutputSession::~OutputSession() = default;

const std::filesystem::path& OutputSession::output_directory() const noexcept {
    return impl_->directory;
}

void OutputSession::log(const std::string& message) {
    if (impl_->completed) {
        fail("cannot write output after completion");
    }
    impl_->stdout_log << message << '\n';
    require_open(impl_->stdout_log, impl_->directory / "stdout.log");
}

void OutputSession::write_partition_diagnostics(const std::vector<PartitionDiagnosticsRecord>& records) {
    if (impl_->completed || records.empty()) {
        fail("partition diagnostics require at least one pre-completion record");
    }
    for (const auto& record : records) {
        if (record.rank < 0 || record.num_neighbor_ranks < 0) {
            fail("partition diagnostics ranks must not be negative");
        }
        require_nonnegative(record.num_cells_owned, "num_cells_owned");
        require_nonnegative(record.num_cells_ghost, "num_cells_ghost");
        require_nonnegative(record.num_boundary_faces, "num_boundary_faces");
        impl_->partition_diagnostics << record.rank << ',' << record.num_cells_owned << ','
                                    << record.num_cells_ghost << ',' << record.num_boundary_faces << ','
                                    << record.num_neighbor_ranks << ',' << csv_escape(record.neighbor_ranks)
                                    << ',' << csv_escape(record.send_cells) << ','
                                    << csv_escape(record.recv_cells) << '\n';
    }
    require_open(impl_->partition_diagnostics, impl_->directory / "partition_diagnostics.csv");
    impl_->has_partition_diagnostics = true;
}

void OutputSession::append_residual(const ResidualRecord& record) {
    if (impl_->completed || record.step < 0 || record.inner_iter < 0) {
        fail("residual record has an invalid step or is written after completion");
    }
    require_all_finite(record.physical_time, record.cfl, record.dt, record.rho, record.rhou,
                       record.rhov, record.rhoE, record.residual_l2, record.residual_linf);
    impl_->residuals << record.step << ',' << record.physical_time << ',' << record.inner_iter << ','
                     << record.cfl << ',' << record.dt << ',' << record.rho << ',' << record.rhou
                     << ',' << record.rhov << ',' << record.rhoE << ',' << record.residual_l2 << ','
                     << record.residual_linf << '\n';
    require_open(impl_->residuals, impl_->directory / "residuals.csv");
    impl_->has_residual = true;
}

void OutputSession::append_force(const ForceRecord& record) {
    if (impl_->completed || record.step < 0) {
        fail("force record has an invalid step or is written after completion");
    }
    require_all_finite(record.physical_time, record.cl, record.cd, record.cmz, record.pressure_drag,
                       record.viscous_drag, record.pressure_lift, record.viscous_lift);
    impl_->forces << record.step << ',' << record.physical_time << ',' << record.cl << ',' << record.cd
                  << ',' << record.cmz << ',' << record.pressure_drag << ',' << record.viscous_drag
                  << ',' << record.pressure_lift << ',' << record.viscous_lift << '\n';
    require_open(impl_->forces, impl_->directory / "forces.csv");
    impl_->last_force = record;
}

void OutputSession::update_transient_statistics(const TransientStatistics& statistics) {
    if (impl_->completed) fail("cannot update transient statistics after completion");
    impl_->metadata.transient_statistics = statistics;
    validate_metadata(impl_->metadata);
}

void OutputSession::write_final_surface(const std::vector<SurfaceRecord>& records,
                                        FinalStateDescriptor state) {
    if (impl_->completed || records.empty()) {
        fail("final surface requires at least one pre-completion row");
    }
    require_valid_final_state(state, "surface final state");
    for (const auto& record : records) {
        if (record.tag.empty()) {
            fail("surface tag must not be empty");
        }
        require_all_finite(record.x, record.y, record.nx, record.ny, record.pressure, record.cp,
                           record.cf, record.rho, record.u, record.v, record.mach);
        impl_->surface << record.x << ',' << record.y << ',' << record.nx << ',' << record.ny << ','
                       << record.pressure << ',' << record.cp << ',' << record.cf << ',' << record.rho
                       << ',' << record.u << ',' << record.v << ',' << record.mach << ','
                       << csv_escape(record.tag) << '\n';
    }
    require_open(impl_->surface, impl_->directory / "surface.csv");
    impl_->surface_state = state;
}

void OutputSession::write_final_field_vtk(const std::vector<GatheredPolygonRecord>& records,
                                          FinalStateDescriptor state) {
    if (impl_->completed || records.empty()) {
        fail("final VTK field requires at least one pre-completion polygon");
    }
    require_valid_final_state(state, "field final state");
    const auto path = impl_->directory / "field_final.vtk";
    write_vtk_file(path, records, "CFD final field");
    impl_->field_state = state;
}

void OutputSession::write_field_snapshot_vtk(
    const std::vector<GatheredPolygonRecord>& records, const std::string& filename) {
    if (impl_->completed || filename.empty() ||
        std::filesystem::path(filename).filename().string() != filename ||
        std::filesystem::path(filename).extension() != ".vtk" ||
        filename == "field_final.vtk") {
        fail("snapshot filename must be a non-final .vtk basename written before completion");
    }
    write_vtk_file(impl_->directory / filename, records, "CFD transient snapshot");
}

void OutputSession::write_final_restart(const std::vector<RestartStateRecord>& records) {
    if (impl_->completed || records.empty()) {
        fail("final restart requires at least one pre-completion state");
    }
    auto sorted = records;
    std::sort(sorted.begin(), sorted.end(), [](const RestartStateRecord& lhs,
                                                const RestartStateRecord& rhs) {
        return lhs.global_cell_id < rhs.global_cell_id;
    });
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        if (sorted[index].global_cell_id < 0 ||
            (index > 0 && sorted[index - 1].global_cell_id == sorted[index].global_cell_id)) {
            fail("restart global ids must be nonnegative and unique");
        }
        for (const Real value : sorted[index].state) {
            require_finite(value, "restart state");
        }
    }
    const auto path = impl_->directory / "restart_final.bin";
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    require_open(output, temporary);
    const auto count = static_cast<std::uint64_t>(sorted.size());
    output.write(kRestartMagic.data(), static_cast<std::streamsize>(kRestartMagic.size()));
    output.write(reinterpret_cast<const char*>(&kRestartVersion), sizeof(kRestartVersion));
    output.write(reinterpret_cast<const char*>(&kRestartStateWidth), sizeof(kRestartStateWidth));
    output.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& record : sorted) {
        output.write(reinterpret_cast<const char*>(&record.global_cell_id), sizeof(record.global_cell_id));
        output.write(reinterpret_cast<const char*>(record.state.data()),
                     static_cast<std::streamsize>(sizeof(Real) * record.state.size()));
    }
    output.flush();
    require_open(output, temporary);
    output.close();
    if (output.fail()) fail("cannot close restart checkpoint " + temporary.string());
    std::error_code rename_error;
    std::filesystem::rename(temporary, path, rename_error);
    if (rename_error) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        fail("cannot atomically replace restart " + path.string() + ": " +
             rename_error.message());
    }
    impl_->has_restart = true;
}

void OutputSession::complete(const RunStatus& status) {
    if (impl_->completed) {
        fail("OutputSession has already completed");
    }
    if (!impl_->has_partition_diagnostics || !impl_->has_residual || !impl_->last_force ||
        !impl_->surface_state || !impl_->field_state || !impl_->has_restart) {
        fail("cannot complete: one or more mandatory output artifacts were not written");
    }
    require_valid_final_state(status.final_state, "run status final state");
    if (status.command.empty() || status.notes.empty() || status.mpi_ranks != impl_->metadata.mpi_ranks ||
        status.mpi_ranks <= 0 || status.convergence_status == ConvergenceStatus::failed) {
        fail("run status is incomplete, inconsistent, or failed");
    }
    require_all_finite(status.wall_time_seconds, status.residual_reduction_orders);
    if (status.wall_time_seconds < 0.0) {
        fail("run status wall_time_seconds must not be negative");
    }
    const FinalStateDescriptor force_state{impl_->last_force->step, impl_->last_force->physical_time};
    if (!same_final_state(force_state, *impl_->surface_state) ||
        !same_final_state(force_state, *impl_->field_state) ||
        !same_final_state(force_state, status.final_state)) {
        fail("last force row, final surface, final field, and run status must share one final state");
    }
    impl_->stdout_log.flush();
    impl_->residuals.flush();
    impl_->forces.flush();
    impl_->surface.flush();
    impl_->partition_diagnostics.flush();
    write_json_file(impl_->directory / "metadata.json",
                    metadata_json(impl_->config, impl_->metadata, status.convergence_status));
    write_json_file(impl_->directory / "run_status.json",
                    json{{"case_id", impl_->config.case_id},
                         {"command", status.command},
                         {"mpi_ranks", status.mpi_ranks},
                         {"wall_time_seconds", status.wall_time_seconds},
                         {"final_step", status.final_state.step},
                         {"final_physical_time", status.final_state.physical_time},
                         {"convergence_status", to_string(status.convergence_status)},
                         {"residual_reduction_orders", status.residual_reduction_orders},
                         {"notes", status.notes}});
    log("OutputSession completed with status " + std::string(to_string(status.convergence_status)));
    impl_->stdout_log.flush();
    impl_->completed = true;
}

void OutputSession::record_failure(const RunStatus& status) {
    if (impl_->completed || status.convergence_status != ConvergenceStatus::failed ||
        status.command.empty() || status.notes.empty() ||
        status.mpi_ranks != impl_->metadata.mpi_ranks || status.mpi_ranks <= 0) {
        fail("failed run status is incomplete or inconsistent");
    }
    require_valid_final_state(status.final_state, "failed run final state");
    require_all_finite(status.wall_time_seconds, status.residual_reduction_orders);
    impl_->stdout_log.flush();
    impl_->residuals.flush();
    impl_->forces.flush();
    impl_->surface.flush();
    impl_->partition_diagnostics.flush();
    write_json_file(impl_->directory / "metadata.json",
                    metadata_json(impl_->config, impl_->metadata,
                                  ConvergenceStatus::failed, false));
    write_json_file(impl_->directory / "run_status.json",
                    json{{"case_id", impl_->config.case_id},
                         {"command", status.command},
                         {"mpi_ranks", status.mpi_ranks},
                         {"wall_time_seconds", status.wall_time_seconds},
                         {"final_step", status.final_state.step},
                         {"final_physical_time", status.final_state.physical_time},
                         {"convergence_status", "failed"},
                         {"residual_reduction_orders", status.residual_reduction_orders},
                         {"notes", status.notes}});
    log("OutputSession recorded failed numerical run honestly");
    impl_->stdout_log.flush();
    impl_->completed = true;
}

std::vector<RestartStateRecord> read_restart_file(const std::filesystem::path& restart_file) {
    std::ifstream input(restart_file, std::ios::binary);
    if (!input) {
        fail("cannot open restart file '" + restart_file.string() + "'");
    }
    std::array<char, 8> magic{};
    std::uint32_t version{};
    std::uint32_t state_width{};
    std::uint64_t count{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    input.read(reinterpret_cast<char*>(&version), sizeof(version));
    input.read(reinterpret_cast<char*>(&state_width), sizeof(state_width));
    input.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!input || magic != kRestartMagic || version != kRestartVersion ||
        state_width != kRestartStateWidth) {
        fail("restart file has an unsupported or corrupt header");
    }
    if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        fail("restart record count exceeds addressable memory");
    }
    std::vector<RestartStateRecord> records(static_cast<std::size_t>(count));
    for (auto& record : records) {
        input.read(reinterpret_cast<char*>(&record.global_cell_id), sizeof(record.global_cell_id));
        input.read(reinterpret_cast<char*>(record.state.data()),
                   static_cast<std::streamsize>(sizeof(Real) * record.state.size()));
        if (!input || record.global_cell_id < 0) {
            fail("restart file is truncated or has an invalid global id");
        }
        for (const Real value : record.state) {
            require_finite(value, "restart state");
        }
    }
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (records[index - 1].global_cell_id >= records[index].global_cell_id) {
            fail("restart global ids are not strictly sorted");
        }
    }
    return records;
}

}  // namespace cfd
