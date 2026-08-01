#include "cfd/case_config.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace cfd {
namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::filesystem::path& file, const std::string& message) {
    throw CaseConfigError("Invalid case file '" + file.string() + "': " + message);
}

const json& required_object(const json& object, const char* key,
                            const std::filesystem::path& file) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_object()) {
        fail(file, "required object '" + std::string(key) + "' is missing or not an object");
    }
    return *it;
}

template <typename T>
T required_value(const json& object, const char* key, const std::filesystem::path& file) {
    const auto it = object.find(key);
    if (it == object.end()) {
        fail(file, "required field '" + std::string(key) + "' is missing");
    }
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        fail(file, "field '" + std::string(key) + "' has the wrong type");
    }
}

template <typename T>
std::optional<T> optional_value(const json& object, const char* key,
                                const std::filesystem::path& file) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        fail(file, "optional field '" + std::string(key) + "' has the wrong type");
    }
}

void require_nonempty(const std::string& value, const char* field,
                      const std::filesystem::path& file) {
    if (value.empty()) {
        fail(file, "field '" + std::string(field) + "' must not be empty");
    }
}

void require_finite(Real value, const char* field, const std::filesystem::path& file) {
    if (!std::isfinite(value)) {
        fail(file, "field '" + std::string(field) + "' must be finite");
    }
}

void require_positive(Real value, const char* field, const std::filesystem::path& file) {
    require_finite(value, field, file);
    if (value <= 0.0) {
        fail(file, "field '" + std::string(field) + "' must be positive");
    }
}

void require_positive(int value, const char* field, const std::filesystem::path& file) {
    if (value <= 0) {
        fail(file, "field '" + std::string(field) + "' must be positive");
    }
}

PhysicsMode parse_mode(const std::string& value, const std::filesystem::path& file) {
    if (value == "inviscid") {
        return PhysicsMode::inviscid;
    }
    if (value == "laminar") {
        return PhysicsMode::laminar;
    }
    fail(file, "unsupported physics.mode '" + value + "'");
}

BoundaryType parse_boundary_condition(const std::string& value,
                                      const std::filesystem::path& file) {
    if (value == "farfield") {
        return BoundaryType::farfield;
    }
    if (value == "slip_wall") {
        return BoundaryType::slip_wall;
    }
    if (value == "no_slip_adiabatic_wall") {
        return BoundaryType::no_slip_adiabatic_wall;
    }
    fail(file, "unsupported boundary condition type '" + value + "'");
}

RunType parse_run_type(const std::string& value, const std::filesystem::path& file) {
    if (value == "steady") {
        return RunType::steady;
    }
    if (value == "transient") {
        return RunType::transient;
    }
    fail(file, "unsupported run_control.type '" + value + "'");
}

Vec2 parse_vec2(const json& object, const char* key, const std::filesystem::path& file) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() != 2U) {
        fail(file, "field '" + std::string(key) + "' must be a two-element array");
    }
    try {
        const Vec2 value{(*it)[0].get<Real>(), (*it)[1].get<Real>()};
        require_finite(value[0], key, file);
        require_finite(value[1], key, file);
        return value;
    } catch (const json::exception&) {
        fail(file, "field '" + std::string(key) + "' must contain numbers");
    }
}

