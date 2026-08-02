#pragma once

#include <array>
#include <string>

#include "cfd/types.hpp"

namespace cfd {

using ConservativeState = State;

struct GasModel {
    Real gamma{1.4};
    Real gas_constant{1.0};
    Real prandtl{0.72};
    Real density_floor{1.0e-12};
    Real pressure_floor{1.0e-12};
};

struct ThermodynamicState {
    Real density{};
    Real velocity_x{};
    Real velocity_y{};
    Real pressure{};
    Real temperature{};
    Real sound_speed{};
    Real total_enthalpy{};
};

/// Gradients of [density, velocity_x, velocity_y, pressure].
using PrimitiveGradients = std::array<Vec2, 4>;

struct ViscousGradients {
    Vec2 velocity_x{};
    Vec2 velocity_y{};
    Vec2 temperature{};
};

struct NumericalFlux {
    ConservativeState value{};
    Real spectral_radius{};
    bool used_fallback{};
};

using StateJacobian = std::array<ConservativeState, 4>;

[[nodiscard]] ThermodynamicState decode_state(const ConservativeState& state,
                                              const GasModel& gas);
[[nodiscard]] ConservativeState encode_state(const ThermodynamicState& primitive,
                                             const GasModel& gas);
[[nodiscard]] ConservativeState freestream_state(Real density, Real velocity,
                                                 Real angle_degrees, Real pressure,
                                                 const GasModel& gas);
[[nodiscard]] bool is_admissible(const ConservativeState& state, const GasModel& gas) noexcept;

/// Replaces only total energy so the state has the requested perfect-gas
/// total enthalpy. Density and both momentum components are preserved. The
/// state is left unchanged and false is returned when the requested enthalpy
/// cannot produce finite pressure above the gas-model floor.
[[nodiscard]] bool project_state_to_total_enthalpy(ConservativeState& state,
                                                   Real target_total_enthalpy,
                                                   const GasModel& gas) noexcept;

/// True when both density and pressure are strictly below the same fraction
/// of positive reference values. Invalid states or invalid reference inputs
/// are treated as violations so this can be composed with update safeguards.
[[nodiscard]] bool violates_joint_reference_floor(const ConservativeState& state,
                                                  Real reference_density,
                                                  Real reference_pressure,
                                                  Real fraction,
                                                  const GasModel& gas) noexcept;

/// Smooth sensor for a joint low-density/low-pressure state on a compactness-
/// limited mesh cell. It is zero above either onset fraction or outside the
/// compactness range, and rises linearly to one as both variables approach the
/// full-response fraction. Invalid input returns one (the conservative choice).
[[nodiscard]] Real joint_reference_rarefaction_sensor(
    const ConservativeState& state, Real reference_density,
    Real reference_pressure, Real cell_compactness, const GasModel& gas,
    Real compactness_threshold = 0.1, Real onset_fraction = 0.25,
    Real full_response_fraction = 0.1) noexcept;

[[nodiscard]] ConservativeState euler_flux(const ConservativeState& state,
                                           const Vec2& unit_normal,
                                           const GasModel& gas);
[[nodiscard]] StateJacobian euler_flux_jacobian(const ConservativeState& state,
                                                const Vec2& unit_normal,
                                                const GasModel& gas);
[[nodiscard]] NumericalFlux rusanov_flux(const ConservativeState& left,
                                         const ConservativeState& right,
                                         const Vec2& unit_normal,
                                         const GasModel& gas,
                                         Real dissipation_scale = 1.0);
/// Rusanov mass and momentum flux with the Euler energy flux written as
/// numerical mass flux times upwind total enthalpy.  This is conservative and
/// consistent, and preserves a uniform-total-enthalpy inviscid manifold.
[[nodiscard]] NumericalFlux enthalpy_upwind_rusanov_flux(
    const ConservativeState& left, const ConservativeState& right,
    const Vec2& unit_normal, const GasModel& gas,
    Real dissipation_scale = 1.0);
/// Exact inviscid flux through a stationary impermeable wall.  The raw
/// interior state supplies pressure and a conservative spectral radius for
/// the implicit preconditioner; reflected-state dissipation is not part of
/// the physical wall flux.
[[nodiscard]] NumericalFlux stationary_wall_flux(const ConservativeState& interior,
                                                  const Vec2& outward_unit_normal,
                                                  const GasModel& gas);
/// Analytic derivative of stationary_wall_flux.value with respect to the
/// conservative interior state.
[[nodiscard]] StateJacobian stationary_wall_flux_jacobian(
    const ConservativeState& interior, const Vec2& outward_unit_normal,
    const GasModel& gas);
[[nodiscard]] NumericalFlux hllc_flux(const ConservativeState& left,
                                      const ConservativeState& right,
                                      const Vec2& unit_normal,
                                      const GasModel& gas,
                                      Real fallback_dissipation_scale = 1.0);

[[nodiscard]] ConservativeState slip_wall_exterior(const ConservativeState& interior,
                                                   const Vec2& outward_unit_normal,
                                                   const GasModel& gas);
[[nodiscard]] ConservativeState no_slip_wall_exterior(const ConservativeState& interior,
                                                      const GasModel& gas);
/// Non-reflecting one-dimensional characteristic farfield state in the face-normal
/// direction. Incoming information is taken from freestream; outgoing information
/// is extrapolated from the interior. Supersonic limits reduce to full inflow or
/// full outflow states.
[[nodiscard]] ConservativeState characteristic_farfield_exterior(
    const ConservativeState& interior, const ConservativeState& freestream,
    const Vec2& outward_unit_normal, const GasModel& gas);

/// Returns the viscous flux F_v dot n (the solver subtracts it from inviscid flux).
[[nodiscard]] ConservativeState viscous_normal_flux(const ThermodynamicState& face_state,
                                                    const ViscousGradients& gradients,
                                                    const Vec2& unit_normal, Real viscosity,
                                                    const GasModel& gas);

/// Cauchy viscous traction tau*n on a face whose normal points out of the fluid.
[[nodiscard]] Vec2 viscous_traction(const ViscousGradients& gradients,
                                    const Vec2& unit_normal, Real viscosity);

[[nodiscard]] Real convective_spectral_radius(const ConservativeState& state,
                                              const Vec2& unit_normal,
                                              const GasModel& gas);

/// Blends candidate toward base until density and pressure are admissible.
[[nodiscard]] ConservativeState positivity_limited_state(const ConservativeState& base,
                                                         const ConservativeState& candidate,
                                                         const GasModel& gas,
                                                         Real safety = 0.95);

}  // namespace cfd
