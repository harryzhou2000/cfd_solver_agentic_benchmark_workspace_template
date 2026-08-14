#include "case_io.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace cfd2d {

std::string resolveMeshPath(const std::string& casePath, const std::string& meshFile) {
  fs::path cp(casePath);
  fs::path dir = cp.parent_path();
  fs::path mp(meshFile);
  if (mp.is_absolute()) return mp.string();
  fs::path resolved = dir / mp;
  return resolved.string();
}

bool loadCase(const std::string& path, CaseInput& ci, std::string& err) {
  std::ifstream f(path);
  if (!f) { err = "cannot open case file: " + path; return false; }
  json j;
  try { f >> j; } catch (std::exception& e) {
    err = std::string("JSON parse error: ") + e.what(); return false;
  }
  try {
    ci.schema_version = j.value("schema_version", 1);
    if (ci.schema_version != 1) {
      err = "unsupported schema_version " + std::to_string(ci.schema_version);
      return false;
    }
    ci.case_id = j.value("case_id", "");
    ci.description = j.value("description", "");
    if (j.contains("mesh")) {
      ci.meshFile = j["mesh"].value("file", "");
      ci.meshFormat = j["mesh"].value("format", "CGNS");
      ci.dimension = j["mesh"].value("dimension", 2);
    }
    if (j.contains("physics")) {
      ci.physicsMode = j["physics"].value("mode", "inviscid");
      ci.reynolds = j["physics"].value("reynolds", 0.0);
      ci.viscosityModel = j["physics"].value("viscosity_model", "constant");
    }
    if (j.contains("gas")) {
      ci.gamma = j["gas"].value("gamma", 1.4);
      ci.R = j["gas"].value("R", 1.0);
      ci.prandtl = j["gas"].value("prandtl", 0.72);
    }
    if (j.contains("freestream")) {
      ci.mach = j["freestream"].value("mach", 0.1);
      ci.aoaDeg = j["freestream"].value("aoa_degrees", 0.0);
      ci.rhoInf = j["freestream"].value("rho", 1.0);
      ci.velMag = j["freestream"].value("velocity_magnitude", 1.0);
      ci.pressureInf = j["freestream"].value("pressure", 1.0);
    }
    if (j.contains("reference")) {
      ci.refLength = j["reference"].value("length", 1.0);
      ci.refArea = j["reference"].value("area", 1.0);
      auto mc = j["reference"].value("moment_center", std::vector<double>{0.25, 0.0});
      if (mc.size() >= 2) { ci.momentCx = mc[0]; ci.momentCy = mc[1]; }
      ci.reynoldsLength = j["reference"].value("reynolds_length", 1.0);
    }
    if (j.contains("boundary_conditions")) {
      for (auto it = j["boundary_conditions"].begin(); it != j["boundary_conditions"].end(); ++it) {
        ci.boundaryConditions[it.key()] = it.value().get<std::string>();
      }
    }
    if (j.contains("run_control")) {
      auto& rc = j["run_control"];
      ci.runType = rc.value("type", "steady");
      ci.maxSteps = rc.value("max_steps", 20000);
      ci.residualReductionTarget = rc.value("residual_reduction_target", 4.0);
      ci.cflInitial = rc.value("cfl_initial", 1.0);
      ci.cflMax = rc.value("cfl_max", 100.0);
      ci.pseudoCflRampSteps = rc.value("pseudo_cfl_ramp_steps", 0);
      ci.minInnerIter = rc.value("min_inner_iterations", 3);
      ci.maxInnerIter = rc.value("max_inner_iterations", 50);
      ci.innerResidualReductionTarget = rc.value("inner_residual_reduction_target", 0.01);
      ci.timeIntegrator = rc.value("time_integrator", "");
      ci.timeStep = rc.value("time_step", 0.01);
      ci.finalTime = rc.value("final_time", 300.0);
      ci.innerResidualNorm = rc.value("inner_residual_norm", "");
      ci.bdf2HistoryUpdate = rc.value("bdf2_history_update", "");
      ci.rusanovDissipationScale = rc.value("rusanov_dissipation_scale", 1.0);
    }
    if (j.contains("outputs")) {
      auto& o = j["outputs"];
      ci.writeFinalField = o.value("write_final_field", true);
      ci.writeSurface = o.value("write_surface", true);
      ci.writeForcesEvery = o.value("write_forces_every", 1);
      ci.writeResidualsEvery = o.value("write_residuals_every", 1);
      ci.writeFieldEveryTime = o.value("write_field_every_time", 0.0);
      ci.wakeVisualization = o.value("wake_visualization", "");
      if (o.contains("recommended_vorticity_clip_range")) {
        for (auto& v : o["recommended_vorticity_clip_range"])
          ci.recommendedVorticityClipRange.push_back(v.get<double>());
      }
    }
  } catch (std::exception& e) {
    err = std::string("case parse error: ") + e.what();
    return false;
  }
  if (ci.case_id.empty()) { err = "case_id missing"; return false; }
  if (ci.meshFile.empty()) { err = "mesh.file missing"; return false; }
  return true;
}

} // namespace cfd2d
