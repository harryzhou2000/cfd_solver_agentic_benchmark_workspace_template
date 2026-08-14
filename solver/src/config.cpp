#include "config.hpp"

#include <cmath>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace cfd2d {

namespace {

std::string dirName(const std::string& path) {
  size_t p = path.find_last_of('/');
  return (p == std::string::npos) ? std::string(".") : path.substr(0, p);
}

std::string resolvePath(const std::string& baseDir, const std::string& p) {
  if (!p.empty() && p[0] == '/') return p;
  return baseDir + "/" + p;
}

template <typename T>
T getOr(const nlohmann::json& j, const char* key, T def) {
  if (j.contains(key)) return j.at(key).get<T>();
  return def;
}

}  // namespace

CaseConfig loadCase(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw FatalError("cannot open case file: " + path);
  nlohmann::json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    throw FatalError("malformed JSON in case file " + path + ": " + e.what());
  }

  CaseConfig c;
  c.schemaVersion = getOr<int>(j, "schema_version", 1);
  if (c.schemaVersion != 1) {
    throw FatalError("unsupported case schema_version " +
                     std::to_string(c.schemaVersion) + " in " + path);
  }
  c.caseId = j.value("case_id", "");
  if (c.caseId.empty()) throw FatalError("case file missing case_id: " + path);
  c.description = j.value("description", "");

  const std::string base = dirName(path);
  if (!j.contains("mesh") || !j.at("mesh").contains("file")) {
    throw FatalError("case file missing mesh.file: " + path);
  }
  c.meshFile = resolvePath(base, j.at("mesh").at("file").get<std::string>());
  {
    std::ifstream meshIn(c.meshFile);
    if (!meshIn) throw FatalError("missing mesh file: " + c.meshFile);
  }

  const auto& phys = j.at("physics");
  c.physicsMode = phys.value("mode", "inviscid");
  if (c.physicsMode != "inviscid" && c.physicsMode != "laminar") {
    throw FatalError("unsupported physics.mode: " + c.physicsMode);
  }
  c.reynolds = phys.value("reynolds", 0.0);
  c.viscosityModel = phys.value("viscosity_model", "constant");
  if (c.isViscous() && !(c.reynolds > 0.0)) {
    throw FatalError("laminar case requires positive physics.reynolds");
  }
  if (c.viscosityModel != "constant") {
    throw FatalError("unsupported viscosity_model: " + c.viscosityModel);
  }

  const auto& gas = j.at("gas");
  c.gas.gamma = gas.value("gamma", 1.4);
  c.gas.R = gas.value("R", 1.0);
  c.gas.prandtl = gas.value("prandtl", 0.72);
  if (c.gas.gamma <= 1.0 || c.gas.R <= 0.0 || c.gas.prandtl <= 0.0) {
    throw FatalError("invalid gas constants");
  }

  const auto& fs = j.at("freestream");
  c.freestream.mach = fs.value("mach", 0.0);
  c.freestream.aoaDeg = fs.value("aoa_degrees", 0.0);
  c.freestream.rho = fs.value("rho", 1.0);
  c.freestream.velMag = fs.value("velocity_magnitude", 1.0);
  c.freestream.pressure = fs.value("pressure", 1.0);
  const double aoa = c.freestream.aoaDeg * M_PI / 180.0;
  c.freestream.u = c.freestream.velMag * std::cos(aoa);
  c.freestream.v = c.freestream.velMag * std::sin(aoa);
  c.freestream.soundSpeed =
      std::sqrt(c.gas.gamma * c.freestream.pressure / c.freestream.rho);

  const auto& ref = j.at("reference");
  c.reference.length = ref.value("length", 1.0);
  c.reference.area = ref.value("area", 1.0);
  c.reference.reynoldsLength = ref.value("reynolds_length", c.reference.length);
  if (ref.contains("moment_center")) {
    c.reference.momentCenter = {ref.at("moment_center").at(0).get<double>(),
                                ref.at("moment_center").at(1).get<double>()};
  }

  const std::set<std::string> supportedBc = {"farfield", "slip_wall",
                                             "no_slip_adiabatic_wall"};
  for (const auto& [name, type] : j.at("boundary_conditions").items()) {
    const std::string t = type.get<std::string>();
    if (!supportedBc.count(t)) {
      throw FatalError("unsupported boundary condition type '" + t +
                       "' for family '" + name + "'");
    }
    c.bcMap[name] = t;
  }
  if (c.bcMap.empty()) throw FatalError("case has no boundary_conditions");

  const auto& rc = j.at("run_control");
  c.run.type = rc.value("type", "steady");
  if (c.run.type != "steady" && c.run.type != "transient") {
    throw FatalError("unsupported run_control.type: " + c.run.type);
  }
  c.run.maxSteps = rc.value("max_steps", 0);
  c.run.residualReductionTarget = rc.value("residual_reduction_target", 4.0);
  c.run.cflInitial = rc.value("cfl_initial", 1.0);
  c.run.cflMax = rc.value("cfl_max", 100.0);
  c.run.pseudoCflRampSteps = rc.value("pseudo_cfl_ramp_steps", 0);
  c.run.minInner = rc.value("min_inner_iterations", 3);
  c.run.maxInner = rc.value("max_inner_iterations", 50);
  c.run.innerResidualReductionTarget =
      rc.value("inner_residual_reduction_target", 0.01);
  c.run.timeIntegrator = rc.value("time_integrator", "");
  c.run.timeStep = rc.value("time_step", 0.0);
  c.run.finalTime = rc.value("final_time", 0.0);
  c.run.rusanovDissipationScale = rc.value("rusanov_dissipation_scale", 1.0);

  if (c.run.type == "steady") {
    if (c.run.maxSteps <= 0) throw FatalError("steady case needs max_steps>0");
  } else {
    if (!(c.run.timeStep > 0.0) || !(c.run.finalTime > 0.0)) {
      throw FatalError("transient case needs time_step>0 and final_time>0");
    }
    if (c.run.timeIntegrator != "bdf2_or_trapezoidal" &&
        c.run.timeIntegrator != "bdf2" && c.run.timeIntegrator != "") {
      throw FatalError("unsupported time_integrator: " + c.run.timeIntegrator);
    }
  }
  if (c.run.cflInitial <= 0.0 || c.run.cflMax < c.run.cflInitial) {
    throw FatalError("invalid CFL range");
  }
  if (c.run.minInner < 1 || c.run.maxInner < c.run.minInner) {
    throw FatalError("invalid inner iteration bounds");
  }

  if (j.contains("outputs")) {
    const auto& out = j.at("outputs");
    c.writeForcesEvery = out.value("write_forces_every", 1);
    c.writeResidualsEvery = out.value("write_residuals_every", 1);
    c.run.writeFieldEveryTime = out.value("write_field_every_time", 0.0);
  }
  return c;
}

}  // namespace cfd2d