void validate_run_control(const RunControlConfig& control, const std::filesystem::path& file) {
    require_positive(control.min_inner_iterations, "run_control.min_inner_iterations", file);
    require_positive(control.max_inner_iterations, "run_control.max_inner_iterations", file);
    if (control.min_inner_iterations > control.max_inner_iterations) {
        fail(file, "run_control.min_inner_iterations exceeds max_inner_iterations");
    }
    require_positive(control.inner_residual_reduction_target,
                     "run_control.inner_residual_reduction_target", file);
    if (control.inner_residual_reduction_target > 1.0) {
        fail(file, "run_control.inner_residual_reduction_target must not exceed 1");
    }
    require_positive(control.cfl_initial, "run_control.cfl_initial", file);
    require_positive(control.cfl_max, "run_control.cfl_max", file);
    if (control.cfl_initial > control.cfl_max) {
        fail(file, "run_control.cfl_initial exceeds cfl_max");
    }
    if (control.pseudo_cfl_ramp_steps < 0) {
        fail(file, "run_control.pseudo_cfl_ramp_steps must not be negative");
    }

    if (control.type == RunType::steady) {
        if (!control.max_steps || !control.residual_reduction_target) {
            fail(file, "steady run_control requires max_steps and residual_reduction_target");
        }
        require_positive(*control.max_steps, "run_control.max_steps", file);
        require_positive(*control.residual_reduction_target,
                         "run_control.residual_reduction_target", file);
        return;
    }

    if (!control.time_integrator || !control.time_step || !control.final_time ||
        !control.inner_residual_norm || !control.bdf2_history_update) {
        fail(file, "transient run_control is missing a required physical-time field");
    }
    if (*control.time_integrator != "bdf2_or_trapezoidal") {
        fail(file, "unsupported transient time_integrator '" + *control.time_integrator + "'");
    }
    if (*control.inner_residual_norm != "total_spatial_plus_physical_time") {
        fail(file, "transient inner_residual_norm must include spatial and physical-time terms");
    }
    if (*control.bdf2_history_update != "after_inner_convergence") {
        fail(file, "transient bdf2_history_update must be after_inner_convergence");
    }
    require_positive(*control.time_step, "run_control.time_step", file);
    require_positive(*control.final_time, "run_control.final_time", file);
    if (control.rusanov_dissipation_scale) {
        require_positive(*control.rusanov_dissipation_scale,
                         "run_control.rusanov_dissipation_scale", file);
    }
}

}  // namespace

const char* to_string(PhysicsMode value) noexcept {
    return value == PhysicsMode::inviscid ? "inviscid" : "laminar";
}

const char* to_string(BoundaryType value) noexcept {
    switch (value) {
        case BoundaryType::farfield:
            return "farfield";
        case BoundaryType::slip_wall:
            return "slip_wall";
        case BoundaryType::no_slip_adiabatic_wall:
            return "no_slip_adiabatic_wall";
    }
    return "unknown";
}

const char* to_string(RunType value) noexcept {
    return value == RunType::steady ? "steady" : "transient";
}

