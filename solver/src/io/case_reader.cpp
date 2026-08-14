#include "io/case_reader.hpp"
#include <fstream>
#include <cmath>
#include <iostream>

namespace omo {

CaseConfig CaseReader::read(const std::string& case_path) {
    CaseConfig cfg;

    std::ifstream f(case_path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open case file: " + case_path);
    }

    nlohmann::json j;
    f >> j;

    // Schema version check
    int schema = j.value("schema_version", 0);
    if (schema != 1) {
        throw std::runtime_error("Unsupported schema_version: " + std::to_string(schema));
    }

    cfg.case_id = j.value("case_id", "unknown");
    cfg.description = j.value("description", "");

    // Mesh
    auto& mesh = j["mesh"];
    cfg.mesh_file = mesh.value("file", "");
    cfg.mesh_format = mesh.value("format", "CGNS");
    cfg.mesh_dimension = mesh.value("dimension", 2);

    // Physics
    auto& physics = j["physics"];
    cfg.physics_mode = physics.value("mode", "inviscid");
    cfg.reynolds = physics.value("reynolds", -1.0);
    cfg.viscosity_model = physics.value("viscosity_model", "constant");

    // Gas
    auto& gas = j["gas"];
    cfg.gas.gamma = gas.value("gamma", 1.4);
    cfg.gas.R = gas.value("R", 1.0);
    cfg.gas.Pr = gas.value("prandtl", 0.72);

    // Freestream
    parse_freestream(j["freestream"], cfg);

    // Reference
    auto& ref = j["reference"];
    cfg.reference.length = ref.value("length", 1.0);
    cfg.reference.area = ref.value("area", 1.0);
    if (ref.contains("moment_center") && ref["moment_center"].size() >= 2) {
        cfg.reference.moment_center = Vector2(ref["moment_center"][0], ref["moment_center"][1]);
    }
    cfg.reference.reynolds_length = ref.value("reynolds_length", 1.0);

    // Boundary conditions
    for (auto& [bc_name, bc_type] : j["boundary_conditions"].items()) {
        cfg.boundary_conditions[bc_name] = bc_type.get<std::string>();
    }

    // Run control
    parse_run_control(j["run_control"], cfg);

    // Outputs
    if (j.contains("outputs")) {
        parse_outputs(j["outputs"], cfg);
    }

    std::cout << "[CaseReader] Loaded case: " << cfg.case_id
              << " mode=" << cfg.physics_mode
              << " M=" << cfg.freestream.mach
              << " Re=" << cfg.reynolds << std::endl;

    return cfg;
}

void CaseReader::parse_freestream(const nlohmann::json& j, CaseConfig& cfg) {
    cfg.freestream.mach = j.value("mach", 0.1);
    Real aoa_deg = j.value("aoa_degrees", 0.0);
    cfg.freestream.aoa_deg = aoa_deg;
    cfg.freestream.rho = j.value("rho", 1.0);

    Real aoa = aoa_deg * M_PI / 180.0;
    Real Vmag = j.value("velocity_magnitude", 1.0);
    cfg.freestream.u_inf = Vmag * std::cos(aoa);
    cfg.freestream.v_inf = Vmag * std::sin(aoa);
    cfg.freestream.p_inf = j.value("pressure", 1.0);

    Real gamma = cfg.gas.gamma;
    Real R = cfg.gas.R;
    cfg.freestream.a_inf = std::sqrt(gamma * cfg.freestream.p_inf / cfg.freestream.rho);
    cfg.freestream.T_inf = cfg.freestream.p_inf / (cfg.freestream.rho * R);
}

void CaseReader::parse_run_control(const nlohmann::json& j, CaseConfig& cfg) {
    auto& rc = cfg.run_control;
    std::string type = j.value("type", "steady");
    rc.type = (type == "transient") ? RunControl::TRANSIENT : RunControl::STEADY;

    rc.max_steps = j.value("max_steps", 20000);
    rc.residual_reduction_target = j.value("residual_reduction_target", 4.0);
    rc.cfl_initial = j.value("cfl_initial", 1.0);
    rc.cfl_max = j.value("cfl_max", 100.0);
    rc.pseudo_cfl_ramp_steps = j.value("pseudo_cfl_ramp_steps", 2000);
    rc.min_inner_iterations = j.value("min_inner_iterations", 3);
    rc.max_inner_iterations = j.value("max_inner_iterations", 50);
    rc.inner_residual_reduction_target = j.value("inner_residual_reduction_target", 0.01);

    if (rc.type == RunControl::TRANSIENT) {
        rc.time_integrator = j.value("time_integrator", "bdf2_or_trapezoidal");
        rc.time_step = j.value("time_step", 0.01);
        rc.final_time = j.value("final_time", 300.0);
        rc.inner_residual_norm = j.value("inner_residual_norm", "total_spatial_plus_physical_time");
        rc.bdf2_history_update = j.value("bdf2_history_update", "after_inner_convergence");
        rc.rusanov_dissipation_scale = j.value("rusanov_dissipation_scale", 1.0);
    }
}

void CaseReader::parse_outputs(const nlohmann::json& j, CaseConfig& cfg) {
    auto& out = cfg.outputs;
    out.write_final_field = j.value("write_final_field", true);
    out.write_surface = j.value("write_surface", true);
    out.write_forces_every = j.value("write_forces_every", 1);
    out.write_residuals_every = j.value("write_residuals_every", 1);
    out.write_field_every_time = j.value("write_field_every_time", -1.0);
    out.wake_visualization = j.value("wake_visualization", "");
    if (j.contains("recommended_vorticity_clip_range") && j["recommended_vorticity_clip_range"].size() >= 2) {
        out.recommended_vorticity_clip = {
            j["recommended_vorticity_clip_range"][0],
            j["recommended_vorticity_clip_range"][1]
        };
    }
}

} // namespace omo
