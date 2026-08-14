#pragma once

#include "common.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace cfd {

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static CaseConfig read_case_json(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Cannot open case file: " + path);
    }
    nlohmann::json j;
    f >> j;
    f.close();

    CaseConfig cfg;
    cfg.case_id = j.value("case_id", "unknown");
    cfg.mesh_file = j["mesh"]["file"].get<std::string>();
    cfg.mesh_format = j["mesh"].value("format", "CGNS");
    std::string mode = j["physics"].value("mode", "inviscid");
    cfg.viscous = (mode == "laminar");
    if (j["physics"].contains("reynolds")) {
        cfg.reynolds = j["physics"]["reynolds"].get<double>();
    }
    if (j["physics"].contains("viscosity_model")) {
        cfg.viscosity_model = j["physics"]["viscosity_model"].get<std::string>();
    }

    auto& gas = j["gas"];
    cfg.gamma = gas.value("gamma", 1.4);
    cfg.gas_R = gas.value("R", 1.0);
    cfg.prandtl = gas.value("prandtl", 0.72);

    auto& fs = j["freestream"];
    cfg.mach = fs["mach"].get<double>();
    cfg.aoa_degrees = fs.value("aoa_degrees", 0.0);
    cfg.rho_inf = fs.value("rho", 1.0);
    cfg.vel_mag = fs.value("velocity_magnitude", 1.0);
    cfg.p_inf = fs["pressure"].get<double>();

    auto& ref = j["reference"];
    cfg.ref_length = ref.value("length", 1.0);
    cfg.ref_area = ref.value("area", 1.0);
    cfg.ref_reynolds_length = ref.value("reynolds_length", 1.0);
    if (ref.contains("moment_center")) {
        auto& mc = ref["moment_center"];
        cfg.moment_center = {mc[0].get<double>(), mc[1].get<double>()};
    }

    auto& bcs = j["boundary_conditions"];
    for (auto& el : bcs.items()) {
        cfg.bc_map.emplace_back(el.key(), el.value().get<std::string>());
    }

    auto& rc = j["run_control"];
    cfg.run_type = rc.value("type", "steady");
    cfg.max_steps = rc.value("max_steps", 20000);
    cfg.residual_reduction_target = rc.value("residual_reduction_target", 4.0);
    cfg.cfl_initial = rc.value("cfl_initial", 1.0);
    cfg.cfl_max = rc.value("cfl_max", 100.0);
    cfg.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps", 2000);
    cfg.min_inner_iterations = rc.value("min_inner_iterations", 3);
    cfg.max_inner_iterations = rc.value("max_inner_iterations", 50);
    cfg.inner_residual_reduction_target = rc.value("inner_residual_reduction_target", 0.01);
    if (cfg.run_type == "transient") {
        cfg.time_integrator = rc.value("time_integrator", "bdf2_or_trapezoidal");
        cfg.time_step = rc.value("time_step", 0.01);
        cfg.final_time = rc.value("final_time", 300.0);
    }
    if (rc.contains("rusanov_dissipation_scale")) {
        cfg.rusanov_dissipation_scale = rc["rusanov_dissipation_scale"].get<double>();
    }

    auto& outp = j["outputs"];
    cfg.write_forces_every = outp.value("write_forces_every", 1);
    cfg.write_residuals_every = outp.value("write_residuals_every", 1);
    cfg.write_field_every_time = outp.value("write_field_every_time", 1.0);

    return cfg;
}

}  // namespace cfd
