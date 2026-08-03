#include "cfd/output.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

using cfd::CaseConfig;
using cfd::ConvergenceStatus;
using cfd::FinalStateDescriptor;
using cfd::ForceRecord;
using cfd::GatheredPolygonRecord;
using cfd::OutputMetadata;
using cfd::OutputSession;
using cfd::PartitionDiagnosticsRecord;
using cfd::ResidualRecord;
using cfd::RestartStateRecord;
using cfd::RunStatus;
using cfd::SurfaceRecord;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(bool value, const std::string& message) {
    if (!value) {
        fail(message);
    }
}

std::filesystem::path unique_directory(const std::string& name) {
    const auto directory = std::filesystem::temp_directory_path() / "cfd_output_tests" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

CaseConfig test_case() {
    return cfd::load_case_config(std::filesystem::path(CFD_BENCHMARK_CASES_DIR) /
                                 "naca0012_m015_inviscid.json");
}

OutputMetadata metadata() {
    OutputMetadata value;
    value.solver_name = "output-tests";
    value.solver_version = "0.1";
    value.git_revision = "test";
    value.mpi_ranks = 1;
    value.num_cells_global = 2;
    value.num_faces_global = 7;
    value.num_cells_owned_local = 2;
    value.num_cells_ghost_local = 0;
    value.partitioner = "metis_kway";
    value.partition_edge_cut = 0;
    value.halo_exchange = "neighbor_isend_irecv";
    value.inviscid_flux = "rusanov";
    value.viscous_flux = "disabled";
    value.time_integrator = "implicit_pseudo_time";
    value.implicit_solver = "block_jacobi";
    value.reconstruction = "least_squares_linear";
    value.limiter = "barth_jespersen";
    value.positivity_preservation = "fallback";
    value.start_time_utc = "2026-01-01T00:00:00Z";
    value.transient_statistics = {5, 3, 7, 5.0, 0, 1.0, 1e-4};
    return value;
}

void write_valid_package(OutputSession& output, FinalStateDescriptor state = {12, 0.12}) {
    output.write_partition_diagnostics({{0, 2, 0, 2, 0, "", "", ""}});
    output.append_residual({12, 0.12, 3, 1.0, 0.01, 0.1, 0.2, 0.3, 0.4, 1e-4, 2e-4});
    output.append_force({12, 0.12, 0.0, 0.05, 0.0, 0.05, 0.0, 0.0, 0.0});
    output.write_final_surface({{0.0, 0.0, 1.0, 0.0, 1.0, 0.1, 0.0, 1.0, 1.0, 0.0, 0.15, "bc-4"}}, state);
    output.write_final_field_vtk({{4, {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, 1.0, {1.0, 0.0}, 1.0, 0.15, 2.5, 1.0, 0}}, state);
    output.write_final_restart({{9, {1.0, 2.0, 3.0, 4.0}}, {2, {4.0, 3.0, 2.0, 1.0}}});
}

void test_exact_contract_and_vtk_restart() {
    const auto directory = unique_directory("contract");
    OutputSession output(test_case(), directory, metadata());
    write_valid_package(output);
    output.complete({"mpirun -np 1 solver solve", 1, 3.0, {12, 0.12},
                     ConvergenceStatus::converged, 4.0, "converged"});

    const std::vector<std::pair<std::string, std::string>> headers = {
        {"residuals.csv", "step,physical_time,inner_iter,cfl,dt,rho,rhou,rhov,rhoE,residual_l2,residual_linf"},
        {"forces.csv", "step,physical_time,cl,cd,cmz,pressure_drag,viscous_drag,pressure_lift,viscous_lift"},
        {"surface.csv", "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag"},
        {"partition_diagnostics.csv", "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells"},
    };
    for (const auto& [file, header] : headers) {
        std::ifstream input(directory / file);
        std::string actual;
        std::getline(input, actual);
        expect(actual == header, "wrong header for " + file);
    }
    for (const auto& file : {"stdout.log", "metadata.json", "run_status.json", "field_final.vtk", "restart_final.bin"}) {
        expect(std::filesystem::is_regular_file(directory / file), "missing " + std::string(file));
    }
    std::ifstream vtk(directory / "field_final.vtk");
    const std::string vtk_text{std::istreambuf_iterator<char>(vtk), std::istreambuf_iterator<char>()};
    for (const auto* required : {"DATASET UNSTRUCTURED_GRID", "SCALARS density", "VECTORS velocity", "SCALARS pressure", "SCALARS mach", "SCALARS total_energy", "SCALARS temperature", "SCALARS owner_rank"}) {
        expect(vtk_text.find(required) != std::string::npos, "VTK missing " + std::string(required));
    }
    const auto restart = cfd::read_restart_file(directory / "restart_final.bin");
    expect(restart.size() == 2U && restart[0].global_cell_id == 2 && restart[1].global_cell_id == 9,
           "restart records are not sorted/readable");

    std::ifstream metadata_input(directory / "metadata.json");
    const auto metadata_json = nlohmann::json::parse(metadata_input);
    expect(metadata_json.at("completed") == true && metadata_json.at("observed_mean_inner_iterations") == 5.0,
           "metadata lacks completion or observed mean statistic");
    std::ifstream status_input(directory / "run_status.json");
    const auto status_json = nlohmann::json::parse(status_input);
    expect(status_json.at("final_step") == 12 && status_json.at("convergence_status") == "converged",
           "run status schema is wrong");
}

void test_invalid_status_and_nan_refused() {
    const auto nan_directory = unique_directory("nan");
    OutputSession nan_output(test_case(), nan_directory, metadata());
    bool nan_refused = false;
    try {
        nan_output.append_residual({1, 0.0, 0, 1.0, 0.1, 1.0, 1.0, 1.0, 1.0,
                                    std::numeric_limits<double>::quiet_NaN(), 1.0});
    } catch (const cfd::OutputError&) {
        nan_refused = true;
    }
    expect(nan_refused, "NaN residual was accepted");

    const auto status_directory = unique_directory("status");
    OutputSession status_output(test_case(), status_directory, metadata());
    write_valid_package(status_output);
    bool status_refused = false;
    try {
        status_output.complete({"cmd", 1, 1.0, {13, 0.13}, ConvergenceStatus::failed, 1.0, "failed"});
    } catch (const cfd::OutputError&) {
        status_refused = true;
    }
    expect(status_refused, "invalid/failing completion status was accepted");
}

void test_atomic_restart_replacement() {
    const auto directory = unique_directory("atomic_restart");
    OutputSession output(test_case(), directory, metadata());
    output.write_final_restart({{2, {1.0, 2.0, 3.0, 4.0}}});
    auto restart = cfd::read_restart_file(directory / "restart_final.bin");
    expect(restart.size() == 1U && restart[0].state[0] == 1.0,
           "first restart checkpoint is unreadable");

    output.write_final_restart({{2, {9.0, 8.0, 7.0, 6.0}}});
    restart = cfd::read_restart_file(directory / "restart_final.bin");
    expect(restart.size() == 1U && restart[0].state[0] == 9.0 &&
               restart[0].state[3] == 6.0,
           "replacement restart checkpoint is not the complete newer state");
    expect(!std::filesystem::exists(directory / "restart_final.bin.tmp"),
           "atomic restart left a temporary file after success");
}

void test_csv_round_trip_precision() {
    const auto directory = unique_directory("csv_precision");
    constexpr double physical_time = 0.012345678901234567;
    constexpr double lift = 0.024872012345678901;
    constexpr double drag = -0.072275312345678901;
    constexpr double pressure_drag = -7.6814812345678901;
    constexpr double viscous_drag = drag - pressure_drag;
    {
        OutputSession output(test_case(), directory, metadata());
        output.append_force({1, physical_time, lift, drag, 0.0, pressure_drag,
                             viscous_drag, 0.00578125123456789,
                             lift - 0.00578125123456789});
    }

    std::ifstream input(directory / "forces.csv");
    std::string line;
    std::getline(input, line);
    std::getline(input, line);
    std::stringstream parser(line);
    std::vector<std::string> columns;
    while (std::getline(parser, line, ',')) columns.push_back(line);
    expect(columns.size() == 9U, "force CSV precision fixture has the wrong width");
    expect(std::stod(columns[1]) == physical_time &&
               std::stod(columns[2]) == lift && std::stod(columns[3]) == drag &&
               std::stod(columns[5]) == pressure_drag &&
               std::stod(columns[6]) == viscous_drag,
           "force CSV did not preserve round-trip floating-point precision");
}

void test_transient_checkpoint_round_trip_and_history_resume() {
    const auto directory = unique_directory("transient_resume");
    {
        OutputSession output(test_case(), directory, metadata());
        output.write_partition_diagnostics({{0, 2, 0, 2, 0, "", "", ""}});
        for (int step = 1; step <= 1; ++step) {
            const double time = 0.01 * static_cast<double>(step);
            output.append_residual({step, time, 5, 1.0, 0.01, 0.1, 0.2, 0.3, 0.4,
                                    1e-4, 2e-4});
            output.append_force({step, time, 0.01 * step, 0.05, 0.0, 0.05, 0.0,
                                 0.01 * step, 0.0});
        }
        output.write_transient_checkpoint(
            {test_case().case_id, 1, 0.01, 0.01, 7.5, true, 2,
             {{2, {1.0, 2.0, 3.0, 4.0}, {0.5, 1.5, 2.5, 3.5}},
              {9, {4.0, 3.0, 2.0, 1.0}, {3.5, 2.5, 1.5, 0.5}}},
             {{1, 0.01, 0.01, 0.05}},
             {5}});

        // Checkpoint durability includes static package evidence and CSV
        // headers, not only the two cadence-1 histories.  Read these while the
        // OutputSession streams are still open so destructor flushing cannot
        // mask an interruption-time data-loss regression.
        const auto expect_visible_line = [&](const std::string& filename,
                                             const std::string& expected) {
            std::ifstream input(directory / filename);
            std::string line;
            std::getline(input, line);
            expect(line == expected,
                   filename + " was not durable when the checkpoint became visible");
        };
        expect_visible_line(
            "partition_diagnostics.csv",
            "rank,num_cells_owned,num_cells_ghost,num_boundary_faces,num_neighbor_ranks,neighbor_ranks,send_cells,recv_cells");
        expect_visible_line("surface.csv",
                            "x,y,nx,ny,pressure,cp,cf,rho,u,v,mach,tag");
        std::ifstream log_input(directory / "stdout.log");
        const std::string log_text{std::istreambuf_iterator<char>(log_input),
                                   std::istreambuf_iterator<char>()};
        expect(log_text.find("OutputSession initialized") != std::string::npos,
               "stdout log was not durable when the checkpoint became visible");
    }
    const auto checkpoint = cfd::read_transient_checkpoint_file(directory / "transient_checkpoint.bin");
    expect(checkpoint.step == 1 && checkpoint.states.size() == 2U &&
               checkpoint.states[1].older[3] == 0.5 && checkpoint.force_history.size() == 1U &&
               checkpoint.initial_global_residual == 7.5 &&
               checkpoint.initial_symmetry_seed_applied &&
               checkpoint.inner_iterations == std::vector<int>({5}),
           "step-1 transient checkpoint did not preserve BDF histories and accepted statistics");
    expect(std::filesystem::is_regular_file(directory / "transient_checkpoint.json"),
           "transient checkpoint manifest is missing");

    // Simulate rows flushed immediately before an interruption but after the
    // durable step-1 checkpoint.  Resume must remove them rather than duplicate
    // or skip accepted physical samples.
    {
        std::ofstream residuals(directory / "residuals.csv", std::ios::app);
        std::ofstream forces(directory / "forces.csv", std::ios::app);
        residuals << "2,0.02,5,1,0.01,0.1,0.2,0.3,0.4,0.0001,0.0002\n";
        forces << "2,0.02,0.02,0.05,0,0.05,0,0.02,0\n";
    }
    OutputSession resumed(test_case(), directory, metadata(), 0,
                          cfd::OutputResumeState{1, 0.01,
                                                 directory / "transient_checkpoint.bin"});
    resumed.append_residual({2, 0.02, 5, 1.0, 0.01, 0.1, 0.2, 0.3, 0.4, 1e-4, 2e-4});
    resumed.append_force({2, 0.02, 0.02, 0.05, 0.0, 0.05, 0.0, 0.02, 0.0});
    resumed.write_final_surface({{0.0, 0.0, 1.0, 0.0, 1.0, 0.1, 0.0, 1.0, 1.0, 0.0, 0.15, "bc-4"}},
                                {2, 0.02});
    resumed.write_final_field_vtk({{4, {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}}, 1.0,
                                    {1.0, 0.0}, 1.0, 0.15, 2.5, 1.0, 0}}, {2, 0.02});
    resumed.write_final_restart({{2, {1.0, 2.0, 3.0, 4.0}}, {9, {4.0, 3.0, 2.0, 1.0}}});
    resumed.complete({"resume test", 1, 1.0, {2, 0.02}, ConvergenceStatus::converged, 1.0,
                      "resumed"});
    std::ifstream residuals(directory / "residuals.csv");
    std::string line;
    int rows = 0;
    while (std::getline(residuals, line)) ++rows;
    expect(rows == 3, "resumed residual history has duplicate or missing rows");
    std::ifstream metadata_input(directory / "metadata.json");
    const auto metadata_json = nlohmann::json::parse(metadata_input);
    expect(metadata_json.at("resumed_from_checkpoint").at("step") == 1,
           "resume provenance was not retained in metadata");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"exact contract, VTK, and restart", test_exact_contract_and_vtk_restart},
        {"invalid status and NaN refusal", test_invalid_status_and_nan_refused},
        {"atomic restart replacement", test_atomic_restart_replacement},
        {"CSV round-trip precision", test_csv_round_trip_precision},
        {"transient checkpoint and history resume", test_transient_checkpoint_round_trip_and_history_resume},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
