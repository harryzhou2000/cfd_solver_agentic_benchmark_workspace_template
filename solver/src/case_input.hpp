// case_input.hpp — Parse case JSON files using nlohmann_json
#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <map>
#include <stdexcept>
#include <cmath>

namespace cfd2d {

enum class BCType { Farfield, SlipWall, NoSlipAdiabaticWall, Invalid };

inline std::string bcTypeToString(BCType t) {
    switch (t) {
        case BCType::Farfield: return "farfield";
        case BCType::SlipWall: return "slip_wall";
        case BCType::NoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
        default: return "invalid";
    }
}

struct CaseInput {
    std::string caseId;
    std::string meshFile;
    std::string meshFormat;
    int dimension = 2;
    std::string mode; // "inviscid" or "laminar"
    double reynolds = 0.0;
    std::string viscosityModel = "constant";
    double gamma = 1.4, R = 1.0, prandtl = 0.72;
    double mach = 0.0, aoaDeg = 0.0;
    double rhoInf = 1.0, velMag = 1.0, pInf = 1.0;
    double refLength = 1.0, refArea = 1.0;
    double momentCx = 0.25, momentCy = 0.0;
    double reynoldsLength = 1.0;
    std::map<std::string, BCType> bcMap;
    // Run control
    std::string runType = "steady"; // "steady" or "transient"
    int maxSteps = 20000;
    double residualTarget = 4.0; // orders of magnitude
    double cflInitial = 1.0, cflMax = 100.0;
    int pseudoCflRampSteps = 2000;
    int minInner = 3, maxInner = 50;
    double innerResidualTarget = 0.01;
    // Transient
    double timeStep = 0.0;
    double finalTime = 0.0;
    std::string timeIntegrator = "steady";
    double rusanovDissipationScale = 1.0;
    // Outputs
    bool writeFinalField = true;
    bool writeSurface = true;
    int writeForcesEvery = 1;
    int writeResidualsEvery = 1;
    // Computed
    double mu = 0.0; // nondimensional viscosity
    double aoaRad = 0.0;
    double uInf = 0.0, vInf = 0.0;
    double aInf = 0.0;
    double qInf = 0.0; // dynamic pressure = 0.5 * rho * U^2
    // schema
    int schemaVersion = 1;

    void computeDerived() {
        aoaRad = aoaDeg * M_PI / 180.0;
        uInf = velMag * std::cos(aoaRad);
        vInf = velMag * std::sin(aoaRad);
        aInf = mach > 0 ? velMag / mach : velMag;
        qInf = 0.5 * rhoInf * velMag * velMag;
        // Nondimensional viscosity: mu = 1/Re (since rho_inf=1, U_inf=1, L_ref=1)
        if (mode == "laminar" && reynolds > 0) {
            mu = rhoInf * velMag * reynoldsLength / reynolds;
        }
    }

    // Freestream conservative state
    void freestreamState(double U[4]) const {
        U[0] = rhoInf;
        U[1] = rhoInf * uInf;
        U[2] = rhoInf * vInf;
        double e = pInf / ((gamma - 1.0) * rhoInf);
        double E = e + 0.5 * (uInf*uInf + vInf*vInf);
        U[3] = rhoInf * E;
    }
};

inline CaseInput parseCaseFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open case file: " + path);
    nlohmann::json j;
    f >> j;

    CaseInput c;
    c.schemaVersion = j.value("schema_version", 1);
    if (c.schemaVersion != 1)
        throw std::runtime_error("Unsupported schema_version: " + std::to_string(c.schemaVersion));

    c.caseId = j.at("case_id").get<std::string>();

    const auto& mesh = j.at("mesh");
    c.meshFile = mesh.at("file").get<std::string>();
    c.meshFormat = mesh.value("format", "CGNS");
    c.dimension = mesh.value("dimension", 2);

    const auto& physics = j.at("physics");
    c.mode = physics.value("mode", "inviscid");
    c.reynolds = physics.value("reynolds", 0.0);
    c.viscosityModel = physics.value("viscosity_model", "constant");

    const auto& gas = j.at("gas");
    c.gamma = gas.value("gamma", 1.4);
    c.R = gas.value("R", 1.0);
    c.prandtl = gas.value("prandtl", 0.72);

    const auto& fs = j.at("freestream");
    c.mach = fs.at("mach").get<double>();
    c.aoaDeg = fs.value("aoa_degrees", 0.0);
    c.rhoInf = fs.value("rho", 1.0);
    c.velMag = fs.value("velocity_magnitude", 1.0);
    c.pInf = fs.at("pressure").get<double>();

    const auto& ref = j.at("reference");
    c.refLength = ref.value("length", 1.0);
    c.refArea = ref.value("area", 1.0);
    auto mc = ref.value("moment_center", std::vector<double>{0.25, 0.0});
    c.momentCx = mc.size() > 0 ? mc[0] : 0.25;
    c.momentCy = mc.size() > 1 ? mc[1] : 0.0;
    c.reynoldsLength = ref.value("reynolds_length", 1.0);

    const auto& bc = j.at("boundary_conditions");
    for (auto it = bc.begin(); it != bc.end(); ++it) {
        std::string bcName = it.value().get<std::string>();
        BCType bt = BCType::Invalid;
        if (bcName == "farfield") bt = BCType::Farfield;
        else if (bcName == "slip_wall") bt = BCType::SlipWall;
        else if (bcName == "no_slip_adiabatic_wall") bt = BCType::NoSlipAdiabaticWall;
        else throw std::runtime_error("Unknown BC type: " + bcName);
        c.bcMap[it.key()] = bt;
    }

    const auto& rc = j.at("run_control");
    c.runType = rc.value("type", "steady");
    c.maxSteps = rc.value("max_steps", 20000);
    c.residualTarget = rc.value("residual_reduction_target", 4.0);
    c.cflInitial = rc.value("cfl_initial", 1.0);
    c.cflMax = rc.value("cfl_max", 100.0);
    c.pseudoCflRampSteps = rc.value("pseudo_cfl_ramp_steps", 2000);
    c.minInner = rc.value("min_inner_iterations", 3);
    c.maxInner = rc.value("max_inner_iterations", 50);
    c.innerResidualTarget = rc.value("inner_residual_reduction_target", 0.01);
    c.timeStep = rc.value("time_step", 0.0);
    c.finalTime = rc.value("final_time", 0.0);
    c.timeIntegrator = rc.value("time_integrator", "steady");
    c.rusanovDissipationScale = rc.value("rusanov_dissipation_scale", 1.0);

    if (j.contains("outputs")) {
        const auto& out = j["outputs"];
        c.writeFinalField = out.value("write_final_field", true);
        c.writeSurface = out.value("write_surface", true);
        c.writeForcesEvery = out.value("write_forces_every", 1);
        c.writeResidualsEvery = out.value("write_residuals_every", 1);
    }

    c.computeDerived();
    return c;
}

// Resolve mesh path relative to case file directory
inline std::string resolveMeshPath(const std::string& caseFile, const std::string& meshFile) {
    // If absolute, use as-is
    if (meshFile[0] == '/') return meshFile;
    // Find case file directory
    size_t pos = caseFile.find_last_of('/');
    std::string dir = (pos != std::string::npos) ? caseFile.substr(0, pos) : ".";
    // meshFile is relative to case file dir (e.g., "../meshes/NACA0012_H2.cgns")
    return dir + "/" + meshFile;
}

} // namespace cfd2d
