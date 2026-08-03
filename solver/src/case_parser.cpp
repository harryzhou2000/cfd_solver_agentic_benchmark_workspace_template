#include "solver/case_config.hpp"

#include <nlohmann/json.hpp>
#include <fmt/core.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace solver {

namespace {

using json = nlohmann::json;

constexpr double kPi = 3.14159265358979323846;

// Get a nested object, returning an empty object if missing.
const json& get_object(const json& j, const char* key) {
    static const json empty = json::object();
    if (j.contains(key) && j[key].is_object()) return j[key];
    return empty;
}

double get_double(const json& j, const char* key, double def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return def;
}

int get_int(const json& j, const char* key, int def) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<int>();
    return def;
}

bool get_bool(const json& j, const char* key, bool def) {
    if (j.contains(key) && j[key].is_boolean()) return j[key].get<bool>();
    return def;
}

std::string get_string(const json& j, const char* key, const std::string& def) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

std::vector<double> get_double_array(const json& j, const char* key, std::vector<double> def) {
    if (j.contains(key) && j[key].is_array()) return j[key].get<std::vector<double>>();
    return def;
}

void parse_mesh(const json& j, CaseConfig& cfg) {
    cfg.mesh.file = get_string(j, "file", "");
    cfg.mesh.format = get_string(j, "format", "CGNS");
    cfg.mesh.dimension = get_int(j, "dimension", 2);
}

void parse_physics(const json& j, CaseConfig& cfg) {
    cfg.physics.equations = get_string(j, "equations", "");
    cfg.physics.mode = get_string(j, "mode", "");
    cfg.physics.reynolds = get_double(j, "reynolds", 1.0);
    cfg.physics.viscosity_model = get_string(j, "viscosity_model", "");
}

void parse_gas(const json& j, CaseConfig& cfg) {
    cfg.gas.model = get_string(j, "model", "");
    cfg.gas.gamma = get_double(j, "gamma", 1.4);
    cfg.gas.R = get_double(j, "R", 1.0);
    cfg.gas.prandtl = get_double(j, "prandtl", 0.72);
}

void parse_freestream(const json& j, CaseConfig& cfg) {
    cfg.freestream.mach = get_double(j, "mach", 0.0);
    cfg.freestream.aoa_degrees = get_double(j, "aoa_degrees", 0.0);
    cfg.freestream.rho = get_double(j, "rho", 1.0);
    cfg.freestream.velocity_magnitude = get_double(j, "velocity_magnitude", 1.0);
    cfg.freestream.pressure = get_double(j, "pressure", 0.0);
}

void parse_reference(const json& j, CaseConfig& cfg) {
    cfg.reference.length = get_double(j, "length", 1.0);
    cfg.reference.area = get_double(j, "area", 1.0);
    cfg.reference.reynolds_length = get_double(j, "reynolds_length", 1.0);
    auto mc = get_double_array(j, "moment_center", {0.0, 0.0});
    cfg.reference.moment_center[0] = mc.size() > 0 ? mc[0] : 0.0;
    cfg.reference.moment_center[1] = mc.size() > 1 ? mc[1] : 0.0;
}

void parse_run_control(const json& j, CaseConfig& cfg) {
    auto& rc = cfg.run_control;
    rc.type = get_string(j, "type", "steady");
    rc.max_steps = get_int(j, "max_steps", 1000);
    rc.residual_reduction_target = get_double(j, "residual_reduction_target", 4.0);
    rc.cfl_initial = get_double(j, "cfl_initial", 1.0);
    rc.cfl_max = get_double(j, "cfl_max", 100.0);
    rc.pseudo_cfl_ramp_steps = get_int(j, "pseudo_cfl_ramp_steps", 1000);
    rc.min_inner_iterations = get_int(j, "min_inner_iterations", 3);
    rc.max_inner_iterations = get_int(j, "max_inner_iterations", 50);
    rc.inner_residual_reduction_target = get_double(j, "inner_residual_reduction_target", 0.01);
    rc.time_integrator = get_string(j, "time_integrator", "");
    rc.time_step = get_double(j, "time_step", 0.0);
    rc.final_time = get_double(j, "final_time", 0.0);
    rc.inner_residual_norm = get_string(j, "inner_residual_norm", "");
    rc.bdf2_history_update = get_string(j, "bdf2_history_update", "");
    rc.rusanov_dissipation_scale = get_double(j, "rusanov_dissipation_scale", 1.0);
}

void parse_outputs(const json& j, CaseConfig& cfg) {
    auto& o = cfg.outputs;
    o.write_final_field = get_bool(j, "write_final_field", true);
    o.write_surface = get_bool(j, "write_surface", true);
    o.write_forces_every = get_int(j, "write_forces_every", 1);
    o.write_residuals_every = get_int(j, "write_residuals_every", 1);
    o.write_field_every_time = get_double(j, "write_field_every_time", 0.0);
    o.wake_visualization = get_string(j, "wake_visualization", "");
    auto clip = get_double_array(j, "recommended_vorticity_clip_range", {0.0, 0.0});
    o.recommended_vorticity_clip_range[0] = clip.size() > 0 ? clip[0] : 0.0;
    o.recommended_vorticity_clip_range[1] = clip.size() > 1 ? clip[1] : 0.0;
}

