#include "core/case_input.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/exceptions.h"
#include "core/path_utils.h"

namespace cns2d {
namespace {

using json = nlohmann::json;

constexpr int kSupportedSchemaVersion = 1;

// --- small typed accessors with explicit error messages --------------------

const json &requireObject(const json &parent, const char *key, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end()) {
    throw CnsError(ctx + ": missing required object '" + key + "'");
  }
  if (!it->is_object()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a JSON object");
  }
  return *it;
}

Real requireReal(const json &parent, const char *key, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end()) {
    throw CnsError(ctx + ": missing required number '" + key + "'");
  }
  if (!it->is_number()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a number");
  }
  return it->get<Real>();
}

Real optionalReal(const json &parent, const char *key, Real fallback, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end() || it->is_null()) return fallback;
  if (!it->is_number()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a number");
  }
  return it->get<Real>();
}

int optionalInt(const json &parent, const char *key, int fallback, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end() || it->is_null()) return fallback;
  if (!it->is_number_integer() && !it->is_number_unsigned()) {
    if (it->is_number_float()) {
      const Real v = it->get<Real>();
      return static_cast<int>(v);
    }
    throw CnsError(ctx + ": '" + std::string(key) + "' must be an integer");
  }
  return it->get<int>();
}

bool optionalBool(const json &parent, const char *key, bool fallback, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end() || it->is_null()) return fallback;
  if (!it->is_boolean()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a boolean");
  }
  return it->get<bool>();
}

std::string optionalString(const json &parent, const char *key, const std::string &fallback,
                           const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end() || it->is_null()) return fallback;
  if (!it->is_string()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a string");
  }
  return it->get<std::string>();
}

std::string requireString(const json &parent, const char *key, const std::string &ctx) {
  auto it = parent.find(key);
  if (it == parent.end()) {
    throw CnsError(ctx + ": missing required string '" + key + "'");
  }
  if (!it->is_string()) {
    throw CnsError(ctx + ": '" + std::string(key) + "' must be a string");
  }
  return it->get<std::string>();
}

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

}  // namespace

