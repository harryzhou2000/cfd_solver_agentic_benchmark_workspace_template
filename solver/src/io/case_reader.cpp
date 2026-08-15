#include "mesh/mesh_types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <cmath>

namespace cfd {

using json = nlohmann::json;

BcType bc_type_from_string(const std::string& s) {
    if (s == "farfield") return BcType::Farfield;
    if (s == "slip_wall") return BcType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
    return BcType::Unsupported;
}

CaseConfig parse_case_config(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open case file: " + json_path);
    }
    json j = json::parse(f);
    CaseConfig cfg;

    cfg.schema_version = j.value("schema_version", 1);
    cfg.case_id = j.at("case_id").get<std::string>();
    cfg.description = j.value("description", "");

    // Mesh
    if (j.contains("mesh")) {
        auto& m = j["mesh"];
        cfg.mesh_file = m.at("file").get<std::string>();
    }

    // Physics
    if (j.contains("physics")) {
        auto& p = j["physics"];
        cfg.physics_mode = p.value("mode", "inviscid");
        cfg.reynolds_number = p.value("reynolds", 0.0);
        cfg.viscosity_model = p.value("viscosity_model", "none");
    }

    // Gas
    if (j.contains("gas")) {
        auto& g = j["gas"];
        cfg.gas.gamma = g.value("gamma", 1.4);
        cfg.gas.R = g.value("R", 1.0);
        cfg.gas.prandtl = g.value("prandtl", 0.72);
    }

    // Freestream
    if (j.contains("freestream")) {
        auto& fs = j["freestream"];
        cfg.freestream.mach = fs.at("mach").get<Real>();
        cfg.freestream.aoa_deg = fs.value("aoa_degrees", 0.0);
        cfg.freestream.rho = fs.at("rho").get<Real>();
        // NOTE: pressure must be read BEFORE deriving the velocity from the
        // Mach number: the speed of sound depends on the case pressure.
        cfg.freestream.pressure = fs.at("pressure").get<Real>();
        Real aoa_rad = cfg.freestream.aoa_deg * M_PI / 180.0;
        // Derive velocity magnitude from Mach number for consistency
        Real speed_of_sound = std::sqrt(cfg.gas.gamma * cfg.freestream.pressure /
                                         cfg.freestream.rho);
        Real vmag = cfg.freestream.mach * speed_of_sound;
        cfg.freestream.u = vmag * std::cos(aoa_rad);
        cfg.freestream.v = vmag * std::sin(aoa_rad);
        cfg.freestream.temperature = cfg.freestream.pressure /
            (cfg.freestream.rho * cfg.gas.R);
    }

    // Reference quantities
    if (j.contains("reference")) {
        auto& ref = j["reference"];
        cfg.reference.length = ref.value("length", 1.0);
        cfg.reference.area = ref.value("area", 1.0);
        if (ref.contains("moment_center")) {
            auto& mc = ref["moment_center"];
            cfg.reference.moment_center = Vec2{mc[0].get<Real>(), mc[1].get<Real>()};
        }
        cfg.reference.reynolds_length = ref.value("reynolds_length", 1.0);
    }

    // Boundary condition mappings
    if (j.contains("boundary_conditions")) {
        for (auto& [fam, bc_type_str] : j["boundary_conditions"].items()) {
            cfg.bc_mappings.push_back({fam, bc_type_from_string(bc_type_str.get<std::string>())});
        }
    }

    // Run control
    if (j.contains("run_control")) {
        auto& rc = j["run_control"];
        cfg.run_control.type = rc.value("type", "steady");
        cfg.run_control.max_steps = rc.value("max_steps", 0);
        cfg.run_control.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
        cfg.run_control.cfl_initial = rc.value("cfl_initial", 1.0);
        cfg.run_control.cfl_max = rc.value("cfl_max", 100.0);
        cfg.run_control.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 0);
        cfg.run_control.min_inner_iterations = rc.value("min_inner_iterations", 3);
        cfg.run_control.max_inner_iterations = rc.value("max_inner_iterations", 50);
        cfg.run_control.inner_residual_reduction_target =
            rc.value("inner_residual_reduction_target", 0.01);
        cfg.run_control.time_integrator = rc.value("time_integrator", "");
        cfg.run_control.time_step = rc.value("time_step", 0.01);
        cfg.run_control.final_time = rc.value("final_time", 300.0);
        cfg.run_control.inner_residual_norm =
            rc.value("inner_residual_norm", "");
        cfg.run_control.bdf2_history_update =
            rc.value("bdf2_history_update", "");
        cfg.run_control.rusanov_dissipation_scale =
            rc.value("rusanov_dissipation_scale", 1.0);
    }

    // Output config
    if (j.contains("outputs")) {
        auto& out = j["outputs"];
        cfg.output.write_final_field = out.value("write_final_field", true);
        cfg.output.write_surface = out.value("write_surface", true);
        cfg.output.write_forces_every = out.value("write_forces_every", 1);
        cfg.output.write_residuals_every = out.value("write_residuals_every", 1);
        cfg.output.write_field_every_time = out.value("write_field_every_time", 1.0);
        cfg.output.wake_visualization = out.value("wake_visualization", "");
        if (out.contains("recommended_vorticity_clip_range")) {
            auto& vr = out["recommended_vorticity_clip_range"];
            cfg.output.recommended_vorticity_clip_range =
                Vec2{vr[0].get<Real>(), vr[1].get<Real>()};
        }
    }

    return cfg;
}

} // namespace cfd
