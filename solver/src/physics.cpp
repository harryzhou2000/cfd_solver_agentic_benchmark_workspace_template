#include "cfd/physics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cfd {
namespace {

[[nodiscard]] Real dot(const Vec2& a, const Vec2& b) noexcept {
    return a[0] * b[0] + a[1] * b[1];
}

[[nodiscard]] bool finite_state(const ConservativeState& value) noexcept {
    for (const Real component : value) {
        if (!std::isfinite(component)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ConservativeState local_to_cartesian_flux(const ConservativeState& local,
                                                        const Vec2& normal) noexcept {
    const Vec2 tangent{-normal[1], normal[0]};
    return {local[0], local[1] * normal[0] + local[2] * tangent[0],
            local[1] * normal[1] + local[2] * tangent[1], local[3]};
}

[[nodiscard]] ConservativeState cartesian_to_local_state(const ConservativeState& state,
                                                         const Vec2& normal) noexcept {
    const Vec2 tangent{-normal[1], normal[0]};
    return {state[0], state[1] * normal[0] + state[2] * normal[1],
            state[1] * tangent[0] + state[2] * tangent[1], state[3]};
}

[[nodiscard]] ConservativeState local_euler_flux(const ConservativeState& local,
                                                 Real pressure) noexcept {
    const Real normal_velocity = local[1] / local[0];
    return {local[1], local[1] * normal_velocity + pressure,
            local[2] * normal_velocity, normal_velocity * (local[3] + pressure)};
}

[[nodiscard]] Real pressure_unchecked(const ConservativeState& state,
                                      const GasModel& gas) noexcept {
    if (!(state[0] > 0.0)) {
        return -std::numeric_limits<Real>::infinity();
    }
    const Real kinetic = 0.5 * (state[1] * state[1] + state[2] * state[2]) / state[0];
    return (gas.gamma - 1.0) * (state[3] - kinetic);
}

}  // namespace

ThermodynamicState decode_state(const ConservativeState& state, const GasModel& gas) {
    if (!finite_state(state) || !(state[0] > gas.density_floor)) {
        throw std::domain_error("conservative state has non-positive or non-finite density");
    }
    if (!(gas.gamma > 1.0) || !(gas.gas_constant > 0.0) || !(gas.prandtl > 0.0)) {
        throw std::invalid_argument("invalid perfect-gas parameters");
    }

    ThermodynamicState result{};
    result.density = state[0];
    result.velocity_x = state[1] / state[0];
    result.velocity_y = state[2] / state[0];
    result.pressure = pressure_unchecked(state, gas);
    if (!std::isfinite(result.pressure) || !(result.pressure > gas.pressure_floor)) {
        throw std::domain_error("conservative state has non-positive or non-finite pressure");
    }
    result.temperature = result.pressure / (result.density * gas.gas_constant);
    result.sound_speed = std::sqrt(gas.gamma * result.pressure / result.density);
    result.total_enthalpy = (state[3] + result.pressure) / result.density;
    return result;
}

ConservativeState encode_state(const ThermodynamicState& primitive, const GasModel& gas) {
    if (!(primitive.density > gas.density_floor) ||
        !(primitive.pressure > gas.pressure_floor) || !std::isfinite(primitive.density) ||
        !std::isfinite(primitive.velocity_x) || !std::isfinite(primitive.velocity_y) ||
        !std::isfinite(primitive.pressure)) {
        throw std::domain_error("primitive state is not physically admissible");
    }
    const Real kinetic = 0.5 * primitive.density *
                         (primitive.velocity_x * primitive.velocity_x +
                          primitive.velocity_y * primitive.velocity_y);
    const Real energy = primitive.pressure / (gas.gamma - 1.0) + kinetic;
    return {primitive.density, primitive.density * primitive.velocity_x,
            primitive.density * primitive.velocity_y, energy};
}

ConservativeState freestream_state(Real density, Real velocity, Real angle_degrees,
                                    Real pressure, const GasModel& gas) {
    constexpr Real kPi = 3.141592653589793238462643383279502884;
    const Real angle = angle_degrees * kPi / 180.0;
    ThermodynamicState primitive{};
    primitive.density = density;
    primitive.velocity_x = velocity * std::cos(angle);
    primitive.velocity_y = velocity * std::sin(angle);
    primitive.pressure = pressure;
    return encode_state(primitive, gas);
}

bool is_admissible(const ConservativeState& state, const GasModel& gas) noexcept {
    return finite_state(state) && state[0] > gas.density_floor &&
           pressure_unchecked(state, gas) > gas.pressure_floor;
}

ConservativeState euler_flux(const ConservativeState& state, const Vec2& unit_normal,
                             const GasModel& gas) {
    const ThermodynamicState primitive = decode_state(state, gas);
    const Real normal_velocity = primitive.velocity_x * unit_normal[0] +
                                 primitive.velocity_y * unit_normal[1];
    return {primitive.density * normal_velocity,
            state[1] * normal_velocity + primitive.pressure * unit_normal[0],
            state[2] * normal_velocity + primitive.pressure * unit_normal[1],
            (state[3] + primitive.pressure) * normal_velocity};
}

StateJacobian euler_flux_jacobian(const ConservativeState& state,
                                  const Vec2& unit_normal, const GasModel& gas) {
    const ThermodynamicState primitive = decode_state(state, gas);
    const Real u = primitive.velocity_x;
    const Real v = primitive.velocity_y;
    const Real velocity2 = u * u + v * v;
    const Real gm1 = gas.gamma - 1.0;
    StateJacobian x{};
    x[0] = {0.0, 1.0, 0.0, 0.0};
    x[1] = {0.5 * ((gas.gamma - 3.0) * u * u + gm1 * v * v),
            (3.0 - gas.gamma) * u, -gm1 * v, gm1};
    x[2] = {-u * v, v, u, 0.0};
    x[3] = {u * (0.5 * gm1 * velocity2 - primitive.total_enthalpy),
            primitive.total_enthalpy - gm1 * u * u, -gm1 * u * v,
            gas.gamma * u};
    StateJacobian y{};
    y[0] = {0.0, 0.0, 1.0, 0.0};
    y[1] = {-u * v, v, u, 0.0};
    y[2] = {0.5 * (gm1 * u * u + (gas.gamma - 3.0) * v * v),
            -gm1 * u, (3.0 - gas.gamma) * v, gm1};
    y[3] = {v * (0.5 * gm1 * velocity2 - primitive.total_enthalpy),
            -gm1 * u * v, primitive.total_enthalpy - gm1 * v * v,
            gas.gamma * v};
    StateJacobian normal{};
    for (std::size_t row = 0; row < normal.size(); ++row) {
        for (std::size_t column = 0; column < normal[row].size(); ++column) {
            normal[row][column] = unit_normal[0] * x[row][column] +
                                  unit_normal[1] * y[row][column];
        }
    }
    return normal;
}

NumericalFlux rusanov_flux(const ConservativeState& left, const ConservativeState& right,
                           const Vec2& unit_normal, const GasModel& gas,
                           Real dissipation_scale) {
    const ThermodynamicState left_primitive = decode_state(left, gas);
    const ThermodynamicState right_primitive = decode_state(right, gas);
    const ConservativeState left_flux = euler_flux(left, unit_normal, gas);
    const ConservativeState right_flux = euler_flux(right, unit_normal, gas);
    const Real left_normal_velocity = left_primitive.velocity_x * unit_normal[0] +
                                      left_primitive.velocity_y * unit_normal[1];
    const Real right_normal_velocity = right_primitive.velocity_x * unit_normal[0] +
                                       right_primitive.velocity_y * unit_normal[1];
    const Real spectral = std::max(std::abs(left_normal_velocity) + left_primitive.sound_speed,
                                   std::abs(right_normal_velocity) +
                                       right_primitive.sound_speed);
    NumericalFlux result{};
    result.spectral_radius = spectral;
    result.used_fallback = false;
    for (std::size_t component = 0; component < result.value.size(); ++component) {
        result.value[component] =
            0.5 * (left_flux[component] + right_flux[component]) -
            0.5 * dissipation_scale * spectral * (right[component] - left[component]);
    }
    return result;
}

NumericalFlux stationary_wall_flux(const ConservativeState& interior,
                                   const Vec2& outward_unit_normal,
                                   const GasModel& gas) {
    const ThermodynamicState primitive = decode_state(interior, gas);
    const Real normal_velocity = primitive.velocity_x * outward_unit_normal[0] +
                                 primitive.velocity_y * outward_unit_normal[1];
    NumericalFlux result{};
    result.value = {0.0,
                    primitive.pressure * outward_unit_normal[0],
                    primitive.pressure * outward_unit_normal[1],
                    0.0};
    result.spectral_radius = std::abs(normal_velocity) + primitive.sound_speed;
    result.used_fallback = false;
    return result;
}

NumericalFlux hllc_flux(const ConservativeState& left, const ConservativeState& right,
                        const Vec2& unit_normal, const GasModel& gas,
                        Real fallback_dissipation_scale) {
    const ThermodynamicState pl = decode_state(left, gas);
    const ThermodynamicState pr = decode_state(right, gas);
    const ConservativeState ul = cartesian_to_local_state(left, unit_normal);
    const ConservativeState ur = cartesian_to_local_state(right, unit_normal);
    const Real unl = ul[1] / ul[0];
    const Real unr = ur[1] / ur[0];
    const Real sl = std::min(unl - pl.sound_speed, unr - pr.sound_speed);
    const Real sr = std::max(unl + pl.sound_speed, unr + pr.sound_speed);
    const Real denominator = pl.density * (sl - unl) - pr.density * (sr - unr);
    const Real scale = std::max({1.0, std::abs(pl.pressure), std::abs(pr.pressure)});
    if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-14 * scale) {
        NumericalFlux fallback =
            rusanov_flux(left, right, unit_normal, gas, fallback_dissipation_scale);
        fallback.used_fallback = true;
        return fallback;
    }

    const Real contact = (pr.pressure - pl.pressure + pl.density * unl * (sl - unl) -
                          pr.density * unr * (sr - unr)) /
                         denominator;
    const ConservativeState fl = local_euler_flux(ul, pl.pressure);
    const ConservativeState fr = local_euler_flux(ur, pr.pressure);

    ConservativeState local_flux{};
    bool valid = std::isfinite(contact) && contact > sl && contact < sr;
    if (valid && sl >= 0.0) {
        local_flux = fl;
    } else if (valid && contact >= 0.0) {
        ConservativeState star{};
        const Real rho_star = pl.density * (sl - unl) / (sl - contact);
        const Real pressure_star =
            pl.pressure + pl.density * (sl - unl) * (contact - unl);
        const Real specific_energy = ul[3] / pl.density;
        star[0] = rho_star;
        star[1] = rho_star * contact;
        star[2] = rho_star * (ul[2] / pl.density);
        star[3] = rho_star *
                  (specific_energy + (contact - unl) *
                                         (contact + pl.pressure /
                                                        (pl.density * (sl - unl))));
        valid = std::isfinite(pressure_star) && pressure_star > gas.pressure_floor &&
                is_admissible(star, gas);
        for (std::size_t component = 0; component < local_flux.size(); ++component) {
            local_flux[component] = fl[component] + sl * (star[component] - ul[component]);
        }
    } else if (valid && sr > 0.0) {
        ConservativeState star{};
        const Real rho_star = pr.density * (sr - unr) / (sr - contact);
        const Real pressure_star =
            pr.pressure + pr.density * (sr - unr) * (contact - unr);
        const Real specific_energy = ur[3] / pr.density;
        star[0] = rho_star;
        star[1] = rho_star * contact;
        star[2] = rho_star * (ur[2] / pr.density);
        star[3] = rho_star *
                  (specific_energy + (contact - unr) *
                                         (contact + pr.pressure /
                                                        (pr.density * (sr - unr))));
        valid = std::isfinite(pressure_star) && pressure_star > gas.pressure_floor &&
                is_admissible(star, gas);
        for (std::size_t component = 0; component < local_flux.size(); ++component) {
            local_flux[component] = fr[component] + sr * (star[component] - ur[component]);
        }
    } else if (valid) {
        local_flux = fr;
    }

    if (!valid || !finite_state(local_flux)) {
        NumericalFlux fallback =
            rusanov_flux(left, right, unit_normal, gas, fallback_dissipation_scale);
        fallback.used_fallback = true;
        return fallback;
    }

    NumericalFlux result{};
    result.value = local_to_cartesian_flux(local_flux, unit_normal);
    result.spectral_radius = std::max(std::abs(sl), std::abs(sr));
    result.used_fallback = false;
    return result;
}

ConservativeState slip_wall_exterior(const ConservativeState& interior,
                                     const Vec2& outward_unit_normal,
                                     const GasModel& gas) {
    ThermodynamicState primitive = decode_state(interior, gas);
    const Real normal_velocity = primitive.velocity_x * outward_unit_normal[0] +
                                 primitive.velocity_y * outward_unit_normal[1];
    primitive.velocity_x -= 2.0 * normal_velocity * outward_unit_normal[0];
    primitive.velocity_y -= 2.0 * normal_velocity * outward_unit_normal[1];
    return encode_state(primitive, gas);
}

ConservativeState no_slip_wall_exterior(const ConservativeState& interior,
                                        const GasModel& gas) {
    ThermodynamicState primitive = decode_state(interior, gas);
    primitive.velocity_x = -primitive.velocity_x;
    primitive.velocity_y = -primitive.velocity_y;
    return encode_state(primitive, gas);
}

ConservativeState characteristic_farfield_exterior(
    const ConservativeState& interior, const ConservativeState& freestream,
    const Vec2& outward_unit_normal, const GasModel& gas) {
    const ThermodynamicState inside = decode_state(interior, gas);
    const ThermodynamicState infinity = decode_state(freestream, gas);
    const Real inside_normal = inside.velocity_x * outward_unit_normal[0] +
                               inside.velocity_y * outward_unit_normal[1];
    if (inside_normal <= -inside.sound_speed) return freestream;
    if (inside_normal >= inside.sound_speed) return interior;

    const Real infinity_normal = infinity.velocity_x * outward_unit_normal[0] +
                                 infinity.velocity_y * outward_unit_normal[1];
    const Real outgoing = inside_normal + 2.0 * inside.sound_speed / (gas.gamma - 1.0);
    const Real incoming = infinity_normal - 2.0 * infinity.sound_speed / (gas.gamma - 1.0);
    const Real boundary_normal = 0.5 * (outgoing + incoming);
    const Real boundary_sound = 0.25 * (gas.gamma - 1.0) * (outgoing - incoming);
    if (!(boundary_sound > 0.0) || !std::isfinite(boundary_sound)) return freestream;

    const Vec2 tangent{-outward_unit_normal[1], outward_unit_normal[0]};
    const bool outflow = boundary_normal >= 0.0;
    const ThermodynamicState& entropy_source = outflow ? inside : infinity;
    const Real source_tangent = entropy_source.velocity_x * tangent[0] +
                                entropy_source.velocity_y * tangent[1];
    const Real entropy = entropy_source.pressure /
                         std::pow(entropy_source.density, gas.gamma);
    const Real density = std::pow(boundary_sound * boundary_sound /
                                      (gas.gamma * entropy),
                                  1.0 / (gas.gamma - 1.0));
    const Real pressure = entropy * std::pow(density, gas.gamma);
    ThermodynamicState boundary{};
    boundary.density = density;
    boundary.velocity_x = boundary_normal * outward_unit_normal[0] +
                          source_tangent * tangent[0];
    boundary.velocity_y = boundary_normal * outward_unit_normal[1] +
                          source_tangent * tangent[1];
    boundary.pressure = pressure;
    try {
        return encode_state(boundary, gas);
    } catch (const std::exception&) {
        return freestream;
    }
}

Vec2 viscous_traction(const ViscousGradients& gradients, const Vec2& unit_normal,
                      Real viscosity) {
    const Real divergence = gradients.velocity_x[0] + gradients.velocity_y[1];
    const Real tau_xx = 2.0 * viscosity * gradients.velocity_x[0] -
                        (2.0 / 3.0) * viscosity * divergence;
    const Real tau_yy = 2.0 * viscosity * gradients.velocity_y[1] -
                        (2.0 / 3.0) * viscosity * divergence;
    const Real tau_xy = viscosity * (gradients.velocity_x[1] + gradients.velocity_y[0]);
    return {tau_xx * unit_normal[0] + tau_xy * unit_normal[1],
            tau_xy * unit_normal[0] + tau_yy * unit_normal[1]};
}

ConservativeState viscous_normal_flux(const ThermodynamicState& face_state,
                                      const ViscousGradients& gradients,
                                      const Vec2& unit_normal, Real viscosity,
                                      const GasModel& gas) {
    if (!(viscosity >= 0.0) || !std::isfinite(viscosity)) {
        throw std::invalid_argument("viscosity must be finite and non-negative");
    }
    const Vec2 traction = viscous_traction(gradients, unit_normal, viscosity);
    const Real heat_conductivity =
        viscosity * gas.gamma * gas.gas_constant / ((gas.gamma - 1.0) * gas.prandtl);
    const Real heat_flux = heat_conductivity * dot(gradients.temperature, unit_normal);
    return {0.0, traction[0], traction[1],
            face_state.velocity_x * traction[0] + face_state.velocity_y * traction[1] +
                heat_flux};
}

Real convective_spectral_radius(const ConservativeState& state, const Vec2& unit_normal,
                                const GasModel& gas) {
    const ThermodynamicState primitive = decode_state(state, gas);
    return std::abs(primitive.velocity_x * unit_normal[0] +
                    primitive.velocity_y * unit_normal[1]) +
           primitive.sound_speed;
}

ConservativeState positivity_limited_state(const ConservativeState& base,
                                           const ConservativeState& candidate,
                                           const GasModel& gas, Real safety) {
    if (!is_admissible(base, gas)) {
        throw std::domain_error("positivity limiter base state is inadmissible");
    }
    if (is_admissible(candidate, gas)) {
        return candidate;
    }
    const Real bounded_safety = std::clamp(safety, 0.0, 1.0);
    Real low = 0.0;
    Real high = 1.0;
    ConservativeState trial = base;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const Real theta = 0.5 * (low + high);
        for (std::size_t component = 0; component < trial.size(); ++component) {
            trial[component] = base[component] + theta * (candidate[component] - base[component]);
        }
        if (is_admissible(trial, gas)) {
            low = theta;
        } else {
            high = theta;
        }
    }
    const Real theta = bounded_safety * low;
    for (std::size_t component = 0; component < trial.size(); ++component) {
        trial[component] = base[component] + theta * (candidate[component] - base[component]);
    }
    return trial;
}

}  // namespace cfd
