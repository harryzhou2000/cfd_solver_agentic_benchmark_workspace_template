// JSON case parsing (nlohmann/json) for the benchmark case-file schema:
// mesh, physics, gas, freestream, reference, boundary_conditions,
// numerics_required, run_control, outputs.

#include "config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {

namespace {

// Supported case-file schema version.
constexpr int kSchemaVersion = 1;

template <typename T>
T get_or(const nlohmann::json& j, const char* key, T fallback) {
    auto it = j.find(key);
    return (it != j.end()) ? it->get<T>() : fallback;
}

FreestreamParams parse_freestream(const nlohmann::json& j) {
    FreestreamParams f;
    f.mach = get_or(j, "mach", 0.0);
    f.alpha = get_or(j, "aoa_degrees", get_or(j, "alpha", 0.0));
    f.density = get_or(j, "rho", get_or(j, "density", 1.0));
    f.velocity = get_or(j, "velocity_magnitude",
                        get_or(j, "velocity", 1.0));
    f.pressure = get_or(j, "pressure", 1.0);
    f.temperature = get_or(j, "temperature", 1.0);
    return f;
}

RunControlParams parse_run_control(const nlohmann::json& j) {
    RunControlParams c;
    c.type = get_or(j, "type", std::string("steady"));
    c.time_integrator = get_or(j, "time_integrator", std::string("none"));
    c.time_step = get_or(j, "time_step", 0.0);
    c.final_time = get_or(j, "final_time", 0.0);
    c.rusanov_dissipation_scale =
        get_or(j, "rusanov_dissipation_scale", 1.0);
    c.inner_residual_norm =
        get_or(j, "inner_residual_norm", std::string("none"));
    c.bdf2_history_update =
        get_or(j, "bdf2_history_update", std::string("none"));

    c.max_iterations = get_or(j, "max_steps",
                              get_or(j, "max_iterations", 1000));
    c.cfl = get_or(j, "cfl_initial", get_or(j, "cfl", 1.0));
    c.cfl_max = get_or(j, "cfl_max", 1.0e6);
    c.cfl_ramp_steps = get_or(j, "pseudo_cfl_ramp_steps", 0);
    c.residual_reduction_target =
        get_or(j, "residual_reduction_target", 0.0);
    c.residual_target = get_or(j, "residual_target", 1.0e-12);
    // Inner-iteration bounds: 3 minimum / 50 maximum by default (steady);
    // transient case files set 5 / up to 1000 explicitly.
    c.min_inner_iterations = get_or(j, "min_inner_iterations", 3);
    c.max_inner_iterations = get_or(j, "max_inner_iterations", 50);
    c.inner_residual_reduction_target =
        get_or(j, "inner_residual_reduction_target", 0.0);
    c.output_interval = get_or(j, "output_interval",
                               get_or(j, "write_residuals_every", 100));
    c.output_dir = get_or(j, "output_dir", std::string("results"));
    c.n_stages = get_or(j, "n_stages", 1);
    return c;
}

}  // namespace

CaseConfig parse_case_string(const std::string& json_text) {
    const nlohmann::json root = nlohmann::json::parse(json_text);

    // FIX 12: schema version guard.
    const int schema_version = get_or(root, "schema_version", 0);
    if (schema_version != kSchemaVersion) {
        throw std::runtime_error(
            "unsupported case schema_version " +
            std::to_string(schema_version) + " (expected " +
            std::to_string(kSchemaVersion) + ")");
    }

    CaseConfig cfg;
    cfg.case_id = get_or(root, "case_id", std::string(""));
    cfg.description = get_or(root, "description", std::string(""));

    const nlohmann::json& mesh =
        root.contains("mesh") ? root["mesh"] : nlohmann::json::object();
    cfg.mesh_file = get_or(mesh, "file", get_or(root, "mesh_file", std::string("")));
    cfg.mesh_format = get_or(mesh, "format", std::string("CGNS"));

    cfg.output_dir = get_or(root, "output_dir", std::string("results"));

    const nlohmann::json& phys =
        root.contains("physics") ? root["physics"] : nlohmann::json::object();
    cfg.equations = get_or(phys, "equations", std::string("compressible_navier_stokes"));
    cfg.mode = get_or(phys, "mode", std::string("inviscid"));
    cfg.reynolds = get_or(phys, "reynolds", 0.0);
    cfg.viscosity_model = get_or(phys, "viscosity_model", std::string("constant"));

    const nlohmann::json& num =
        root.contains("numerics_required")
            ? root["numerics_required"]
            : nlohmann::json::object();
    cfg.spatial_order = get_or(num, "spatial_order", 2);
    cfg.inviscid_flux = get_or(num, "inviscid_flux", std::string("approximate_riemann"));
    cfg.main_time_method = get_or(num, "main_time_method", std::string("implicit"));

    const nlohmann::json& fs =
        root.contains("freestream") ? root["freestream"] : nlohmann::json::object();
    cfg.freestream = parse_freestream(fs);

    const nlohmann::json& ctrl =
        root.contains("run_control") ? root["run_control"] : nlohmann::json::object();
    cfg.control = parse_run_control(ctrl);

    const nlohmann::json& out =
        root.contains("outputs") ? root["outputs"] : nlohmann::json::object();
    cfg.outputs.write_final_field = get_or(out, "write_final_field", true);
    cfg.outputs.write_surface = get_or(out, "write_surface", true);
    cfg.outputs.write_forces_every = get_or(out, "write_forces_every", 1);
    cfg.outputs.write_residuals_every = get_or(out, "write_residuals_every", 1);

    const nlohmann::json& ref =
        root.contains("reference") ? root["reference"] : nlohmann::json::object();
    cfg.reference.length = get_or(ref, "length", 1.0);
    cfg.reference.area = get_or(ref, "area", 1.0);
    cfg.reference.reynolds_length = get_or(ref, "reynolds_length", 1.0);
    if (ref.contains("moment_center") && ref["moment_center"].is_array() &&
        ref["moment_center"].size() >= 2) {
        cfg.reference.moment_x = ref["moment_center"][0].get<double>();
        cfg.reference.moment_y = ref["moment_center"][1].get<double>();
    }

    const nlohmann::json& g =
        root.contains("gas") ? root["gas"] : nlohmann::json::object();
    cfg.gas.gamma = get_or(g, "gamma", 1.4);
    cfg.gas.R = get_or(g, "R", 1.0);
    cfg.gas.Pr = get_or(g, "prandtl", get_or(g, "Pr", 0.72));

    if (root.contains("boundary_conditions") &&
        root["boundary_conditions"].is_object()) {
        for (auto it = root["boundary_conditions"].begin();
             it != root["boundary_conditions"].end(); ++it) {
            cfg.boundary_conditions[it.key()] = it.value().get<std::string>();
        }
    }

    return cfg;
}

CaseConfig parse_case_file(const std::string& filename) {
    std::ifstream in(filename);
    if (!in.is_open()) {
        throw std::runtime_error("cannot open case file: " + filename);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parse_case_string(buffer.str());
}

}  // namespace cfd
