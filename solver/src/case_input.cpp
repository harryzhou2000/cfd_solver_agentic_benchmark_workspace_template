#include "case_input.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace cfd {

namespace {

[[noreturn]] void case_error(const std::string& case_id, const std::string& msg) {
    throw std::runtime_error("case '" + case_id + "': " + msg);
}

double get_double(const nlohmann::json& j, const char* key,
                  const std::string& case_id) {
    if (!j.contains(key)) case_error(case_id, std::string("missing field ") + key);
    const auto& v = j[key];
    if (!v.is_number())
        case_error(case_id, std::string("field ") + key + " must be a number");
    return v.get<double>();
}

int get_int(const nlohmann::json& j, const char* key,
            const std::string& case_id) {
    if (!j.contains(key)) case_error(case_id, std::string("missing field ") + key);
    const auto& v = j[key];
    if (!v.is_number_integer())
        case_error(case_id, std::string("field ") + key + " must be an integer");
    return v.get<int>();
}

}  // namespace

CaseInput parse_case(const std::string& case_path) {
    fs::path path(case_path);
    if (!fs::exists(path)) {
        throw std::runtime_error("case file does not exist: " + case_path);
    }
    path = fs::absolute(path);

    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open case file: " + path.string());
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("invalid JSON in " + path.string() + ": " +
                                 e.what());
    }

    CaseInput c;
    c.schema_version = get_int(j, "schema_version", "unknown");
    if (c.schema_version != 1) {
        throw std::runtime_error(
            "unsupported case schema_version " + std::to_string(c.schema_version) +
            " (only version 1 is supported)");
    }
    c.case_id = j.value("case_id", std::string{});
    if (c.case_id.empty()) case_error(c.case_id, "case_id is empty");
    c.description = j.value("description", std::string{});

    // Mesh.
    if (!j.contains("mesh") || !j["mesh"].is_object())
        case_error(c.case_id, "missing mesh object");
    const auto& mesh = j["mesh"];
    const std::string mesh_rel = mesh.value("file", std::string{});
    if (mesh_rel.empty()) case_error(c.case_id, "mesh.file is empty");
    fs::path mesh_path(mesh_rel);
    if (mesh_path.is_absolute()) {
        c.mesh_file = mesh_path.string();
    } else {
        c.mesh_file = (path.parent_path() / mesh_path).lexically_normal().string();
    }
    c.mesh_dir = path.parent_path().string();
    const std::string format = mesh.value("format", std::string{});
    if (format != "CGNS")
        case_error(c.case_id, "only mesh format 'CGNS' is supported");
    if (mesh.value("dimension", 0) != 2)
        case_error(c.case_id, "only mesh dimension 2 is supported");

    // Physics.
    if (!j.contains("physics")) case_error(c.case_id, "missing physics object");
    const auto& physics = j["physics"];
    c.equations = physics.value("equations", std::string{});
    if (c.equations != "compressible_navier_stokes")
        case_error(c.case_id, "unsupported equation set '" + c.equations + "'");
    c.mode = physics.value("mode", std::string{});
    if (c.mode != "inviscid" && c.mode != "laminar")
        case_error(c.case_id, "unsupported physics.mode '" + c.mode + "'");
    if (c.mode == "laminar") {
        c.reynolds = get_double(physics, "reynolds", c.case_id);
        c.viscosity_model = physics.value("viscosity_model", std::string{});
        if (c.viscosity_model != "constant" && c.viscosity_model != "sutherland")
            case_error(c.case_id,
                       "unsupported viscosity_model '" + c.viscosity_model + "'");
    }

    // Gas.
    if (!j.contains("gas")) case_error(c.case_id, "missing gas object");
    const auto& gasj = j["gas"];
    if (gasj.value("model", std::string{}) != "calorically_perfect")
        case_error(c.case_id, "unsupported gas model");
    c.gas.gamma = get_double(gasj, "gamma", c.case_id);
    c.gas.R = get_double(gasj, "R", c.case_id);
    c.gas.prandtl = get_double(gasj, "prandtl", c.case_id);
    if (c.gas.gamma <= 1.0) case_error(c.case_id, "gamma must be > 1");
    if (c.gas.R <= 0.0 || c.gas.prandtl <= 0.0)
        case_error(c.case_id, "R and Prandtl must be positive");

    // Freestream.
    if (!j.contains("freestream")) case_error(c.case_id, "missing freestream");
    const auto& fsj = j["freestream"];
    c.mach_inf = get_double(fsj, "mach", c.case_id);
    c.aoa_deg = get_double(fsj, "aoa_degrees", c.case_id);
    if (c.aoa_deg != 0.0) {
        // Supported in the solver, but no supplied case uses nonzero AoA.
        // Convert the velocity vector below.
    }
    const double rho_inf = get_double(fsj, "rho", c.case_id);
    const double vmag_inf = get_double(fsj, "velocity_magnitude", c.case_id);
    const double p_inf = get_double(fsj, "pressure", c.case_id);
    if (rho_inf <= 0.0 || p_inf <= 0.0 || vmag_inf < 0.0)
        case_error(c.case_id, "non-positive freestream density/pressure");
    const double alpha = c.aoa_deg * kPi / 180.0;
    c.freestream.rho = rho_inf;
    c.freestream.u = vmag_inf * std::cos(alpha);
    c.freestream.v = vmag_inf * std::sin(alpha);
    c.freestream.p = p_inf;
    // The case files scale velocity_magnitude to the requested Mach number; a
    // mismatch is reported rather than silently corrected.
    const double a_inf = sound_speed(c.freestream, c.gas);
    if (std::abs(vmag_inf / std::max(a_inf, 1e-300) - c.mach_inf) > 1e-6) {
        std::cerr << "warning: freestream velocity/pressure imply Mach "
                  << vmag_inf / a_inf << " but requested Mach is " << c.mach_inf
                  << "\n";
    }
    c.q_inf = 0.5 * rho_inf * vmag_inf * vmag_inf;

    // Reference values.
    if (!j.contains("reference")) case_error(c.case_id, "missing reference");
    const auto& ref = j["reference"];
    c.ref_length = get_double(ref, "length", c.case_id);
    c.ref_area = get_double(ref, "area", c.case_id);
    if (ref.contains("moment_center") && ref["moment_center"].is_array() &&
        ref["moment_center"].size() >= 2) {
        c.moment_center =
            Vec2{ref["moment_center"][0].get<double>(),
                 ref["moment_center"][1].get<double>()};
    }
    if (ref.contains("reynolds_length"))
        c.ref_reynolds_length = get_double(ref, "reynolds_length", c.case_id);

    // Boundary condition map.
    if (!j.contains("boundary_conditions"))
        case_error(c.case_id, "missing boundary_conditions");
    for (const auto& [name, type] : j["boundary_conditions"].items()) {
        if (!type.is_string()) case_error(c.case_id, "BC type must be a string");
        BcType t = bc_from_string(type.get<std::string>());
        if (t == BcType::Unknown)
            case_error(c.case_id, "unsupported boundary condition type '" +
                                      type.get<std::string>() + "'");
        c.bc_map[name] = t;
    }
    if (c.bc_map.empty()) case_error(c.case_id, "boundary_conditions is empty");

    // Required numerics (informational; solver capability is fixed).
    if (j.contains("numerics_required")) {
        const auto& nr = j["numerics_required"];
        c.spatial_order = nr.value("spatial_order", 2);
        c.inviscid_flux = nr.value("inviscid_flux", std::string{});
        c.main_time_method = nr.value("main_time_method", std::string{});
        c.implicit_solver = nr.value("implicit_solver", std::string{});
        if (c.spatial_order < 2)
            case_error(c.case_id, "spatial_order below 2 is not supported");
        if (c.main_time_method != "implicit")
            case_error(c.case_id, "main time method must be implicit");
    }

    // Run control.
    if (!j.contains("run_control"))
        case_error(c.case_id, "missing run_control");
    const auto& rc = j["run_control"];
    const std::string rtype = rc.value("type", std::string{});
    if (rtype == "steady") {
        c.run_control.type = RunType::Steady;
        c.run_control.max_steps = get_int(rc, "max_steps", c.case_id);
        c.run_control.residual_reduction_target =
            get_double(rc, "residual_reduction_target", c.case_id);
        c.run_control.cfl_initial = get_double(rc, "cfl_initial", c.case_id);
        c.run_control.cfl_max = get_double(rc, "cfl_max", c.case_id);
        c.run_control.pseudo_cfl_ramp_steps =
            get_int(rc, "pseudo_cfl_ramp_steps", c.case_id);
        c.run_control.min_inner_iterations =
            get_int(rc, "min_inner_iterations", c.case_id);
        c.run_control.max_inner_iterations =
            get_int(rc, "max_inner_iterations", c.case_id);
        c.run_control.inner_residual_reduction_target =
            get_double(rc, "inner_residual_reduction_target", c.case_id);
    } else if (rtype == "transient") {
        c.run_control.type = RunType::Transient;
        c.run_control.time_integrator =
            rc.value("time_integrator", std::string{});
        if (c.run_control.time_integrator != "bdf2_or_trapezoidal")
            case_error(c.case_id, "unsupported transient time_integrator");
        c.run_control.time_step = get_double(rc, "time_step", c.case_id);
        c.run_control.final_time = get_double(rc, "final_time", c.case_id);
        c.run_control.min_inner_iterations =
            get_int(rc, "min_inner_iterations", c.case_id);
        c.run_control.max_inner_iterations =
            get_int(rc, "max_inner_iterations", c.case_id);
        c.run_control.inner_residual_reduction_target =
            get_double(rc, "inner_residual_reduction_target", c.case_id);
        if (rc.contains("inner_residual_norm"))
            c.run_control.inner_residual_norm =
                rc["inner_residual_norm"].get<std::string>();
        if (rc.contains("bdf2_history_update"))
            c.run_control.bdf2_history_update =
                rc["bdf2_history_update"].get<std::string>();
        if (rc.contains("cfl_initial"))
            c.run_control.cfl_initial = get_double(rc, "cfl_initial", c.case_id);
        if (rc.contains("cfl_max"))
            c.run_control.cfl_max = get_double(rc, "cfl_max", c.case_id);
        if (rc.contains("pseudo_cfl_ramp_steps"))
            c.run_control.pseudo_cfl_ramp_steps =
                get_int(rc, "pseudo_cfl_ramp_steps", c.case_id);
        if (rc.contains("rusanov_dissipation_scale"))
            c.run_control.rusanov_dissipation_scale =
                get_double(rc, "rusanov_dissipation_scale", c.case_id);
        c.run_control.residual_reduction_target = 0.0;
    } else {
        case_error(c.case_id, "unknown run_control.type '" + rtype + "'");
    }

    // Outputs.
    if (j.contains("outputs")) {
        const auto& out = j["outputs"];
        c.outputs.write_final_field = out.value("write_final_field", true);
        c.outputs.write_surface = out.value("write_surface", true);
        c.outputs.write_forces_every = out.value("write_forces_every", 1);
        c.outputs.write_residuals_every = out.value("write_residuals_every", 1);
        if (out.contains("write_field_every_time") &&
            out["write_field_every_time"].is_number())
            c.outputs.write_field_every_time =
                out["write_field_every_time"].get<double>();
    }

    // Derived viscosity: mu = rho_inf * U_inf * L / Re (constant-viscosity
    // case files define the freestream so this matches Re).
    if (c.mode == "laminar") {
        c.viscosity = rho_inf * vmag_inf * c.ref_reynolds_length / c.reynolds;
        c.thermal_conductivity = c.viscosity * c.gas.cp() / c.gas.prandtl;
    }

    // Roe with the Harten-Yee entropy fix is used for both modes. At the low
    // Mach numbers of the laminar cases Rusanov's (|u_n| + a) dissipation is
    // dominated by the sound speed and smears the boundary layer, inflating
    // skin friction; the Roe/entropy-fix dissipation stays proportional to
    // the local wave speeds. CFD_RUSANOV=1 restores the Rusanov diagnostic
    // path at run time.
    c.flux_scheme = FluxScheme::Roe;

    return c;
}

}  // namespace cfd
