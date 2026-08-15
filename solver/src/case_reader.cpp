/// @file case_reader.cpp
/// Implementation of JSON case-file parsing.

#include "case_reader.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace cfd {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

PhysicsMode parse_physics_mode(const std::string& mode) {
    if (mode == "inviscid") return PhysicsMode::Inviscid;
    if (mode == "laminar") return PhysicsMode::Laminar;
    throw std::runtime_error("Unknown physics mode: '" + mode + "' (expected 'inviscid' or 'laminar')");
}

RunType parse_run_type(const std::string& type) {
    if (type == "steady") return RunType::Steady;
    if (type == "transient") return RunType::Transient;
    throw std::runtime_error("Unknown run_control type: '" + type + "' (expected 'steady' or 'transient')");
}

TimeIntegrator parse_time_integrator(const std::string& name) {
    if (name == "bdf2" || name == "bdf2_or_trapezoidal") return TimeIntegrator::BDF2;
    if (name == "trapezoidal" || name == "crank_nicolson") return TimeIntegrator::Trapezoidal;
    throw std::runtime_error("Unknown time integrator: '" + name + "'");
}

} // namespace

CaseConfig read_case(const std::string& case_file_path) {
    std::ifstream in(case_file_path);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open case file: " + case_file_path);
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error in '" + case_file_path + "': " + e.what());
    }

    CaseConfig cfg;

    // --- Schema version ----------------------------------------------------
    cfg.schema_version = j.value("schema_version", 1);
    if (cfg.schema_version != 1) {
        throw std::runtime_error("Unsupported schema_version " + std::to_string(cfg.schema_version) +
                                 " in '" + case_file_path + "' (only version 1 is supported)");
    }

    cfg.case_id = j.value("case_id", "");
    cfg.description = j.value("description", "");

    // --- Mesh ---------------------------------------------------------------
    {
        const auto& m = j.at("mesh");
        cfg.mesh.file = m.at("file").get<std::string>();
        cfg.mesh.format = m.value("format", "CGNS");
        cfg.mesh.dimension = m.value("dimension", 2);

        // Resolve relative paths against the case file directory.
        std::filesystem::path mesh_path(cfg.mesh.file);
        if (mesh_path.is_relative()) {
            mesh_path = std::filesystem::path(case_file_path).parent_path() / mesh_path;
        }
        cfg.mesh.file = std::filesystem::weakly_canonical(mesh_path).string();
    }

    // --- Physics -------------------------------------------------------------
    {
        const auto& p = j.at("physics");
        cfg.physics.mode = parse_physics_mode(p.value("mode", "inviscid"));
        cfg.physics.reynolds = p.value("reynolds", 0.0);
        cfg.physics.viscosity_model = p.value("viscosity_model", "constant");
    }

    // --- Gas -----------------------------------------------------------------
    {
        const auto& g = j.at("gas");
        cfg.gas.gamma = g.value("gamma", 1.4);
        cfg.gas.R = g.value("R", 1.0);
        cfg.gas.prandtl = g.value("prandtl", 0.72);
    }

    // --- Reference -------------------------------------------------------------
    // Parsed BEFORE freestream: the freestream viscosity derivation needs
    // reference.reynolds_length (l_ref) and must not read uninitialized
    // struct fields.
    {
        const auto& r = j.at("reference");
        cfg.reference.length = r.value("length", 1.0);
        cfg.reference.area = r.value("area", 1.0);
        cfg.reference.reynolds_length = r.value("reynolds_length", cfg.reference.length);
        if (r.contains("moment_center") && r["moment_center"].is_array() &&
            r["moment_center"].size() >= 2) {
            cfg.reference.moment_center << r["moment_center"][0].get<Real>(),
                                           r["moment_center"][1].get<Real>();
        } else {
            cfg.reference.moment_center << 0.0, 0.0;
        }
    }

    // --- Freestream + derived quantities --------------------------------------
    {
        const auto& fs = j.at("freestream");
        cfg.freestream.mach = fs.value("mach", 0.0);
        cfg.freestream.aoa_degrees = fs.value("aoa_degrees", 0.0);
        cfg.freestream.rho = fs.value("rho", 1.0);
        cfg.freestream.velocity_magnitude = fs.value("velocity_magnitude", 0.0);
        cfg.freestream.pressure = fs.value("pressure", 0.0);

        cfg.freestream.aoa_radians = cfg.freestream.aoa_degrees * kDegToRad;
        const Real cos_a = std::cos(cfg.freestream.aoa_radians);
        const Real sin_a = std::sin(cfg.freestream.aoa_radians);
        cfg.freestream.velocity << cfg.freestream.velocity_magnitude * cos_a,
                                  cfg.freestream.velocity_magnitude * sin_a;

        cfg.freestream.temperature = cfg.freestream.pressure / (cfg.freestream.rho * cfg.gas.R);
        cfg.freestream.speed_of_sound =
            std::sqrt(cfg.gas.gamma * cfg.freestream.pressure / cfg.freestream.rho);

        // Viscosity: mu = rho_inf * U_inf * L_ref / Re (laminar only).
        if (cfg.physics.mode == PhysicsMode::Laminar && cfg.physics.reynolds > 0.0) {
            const Real l_ref = cfg.reference.reynolds_length > 0.0
                                   ? cfg.reference.reynolds_length
                                   : 1.0;  // default reference length
            cfg.freestream.viscosity = cfg.freestream.rho * cfg.freestream.velocity_magnitude *
                                       l_ref / cfg.physics.reynolds;
        } else {
            cfg.freestream.viscosity = 0.0;  // N/A for inviscid
        }
    }

    // --- Boundary conditions ------------------------------------------------------
    if (j.contains("boundary_conditions")) {
        for (const auto& [family, type] : j["boundary_conditions"].items()) {
            cfg.boundary_conditions[family] = type.get<std::string>();
        }
    }

    // --- Numerics required -----------------------------------------------------
    {
        const auto& n = j.value("numerics_required", nlohmann::json::object());
        cfg.numerics_required.spatial_order = n.value("spatial_order", 2);
        cfg.numerics_required.inviscid_flux = n.value("inviscid_flux", "approximate_riemann");
        cfg.numerics_required.viscous_flux = n.value("viscous_flux", "disabled");
        cfg.numerics_required.main_time_method = n.value("main_time_method", "implicit");
        cfg.numerics_required.implicit_solver = n.value("implicit_solver", "required");
        cfg.numerics_required.transient_order = n.value("transient_order", 2);
    }

    // --- Run control ------------------------------------------------------------
    {
        const auto& rc = j.at("run_control");
        cfg.run_control.type = parse_run_type(rc.value("type", "steady"));
        cfg.run_control.max_steps = rc.value("max_steps", 100000);
        cfg.run_control.residual_reduction_target = rc.value("residual_reduction_target", 4.0);

        // Steady / pseudo-time controls (present in both steady and transient).
        cfg.run_control.cfl_initial = rc.value("cfl_initial", 1.0);
        cfg.run_control.cfl_max = rc.value("cfl_max", 1.0);
        cfg.run_control.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 0);
        cfg.run_control.min_inner_iterations = rc.value("min_inner_iterations", 1);
        cfg.run_control.max_inner_iterations = rc.value("max_inner_iterations", 100);
        cfg.run_control.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.1);

        // Transient controls.
        cfg.run_control.time_step = rc.value("time_step", 0.0);
        cfg.run_control.final_time = rc.value("final_time", 0.0);
        cfg.run_control.time_integrator =
            rc.contains("time_integrator") ? parse_time_integrator(rc["time_integrator"].get<std::string>())
                                           : TimeIntegrator::None;
        cfg.run_control.inner_residual_norm = rc.value("inner_residual_norm", "total_spatial_plus_physical_time");
        cfg.run_control.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);
    }

    // --- Outputs -------------------------------------------------------------------
    {
        const auto& o = j.value("outputs", nlohmann::json::object());
        cfg.outputs.write_final_field = o.value("write_final_field", false);
        cfg.outputs.write_surface = o.value("write_surface", false);
        cfg.outputs.write_forces_every = o.value("write_forces_every", 1);
        cfg.outputs.write_residuals_every = o.value("write_residuals_every", 1);
        cfg.outputs.write_field_every_time = o.value("write_field_every_time", 0.0);
        cfg.outputs.wake_visualization = o.value("wake_visualization", "");
        if (o.contains("recommended_vorticity_clip_range") &&
            o["recommended_vorticity_clip_range"].is_array()) {
            for (const auto& v : o["recommended_vorticity_clip_range"]) {
                cfg.outputs.recommended_vorticity_clip_range.push_back(v.get<Real>());
            }
        }
    }

    return cfg;
}

} // namespace cfd
