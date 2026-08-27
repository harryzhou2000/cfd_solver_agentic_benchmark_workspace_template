#pragma once
#include <string>
#include <map>
#include <array>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

enum class PhysicsMode { Inviscid, Laminar };
enum class RunType { Steady, Transient };
enum class BcType { Farfield, SlipWall, NoSlipAdiabaticWall, Unknown };

struct GasConfig {
    double gamma = 1.4, R = 1.0, prandtl = 0.72;
};
struct FreestreamConfig {
    double mach = 0.1, aoa_degrees = 0.0;
    double rho = 1.0, velocity = 1.0, pressure = 71.43;
};
struct ReferenceConfig {
    double length = 1.0, area = 1.0;
    std::array<double,2> moment_center = {0.0, 0.0};
    double reynolds_length = 1.0;
};
struct RunControl {
    RunType type = RunType::Steady;
    int max_steps = 10000;
    double residual_reduction_target = 4.0;
    double cfl_initial = 1.0, cfl_max = 100.0;
    int pseudo_cfl_ramp_steps = 2000;
    int min_inner = 3, max_inner = 50;
    double inner_residual_tol = 0.01;
    double time_step = 0.01, final_time = 300.0;
    double rusanov_dissipation_scale = 1.0;
    bool write_final_field = true;
    bool write_surface = true;
    int write_forces_every = 1;
    int write_residuals_every = 1;
    double write_field_every_time = 1.0;
};
struct CaseConfig {
    int schema_version = 1;
    std::string case_id, mesh_file;
    PhysicsMode mode = PhysicsMode::Inviscid;
    double reynolds = 0.0;
    GasConfig gas;
    FreestreamConfig freestream;
    ReferenceConfig reference;
    std::map<std::string, BcType> boundary_conditions;
    RunControl run_control;

    static BcType parseBcType(const std::string& s) {
        if (s == "farfield") return BcType::Farfield;
        if (s == "slip_wall") return BcType::SlipWall;
        if (s == "no_slip_adiabatic_wall") return BcType::NoSlipAdiabaticWall;
        return BcType::Unknown;
    }
    static CaseConfig fromJson(const json& j, const std::string& case_dir) {
        CaseConfig c;
        c.schema_version = j.value("schema_version", 1);
        if (c.schema_version != 1)
            throw std::runtime_error("Unsupported schema_version: " + std::to_string(c.schema_version));
        c.case_id = j.at("case_id").get<std::string>();
        auto mf = j.at("mesh").at("file").get<std::string>();
        c.mesh_file = (mf[0]=='/') ? mf : case_dir + "/" + mf;
        auto phys = j.at("physics");
        c.mode = (phys.at("mode").get<std::string>()=="laminar") ? PhysicsMode::Laminar : PhysicsMode::Inviscid;
        if (phys.contains("reynolds")) c.reynolds = phys.at("reynolds").get<double>();
        auto gas = j.at("gas");
        c.gas = {gas.value("gamma",1.4), gas.value("R",1.0), gas.value("prandtl",0.72)};
        auto fs = j.at("freestream");
        c.freestream = {fs.value("mach",0.1), fs.value("aoa_degrees",0.0),
                        fs.value("rho",1.0), fs.value("velocity_magnitude",1.0), fs.value("pressure",71.43)};
        if (j.contains("reference")) {
            auto ref = j.at("reference");
            c.reference.length = ref.value("length",1.0);
            c.reference.area   = ref.value("area",1.0);
            c.reference.reynolds_length = ref.value("reynolds_length",1.0);
            if (ref.contains("moment_center")) {
                c.reference.moment_center[0] = ref.at("moment_center")[0].get<double>();
                c.reference.moment_center[1] = ref.at("moment_center")[1].get<double>();
            }
        }
        for (auto& [k, v] : j.at("boundary_conditions").items())
            c.boundary_conditions[k] = parseBcType(v.get<std::string>());
        auto rc = j.at("run_control");
        auto rtype = rc.value("type","steady");
        c.run_control.type = (rtype=="transient") ? RunType::Transient : RunType::Steady;
        c.run_control.max_steps = rc.value("max_steps",10000);
        c.run_control.residual_reduction_target = rc.value("residual_reduction_target",4.0);
        c.run_control.cfl_initial = rc.value("cfl_initial",1.0);
        c.run_control.cfl_max = rc.value("cfl_max",100.0);
        c.run_control.pseudo_cfl_ramp_steps = rc.value("pseudo_cfl_ramp_steps",2000);
        c.run_control.min_inner = rc.value("min_inner_iterations",3);
        c.run_control.max_inner = rc.value("max_inner_iterations",50);
        c.run_control.inner_residual_tol = rc.value("inner_residual_reduction_target",0.01);
        c.run_control.time_step = rc.value("time_step",0.01);
        c.run_control.final_time = rc.value("final_time",300.0);
        c.run_control.rusanov_dissipation_scale = rc.value("rusanov_dissipation_scale",1.0);
        if (j.contains("outputs")) {
            auto out = j.at("outputs");
            c.run_control.write_final_field  = out.value("write_final_field",true);
            c.run_control.write_surface      = out.value("write_surface",true);
            c.run_control.write_forces_every = out.value("write_forces_every",1);
            c.run_control.write_residuals_every = out.value("write_residuals_every",1);
            c.run_control.write_field_every_time = out.value("write_field_every_time",1.0);
        }
        return c;
    }
};
