#include "case_config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("case file error: " + msg); }

template <typename T>
T get_or(const json& j, const char* key, T def) {
    if (j.contains(key)) return j.at(key).get<T>();
    return def;
}

std::string resolve_path(const std::string& case_file, const std::string& rel) {
    namespace fs = std::filesystem;
    fs::path p(rel);
    if (p.is_absolute()) return p.lexically_normal().string();
    fs::path base = fs::path(case_file).parent_path();
    return (base / p).lexically_normal().string();
}
} // namespace

double CaseConfig::viscosity() const {
    if (physics_mode != "laminar") return 0.0;
    if (reynolds <= 0.0) fail("laminar case requires positive reynolds number");
    return freestream.rho * freestream.vel_mag * reference.reynolds_length / reynolds;
}

CaseConfig load_case(const std::string& path) {
    std::ifstream in(path);
    if (!in) fail("cannot open case file: " + path);
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        fail(std::string("JSON parse error: ") + e.what());
    }

    CaseConfig c;
    c.schema_version = get_or<int>(j, "schema_version", 1);
    if (c.schema_version != 1)
        fail("unsupported schema_version " + std::to_string(c.schema_version) + " (only 1 supported)");
    c.case_id = get_or<std::string>(j, "case_id", "");
    if (c.case_id.empty()) fail("missing case_id");
    c.description = get_or<std::string>(j, "description", "");

    if (!j.contains("mesh") || !j["mesh"].contains("file")) fail("missing mesh.file");
    std::string fmt = get_or<std::string>(j["mesh"], "format", "CGNS");
    if (fmt != "CGNS" && fmt != "cgns") fail("unsupported mesh format: " + fmt);
    c.mesh_file = resolve_path(path, j["mesh"]["file"].get<std::string>());

    if (!j.contains("physics")) fail("missing physics");
    c.physics_mode = get_or<std::string>(j["physics"], "mode", "");
    if (c.physics_mode != "inviscid" && c.physics_mode != "laminar")
        fail("unsupported physics.mode: " + c.physics_mode);
    c.reynolds = get_or<double>(j["physics"], "reynolds", 0.0);
    c.viscosity_model = get_or<std::string>(j["physics"], "viscosity_model", "constant");
    if (c.physics_mode == "laminar" && c.viscosity_model != "constant")
        fail("unsupported viscosity_model: " + c.viscosity_model);

    if (j.contains("gas")) {
        c.gas.gamma = get_or<double>(j["gas"], "gamma", 1.4);
        c.gas.R = get_or<double>(j["gas"], "R", 1.0);
        c.gas.prandtl = get_or<double>(j["gas"], "prandtl", 0.72);
    }
    if (j.contains("freestream")) {
        const json& f = j["freestream"];
        c.freestream.mach = get_or<double>(f, "mach", 0.0);
        c.freestream.aoa_deg = get_or<double>(f, "aoa_degrees", 0.0);
        c.freestream.rho = get_or<double>(f, "rho", 1.0);
        c.freestream.vel_mag = get_or<double>(f, "velocity_magnitude", 1.0);
        c.freestream.pressure = get_or<double>(f, "pressure", 1.0);
    }
    const double aoa = c.freestream.aoa_deg * M_PI / 180.0;
    c.freestream.u = c.freestream.vel_mag * std::cos(aoa);
    c.freestream.v = c.freestream.vel_mag * std::sin(aoa);

    if (j.contains("reference")) {
        const json& r = j["reference"];
        c.reference.length = get_or<double>(r, "length", 1.0);
        c.reference.area = get_or<double>(r, "area", 1.0);
        c.reference.reynolds_length = get_or<double>(r, "reynolds_length", c.reference.length);
        if (r.contains("moment_center")) {
            c.reference.moment_center[0] = r["moment_center"][0].get<double>();
            c.reference.moment_center[1] = r["moment_center"][1].get<double>();
        }
    }

    if (!j.contains("boundary_conditions")) fail("missing boundary_conditions");
    for (auto& [k, v] : j["boundary_conditions"].items()) {
        std::string t = v.get<std::string>();
        if (t != "farfield" && t != "slip_wall" && t != "no_slip_adiabatic_wall")
            fail("unsupported boundary condition type: " + t);
        c.boundary_conditions[k] = t;
    }

    if (j.contains("run_control")) {
        const json& rc = j["run_control"];
        auto& r = c.run_control;
        r.type = get_or<std::string>(rc, "type", "steady");
        if (r.type != "steady" && r.type != "transient") fail("unsupported run_control.type: " + r.type);
        r.max_steps = get_or<long>(rc, "max_steps", r.max_steps);
        r.residual_reduction_target = get_or<double>(rc, "residual_reduction_target", r.residual_reduction_target);
        r.cfl_initial = get_or<double>(rc, "cfl_initial", r.cfl_initial);
        r.cfl_max = get_or<double>(rc, "cfl_max", r.cfl_max);
        r.pseudo_cfl_ramp_steps = get_or<long>(rc, "pseudo_cfl_ramp_steps", r.pseudo_cfl_ramp_steps);
        r.min_inner_iterations = get_or<int>(rc, "min_inner_iterations", r.min_inner_iterations);
        r.max_inner_iterations = get_or<int>(rc, "max_inner_iterations", r.max_inner_iterations);
        r.inner_residual_reduction_target =
            get_or<double>(rc, "inner_residual_reduction_target", r.inner_residual_reduction_target);
        r.time_integrator = get_or<std::string>(rc, "time_integrator", r.time_integrator);
        r.time_step = get_or<double>(rc, "time_step", r.time_step);
        r.final_time = get_or<double>(rc, "final_time", r.final_time);
        r.rusanov_dissipation_scale = get_or<double>(rc, "rusanov_dissipation_scale", r.rusanov_dissipation_scale);
    }

    if (j.contains("outputs")) {
        const json& o = j["outputs"];
        c.outputs.write_final_field = get_or<bool>(o, "write_final_field", true);
        c.outputs.write_surface = get_or<bool>(o, "write_surface", true);
        c.outputs.write_forces_every = get_or<int>(o, "write_forces_every", 1);
        c.outputs.write_residuals_every = get_or<int>(o, "write_residuals_every", 1);
        c.outputs.write_field_every_time = get_or<double>(o, "write_field_every_time", 0.0);
    }
    return c;
}
