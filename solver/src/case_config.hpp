#pragma once
#include "types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>
#include <map>
#include <cmath>

namespace cfd {

using json = nlohmann::json;

struct CaseConfig {
    std::string caseId;
    std::string meshFile;
    std::string meshPath; // resolved absolute path

    // Physics
    std::string mode; // "inviscid" or "laminar"
    Real reynolds = 0;
    Real gamma = 1.4, R = 1.0, prandtl = 0.72;

    // Freestream
    Real mach = 0.0, aoa = 0.0;
    Real rhoInf = 1.0, velMag = 1.0, pInf = 1.0;
    Real uInf = 0, vInf = 0;
    Real aInf = 0, TInf = 0;
    Real mu = 0; // dynamic viscosity
    Real qInf = 0; // dynamic pressure

    // Reference
    Real refLength = 1.0, refArea = 1.0;
    Real mcx = 0.25, mcy = 0.0;
    Real reynoldsLength = 1.0;

    // Boundary conditions: family name -> type
    std::map<std::string, std::string> bcMapping;

    // Run control
    std::string runType = "steady"; // "steady" or "transient"
    int maxSteps = 20000;
    Real residualTarget = 4.0; // orders of magnitude
    Real cflInitial = 1.0, cflMax = 100.0;
    int cflRampSteps = 2000;
    int minInner = 3, maxInner = 50;
    Real innerTarget = 0.01;

    // Transient
    Real timeStep = 0.01, finalTime = 300.0;
    std::string timeIntegrator = "bdf2";
    Real rusanovScale = 1.0;

    // Computed
    Real freestreamRhoE;
    PrimState freestreamW;
    ConsState freestreamU;

    bool load(const std::string& caseFile, std::string& err) {
        std::ifstream ifs(caseFile);
        if (!ifs) { err = "Cannot open case file: " + caseFile; return false; }

        json j;
        try {
            ifs >> j;
        } catch (std::exception& e) {
            err = std::string("JSON parse error: ") + e.what();
            return false;
        }

        caseId = j.value("case_id", "unknown");

        // Mesh
        meshFile = j["mesh"]["file"].get<std::string>();
        // Resolve relative to case file directory
        std::string caseDir = caseFile.substr(0, caseFile.find_last_of('/'));
        if (meshFile[0] == '/') {
            meshPath = meshFile;
        } else {
            meshPath = caseDir + "/" + meshFile;
        }

        // Physics
        mode = j["physics"].value("mode", "inviscid");
        reynolds = j["physics"].value("reynolds", 0.0);

        // Gas
        gamma = j["gas"].value("gamma", 1.4);
        R = j["gas"].value("R", 1.0);
        prandtl = j["gas"].value("prandtl", 0.72);

        // Freestream
        mach = j["freestream"]["mach"].get<Real>();
        aoa = j["freestream"].value("aoa_degrees", 0.0) * M_PI / 180.0;
        rhoInf = j["freestream"].value("rho", 1.0);
        velMag = j["freestream"].value("velocity_magnitude", 1.0);
        pInf = j["freestream"]["pressure"].get<Real>();

        uInf = velMag * std::cos(aoa);
        vInf = velMag * std::sin(aoa);
        aInf = std::sqrt(gamma * pInf / rhoInf);
        TInf = pInf / (rhoInf * R);

        // Viscosity
        if (mode == "laminar" && reynolds > 0) {
            mu = rhoInf * velMag * reynoldsLength / reynolds;
        }

        qInf = 0.5 * rhoInf * velMag * velMag;

        // Reference
        refLength = j["reference"].value("length", 1.0);
        refArea = j["reference"].value("area", 1.0);
        auto mc = j["reference"]["moment_center"];
        mcx = mc[0].get<Real>();
        mcy = mc[1].get<Real>();
        reynoldsLength = j["reference"].value("reynolds_length", 1.0);

        // Recompute mu with correct reynoldsLength
        if (mode == "laminar" && reynolds > 0) {
            mu = rhoInf * velMag * reynoldsLength / reynolds;
        }

        // Boundary conditions
        for (auto& [key, val] : j["boundary_conditions"].items()) {
            bcMapping[key] = val.get<std::string>();
        }

        // Run control
        auto& rc = j["run_control"];
        runType = rc.value("type", "steady");
        maxSteps = rc.value("max_steps", 20000);
        residualTarget = rc.value("residual_reduction_target", 4.0);
        cflInitial = rc.value("cfl_initial", 1.0);
        cflMax = rc.value("cfl_max", 100.0);
        cflRampSteps = rc.value("pseudo_cfl_ramp_steps", 2000);
        minInner = rc.value("min_inner_iterations", 3);
        maxInner = rc.value("max_inner_iterations", 50);
        innerTarget = rc.value("inner_residual_reduction_target", 0.01);

        if (runType == "transient") {
            timeStep = rc.value("time_step", 0.01);
            finalTime = rc.value("final_time", 300.0);
            timeIntegrator = rc.value("time_integrator", "bdf2");
            minInner = rc.value("min_inner_iterations", 5);
            maxInner = rc.value("max_inner_iterations", 1000);
            innerTarget = rc.value("inner_residual_reduction_target", 0.001);
            cflInitial = rc.value("cfl_initial", 1.0);
            cflMax = rc.value("cfl_max", 1.0);
        }
        rusanovScale = rc.value("rusanov_dissipation_scale", 1.0);

        // Compute freestream state
        freestreamW = {rhoInf, uInf, vInf, pInf, TInf};
        Real E = pInf / (gamma - 1.0) + 0.5 * rhoInf * (uInf*uInf + vInf*vInf);
        freestreamU = {rhoInf, rhoInf*uInf, rhoInf*vInf, E};
        freestreamRhoE = E;

        return true;
    }

    Real getCFL(int step) const {
        if (cflRampSteps <= 0 || step >= cflRampSteps) return cflMax;
        Real t = (Real)step / (Real)cflRampSteps;
        return cflInitial + (cflMax - cflInitial) * t;
        }
};

} // namespace cfd
