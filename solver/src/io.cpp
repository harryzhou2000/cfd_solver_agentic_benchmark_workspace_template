#include "io.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <cmath>

namespace cfd {

using nlohmann::json;

std::string resolve_path(const std::string& path, const std::string& base_file) {
    namespace fs = std::filesystem;
    fs::path p(path);
    if (p.is_absolute()) return p.lexically_normal().string();
    fs::path base(base_file);
    return (base.parent_path() / p).lexically_normal().string();
}

static real_t get_num(const json& j, const std::string& key) {
    if (!j.contains(key)) throw std::runtime_error("missing field: " + key);
    return j.at(key).get<real_t>();
}

static idx_t get_int(const json& j, const std::string& key) {
    if (!j.contains(key)) throw std::runtime_error("missing field: " + key);
    return j.at(key).get<idx_t>();
}

CaseInput load_case(const std::string& case_json_path) {
    std::ifstream f(case_json_path);
    if (!f.is_open()) throw std::runtime_error("cannot open case file: " + case_json_path);
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("malformed case JSON " + case_json_path + ": " + e.what());
    }

    if (!j.contains("schema_version")) throw std::runtime_error("case file missing schema_version");
    int ver = j.at("schema_version").get<int>();
    if (ver != 1) throw std::runtime_error("unsupported schema_version " + std::to_string(ver));

    CaseInput c;
    c.case_id = j.at("case_id").get<std::string>();

    // mesh
    if (!j.contains("mesh") || !j.at("mesh").contains("file")) {
        throw std::runtime_error("case missing mesh.file");
    }
    c.mesh_file = resolve_path(j.at("mesh").at("file").get<std::string>(), case_json_path);

    // physics
    if (!j.contains("physics")) throw std::runtime_error("case missing physics");
    const json& ph = j.at("physics");
    c.physics_mode = ph.value("mode", "laminar");
    if (c.physics_mode != "inviscid" && c.physics_mode != "laminar") {
        throw std::runtime_error("unsupported physics mode: " + c.physics_mode);
    }
    c.reynolds = ph.value("reynolds", 0.0);

    // gas
    const json& gas = j.at("gas");
    c.gamma = get_num(gas, "gamma");
    c.R = get_num(gas, "R");
    c.prandtl = get_num(gas, "prandtl");
    c.cp = c.gamma * c.R / (c.gamma - 1.0);

    // freestream
    const json& fs = j.at("freestream");
    c.mach = get_num(fs, "mach");
    c.aoa = get_num(fs, "aoa_degrees") * M_PI / 180.0;
    c.rho_inf = get_num(fs, "rho");
    real_t vmag = get_num(fs, "velocity_magnitude");
    c.u_inf = vmag * std::cos(c.aoa);
    c.v_inf = vmag * std::sin(c.aoa);
    c.p_inf = get_num(fs, "pressure");

    // reference
    const json& ref = j.at("reference");
    c.ref_length = get_num(ref, "length");
    c.ref_area = get_num(ref, "area");
    std::vector<real_t> mc = ref.at("moment_center").get<std::vector<real_t>>();
    if (mc.size() != 2) throw std::runtime_error("moment_center must have 2 components");
    c.moment_center = Vec2(mc[0], mc[1]);

    // derived thermodynamics
    c.T_inf = c.p_inf / (c.rho_inf * c.R);
    c.a_inf = std::sqrt(c.gamma * c.p_inf / c.rho_inf);
    c.e_inf = c.p_inf / ((c.gamma - 1.0) * c.rho_inf);
    c.rhoE_inf = c.rho_inf * (c.e_inf + 0.5 * (c.u_inf * c.u_inf + c.v_inf * c.v_inf));
    c.freestream_state << c.rho_inf, c.rho_inf * c.u_inf, c.rho_inf * c.v_inf, c.rhoE_inf;

    // viscosity: constant value matching the case Reynolds number (or zero for
    // inviscid mode)
    c.mu_ref = 0.0;
    if (c.physics_mode == "laminar") {
        if (c.reynolds <= 0) throw std::runtime_error("laminar case requires positive reynolds");
        c.mu_ref = c.rho_inf * vmag * c.ref_length / c.reynolds;
    }

    // boundary conditions
    if (!j.contains("boundary_conditions")) throw std::runtime_error("case missing boundary_conditions");
    for (auto it = j.at("boundary_conditions").begin(); it != j.at("boundary_conditions").end(); ++it) {
        std::string fam = it.key();
        std::string type = it.value().get<std::string>();
        if (bc_from_string(type) == BCType::Interior && type != "interior") {
            throw std::runtime_error("unsupported boundary condition '" + type + "' for family '" + fam + "'");
        }
        c.boundary_conditions[fam] = type;
    }

    // run control
    const json& rc = j.at("run_control");
    c.run_type = rc.value("type", "steady");
    if (c.run_type != "steady" && c.run_type != "transient") {
        throw std::runtime_error("unsupported run type: " + c.run_type);
    }
    c.max_steps = rc.value("max_steps", (idx_t)0);
    c.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
    c.cfl_initial = get_num(rc, "cfl_initial");
    c.cfl_max = get_num(rc, "cfl_max");
    c.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", (idx_t)0);
    c.min_inner_iterations = rc.value("min_inner_iterations", (idx_t)3);
    c.max_inner_iterations = rc.value("max_inner_iterations", (idx_t)50);
    c.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
    c.time_integrator = rc.value("time_integrator", "bdf2_or_trapezoidal");
    c.time_step = rc.value("time_step", 0.0);
    c.final_time = rc.value("final_time", 0.0);
    c.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);

    if (c.run_type == "transient" && (c.time_step <= 0 || c.final_time <= 0)) {
        throw std::runtime_error("transient case requires positive time_step and final_time");
    }
    if (c.run_type == "steady" && c.max_steps <= 0) {
        throw std::runtime_error("steady case requires positive max_steps");
    }

    // outputs
    const json& out = j.value("outputs", json::object());
    c.write_forces_every = out.value("write_forces_every", 1);
    c.write_residuals_every = out.value("write_residuals_every", 1);
    if (c.write_forces_every <= 0) c.write_forces_every = 1;
    if (c.write_residuals_every <= 0) c.write_residuals_every = 1;

    return c;
}

} // namespace cfd
