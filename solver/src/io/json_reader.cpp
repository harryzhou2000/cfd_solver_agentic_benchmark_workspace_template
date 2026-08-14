#include "json_reader.hpp"
#include <cmath>
#include <stdexcept>

CaseConfig parse_case_json(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) throw std::runtime_error("Cannot open case file: " + filepath);

    json j;
    f >> j;

    CaseConfig cfg;

    cfg.case_id        = j["case_id"].get<std::string>();
    cfg.schema_version = j["schema_version"].get<int>();

    // Mesh
    auto& mesh         = j["mesh"];
    cfg.mesh_file      = mesh["file"].get<std::string>();

    // Physics
    auto& phys = j["physics"];
    std::string mode_str = phys["mode"].get<std::string>();
    cfg.physics_mode = (mode_str == "inviscid") ? PhysicsMode::Inviscid : PhysicsMode::Laminar;
    if (cfg.physics_mode == PhysicsMode::Laminar) {
        cfg.reynolds = phys.value("reynolds", 1.0);
        cfg.viscosity_model = phys.value("viscosity_model", std::string("constant"));
    }

    // Gas
    auto& gas       = j["gas"];
    cfg.gamma = gas["gamma"].get<Real>();
    cfg.R_gas = gas["R"].get<Real>();
    cfg.prandtl = gas["prandtl"].get<Real>();

    // Freestream
    auto& fs = j["freestream"];
    cfg.mach              = fs["mach"].get<Real>();
    cfg.aoa_degrees       = fs.value("aoa_degrees", 0.0);
    cfg.rho_inf           = fs["rho"].get<Real>();
    cfg.velocity_magnitude = fs["velocity_magnitude"].get<Real>();
    cfg.p_inf             = fs["pressure"].get<Real>();

    Real aoa_rad = cfg.aoa_degrees * M_PI / 180.0;
    cfg.u_inf = cfg.velocity_magnitude * std::cos(aoa_rad);
    cfg.v_inf = cfg.velocity_magnitude * std::sin(aoa_rad);

    // Reference
    auto& ref            = j["reference"];
    cfg.ref_length       = ref["length"].get<Real>();
    cfg.ref_area         = ref["area"].get<Real>();
    auto& mc             = ref["moment_center"];
    cfg.moment_center    = Vec2(mc[0].get<Real>(), mc[1].get<Real>());
    cfg.ref_reynolds_length = ref["reynolds_length"].get<Real>();

    // Boundary conditions
    for (auto& [family, bc_str] : j["boundary_conditions"].items()) {
        BCType bc = bc_type_from_string(bc_str.get<std::string>());
        cfg.bc_map.emplace_back(family, bc);
    }

    // Numerics required
    if (j.contains("numerics_required")) {
        auto& num = j["numerics_required"];
        cfg.spatial_order      = num.value("spatial_order", 2);
        cfg.inviscid_flux_type = num.value("inviscid_flux", std::string("approximate_riemann"));
        cfg.main_time_method   = num.value("main_time_method", std::string("implicit"));
        cfg.transient_order    = num.value("transient_order", 0);
    }

    // Run control
    auto& rc             = j["run_control"];
    std::string rt       = rc["type"].get<std::string>();
    cfg.run_type = (rt == "transient") ? RunType::Transient : RunType::Steady;

    cfg.max_steps             = rc["max_steps"].get<int>();
    cfg.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
    cfg.cfl_initial           = rc.value("cfl_initial", 1.0);
    cfg.cfl_max               = rc.value("cfl_max", 100.0);
    cfg.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 2000);
    cfg.min_inner_iterations  = rc.value("min_inner_iterations", 3);
    cfg.max_inner_iterations  = rc.value("max_inner_iterations", 50);
    cfg.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);

    if (cfg.run_type == RunType::Transient) {
        cfg.time_step         = rc.value("time_step", 0.01);
        cfg.final_time        = rc.value("final_time", 0.0);
        cfg.time_integrator   = rc.value("time_integrator", std::string("bdf2_or_trapezoidal"));
        cfg.bdf2_history_update = rc.value("bdf2_history_update", std::string("after_inner_convergence"));
        cfg.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);
        cfg.inner_residual_norm = rc.value("inner_residual_norm", std::string("total_spatial_plus_physical_time"));
    }

    // Output
    auto& out            = j["outputs"];
    cfg.write_final_field = out.value("write_final_field", true);
    cfg.write_surface     = out.value("write_surface", true);
    cfg.write_forces_every    = out.value("write_forces_every", 1);
    cfg.write_residuals_every = out.value("write_residuals_every", 1);
    cfg.write_field_every_time = out.value("write_field_every_time", 0.0);

    return cfg;
}
