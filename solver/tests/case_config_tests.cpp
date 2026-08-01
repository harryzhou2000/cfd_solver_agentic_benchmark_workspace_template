#include "cfd/case_config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using cfd::CaseConfig;
using cfd::PhysicsMode;
using cfd::RunType;

struct ExpectedCase {
    const char* filename;
    const char* case_id;
    const char* mesh;
    PhysicsMode mode;
    RunType run_type;
    int max_steps;
    double final_time;
};

constexpr ExpectedCase kExpectedCases[] = {
    {"naca0012_m015_inviscid.json", "naca0012_m015_inviscid", "NACA0012_H2.cgns", PhysicsMode::inviscid, RunType::steady, 20000, 0.0},
    {"naca0012_m080_inviscid.json", "naca0012_m080_inviscid", "NACA0012_H2.cgns", PhysicsMode::inviscid, RunType::steady, 30000, 0.0},
    {"naca0012_m200_inviscid.json", "naca0012_m200_inviscid", "NACA0012_H2.cgns", PhysicsMode::inviscid, RunType::steady, 40000, 0.0},
    {"naca0012_m015_laminar_re5000.json", "naca0012_m015_laminar_re5000", "NACA0012_H2.cgns", PhysicsMode::laminar, RunType::steady, 40000, 0.0},
    {"naca0012_m080_laminar_re5000.json", "naca0012_m080_laminar_re5000", "NACA0012_H2.cgns", PhysicsMode::laminar, RunType::steady, 40000, 0.0},
    {"naca0012_m200_laminar_re5000.json", "naca0012_m200_laminar_re5000", "NACA0012_H2.cgns", PhysicsMode::laminar, RunType::steady, 50000, 0.0},
    {"cylinder_m010_laminar_re20.json", "cylinder_m010_laminar_re20", "CylinderB1.cgns", PhysicsMode::laminar, RunType::steady, 30000, 0.0},
    {"cylinder_m010_laminar_re200.json", "cylinder_m010_laminar_re200", "CylinderB1.cgns", PhysicsMode::laminar, RunType::transient, 0, 300.0},
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void test_all_supplied_cases() {
    const std::filesystem::path cases_dir{CFD_BENCHMARK_CASES_DIR};
    for (const auto& expected : kExpectedCases) {
        const CaseConfig config = cfd::load_case_config(cases_dir / expected.filename);
        expect(config.schema_version == 1, expected.case_id + std::string(": schema version"));
        expect(config.case_id == expected.case_id, expected.case_id + std::string(": case id"));
        expect(config.mesh.resolved_file.filename() == expected.mesh,
               expected.case_id + std::string(": resolved mesh filename"));
        expect(std::filesystem::exists(config.mesh.resolved_file),
               expected.case_id + std::string(": resolved mesh exists"));
        expect(config.physics.mode == expected.mode, expected.case_id + std::string(": physics mode"));
        expect(config.run_control.type == expected.run_type, expected.case_id + std::string(": run type"));
        expect(config.boundary_conditions.size() == 2U, expected.case_id + std::string(": boundary map"));
        expect(config.outputs.write_final_field && config.outputs.write_surface,
               expected.case_id + std::string(": mandatory outputs"));
        expect(config.numerics_required.spatial_order == 2, expected.case_id + std::string(": spatial order"));

        if (expected.run_type == RunType::steady) {
            expect(config.run_control.max_steps.has_value(), expected.case_id + std::string(": max_steps"));
            expect(*config.run_control.max_steps == expected.max_steps,
                   expected.case_id + std::string(": expected max_steps"));
        } else {
            expect(config.run_control.time_step.has_value() && config.run_control.final_time.has_value(),
                   expected.case_id + std::string(": transient controls"));
            expect(std::abs(*config.run_control.time_step - 0.01) < 1e-12,
                   expected.case_id + std::string(": time step"));
            expect(std::abs(*config.run_control.final_time - expected.final_time) < 1e-12,
                   expected.case_id + std::string(": final time"));
            expect(config.run_control.min_inner_iterations == 5 &&
                       config.run_control.max_inner_iterations == 1000,
                   expected.case_id + std::string(": inner iteration range"));
            expect(config.outputs.recommended_vorticity_clip_range.has_value(),
                   expected.case_id + std::string(": wake settings"));
        }
    }
}

std::filesystem::path write_temporary_case(const std::string& contents) {
    const auto directory = std::filesystem::temp_directory_path() / "cfd_case_config_tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / "bad_case.json";
    std::ofstream output(path);
    output << contents;
    return path;
}

void expect_case_error(const std::string& contents, const std::string& expected_fragment) {
    const auto path = write_temporary_case(contents);
    try {
        static_cast<void>(cfd::load_case_config(path));
        fail("expected CaseConfigError containing: " + expected_fragment);
    } catch (const cfd::CaseConfigError& error) {
        expect(std::string(error.what()).find(expected_fragment) != std::string::npos,
               "wrong parser error: " + std::string(error.what()));
    }
}

void test_bad_input_behavior() {
    expect_case_error("{ not valid json", "Cannot parse case file");
    expect_case_error("{\"schema_version\":2}", "unsupported schema_version");
    expect_case_error("{\"schema_version\":1}", "required field 'case_id' is missing");
    expect_case_error(
        "{\"schema_version\":1,\"case_id\":\"bad\",\"description\":\"bad\","
        "\"mesh\":{\"file\":\"missing.cgns\",\"format\":\"CGNS\",\"dimension\":2}}",
        "mesh.file resolves to missing file");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"all supplied schema-v1 cases", test_all_supplied_cases},
        {"bad input and schema rejection", test_bad_input_behavior},
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
