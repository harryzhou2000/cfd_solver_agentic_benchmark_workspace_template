#include "cfd/reconstruction.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool close(double lhs, double rhs, double tolerance = 1.0e-11) {
    return std::abs(lhs - rhs) <= tolerance * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

cfd::Primitive linear_value(const cfd::Vec2& x) {
    cfd::Primitive value{};
    value.rho = 2.0 + 1.5 * x[0] - 0.75 * x[1];
    value.u = -0.4 - 2.0 * x[0] + 0.25 * x[1];
    value.v = 0.7 + 0.5 * x[0] + 3.0 * x[1];
    value.p = 4.0 - 1.0 * x[0] + 2.5 * x[1];
    return value;
}

void test_exact_linear_gradient_on_irregular_stencil() {
    const cfd::Vec2 center{0.2, -0.4};
    const std::vector<cfd::Vec2> locations{{1.1, -0.1}, {-0.7, 0.9}, {0.5, -1.8}, {1.6, 1.2}};
    std::vector<cfd::PrimitiveSample> samples;
    for (const auto& location : locations) samples.push_back({location, linear_value(location)});
    bool singular = true;
    const auto gradients = cfd::weighted_least_squares_gradients(center, linear_value(center), samples, &singular);
    assert(!singular);
    assert(close(gradients[0][0], 1.5)); assert(close(gradients[0][1], -0.75));
    assert(close(gradients[1][0], -2.0)); assert(close(gradients[1][1], 0.25));
    assert(close(gradients[2][0], 0.5)); assert(close(gradients[2][1], 3.0));
    assert(close(gradients[3][0], -1.0)); assert(close(gradients[3][1], 2.5));
}

void test_constant_preservation_and_singular_fallback() {
    cfd::Primitive constant{};
    constant.rho = 1.2; constant.u = -0.3; constant.v = 0.4; constant.p = 2.3;
    const std::vector<cfd::PrimitiveSample> samples{{{1.0, 0.0}, constant}, {{-2.0, 0.0}, constant}};
    bool singular = false;
    const auto gradients = cfd::weighted_least_squares_gradients({0.0, 0.0}, constant, samples, &singular);
    assert(singular);
    for (const auto& gradient : gradients) {
        assert(close(gradient[0], 0.0));
        assert(close(gradient[1], 0.0));
    }
}

void test_active_barth_jespersen_limiter() {
    cfd::Primitive center{};
    center.rho = 2.0; center.u = 0.0; center.v = 0.0; center.p = 2.0;
    // Every neighbor is below the cell-center maximum.  The unconstrained
    // fixed-center least-squares slope is nonzero, so an outward face must be limited.
    const std::vector<cfd::PrimitiveSample> samples{
        {{1.0, 0.0}, {1.0, -1.0, 0.0, 1.0}},
        {{0.0, 1.0}, {1.0, 0.0, -1.0, 1.0}},
        {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
    };
    const auto reconstruction = cfd::reconstruct_limited_primitive(
        {0.0, 0.0}, center, samples, {{1.0, 1.0}, {-1.0, -1.0}});
    assert(reconstruction.limiter[0] < 1.0);
    for (const cfd::Vec2& face : std::vector<cfd::Vec2>{{1.0, 1.0}, {-1.0, -1.0}}) {
        const double rho = center.rho + reconstruction.gradients[0][0] * face[0] + reconstruction.gradients[0][1] * face[1];
        assert(rho <= center.rho + 1.0e-12);
        assert(rho >= 1.0 - 1.0e-12);
    }
}

void test_face_positivity_and_state_encoding() {
    const cfd::GasModel gas{};
    cfd::Primitive center{};
    center.rho = 1.0; center.u = 2.0; center.v = -1.0; center.p = 1.0;
    cfd::PrimitiveGradients gradients{};
    gradients[0] = {-10.0, 0.0};
    gradients[3] = {-12.0, 0.0};
    const auto face = cfd::reconstruct_face_state({0.0, 0.0}, center, gradients, {1.0, 0.0}, gas);
    assert(face.used_positivity_fallback);
    assert(face.positivity_scale < 1.0);
    assert(face.primitive.density > gas.density_floor);
    assert(face.primitive.pressure > gas.pressure_floor);
    assert(cfd::is_admissible(face.conservative, gas));
}

void test_viscous_and_wall_helpers() {
    const cfd::GasModel gas{};
    cfd::Primitive primitive{};
    primitive.rho = 2.0; primitive.p = 10.0;
    cfd::PrimitiveGradients gradients{};
    gradients[0] = {0.5, -0.25};
    gradients[3] = {4.0, 3.0};
    const auto grad_t = cfd::temperature_gradient(primitive, gradients, gas);
    assert(close(grad_t[0], 0.75));
    assert(close(grad_t[1], 2.125));

    const cfd::Vec2 left_center{0.0, 0.0};
    const cfd::Vec2 right_center{2.0, 0.0};
    // q=1+2x-3y. The central estimate has the correct tangential component
    // but misses the connector derivative; correction recovers q's gradient.
    const double left_q = 1.0;
    const double right_q = 5.0;
    const auto corrected = cfd::corrected_central_face_gradient(
        left_q, right_q, left_center, right_center, {0.0, -3.0}, {0.0, -3.0});
    assert(close(corrected[0], 2.0));
    assert(close(corrected[1], -3.0));

    const auto left = linear_value(left_center);
    const auto right = linear_value(right_center);
    const cfd::PrimitiveGradients exact{{{{1.5, -0.75}}, {{-2.0, 0.25}}, {{0.5, 3.0}}, {{-1.0, 2.5}}}};
    const auto all_corrected = cfd::corrected_central_face_gradients(
        left, right, left_center, right_center, exact, exact);
    for (std::size_t variable = 0; variable < exact.size(); ++variable) {
        assert(close(all_corrected[variable][0], exact[variable][0]));
        assert(close(all_corrected[variable][1], exact[variable][1]));
    }

    cfd::Primitive extrapolated{};
    extrapolated.rho = 1.0; extrapolated.u = 8.0; extrapolated.v = -4.0; extrapolated.p = 2.0;
    const auto wall = cfd::no_slip_wall_primitive(extrapolated);
    assert(close(wall.u, 0.0));
    assert(close(wall.v, 0.0));
    const auto adiabatic = cfd::adiabatic_temperature_gradient({3.0, 4.0}, {0.0, 2.0});
    assert(close(adiabatic[0], 3.0));
    assert(close(adiabatic[1], 0.0));
}

}  // namespace

int main() {
    test_exact_linear_gradient_on_irregular_stencil();
    test_constant_preservation_and_singular_fallback();
    test_active_barth_jespersen_limiter();
    test_face_positivity_and_state_encoding();
    test_viscous_and_wall_helpers();
    std::cout << "reconstruction tests passed\n";
    return 0;
}
