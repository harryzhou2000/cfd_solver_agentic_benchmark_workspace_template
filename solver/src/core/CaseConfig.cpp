#include "core/CaseConfig.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Exception.hpp"

namespace cfd {
namespace {

using json = nlohmann::json;

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

const json& require(const json& j, const char* key, const char* ctx) {
  auto it = j.find(key);
  CFD_CHECK(it != j.end(), "case file: missing required field '" << key << "' in " << ctx);
  return *it;
}

template <typename T>
T getOr(const json& j, const char* key, T fallback) {
  auto it = j.find(key);
  if (it == j.end() || it->is_null()) return fallback;
  return it->get<T>();
}

BcType parseBc(const std::string& name, const std::string& family) {
  const std::string s = lower(name);
  if (s == "farfield") return BcType::kFarfield;
  if (s == "slip_wall") return BcType::kSlipWall;
  if (s == "no_slip_adiabatic_wall") return BcType::kNoSlipAdiabaticWall;
  CFD_THROW("unsupported boundary condition '" << name << "' for mesh family '" << family
            << "' (supported: farfield, slip_wall, no_slip_adiabatic_wall)");
}

}  // namespace

const char* toString(BcType t) {
  switch (t) {
    case BcType::kFarfield: return "farfield";
    case BcType::kSlipWall: return "slip_wall";
    case BcType::kNoSlipAdiabaticWall: return "no_slip_adiabatic_wall";
  }
  return "unknown";
}

const char* toString(PhysicsMode m) {
  return m == PhysicsMode::kInviscid ? "inviscid" : "laminar";
}

Vec2 CaseConfig::freestreamVelocity() const {
  const Real a = freestream.aoa_degrees * M_PI / 180.0;
  return {freestream.velocity_magnitude * std::cos(a),
          freestream.velocity_magnitude * std::sin(a)};
}

Real CaseConfig::freestreamTemperature() const {
  return freestream.pressure / (freestream.rho * gas.R);
}

Real CaseConfig::freestreamSoundSpeed() const {
  return std::sqrt(gas.gamma * freestream.pressure / freestream.rho);
}

Real CaseConfig::dynamicPressure() const {
  const Real u = freestream.velocity_magnitude;
  return 0.5 * freestream.rho * u * u;
}

ConsVec CaseConfig::freestreamConservative() const {
  const Vec2 v = freestreamVelocity();
  const Real rho = freestream.rho;
  const Real p = freestream.pressure;
  const Real e = p / ((gas.gamma - 1.0) * rho);
  ConsVec u{};
  u[0] = rho;
  u[1] = rho * v[0];
  u[2] = rho * v[1];
  u[3] = rho * (e + 0.5 * (v[0] * v[0] + v[1] * v[1]));
  return u;
}

Real CaseConfig::molecularViscosity() const {
  if (mode == PhysicsMode::kInviscid) return 0.0;
  CFD_CHECK(reynolds > 0.0, "laminar case requires a positive physics.reynolds value");
  return freestream.rho * freestream.velocity_magnitude * reference.reynolds_length / reynolds;
}

CaseConfig CaseConfig::loadFromFile(const std::string& path) {
  std::ifstream in(path);
  CFD_CHECK(in.good(), "cannot open case file '" << path << "'");
  json j;
  try {
    in >> j;
  } catch (const std::exception& e) {
    CFD_THROW("malformed JSON in case file '" << path << "': " << e.what());
  }
  CFD_CHECK(j.is_object(), "case file '" << path << "' must contain a JSON object");

  CaseConfig c;
  c.source_path = std::filesystem::absolute(path).string();
  c.schema_version = getOr<int>(j, "schema_version", 1);
  CFD_CHECK(c.schema_version == 1,
            "unsupported case schema_version " << c.schema_version << " (this solver supports 1)");

  c.case_id = require(j, "case_id", "case root").get<std::string>();
  c.description = getOr<std::string>(j, "description", "");

  {
    const json& m = require(j, "mesh", "case root");
    const std::string rel = require(m, "file", "mesh").get<std::string>();
    std::filesystem::path p(rel);
    if (p.is_relative()) p = std::filesystem::path(path).parent_path() / p;
    c.mesh_file = std::filesystem::weakly_canonical(p).string();
    c.mesh_format = getOr<std::string>(m, "format", std::string("CGNS"));
    c.mesh_dimension = getOr<int>(m, "dimension", 2);
    CFD_CHECK(lower(c.mesh_format) == "cgns",
              "unsupported mesh format '" << c.mesh_format << "' (only CGNS is implemented)");
    CFD_CHECK(c.mesh_dimension == 2,
              "unsupported mesh dimension " << c.mesh_dimension << " (this build is 2-D)");
    CFD_CHECK(std::filesystem::exists(c.mesh_file),
              "mesh file not found: '" << c.mesh_file << "'");
  }

  {
    const json& p = require(j, "physics", "case root");
    c.equations = getOr<std::string>(p, "equations", std::string("compressible_navier_stokes"));
    CFD_CHECK(lower(c.equations) == "compressible_navier_stokes",
              "unsupported physics.equations '" << c.equations << "'");
    const std::string mode = lower(getOr<std::string>(p, "mode", std::string("inviscid")));
    if (mode == "inviscid") {
      c.mode = PhysicsMode::kInviscid;
    } else if (mode == "laminar") {
      c.mode = PhysicsMode::kLaminar;
    } else {
      CFD_THROW("unsupported physics.mode '" << mode << "' (inviscid|laminar)");
    }
    c.reynolds = getOr<Real>(p, "reynolds", 0.0);
    c.viscosity_model = lower(getOr<std::string>(p, "viscosity_model", std::string("constant")));
    CFD_CHECK(c.viscosity_model == "constant" || c.viscosity_model == "sutherland",
              "unsupported physics.viscosity_model '" << c.viscosity_model << "'");
  }

  {
    const json& g = require(j, "gas", "case root");
    c.gas.model = lower(getOr<std::string>(g, "model", std::string("calorically_perfect")));
    CFD_CHECK(c.gas.model == "calorically_perfect",
              "unsupported gas.model '" << c.gas.model << "' (only calorically_perfect)");
    c.gas.gamma = getOr<Real>(g, "gamma", 1.4);
    c.gas.R = getOr<Real>(g, "R", 1.0);
    c.gas.prandtl = getOr<Real>(g, "prandtl", 0.72);
    CFD_CHECK(c.gas.gamma > 1.0, "gas.gamma must be > 1");
    CFD_CHECK(c.gas.R > 0.0, "gas.R must be > 0");
    CFD_CHECK(c.gas.prandtl > 0.0, "gas.prandtl must be > 0");
  }

  {
    const json& f = require(j, "freestream", "case root");
    c.freestream.mach = getOr<Real>(f, "mach", 0.0);
    c.freestream.aoa_degrees = getOr<Real>(f, "aoa_degrees", 0.0);
    c.freestream.rho = getOr<Real>(f, "rho", 1.0);
    c.freestream.velocity_magnitude = getOr<Real>(f, "velocity_magnitude", 1.0);
    c.freestream.pressure = getOr<Real>(f, "pressure", 1.0);
    CFD_CHECK(c.freestream.rho > 0.0 && c.freestream.pressure > 0.0,
              "freestream rho and pressure must be positive");
  }

  {
    const json& r = require(j, "reference", "case root");
    c.reference.length = getOr<Real>(r, "length", 1.0);
    c.reference.area = getOr<Real>(r, "area", 1.0);
    c.reference.reynolds_length = getOr<Real>(r, "reynolds_length", c.reference.length);
    auto it = r.find("moment_center");
    if (it != r.end() && it->is_array() && it->size() >= 2) {
      c.reference.moment_center = {(*it)[0].get<Real>(), (*it)[1].get<Real>()};
    }
    CFD_CHECK(c.reference.area > 0.0 && c.reference.length > 0.0,
              "reference.area and reference.length must be positive");
  }

  {
    const json& b = require(j, "boundary_conditions", "case root");
    CFD_CHECK(b.is_object() && !b.empty(), "boundary_conditions must be a non-empty object");
    for (auto it = b.begin(); it != b.end(); ++it) {
      c.boundary_conditions[it.key()] = parseBc(it.value().get<std::string>(), it.key());
    }
  }

  {
    auto it = j.find("numerics_required");
    if (it != j.end() && it->is_object()) {
      c.required_spatial_order = getOr<int>(*it, "spatial_order", 2);
      c.required_inviscid_flux = getOr<std::string>(*it, "inviscid_flux", std::string(""));
      c.required_viscous_flux = getOr<std::string>(*it, "viscous_flux", std::string(""));
      c.required_main_time_method = getOr<std::string>(*it, "main_time_method", std::string(""));
      c.required_transient_order = getOr<int>(*it, "transient_order", 0);
    }
  }

  {
    const json& r = require(j, "run_control", "case root");
    const std::string type = lower(getOr<std::string>(r, "type", std::string("steady")));
    if (type == "steady") {
      c.run.type = RunType::kSteady;
    } else if (type == "transient" || type == "unsteady") {
      c.run.type = RunType::kTransient;
    } else {
      CFD_THROW("unsupported run_control.type '" << type << "' (steady|transient)");
    }
    c.run.max_steps = getOr<int>(r, "max_steps", 1000);
    c.run.residual_reduction_target = getOr<Real>(r, "residual_reduction_target", 4.0);
    c.run.time_step = getOr<Real>(r, "time_step", 0.0);
    c.run.final_time = getOr<Real>(r, "final_time", 0.0);
    const std::string ti = lower(getOr<std::string>(r, "time_integrator", std::string("bdf2_or_trapezoidal")));
    if (ti.find("trapezoid") != std::string::npos && ti.find("bdf2") == std::string::npos) {
      c.run.time_integrator = TimeIntegratorType::kTrapezoidal;
    } else {
      c.run.time_integrator = TimeIntegratorType::kBdf2;
    }
    c.run.inner_residual_norm =
        lower(getOr<std::string>(r, "inner_residual_norm", c.run.inner_residual_norm));
    c.run.bdf2_history_update =
        lower(getOr<std::string>(r, "bdf2_history_update", c.run.bdf2_history_update));
    c.run.cfl_initial = getOr<Real>(r, "cfl_initial", 1.0);
    c.run.cfl_max = getOr<Real>(r, "cfl_max", c.run.cfl_initial);
    c.run.pseudo_cfl_ramp_steps = getOr<int>(r, "pseudo_cfl_ramp_steps", 0);
    c.run.min_inner_iterations = getOr<int>(r, "min_inner_iterations", 1);
    c.run.max_inner_iterations = getOr<int>(r, "max_inner_iterations", 20);
    c.run.inner_residual_reduction_target =
        getOr<Real>(r, "inner_residual_reduction_target", 1.0e-2);
    c.run.rusanov_dissipation_scale = getOr<Real>(r, "rusanov_dissipation_scale", 1.0);
    if (c.run.type == RunType::kTransient) {
      CFD_CHECK(c.run.time_step > 0.0, "transient run_control requires time_step > 0");
      CFD_CHECK(c.run.final_time > 0.0, "transient run_control requires final_time > 0");
    } else {
      CFD_CHECK(c.run.max_steps > 0, "steady run_control requires max_steps > 0");
    }
    CFD_CHECK(c.run.cfl_initial > 0.0 && c.run.cfl_max >= c.run.cfl_initial,
              "require 0 < cfl_initial <= cfl_max");
    CFD_CHECK(c.run.min_inner_iterations >= 1 &&
                  c.run.max_inner_iterations >= c.run.min_inner_iterations,
              "require 1 <= min_inner_iterations <= max_inner_iterations");
  }

  {
    auto it = j.find("outputs");
    if (it != j.end() && it->is_object()) {
      const json& o = *it;
      c.outputs.write_final_field = getOr<bool>(o, "write_final_field", true);
      c.outputs.write_surface = getOr<bool>(o, "write_surface", true);
      c.outputs.write_forces_every = std::max(1, getOr<int>(o, "write_forces_every", 1));
      c.outputs.write_residuals_every = std::max(1, getOr<int>(o, "write_residuals_every", 1));
      c.outputs.write_field_every_time = getOr<Real>(o, "write_field_every_time", 0.0);
      c.outputs.wake_visualization = getOr<std::string>(o, "wake_visualization", std::string(""));
      auto cr = o.find("recommended_vorticity_clip_range");
      if (cr != o.end() && cr->is_array()) {
        for (const auto& v : *cr) c.outputs.recommended_vorticity_clip_range.push_back(v.get<Real>());
      }
    }
  }

  c.validate();
  return c;
}

void CaseConfig::validate() const {
  CFD_CHECK(!case_id.empty(), "case_id must not be empty");
  if (mode == PhysicsMode::kLaminar) {
    CFD_CHECK(reynolds > 0.0, "laminar case '" << case_id << "' needs physics.reynolds > 0");
  }
  bool has_wall = false;
  for (const auto& kv : boundary_conditions) {
    if (kv.second != BcType::kFarfield) has_wall = true;
    if (mode == PhysicsMode::kInviscid && kv.second == BcType::kNoSlipAdiabaticWall) {
      CFD_THROW("case '" << case_id << "' is inviscid but maps family '" << kv.first
                << "' to no_slip_adiabatic_wall");
    }
  }
  CFD_CHECK(has_wall, "case '" << case_id << "' declares no wall boundary family");
}

}  // namespace cfd