std::string bcTypeName(BCType t) {
  switch (t) {
    case BCType::kFarfield:
      return "farfield";
    case BCType::kSlipWall:
      return "slip_wall";
    case BCType::kNoSlipAdiabaticWall:
      return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

BCType parseBCType(const std::string &name) {
  const std::string key = toLower(name);
  if (key == "farfield") return BCType::kFarfield;
  if (key == "slip_wall") return BCType::kSlipWall;
  if (key == "no_slip_adiabatic_wall") return BCType::kNoSlipAdiabaticWall;
  throw CnsError("unsupported boundary-condition type '" + name +
                 "' (supported: farfield, slip_wall, no_slip_adiabatic_wall)");
}

CaseInput loadCaseInput(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    throw CnsError("cannot open case file: " + path);
  }
  json root;
  try {
    in >> root;
  } catch (const json::exception &e) {
    throw CnsError("case file " + path + " is not valid JSON: " + e.what());
  }
  if (!root.is_object()) {
    throw CnsError("case file " + path + " must contain a JSON object at top level");
  }

  const std::string ctx = "case file " + path;
  CaseInput c;
  c.case_file_path = absolutePath(path);
  const std::string case_dir = parentDirectory(c.case_file_path);

  c.schema_version = optionalInt(root, "schema_version", 1, ctx);
  if (c.schema_version != kSupportedSchemaVersion) {
    throw CnsError(ctx + ": unsupported schema_version " + std::to_string(c.schema_version) +
                   " (this solver supports version " + std::to_string(kSupportedSchemaVersion) + ")");
  }

  c.case_id = requireString(root, "case_id", ctx);
  c.description = optionalString(root, "description", "", ctx);

  // --- mesh ---------------------------------------------------------------
  {
    const json &mesh = requireObject(root, "mesh", ctx);
    const std::string rel = requireString(mesh, "file", ctx + " [mesh]");
    c.mesh_file = resolveRelativeTo(case_dir, rel);
    c.mesh_format = optionalString(mesh, "format", "CGNS", ctx + " [mesh]");
    if (toLower(c.mesh_format) != "cgns") {
      throw CnsError(ctx + ": unsupported mesh format '" + c.mesh_format + "' (only CGNS is supported)");
    }
    c.mesh_dimension = optionalInt(mesh, "dimension", 2, ctx + " [mesh]");
    if (c.mesh_dimension != 2) {
      throw CnsError(ctx + ": this solver build supports mesh dimension 2, got " +
                     std::to_string(c.mesh_dimension));
    }
    if (!fileExists(c.mesh_file)) {
      throw CnsError(ctx + ": mesh file not found: " + c.mesh_file);
    }
  }

  // --- physics ------------------------------------------------------------
  {
    const json &p = requireObject(root, "physics", ctx);
    c.physics.equations = optionalString(p, "equations", "compressible_navier_stokes", ctx + " [physics]");
    const std::string mode = toLower(requireString(p, "mode", ctx + " [physics]"));
    if (mode == "inviscid") {
      c.physics.mode = PhysicsMode::kInviscid;
    } else if (mode == "laminar") {
      c.physics.mode = PhysicsMode::kLaminar;
    } else {
      throw CnsError(ctx + ": unsupported physics.mode '" + mode + "' (supported: inviscid, laminar)");
    }
    if (c.physics.mode == PhysicsMode::kLaminar) {
      c.physics.reynolds = requireReal(p, "reynolds", ctx + " [physics]");
      if (!(c.physics.reynolds > 0.0)) {
        throw CnsError(ctx + ": physics.reynolds must be positive for laminar mode");
      }
      const std::string vm = toLower(optionalString(p, "viscosity_model", "constant", ctx + " [physics]"));
      if (vm == "constant") {
        c.physics.viscosity_model = ViscosityModel::kConstant;
      } else if (vm == "sutherland") {
        c.physics.viscosity_model = ViscosityModel::kSutherland;
      } else {
        throw CnsError(ctx + ": unsupported physics.viscosity_model '" + vm + "'");
      }
    }
  }

  // --- gas ----------------------------------------------------------------
  {
    const json &g = requireObject(root, "gas", ctx);
    c.gas.model = optionalString(g, "model", "calorically_perfect", ctx + " [gas]");
    if (toLower(c.gas.model) != "calorically_perfect") {
      throw CnsError(ctx + ": unsupported gas.model '" + c.gas.model +
                     "' (this build implements calorically_perfect)");
    }
    c.gas.gamma = requireReal(g, "gamma", ctx + " [gas]");
    c.gas.R = requireReal(g, "R", ctx + " [gas]");
    c.gas.prandtl = optionalReal(g, "prandtl", 0.72, ctx + " [gas]");
    if (!(c.gas.gamma > 1.0)) throw CnsError(ctx + ": gas.gamma must exceed 1");
    if (!(c.gas.R > 0.0)) throw CnsError(ctx + ": gas.R must be positive");
    if (!(c.gas.prandtl > 0.0)) throw CnsError(ctx + ": gas.prandtl must be positive");
  }

  // --- freestream ---------------------------------------------------------
  {
    const json &f = requireObject(root, "freestream", ctx);
    c.freestream.mach = requireReal(f, "mach", ctx + " [freestream]");
    c.freestream.aoa_degrees = optionalReal(f, "aoa_degrees", 0.0, ctx + " [freestream]");
    c.freestream.rho = requireReal(f, "rho", ctx + " [freestream]");
    c.freestream.velocity_magnitude = requireReal(f, "velocity_magnitude", ctx + " [freestream]");
    c.freestream.pressure = requireReal(f, "pressure", ctx + " [freestream]");
    if (!(c.freestream.rho > 0.0)) throw CnsError(ctx + ": freestream.rho must be positive");
    if (!(c.freestream.pressure > 0.0)) throw CnsError(ctx + ": freestream.pressure must be positive");
    if (!(c.freestream.mach > 0.0)) throw CnsError(ctx + ": freestream.mach must be positive");
  }

  // --- reference ----------------------------------------------------------
  {
    const json &r = requireObject(root, "reference", ctx);
    c.reference.length = optionalReal(r, "length", 1.0, ctx + " [reference]");
    c.reference.area = optionalReal(r, "area", 1.0, ctx + " [reference]");
    c.reference.reynolds_length = optionalReal(r, "reynolds_length", c.reference.length, ctx + " [reference]");
    auto it = r.find("moment_center");
    if (it != r.end()) {
      if (!it->is_array() || it->size() < 2) {
        throw CnsError(ctx + ": reference.moment_center must be an array of at least 2 numbers");
      }
      c.reference.moment_center.x = (*it)[0].get<Real>();
      c.reference.moment_center.y = (*it)[1].get<Real>();
    }
    if (!(c.reference.length > 0.0)) throw CnsError(ctx + ": reference.length must be positive");
    if (!(c.reference.area > 0.0)) throw CnsError(ctx + ": reference.area must be positive");
    if (!(c.reference.reynolds_length > 0.0)) throw CnsError(ctx + ": reference.reynolds_length must be positive");
  }

  // --- boundary conditions ------------------------------------------------
  {
    const json &bc = requireObject(root, "boundary_conditions", ctx);
    if (bc.empty()) {
      throw CnsError(ctx + ": boundary_conditions must map at least one mesh family name");
    }
    for (auto it = bc.begin(); it != bc.end(); ++it) {
      if (!it.value().is_string()) {
        throw CnsError(ctx + ": boundary_conditions['" + it.key() + "'] must be a string BC type");
      }
      c.boundary_conditions.emplace(it.key(), parseBCType(it.value().get<std::string>()));
    }
  }

  // --- numerics_required (advisory; global task requirements always apply) -
  {
    auto it = root.find("numerics_required");
    if (it != root.end() && it->is_object()) {
      const json &n = *it;
      const std::string nctx = ctx + " [numerics_required]";
      c.numerics_required.spatial_order = optionalInt(n, "spatial_order", 2, nctx);
      c.numerics_required.inviscid_flux = optionalString(n, "inviscid_flux", "approximate_riemann", nctx);
      c.numerics_required.viscous_flux = optionalString(n, "viscous_flux", "disabled", nctx);
      c.numerics_required.main_time_method = optionalString(n, "main_time_method", "implicit", nctx);
      c.numerics_required.implicit_solver = optionalString(n, "implicit_solver", "required", nctx);
      c.numerics_required.transient_order = optionalInt(n, "transient_order", 0, nctx);
      if (c.numerics_required.spatial_order > 2) {
        throw CnsError(nctx + ": requested spatial_order " +
                       std::to_string(c.numerics_required.spatial_order) +
                       " exceeds the second-order scheme implemented by this solver");
      }
    }
  }

  // --- run control --------------------------------------------------------
  {
    const json &rc = requireObject(root, "run_control", ctx);
    const std::string rctx = ctx + " [run_control]";
    const std::string type = toLower(optionalString(rc, "type", "steady", rctx));
    if (type == "steady") {
      c.run_control.type = RunType::kSteady;
    } else if (type == "transient" || type == "unsteady") {
      c.run_control.type = RunType::kTransient;
    } else {
      throw CnsError(rctx + ": unsupported run_control.type '" + type + "'");
    }

    c.run_control.max_steps = optionalInt(rc, "max_steps", 20000, rctx);
    c.run_control.residual_reduction_target = optionalReal(rc, "residual_reduction_target", 4.0, rctx);
    c.run_control.cfl_initial = optionalReal(rc, "cfl_initial", 1.0, rctx);
    c.run_control.cfl_max = optionalReal(rc, "cfl_max", c.run_control.cfl_initial, rctx);
    c.run_control.pseudo_cfl_ramp_steps = optionalInt(rc, "pseudo_cfl_ramp_steps", 0, rctx);
    c.run_control.min_inner_iterations = optionalInt(rc, "min_inner_iterations", 3, rctx);
    c.run_control.max_inner_iterations = optionalInt(rc, "max_inner_iterations", 50, rctx);
    c.run_control.inner_residual_reduction_target =
        optionalReal(rc, "inner_residual_reduction_target", 1.0e-2, rctx);
    c.run_control.rusanov_dissipation_scale = optionalReal(rc, "rusanov_dissipation_scale", 1.0, rctx);

    if (!(c.run_control.cfl_initial > 0.0) || !(c.run_control.cfl_max > 0.0)) {
      throw CnsError(rctx + ": cfl_initial and cfl_max must be positive");
    }
    if (c.run_control.cfl_max < c.run_control.cfl_initial) {
      throw CnsError(rctx + ": cfl_max must be >= cfl_initial");
    }
    if (c.run_control.min_inner_iterations < 1) {
      throw CnsError(rctx + ": min_inner_iterations must be >= 1");
    }
    if (c.run_control.max_inner_iterations < c.run_control.min_inner_iterations) {
      throw CnsError(rctx + ": max_inner_iterations must be >= min_inner_iterations");
    }

    if (c.run_control.type == RunType::kTransient) {
      c.run_control.time_integrator = optionalString(rc, "time_integrator", "bdf2_or_trapezoidal", rctx);
      c.run_control.time_step = requireReal(rc, "time_step", rctx);
      c.run_control.final_time = requireReal(rc, "final_time", rctx);
      c.run_control.inner_residual_norm =
          optionalString(rc, "inner_residual_norm", "total_spatial_plus_physical_time", rctx);
      c.run_control.bdf2_history_update =
          optionalString(rc, "bdf2_history_update", "after_inner_convergence", rctx);
      if (!(c.run_control.time_step > 0.0)) throw CnsError(rctx + ": time_step must be positive");
      if (!(c.run_control.final_time > 0.0)) throw CnsError(rctx + ": final_time must be positive");
      const std::string ti = toLower(c.run_control.time_integrator);
      if (ti.find("bdf2") == std::string::npos && ti.find("trapezoid") == std::string::npos) {
        throw CnsError(rctx + ": unsupported transient time_integrator '" + c.run_control.time_integrator +
                       "' (this build implements BDF2 / trapezoidal dual-time stepping)");
      }
      if (c.run_control.max_steps <= 0) {
        c.run_control.max_steps = static_cast<int>(c.run_control.final_time / c.run_control.time_step + 0.5);
      }
    }
    if (c.run_control.type == RunType::kSteady && c.run_control.max_steps <= 0) {
      throw CnsError(rctx + ": max_steps must be positive for steady runs");
    }
  }

  // --- outputs ------------------------------------------------------------
  {
    auto it = root.find("outputs");
    if (it != root.end() && it->is_object()) {
      const json &o = *it;
      const std::string octx = ctx + " [outputs]";
      c.outputs.write_final_field = optionalBool(o, "write_final_field", true, octx);
      c.outputs.write_surface = optionalBool(o, "write_surface", true, octx);
      c.outputs.write_forces_every = std::max(1, optionalInt(o, "write_forces_every", 1, octx));
      c.outputs.write_residuals_every = std::max(1, optionalInt(o, "write_residuals_every", 1, octx));
      c.outputs.write_field_every_time = optionalReal(o, "write_field_every_time", 0.0, octx);
      c.outputs.wake_visualization = optionalString(o, "wake_visualization", "", octx);
      auto clip = o.find("recommended_vorticity_clip_range");
      if (clip != o.end() && clip->is_array()) {
        for (const auto &v : *clip) {
          if (v.is_number()) c.outputs.recommended_vorticity_clip_range.push_back(v.get<Real>());
        }
      }
    }
  }

  return c;
}

}  // namespace cns2d