CaseConfig load_case_config(const std::filesystem::path& case_file) {
    std::ifstream input(case_file);
    if (!input) {
        throw CaseConfigError("Cannot open case file '" + case_file.string() + "'");
    }

    json root;
    try {
        input >> root;
    } catch (const json::exception& error) {
        throw CaseConfigError("Cannot parse case file '" + case_file.string() + "': " + error.what());
    }
    if (!root.is_object()) {
        fail(case_file, "top-level JSON value must be an object");
    }

    CaseConfig config;
    config.schema_version = required_value<int>(root, "schema_version", case_file);
    if (config.schema_version != kSupportedCaseSchemaVersion) {
        fail(case_file, "unsupported schema_version " + std::to_string(config.schema_version) +
                            "; supported version is " +
                            std::to_string(kSupportedCaseSchemaVersion));
    }
    config.case_id = required_value<std::string>(root, "case_id", case_file);
    config.description = required_value<std::string>(root, "description", case_file);
    require_nonempty(config.case_id, "case_id", case_file);
    require_nonempty(config.description, "description", case_file);

    const auto& mesh = required_object(root, "mesh", case_file);
    config.mesh.file = required_value<std::string>(mesh, "file", case_file);
    config.mesh.format = required_value<std::string>(mesh, "format", case_file);
    config.mesh.dimension = required_value<int>(mesh, "dimension", case_file);
    require_nonempty(config.mesh.file.string(), "mesh.file", case_file);
    if (config.mesh.format != "CGNS") {
        fail(case_file, "unsupported mesh.format '" + config.mesh.format + "'");
    }
    if (config.mesh.dimension != kSpatialDimension) {
        fail(case_file, "mesh.dimension must be 2");
    }
    config.mesh.resolved_file = config.mesh.file.is_absolute()
                                    ? config.mesh.file.lexically_normal()
                                    : (case_file.parent_path() / config.mesh.file).lexically_normal();
    if (!std::filesystem::exists(config.mesh.resolved_file)) {
        fail(case_file, "mesh.file resolves to missing file '" + config.mesh.resolved_file.string() + "'");
    }

    const auto& physics = required_object(root, "physics", case_file);
    config.physics.equations = required_value<std::string>(physics, "equations", case_file);
    const auto physics_mode = required_value<std::string>(physics, "mode", case_file);
    config.physics.mode = parse_mode(physics_mode, case_file);
    config.physics.reynolds = optional_value<Real>(physics, "reynolds", case_file);
    config.physics.viscosity_model = optional_value<std::string>(physics, "viscosity_model", case_file);
    if (config.physics.equations != "compressible_navier_stokes") {
        fail(case_file, "unsupported physics.equations '" + config.physics.equations + "'");
    }
    if (config.physics.mode == PhysicsMode::laminar) {
        if (!config.physics.reynolds || !config.physics.viscosity_model) {
            fail(case_file, "laminar physics requires reynolds and viscosity_model");
        }
        require_positive(*config.physics.reynolds, "physics.reynolds", case_file);
        require_nonempty(*config.physics.viscosity_model, "physics.viscosity_model", case_file);
    }

    const auto& gas = required_object(root, "gas", case_file);
    config.gas.model = required_value<std::string>(gas, "model", case_file);
    config.gas.gamma = required_value<Real>(gas, "gamma", case_file);
    config.gas.gas_constant = required_value<Real>(gas, "R", case_file);
    config.gas.prandtl = required_value<Real>(gas, "prandtl", case_file);
    if (config.gas.model != "calorically_perfect") {
        fail(case_file, "unsupported gas.model '" + config.gas.model + "'");
    }
    require_finite(config.gas.gamma, "gas.gamma", case_file);
    if (config.gas.gamma <= 1.0) {
        fail(case_file, "gas.gamma must exceed one");
    }
    require_positive(config.gas.gas_constant, "gas.R", case_file);
    require_positive(config.gas.prandtl, "gas.prandtl", case_file);

    const auto& freestream = required_object(root, "freestream", case_file);
    config.freestream.mach = required_value<Real>(freestream, "mach", case_file);
    config.freestream.aoa_degrees = required_value<Real>(freestream, "aoa_degrees", case_file);
    config.freestream.density = required_value<Real>(freestream, "rho", case_file);
    config.freestream.velocity_magnitude = required_value<Real>(freestream, "velocity_magnitude", case_file);
    config.freestream.pressure = required_value<Real>(freestream, "pressure", case_file);
    require_positive(config.freestream.mach, "freestream.mach", case_file);
    require_finite(config.freestream.aoa_degrees, "freestream.aoa_degrees", case_file);
    require_positive(config.freestream.density, "freestream.rho", case_file);
    require_positive(config.freestream.velocity_magnitude, "freestream.velocity_magnitude", case_file);
    require_positive(config.freestream.pressure, "freestream.pressure", case_file);

    const auto& reference = required_object(root, "reference", case_file);
    config.reference.length = required_value<Real>(reference, "length", case_file);
    config.reference.area = required_value<Real>(reference, "area", case_file);
    config.reference.moment_center = parse_vec2(reference, "moment_center", case_file);
    config.reference.reynolds_length = required_value<Real>(reference, "reynolds_length", case_file);
    require_positive(config.reference.length, "reference.length", case_file);
    require_positive(config.reference.area, "reference.area", case_file);
    require_positive(config.reference.reynolds_length, "reference.reynolds_length", case_file);

    const auto& boundary_conditions = required_object(root, "boundary_conditions", case_file);
    if (boundary_conditions.empty()) {
        fail(case_file, "boundary_conditions must not be empty");
    }
    bool has_farfield = false;
    for (auto it = boundary_conditions.begin(); it != boundary_conditions.end(); ++it) {
        const auto tag = it.key();
        if (tag.empty() || !it.value().is_string()) {
            fail(case_file, "boundary_conditions entries require non-empty string tags and types");
        }
        const auto type = parse_boundary_condition(it.value().get<std::string>(), case_file);
        has_farfield = has_farfield || type == BoundaryType::farfield;
        config.boundary_conditions.emplace(tag, type);
    }
    if (!has_farfield) {
        fail(case_file, "boundary_conditions must include a farfield boundary");
    }

    const auto& numerics = required_object(root, "numerics_required", case_file);
    config.numerics_required.spatial_order = required_value<int>(numerics, "spatial_order", case_file);
    config.numerics_required.inviscid_flux = required_value<std::string>(numerics, "inviscid_flux", case_file);
    config.numerics_required.viscous_flux = required_value<std::string>(numerics, "viscous_flux", case_file);
    config.numerics_required.main_time_method = required_value<std::string>(numerics, "main_time_method", case_file);
    config.numerics_required.transient_order = optional_value<int>(numerics, "transient_order", case_file);
    config.numerics_required.implicit_solver = required_value<std::string>(numerics, "implicit_solver", case_file);
    if (config.numerics_required.spatial_order < 2 ||
        config.numerics_required.inviscid_flux != "approximate_riemann" ||
        config.numerics_required.main_time_method != "implicit" ||
        config.numerics_required.implicit_solver != "required") {
        fail(case_file, "numerics_required does not meet the schema-v1 production requirements");
    }
    const auto expected_viscous_flux = config.physics.mode == PhysicsMode::laminar ? "required" : "disabled";
    if (config.numerics_required.viscous_flux != expected_viscous_flux) {
        fail(case_file, "numerics_required.viscous_flux is inconsistent with physics.mode");
    }

    const auto& run_control = required_object(root, "run_control", case_file);
    config.run_control.type = parse_run_type(required_value<std::string>(run_control, "type", case_file), case_file);
    config.run_control.max_steps = optional_value<int>(run_control, "max_steps", case_file);
    config.run_control.residual_reduction_target = optional_value<Real>(run_control, "residual_reduction_target", case_file);
    config.run_control.time_integrator = optional_value<std::string>(run_control, "time_integrator", case_file);
    config.run_control.time_step = optional_value<Real>(run_control, "time_step", case_file);
    config.run_control.final_time = optional_value<Real>(run_control, "final_time", case_file);
    config.run_control.min_inner_iterations = required_value<int>(run_control, "min_inner_iterations", case_file);
    config.run_control.max_inner_iterations = required_value<int>(run_control, "max_inner_iterations", case_file);
    config.run_control.inner_residual_reduction_target = required_value<Real>(run_control, "inner_residual_reduction_target", case_file);
    config.run_control.inner_residual_norm = optional_value<std::string>(run_control, "inner_residual_norm", case_file);
    config.run_control.bdf2_history_update = optional_value<std::string>(run_control, "bdf2_history_update", case_file);
    config.run_control.cfl_initial = required_value<Real>(run_control, "cfl_initial", case_file);
    config.run_control.cfl_max = required_value<Real>(run_control, "cfl_max", case_file);
    config.run_control.pseudo_cfl_ramp_steps = required_value<int>(run_control, "pseudo_cfl_ramp_steps", case_file);
    config.run_control.rusanov_dissipation_scale = optional_value<Real>(run_control, "rusanov_dissipation_scale", case_file);
    validate_run_control(config.run_control, case_file);

    if (config.run_control.type == RunType::transient &&
        (!config.numerics_required.transient_order || *config.numerics_required.transient_order < 2)) {
        fail(case_file, "transient cases require numerics_required.transient_order >= 2");
    }

    const auto& outputs = required_object(root, "outputs", case_file);
    config.outputs.write_final_field = required_value<bool>(outputs, "write_final_field", case_file);
    config.outputs.write_surface = required_value<bool>(outputs, "write_surface", case_file);
    config.outputs.write_forces_every = required_value<int>(outputs, "write_forces_every", case_file);
    config.outputs.write_residuals_every = required_value<int>(outputs, "write_residuals_every", case_file);
    config.outputs.write_field_every_time = optional_value<Real>(outputs, "write_field_every_time", case_file);
    config.outputs.wake_visualization = optional_value<std::string>(outputs, "wake_visualization", case_file);
    if (const auto clip = outputs.find("recommended_vorticity_clip_range"); clip != outputs.end()) {
        config.outputs.recommended_vorticity_clip_range = parse_vec2(outputs, "recommended_vorticity_clip_range", case_file);
    }
    if (!config.outputs.write_final_field || !config.outputs.write_surface) {
        fail(case_file, "outputs must request final field and surface output");
    }
    require_positive(config.outputs.write_forces_every, "outputs.write_forces_every", case_file);
    require_positive(config.outputs.write_residuals_every, "outputs.write_residuals_every", case_file);
    if (config.outputs.write_field_every_time) {
        require_positive(*config.outputs.write_field_every_time, "outputs.write_field_every_time", case_file);
    }
    if (config.run_control.type == RunType::transient &&
        (!config.outputs.write_field_every_time || !config.outputs.wake_visualization ||
         !config.outputs.recommended_vorticity_clip_range)) {
        fail(case_file, "transient cases require field cadence and wake-visualization settings");
    }

    return config;
}

}  // namespace cfd
