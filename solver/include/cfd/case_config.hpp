#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "cfd/types.hpp"

namespace cfd {

enum class PhysicsMode { inviscid, laminar };

[[nodiscard]] const char* to_string(PhysicsMode value) noexcept;
[[nodiscard]] const char* to_string(BoundaryType value) noexcept;
[[nodiscard]] const char* to_string(RunType value) noexcept;

class CaseConfigError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct MeshConfig {
    std::filesystem::path file;
    std::filesystem::path resolved_file;
    std::string format;
    int dimension{};
};

struct PhysicsConfig {
    std::string equations;
    PhysicsMode mode{};
    std::optional<Real> reynolds;
    std::optional<std::string> viscosity_model;
};

struct GasConfig {
    std::string model;
    Real gamma{};
    Real gas_constant{};
    Real prandtl{};
};

struct FreestreamConfig {
    Real mach{};
    Real aoa_degrees{};
    Real density{};
    Real velocity_magnitude{};
    Real pressure{};
};

struct ReferenceConfig {
    Real length{};
    Real area{};
    Vec2 moment_center{};
    Real reynolds_length{};
};

struct NumericsRequiredConfig {
    int spatial_order{};
    std::string inviscid_flux;
    std::string viscous_flux;
    std::string main_time_method;
    std::optional<int> transient_order;
    std::string implicit_solver;
};

struct RunControlConfig {
    RunType type{};
    std::optional<int> max_steps;
    std::optional<Real> residual_reduction_target;
    std::optional<std::string> time_integrator;
    std::optional<Real> time_step;
    std::optional<Real> final_time;
    int min_inner_iterations{};
    int max_inner_iterations{};
    Real inner_residual_reduction_target{};
    std::optional<std::string> inner_residual_norm;
    std::optional<std::string> bdf2_history_update;
    Real cfl_initial{};
    Real cfl_max{};
    int pseudo_cfl_ramp_steps{};
    std::optional<Real> rusanov_dissipation_scale;
};

struct OutputConfig {
    bool write_final_field{};
    bool write_surface{};
    int write_forces_every{};
    int write_residuals_every{};
    std::optional<Real> write_field_every_time;
    std::optional<std::string> wake_visualization;
    std::optional<Vec2> recommended_vorticity_clip_range;
};

struct CaseConfig {
    int schema_version{};
    std::string case_id;
    std::string description;
    MeshConfig mesh;
    PhysicsConfig physics;
    GasConfig gas;
    FreestreamConfig freestream;
    ReferenceConfig reference;
    std::map<std::string, BoundaryType> boundary_conditions;
    NumericsRequiredConfig numerics_required;
    RunControlConfig run_control;
    OutputConfig outputs;
};

/// Loads and validates a schema-v1 benchmark case. Relative mesh paths are resolved
/// against the JSON file's directory; both the original and resolved paths are kept.
[[nodiscard]] CaseConfig load_case_config(const std::filesystem::path& case_file);

}  // namespace cfd
