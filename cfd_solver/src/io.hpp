#pragma once

#include "types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>

namespace cfd {

using json = nlohmann::json;

inline CaseInput parse_case(const std::string& case_path) {
    std::ifstream f(case_path);
    if (!f) throw std::runtime_error("Cannot open case file: " + case_path);
    json j = json::parse(f);
    
    CaseInput ci;
    ci.case_id = j["case_id"];
    ci.mesh_file = j["mesh"]["file"];
    
    // Resolve relative paths
    auto case_dir = case_path.substr(0, case_path.find_last_of('/'));
    if (ci.mesh_file[0] != '/' && case_dir.find('/') != std::string::npos) {
        ci.mesh_file = case_dir + "/" + ci.mesh_file;
    }
    
    auto& phys = j["physics"];
    ci.physics_mode = phys["mode"];
    ci.reynolds = phys.value("reynolds", -1.0);
    
    auto& fs = j["freestream"];
    ci.mach = fs["mach"];
    ci.aoa = fs["aoa_degrees"];
    ci.rho_inf = fs["rho"];
    real_t u_mag = fs["velocity_magnitude"];
    real_t aoa_rad = ci.aoa * M_PI / 180.0;
    ci.u_inf = u_mag * std::cos(aoa_rad);
    ci.v_inf = u_mag * std::sin(aoa_rad);
    ci.p_inf = fs["pressure"];
    
    auto& gas = j["gas"];
    ci.gamma = gas["gamma"];
    ci.R = gas["R"];
    ci.prandtl = gas["prandtl"];
    
    auto& ref = j["reference"];
    ci.ref_length = ref["length"];
    ci.ref_area = ref["area"];
    auto& mc = ref["moment_center"];
    ci.moment_center = Vec2(mc[0], mc[1]);
    
    for (auto& [tag, type] : j["boundary_conditions"].items()) {
        ci.boundary_conditions[tag] = type;
    }
    
    auto& rc = j["run_control"];
    ci.run_type = rc["type"];
    ci.max_steps = rc.value("max_steps", idx_t(0));
    ci.residual_reduction_target = rc.value("residual_reduction_target", 0.0);
    ci.cfl_initial = rc["cfl_initial"];
    ci.cfl_max = rc["cfl_max"];
    ci.pseudo_cfl_ramp_steps = rc["pseudo_cfl_ramp_steps"];
    ci.min_inner_iterations = rc.value("min_inner_iterations", 3);
    ci.max_inner_iterations = rc.value("max_inner_iterations", 50);
    ci.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
    
    if (ci.run_type == "transient") {
        ci.time_integrator = rc["time_integrator"];
        ci.time_step = rc["time_step"];
        ci.final_time = rc["final_time"];
        ci.min_inner_iterations = rc["min_inner_iterations"];
        ci.max_inner_iterations = rc["max_inner_iterations"];
        ci.inner_residual_reduction_target = rc["inner_residual_reduction_target"];
        ci.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale", 1.0);
    }
    
    // Derived quantities
    ci.T_inf = ci.p_inf / (ci.rho_inf * ci.R);
    ci.e_inf = ci.T_inf * ci.R / (ci.gamma - 1.0);
    ci.a_inf = std::sqrt(ci.gamma * ci.p_inf / ci.rho_inf);
    real_t ke = 0.5 * (ci.u_inf * ci.u_inf + ci.v_inf * ci.v_inf);
    ci.rhoE_inf = ci.rho_inf * (ci.e_inf + ke);
    ci.freestream_state = StateVec(ci.rho_inf, ci.rho_inf * ci.u_inf,
                                    ci.rho_inf * ci.v_inf, ci.rhoE_inf);
    
    return ci;
}

} // namespace cfd
