#include "case_file.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>

using nlohmann::json;
namespace fs = std::filesystem;

namespace fv {

static BCType parse_bc_type(const std::string& s) {
    if (s == "farfield") return BCType::Farfield;
    if (s == "slip_wall") return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabatic;
    throw std::runtime_error("unsupported boundary condition type: " + s);
}

std::string bc_type_name(BCType t) {
    switch (t) {
        case BCType::Farfield: return "farfield";
        case BCType::SlipWall: return "slip_wall";
        case BCType::NoSlipAdiabatic: return "no_slip_adiabatic_wall";
    }
    return "unknown";
}

static double get_double(const json& j, const char* key, double def) {
    if (!j.contains(key)) return def;
    return j.at(key).get<double>();
}

static long get_long(const json& j, const char* key, long def) {
    if (!j.contains(key)) return def;
    return j.at(key).get<long>();
}

CaseConfig load_case(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open case file: " + path);
    json j;
    in >> j;

    CaseConfig c;
    c.path = path;
    c.case_dir = fs::absolute(fs::path(path)).parent_path().string();

    c.schema_version = j.value("schema_version", 1);
    if (c.schema_version != 1)
        throw std::runtime_error("unsupported schema_version: " + std::to_string(c.schema_version));
    c.case_id = j.at("case_id").get<std::string>();
    c.description = j.value("description", "");

    // mesh
    std::string mf = j.at("mesh").at("file").get<std::string>();
    fs::path mp(mf);
    c.mesh_file = mp.is_absolute() ? mp.string() : (fs::path(c.case_dir) / mp).lexically_normal().string();
    std::string mfmt = j.at("mesh").value("format", "CGNS");
    if (mfmt != "CGNS") throw std::runtime_error("unsupported mesh format: " + mfmt);

    // physics
    c.physics_mode = j.at("physics").at("mode").get<std::string>();
    if (c.physics_mode != "inviscid" && c.physics_mode != "laminar")
        throw std::runtime_error("unsupported physics.mode: " + c.physics_mode);
    c.reynolds = get_double(j.at("physics"), "reynolds", 0.0);
    c.viscosity_model = j.at("physics").value("viscosity_model", "constant");
    if (c.viscosity_model != "constant" && c.viscosity_model != "sutherland")
        throw std::runtime_error("unsupported viscosity_model: " + c.viscosity_model);

    // gas
    const json& g = j.at("gas");
    if (g.value("model", "calorically_perfect") != "calorically_perfect")
        throw std::runtime_error("unsupported gas model");
    c.gas.gamma = g.value("gamma", 1.4);
    c.gas.R = g.value("R", 1.0);
    c.gas.prandtl = g.value("prandtl", 0.72);

    // freestream
    const json& f = j.at("freestream");
    c.fs.mach = f.at("mach").get<double>();
    c.fs.aoa_deg = f.value("aoa_degrees", 0.0);
    c.fs.rho = f.at("rho").get<double>();
    c.fs.vel_mag = f.at("velocity_magnitude").get<double>();
    c.fs.pressure = f.at("pressure").get<double>();
    double aoa = c.fs.aoa_deg * M_PI / 180.0;
    c.fs.u = c.fs.vel_mag * std::cos(aoa);
    c.fs.v = c.fs.vel_mag * std::sin(aoa);
    c.fs.T = c.fs.pressure / (c.fs.rho * c.gas.R);
    c.fs.a = std::sqrt(c.gas.gamma * c.fs.pressure / c.fs.rho);
    c.fs.q = 0.5 * c.fs.rho * c.fs.vel_mag * c.fs.vel_mag;

    // reference
    const json& r = j.at("reference");
    c.ref_length = r.value("length", 1.0);
    c.ref_area = r.value("area", 1.0);
    c.reynolds_length = r.value("reynolds_length", c.ref_length);
    if (r.contains("moment_center")) {
        c.ref_moment_center[0] = r.at("moment_center").at(0).get<double>();
        c.ref_moment_center[1] = r.at("moment_center").at(1).get<double>();
    }

    // boundary conditions
    for (auto& [name, bctype] : j.at("boundary_conditions").items())
        c.bc_map.emplace_back(name, parse_bc_type(bctype.get<std::string>()));
    if (c.bc_map.empty()) throw std::runtime_error("empty boundary_conditions map");

    // run control
    const json& rc = j.at("run_control");
    c.rc.type = rc.value("type", "steady");
    if (c.rc.type != "steady" && c.rc.type != "transient")
        throw std::runtime_error("unsupported run_control.type: " + c.rc.type);
    c.rc.max_steps = get_long(rc, "max_steps", 10000);
    c.rc.residual_reduction_target = get_double(rc, "residual_reduction_target", 4.0);
    c.rc.cfl_initial = get_double(rc, "cfl_initial", 1.0);
    c.rc.cfl_max = get_double(rc, "cfl_max", 100.0);
    c.rc.pseudo_cfl_ramp_steps = get_long(rc, "pseudo_cfl_ramp_steps", 2000);
    c.rc.min_inner_iterations = (int)get_long(rc, "min_inner_iterations", 3);
    c.rc.max_inner_iterations = (int)get_long(rc, "max_inner_iterations", 50);
    c.rc.inner_residual_reduction_target = get_double(rc, "inner_residual_reduction_target", 0.01);
    c.rc.time_integrator = rc.value("time_integrator", "");
    c.rc.time_step = get_double(rc, "time_step", 0.0);
    c.rc.final_time = get_double(rc, "final_time", 0.0);
    c.rc.inner_residual_norm = rc.value("inner_residual_norm", "");
    c.rc.bdf2_history_update = rc.value("bdf2_history_update", "");
    c.rc.rusanov_dissipation_scale = get_double(rc, "rusanov_dissipation_scale", 1.0);
    if (c.rc.type == "transient") {
        if (c.rc.time_step <= 0.0 || c.rc.final_time <= 0.0)
            throw std::runtime_error("transient case requires positive time_step and final_time");
        if (c.rc.time_integrator != "bdf2_or_trapezoidal")
            throw std::runtime_error("unsupported transient time_integrator: " + c.rc.time_integrator);
    }

    // outputs
    if (j.contains("outputs")) {
        const json& o = j.at("outputs");
        c.write_final_field = o.value("write_final_field", true);
        c.write_surface = o.value("write_surface", true);
        c.write_forces_every = (int)get_long(o, "write_forces_every", 1);
        c.write_residuals_every = (int)get_long(o, "write_residuals_every", 1);
        c.write_field_every_time = get_double(o, "write_field_every_time", 0.0);
    }

    // resolved viscosity
    if (c.is_viscous()) {
        if (c.reynolds <= 0.0) throw std::runtime_error("laminar case requires positive reynolds number");
        c.mu = c.fs.rho * c.fs.vel_mag * c.reynolds_length / c.reynolds;
    }

    if (j.contains("debug")) {
        const json& d = j.at("debug");
        c.dbg_first_order = d.value("first_order", false);
        c.dbg_sweeps = (int)get_long(d, "sweeps", -1);
        c.dbg_explicit = d.value("explicit", false);
    }
    if (j.contains("numerics_options")) {
        c.low_mach_fix = j.at("numerics_options").value("low_mach_fix", false);
    }

    return c;
}

} // namespace fv
