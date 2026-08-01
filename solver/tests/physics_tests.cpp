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
    for (std::size_t component = 0; component < state.size(); ++component) {
        assert(close(hllc.value[component], exact[component]));
        assert(close(rusanov.value[component], exact[component]));
    }
    assert(!hllc.used_fallback);

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
