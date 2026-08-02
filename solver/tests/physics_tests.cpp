#include "cfd/physics.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

bool close(double lhs, double rhs, double tolerance = 1.0e-11) {
    return std::abs(lhs - rhs) <= tolerance * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

}  // namespace

int main() {
    const cfd::GasModel gas{1.4, 1.0, 0.72, 1.0e-12, 1.0e-12};
    const auto state = cfd::freestream_state(1.0, 2.0, 0.0, 3.0, gas);
    const auto primitive = cfd::decode_state(state, gas);
    assert(close(primitive.density, 1.0));
    assert(close(primitive.velocity_x, 2.0));
    assert(close(primitive.velocity_y, 0.0));
    assert(close(primitive.pressure, 3.0));

    const cfd::Vec2 normal{1.0, 0.0};
    const auto exact = cfd::euler_flux(state, normal, gas);
    const auto hllc = cfd::hllc_flux(state, state, normal, gas);
    const auto rusanov = cfd::rusanov_flux(state, state, normal, gas);
    const auto scaled_rusanov = cfd::rusanov_flux(state, state, normal, gas, 2.0);
    for (std::size_t component = 0; component < state.size(); ++component) {
        assert(close(hllc.value[component], exact[component]));
        assert(close(rusanov.value[component], exact[component]));
        assert(close(scaled_rusanov.value[component], exact[component]));
    }
    assert(close(scaled_rusanov.spectral_radius, 2.0 * rusanov.spectral_radius));
    assert(!hllc.used_fallback);

    cfd::ThermodynamicState right_primitive{};
    right_primitive.density = 0.9;
    right_primitive.velocity_x = 1.5;
    right_primitive.velocity_y = -0.2;
    right_primitive.pressure = 2.5;
    const auto right_state = cfd::encode_state(right_primitive, gas);
    const auto right_flux = cfd::euler_flux(right_state, normal, gas);
    const auto unscaled_jump = cfd::rusanov_flux(state, right_state, normal, gas);
    const auto scaled_jump = cfd::rusanov_flux(state, right_state, normal, gas, 2.0);
    for (std::size_t component = 0; component < state.size(); ++component) {
        const double central = 0.5 * (exact[component] + right_flux[component]);
        assert(close(scaled_jump.value[component] - central,
                     2.0 * (unscaled_jump.value[component] - central)));
    }
    assert(close(scaled_jump.spectral_radius,
                 2.0 * unscaled_jump.spectral_radius));

    const auto enthalpy_equal =
        cfd::enthalpy_upwind_rusanov_flux(state, state, normal, gas);
    for (std::size_t component = 0; component < state.size(); ++component) {
        assert(close(enthalpy_equal.value[component], exact[component]));
    }

    // The Euler energy flux is mass flux times total enthalpy.  When two
    // states share H0, the enthalpy-upwind Rusanov flux must preserve that
    // invariant exactly even though rho, velocity, and pressure jump.
    constexpr double common_enthalpy = 6.0;
    const auto constant_enthalpy_state = [&](double density, double velocity_x,
                                              double velocity_y) {
        cfd::ThermodynamicState value{};
        value.density = density;
        value.velocity_x = velocity_x;
        value.velocity_y = velocity_y;
        const double kinetic = 0.5 *
            (velocity_x * velocity_x + velocity_y * velocity_y);
        value.pressure = (common_enthalpy - kinetic) * density *
                         (gas.gamma - 1.0) / gas.gamma;
        return cfd::encode_state(value, gas);
    };
    const auto enthalpy_left = constant_enthalpy_state(1.0, 1.2, 0.1);
    const auto enthalpy_right = constant_enthalpy_state(0.7, 0.4, -0.2);
    assert(close(cfd::decode_state(enthalpy_left, gas).total_enthalpy,
                 common_enthalpy));
    assert(close(cfd::decode_state(enthalpy_right, gas).total_enthalpy,
                 common_enthalpy));
    const auto invariant_flux = cfd::enthalpy_upwind_rusanov_flux(
        enthalpy_left, enthalpy_right, normal, gas);
    assert(close(invariant_flux.value[3],
                 common_enthalpy * invariant_flux.value[0]));

    // A steady-Euler nonlinear update may move density and momentum off the
    // uniform-H0 manifold. The projection restores only energy and rejects a
    // velocity whose kinetic enthalpy already exceeds the target.
    auto projected = right_state;
    const auto projected_before = projected;
    assert(cfd::project_state_to_total_enthalpy(projected, common_enthalpy, gas));
    assert(projected[0] == projected_before[0]);
    assert(projected[1] == projected_before[1]);
    assert(projected[2] == projected_before[2]);
    assert(close(cfd::decode_state(projected, gas).total_enthalpy,
                 common_enthalpy));
    auto impossible_projection = cfd::freestream_state(1.0, 4.0, 0.0, 1.0, gas);
    const auto impossible_before = impossible_projection;
    assert(!cfd::project_state_to_total_enthalpy(impossible_projection, 2.0, gas));
    assert(impossible_projection == impossible_before);

    cfd::ThermodynamicState rarefied{};
    rarefied.density = 0.09;
    rarefied.velocity_x = 0.1;
    rarefied.pressure = 0.09;
    assert(cfd::violates_joint_reference_floor(
        cfd::encode_state(rarefied, gas), 1.0, 1.0, 0.1, gas));
    rarefied.density = 0.1;
    assert(!cfd::violates_joint_reference_floor(
        cfd::encode_state(rarefied, gas), 1.0, 1.0, 0.1, gas));
    rarefied.density = 0.09;
    rarefied.pressure = 0.1;
    assert(!cfd::violates_joint_reference_floor(
        cfd::encode_state(rarefied, gas), 1.0, 1.0, 0.1, gas));

    rarefied.density = 0.175;
    rarefied.pressure = 0.175;
    const auto half_sensor_state = cfd::encode_state(rarefied, gas);
    assert(close(cfd::joint_reference_rarefaction_sensor(
                     half_sensor_state, 1.0, 1.0, 0.05, gas),
                 0.5));
    assert(cfd::joint_reference_rarefaction_sensor(
               half_sensor_state, 1.0, 1.0, 0.2, gas) == 0.0);
    rarefied.density = 0.09;
    rarefied.pressure = 0.09;
    assert(cfd::joint_reference_rarefaction_sensor(
               cfd::encode_state(rarefied, gas), 1.0, 1.0, 0.05, gas) == 1.0);
    rarefied.density = 0.3;
    assert(cfd::joint_reference_rarefaction_sensor(
               cfd::encode_state(rarefied, gas), 1.0, 1.0, 0.05, gas) == 0.0);

    // Davis-wave-speed HLLC is not positivity preserving for every strong
    // two-rarefaction state.  Such a star state must use the robust Rusanov
    // fallback instead of emitting a finite flux built from negative pressure.
    cfd::ThermodynamicState expansion_left{};
    expansion_left.density = 1.0;
    expansion_left.velocity_x = -2.0;
    expansion_left.pressure = 0.1;
    cfd::ThermodynamicState expansion_right = expansion_left;
    expansion_right.velocity_x = 2.0;
    const auto expansion_flux = cfd::hllc_flux(
        cfd::encode_state(expansion_left, gas),
        cfd::encode_state(expansion_right, gas), normal, gas);
    assert(expansion_flux.used_fallback);
    for (double component : expansion_flux.value) assert(std::isfinite(component));

    const auto jacobian = cfd::euler_flux_jacobian(state, normal, gas);
    for (std::size_t column = 0; column < state.size(); ++column) {
        auto perturbed = state;
        const double epsilon = 1.0e-7 * std::max(1.0, std::abs(state[column]));
        perturbed[column] += epsilon;
        const auto changed = cfd::euler_flux(perturbed, normal, gas);
        for (std::size_t row = 0; row < state.size(); ++row) {
            assert(close(jacobian[row][column],
                         (changed[row] - exact[row]) / epsilon, 2.0e-6));
        }
    }

    const cfd::Vec2 wall_normal{0.0, 1.0};
    const auto tangential_state = cfd::freestream_state(1.0, 2.0, 0.0, 3.0, gas);
    const auto reflected = cfd::slip_wall_exterior(tangential_state, wall_normal, gas);
    const auto wall_flux = cfd::hllc_flux(tangential_state, reflected, wall_normal, gas);
    assert(std::abs(wall_flux.value[0]) < 1.0e-12);
    assert(close(wall_flux.value[2], 3.0));

    // A reconstructed wall-face state generally has a nonzero normal
    // velocity.  The physical stationary-wall flux must nevertheless be
    // exactly impermeable and pressure-only for an arbitrary unit normal.
    const cfd::Vec2 oblique_normal{0.6, 0.8};
    cfd::ThermodynamicState oblique_primitive{};
    oblique_primitive.density = 1.7;
    oblique_primitive.velocity_x = -2.3;
    oblique_primitive.velocity_y = 0.4;
    oblique_primitive.pressure = 4.2;
    const auto oblique_state = cfd::encode_state(oblique_primitive, gas);
    const auto exact_wall = cfd::stationary_wall_flux(oblique_state, oblique_normal, gas);
    assert(exact_wall.value[0] == 0.0);
    assert(close(exact_wall.value[1], 4.2 * oblique_normal[0]));
    assert(close(exact_wall.value[2], 4.2 * oblique_normal[1]));
    assert(exact_wall.value[3] == 0.0);
    const auto oblique_decoded = cfd::decode_state(oblique_state, gas);
    const double raw_normal_velocity =
        oblique_decoded.velocity_x * oblique_normal[0] +
        oblique_decoded.velocity_y * oblique_normal[1];
    assert(close(exact_wall.spectral_radius,
                 std::abs(raw_normal_velocity) + oblique_decoded.sound_speed));
    assert(!exact_wall.used_fallback);
    const auto exact_wall_jacobian =
        cfd::stationary_wall_flux_jacobian(oblique_state, oblique_normal, gas);
    for (std::size_t column = 0; column < oblique_state.size(); ++column) {
        auto perturbed = oblique_state;
        const double epsilon =
            1.0e-7 * std::max(1.0, std::abs(oblique_state[column]));
        perturbed[column] += epsilon;
        const auto changed =
            cfd::stationary_wall_flux(perturbed, oblique_normal, gas);
        for (std::size_t row = 0; row < oblique_state.size(); ++row) {
            assert(close(exact_wall_jacobian[row][column],
                         (changed.value[row] - exact_wall.value[row]) / epsilon,
                         2.0e-6));
        }
    }

    const auto characteristic = cfd::characteristic_farfield_exterior(
        tangential_state, tangential_state, normal, gas);
    for (std::size_t component = 0; component < state.size(); ++component) {
        assert(close(characteristic[component], tangential_state[component]));
    }
    const auto supersonic_outflow = cfd::freestream_state(1.0, 5.0, 0.0, 3.0, gas);
    const auto extrapolated = cfd::characteristic_farfield_exterior(
        supersonic_outflow, tangential_state, normal, gas);
    for (std::size_t component = 0; component < state.size(); ++component) {
        assert(close(extrapolated[component], supersonic_outflow[component]));
    }

    cfd::ViscousGradients gradients{};
    gradients.velocity_x = {2.0, 3.0};
    gradients.velocity_y = {5.0, 7.0};
    gradients.temperature = {11.0, 13.0};
    const auto traction = cfd::viscous_traction(gradients, normal, 0.2);
    assert(close(traction[0], -0.4));
    assert(close(traction[1], 1.6));

    auto invalid = state;
    invalid[3] = 0.01;
    assert(!cfd::is_admissible(invalid, gas));
    const auto limited = cfd::positivity_limited_state(state, invalid, gas);
    assert(cfd::is_admissible(limited, gas));

    std::cout << "physics tests passed\n";
    return 0;
}