void derive_freestream(CaseConfig& cfg) {
    const double aoa_rad = cfg.freestream.aoa_degrees * kPi / 180.0;
    cfg.freestream.u_inf = cfg.freestream.velocity_magnitude * std::cos(aoa_rad);
    cfg.freestream.v_inf = cfg.freestream.velocity_magnitude * std::sin(aoa_rad);

    // Nondimensional: speed of sound = velocity / Mach.
    if (cfg.freestream.mach > 0.0) {
        cfg.freestream.speed_of_sound =
            cfg.freestream.velocity_magnitude / cfg.freestream.mach;
        cfg.freestream.temperature =
            cfg.freestream.speed_of_sound * cfg.freestream.speed_of_sound /
            (cfg.gas.gamma * cfg.gas.R);
    }

    if (cfg.physics.mode == "laminar") {
        cfg.freestream.viscosity =
            cfg.freestream.rho * cfg.freestream.velocity_magnitude *
            cfg.reference.reynolds_length / cfg.physics.reynolds;
    }
}

// Resolve the mesh file path relative to the case file's directory.
void resolve_mesh_path(const std::string& case_filepath, CaseConfig& cfg) {
    namespace fs = std::filesystem;
    fs::path mesh_path(cfg.mesh.file);
    if (mesh_path.is_relative() && !cfg.mesh.file.empty()) {
        fs::path case_dir = fs::path(case_filepath).parent_path();
        if (!case_dir.empty()) mesh_path = case_dir / mesh_path;
    }
    cfg.mesh.file = mesh_path.lexically_normal().string();
}

void print_config(const CaseConfig& cfg) {
    fmt::print("---- Case configuration ----\n");
    fmt::print("schema_version: {}\n", cfg.schema_version);
    fmt::print("case_id: {}\n", cfg.case_id);
    fmt::print("description: {}\n", cfg.description);
    fmt::print("mesh.file: {}\n", cfg.mesh.file);
    fmt::print("mesh.format: {}\n", cfg.mesh.format);
    fmt::print("mesh.dimension: {}\n", cfg.mesh.dimension);
    fmt::print("physics.equations: {}\n", cfg.physics.equations);
    fmt::print("physics.mode: {}\n", cfg.physics.mode);
    fmt::print("physics.reynolds: {}\n", cfg.physics.reynolds);
    fmt::print("physics.viscosity_model: {}\n", cfg.physics.viscosity_model);
    fmt::print("gas.model: {}\n", cfg.gas.model);
    fmt::print("gas.gamma: {}\n", cfg.gas.gamma);
    fmt::print("gas.R: {}\n", cfg.gas.R);
    fmt::print("gas.prandtl: {}\n", cfg.gas.prandtl);
    fmt::print("freestream.mach: {}\n", cfg.freestream.mach);
    fmt::print("freestream.aoa_degrees: {}\n", cfg.freestream.aoa_degrees);
    fmt::print("freestream.rho: {}\n", cfg.freestream.rho);
    fmt::print("freestream.velocity_magnitude: {}\n",
               cfg.freestream.velocity_magnitude);
    fmt::print("freestream.pressure: {}\n", cfg.freestream.pressure);
    fmt::print("freestream.u_inf: {}\n", cfg.freestream.u_inf);
    fmt::print("freestream.v_inf: {}\n", cfg.freestream.v_inf);
    fmt::print("freestream.speed_of_sound: {}\n", cfg.freestream.speed_of_sound);
    fmt::print("freestream.temperature: {}\n", cfg.freestream.temperature);
    fmt::print("freestream.viscosity: {}\n", cfg.freestream.viscosity);
    fmt::print("reference.length: {}\n", cfg.reference.length);
    fmt::print("reference.area: {}\n", cfg.reference.area);
    fmt::print("reference.moment_center: ({}, {})\n",
               cfg.reference.moment_center[0], cfg.reference.moment_center[1]);
    fmt::print("reference.reynolds_length: {}\n", cfg.reference.reynolds_length);
    fmt::print("run_control.type: {}\n", cfg.run_control.type);
    fmt::print("run_control.max_steps: {}\n", cfg.run_control.max_steps);
    fmt::print("run_control.cfl_initial: {}\n", cfg.run_control.cfl_initial);
    fmt::print("run_control.cfl_max: {}\n", cfg.run_control.cfl_max);
    fmt::print("outputs.write_final_field: {}\n", cfg.outputs.write_final_field);
    fmt::print("boundary_conditions ({} entries)\n",
               cfg.boundary_conditions.size());
    for (const auto& [name, type] : cfg.boundary_conditions) {
        fmt::print("  {}: {}\n", name, type);
    }
    fmt::print("---- End case configuration ----\n");
}

} // namespace

CaseConfig parse_case_config(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("parse_case_config: cannot open case file: " +
                                 filepath);
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("parse_case_config: JSON parse error in " +
                                 filepath + ": " + e.what());
    }

    CaseConfig cfg;
    cfg.schema_version = get_int(root, "schema_version", 1);
    if (cfg.schema_version != 1) {
        throw std::runtime_error(
            "parse_case_config: unsupported schema_version " +
            std::to_string(cfg.schema_version) + " (expected 1) in " + filepath);
    }
    cfg.case_id = get_string(root, "case_id", "");
    cfg.description = get_string(root, "description", "");

    parse_mesh(get_object(root, "mesh"), cfg);
    parse_physics(get_object(root, "physics"), cfg);
    parse_gas(get_object(root, "gas"), cfg);
    parse_freestream(get_object(root, "freestream"), cfg);
    parse_reference(get_object(root, "reference"), cfg);
    parse_run_control(get_object(root, "run_control"), cfg);
    parse_outputs(get_object(root, "outputs"), cfg);

    if (root.contains("boundary_conditions") &&
        root["boundary_conditions"].is_object()) {
        for (auto it = root["boundary_conditions"].begin();
             it != root["boundary_conditions"].end(); ++it) {
            cfg.boundary_conditions[it.key()] =
                it.value().is_string() ? it.value().get<std::string>()
                                       : it.value().dump();
        }
    }

    derive_freestream(cfg);
    resolve_mesh_path(filepath, cfg);

    print_config(cfg);
    return cfg;
}

} // namespace solver
