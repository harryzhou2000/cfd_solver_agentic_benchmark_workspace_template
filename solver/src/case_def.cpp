// case_def.cpp - Parse JSON case files into CaseDef structures.
#include "cfd2d.hpp"
#include <fstream>
#include <stdexcept>
#include <cmath>

namespace cfd2d {

static BCType parseBCType(const std::string& s) {
    if (s == "farfield") return BCType::Farfield;
    if (s == "slip_wall") return BCType::SlipWall;
    if (s == "no_slip_adiabatic_wall") return BCType::NoSlipAdiabatic;
    return BCType::Unknown;
}

CaseDef CaseDef::parse(const std::string& jsonPath) {
    std::ifstream ifs(jsonPath);
    if (!ifs) throw std::runtime_error("Cannot open case JSON: " + jsonPath);
    json j;
    ifs >> j;

    CaseDef cd;
    cd.rawJson = j;
    cd.caseId = j.value("case_id", "unknown");

    // Mesh
    if (j.contains("mesh")) {
        cd.meshFile = j["mesh"].value("file", "");
    }

    // Gas
    if (j.contains("gas")) {
        cd.gas.gamma = j["gas"].value("gamma", 1.4);
        cd.gas.R = j["gas"].value("R", 1.0);
        cd.gas.prandtl = j["gas"].value("prandtl", 0.72);
    }

    // Freestream
    if (j.contains("freestream")) {
        auto& fs = j["freestream"];
        cd.fs.mach = fs.value("mach", 0.0);
        cd.fs.aoa = fs.value("aoa_degrees", 0.0) * M_PI / 180.0;
        cd.fs.rho = fs.value("rho", 1.0);
        cd.fs.velocity = fs.value("velocity_magnitude", 1.0);
        cd.fs.pressure = fs.value("pressure", 1.0);
    }

    // Reference
    if (j.contains("reference")) {
        auto& r = j["reference"];
        cd.ref.length = r.value("length", 1.0);
        cd.ref.area = r.value("area", 1.0);
        if (r.contains("moment_center")) {
            cd.ref.moment_center = {r["moment_center"][0], r["moment_center"][1]};
        }
        cd.ref.reynolds_length = r.value("reynolds_length", 1.0);
    }

    // Physics
    if (j.contains("physics")) {
        auto& p = j["physics"];
        cd.physics.mode = p.value("mode", "inviscid");
        cd.physics.viscous = (cd.physics.mode == "laminar");
        cd.physics.reynolds = p.value("reynolds", 0.0);
        cd.physics.viscosityModel = p.value("viscosity_model", "constant");
    }

    // Boundary conditions
    if (j.contains("boundary_conditions")) {
        for (auto& [key, val] : j["boundary_conditions"].items()) {
            BCSpec bc;
            bc.familyName = key;
            bc.type = parseBCType(val.get<std::string>());
            cd.bcs.push_back(bc);
        }
    }

    // Run control
    if (j.contains("run_control")) {
        auto& rc = j["run_control"];
        cd.run.type = rc.value("type", "steady");
        cd.run.maxSteps = rc.value("max_steps", 10000);
        cd.run.residualReductionTarget = rc.value("residual_reduction_target", 4.0);
        cd.run.cflInitial = rc.value("cfl_initial", 1.0);
        cd.run.cflMax = rc.value("cfl_max", 100.0);
        cd.run.pseudoCflRampSteps = rc.value("pseudo_cfl_ramp_steps", 2000);
        cd.run.minInner = rc.value("min_inner_iterations", 3);
        cd.run.maxInner = rc.value("max_inner_iterations", 50);
        cd.run.innerResidualTarget = rc.value("inner_residual_reduction_target", 0.01);
        cd.run.timeIntegrator = rc.value("time_integrator", "");
        cd.run.timeStep = rc.value("time_step", 0.0);
        cd.run.finalTime = rc.value("final_time", 0.0);
        cd.run.bdf2HistoryUpdate = rc.value("bdf2_history_update", "");
        cd.run.rusanovDissipationScale = rc.value("rusanov_dissipation_scale", 1.0);
        cd.run.innerResidualNorm = rc.value("inner_residual_norm", "");
        cd.run.writeFieldEvery = (int)rc.value("write_field_every_time", 0.0);
    }

    // Outputs
    if (j.contains("outputs")) {
        auto& o = j["outputs"];
        cd.run.forcesEvery = o.value("write_forces_every", 1);
        cd.run.residualsEvery = o.value("write_residuals_every", 1);
    }

    return cd;
}

} // namespace cfd2d
