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
