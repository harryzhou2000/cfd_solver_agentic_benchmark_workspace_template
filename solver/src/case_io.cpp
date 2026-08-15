#include "case_io.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace cfd {

using nlohmann::json;

static std::string resolve_path(const std::string& path,
                                const std::string& base_file) {
    std::filesystem::path p(path);
    if (p.is_absolute()) return p.lexically_normal().string();
    std::filesystem::path base(base_file);
    return (base.parent_path() / p).lexically_normal().string();
}

static Real need_num(const json& j, const char* key) {
    if (!j.contains(key)) throw std::runtime_error(std::string("missing field: ") + key);
    return j.at(key).get<Real>();
}

static Index need_int(const json& j, const char* key) {
    if (!j.contains(key)) throw std::runtime_error(std::string("missing field: ") + key);
    return j.at(key).get<Index>();
}

CaseInput load_case(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("cannot open case file: " + path);
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("malformed case JSON " + path + ": " + e.what());
    }

    if (!j.contains("schema_version")) throw std::runtime_error("case file missing schema_version");
    if (j.at("schema_version").get<int>() != 1) {
        throw std::runtime_error("unsupported schema_version (only version 1 is supported)");
    }

    CaseInput c;
    c.case_id = j.at("case_id").get<std::string>();

    c.mesh_file = resolve_path(j.at("mesh").at("file").get<std::string>(), path);

    const json& ph = j.at("physics");
    c.physics_mode = ph.value("mode", "laminar");
    if (c.physics_mode != "inviscid" && c.physics_mode != "laminar") {
        throw std::runtime_error("unsupported physics mode: " + c.physics_mode);
    }
    c.reynolds = ph.value("reynolds", 0.0);

    const json& gas = j.at("gas");
    c.gamma = need_num(gas, "gamma");
    c.R = need_num(gas, "R");
    c.prandtl = need_num(gas, "prandtl");
    c.cp = c.gamma * c.R / (c.gamma - 1.0);

    const json& fs = j.at("freestream");
    c.mach = need_num(fs, "mach");
    c.aoa = need_num(fs, "aoa_degrees") * M_PI / 180.0;
    c.rho_inf = need_num(fs, "rho");
    const Real vmag = need_num(fs, "velocity_magnitude");
    c.u_inf = vmag * std::cos(c.aoa);
    c.v_inf = vmag * std::sin(c.aoa);
    c.p_inf = need_num(fs, "pressure");

    const json& ref = j.at("reference");
    c.ref_length = need_num(ref, "length");
    c.ref_area = need_num(ref, "area");
    const std::vector<Real> mc = ref.at("moment_center").get<std::vector<Real>>();
    if (mc.size() != 2) throw std::runtime_error("moment_center must have 2 components");
    c.moment_center = Vec2(mc[0], mc[1]);

    c.T_inf = c.p_inf / (c.rho_inf * c.R);
    c.a_inf = std::sqrt(c.gamma * c.p_inf / c.rho_inf);
    const Real e_inf = c.p_inf / ((c.gamma - 1.0) * c.rho_inf);
    const Real rhoE_inf = c.rho_inf * (e_inf + 0.5 * (c.u_inf * c.u_inf + c.v_inf * c.v_inf));
    c.freestream << c.rho_inf, c.rho_inf * c.u_inf, c.rho_inf * c.v_inf, rhoE_inf;

    c.mu_ref = 0.0;
    if (c.physics_mode == "laminar") {
        if (c.reynolds <= 0.0) throw std::runtime_error("laminar case requires positive reynolds");
        c.mu_ref = c.rho_inf * vmag * c.ref_length / c.reynolds;
    }

    const json& bc = j.at("boundary_conditions");
    for (auto it = bc.begin(); it != bc.end(); ++it) {
        const std::string type = it.value().get<std::string>();
        if (bc_from_name(type) == BcKind::Interior && type != "interior") {
            throw std::runtime_error("unsupported boundary condition '" + type +
                                     "' for family '" + it.key() + "'");
        }
        c.boundary_conditions[it.key()] = type;
    }

    const json& rc = j.at("run_control");
    c.run_type = rc.value("type", "steady");
    if (c.run_type != "steady" && c.run_type != "transient") {
        throw std::runtime_error("unsupported run type: " + c.run_type);
    }
    c.max_steps = rc.value("max_steps", Index(0));
    c.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
    c.cfl_initial = need_num(rc, "cfl_initial");
    c.cfl_max = need_num(rc, "cfl_max");
    c.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", Index(0));
    c.min_inner_iterations = rc.value("min_inner_iterations", Index(3));
    c.max_inner_iterations = rc.value("max_inner_iterations", Index(50));
    c.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
    c.time_integrator = rc.value("time_integrator", "bdf2_or_trapezoidal");
    c.time_step = rc.value("time_step", 0.0);
    c.final_time = rc.value("final_time", 0.0);
    c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

    if (c.run_type == "transient" && (c.time_step <= 0.0 || c.final_time <= 0.0)) {
        throw std::runtime_error("transient case requires positive time_step and final_time");
    }
    if (c.run_type == "steady" && c.max_steps <= 0) {
        throw std::runtime_error("steady case requires positive max_steps");
    }

    const json& out = j.value("outputs", json::object());
    c.write_forces_every = std::max<Index>(1, out.value("write_forces_every", 1));
    c.write_residuals_every = std::max<Index>(1, out.value("write_residuals_every", 1));

    return c;
}

} // namespace cfd
