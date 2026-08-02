#include "cfd/reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cfd {
namespace {

constexpr Real kRankTolerance = 1.0e-13;

[[nodiscard]] Real dot(const Vec2& a, const Vec2& b) noexcept {
    return a[0] * b[0] + a[1] * b[1];
}

[[nodiscard]] Real squared_norm(const Vec2& value) noexcept { return dot(value, value); }

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return std::isfinite(value[0]) && std::isfinite(value[1]);
}

[[nodiscard]] std::array<Real, kStateVariables> components(const Primitive& value) noexcept {
    return {value.rho, value.u, value.v, value.p};
}

[[nodiscard]] Primitive from_components(const std::array<Real, kStateVariables>& values) noexcept {
    Primitive result{};
    result.rho = values[0];
    result.u = values[1];
    result.v = values[2];
    result.p = values[3];
    return result;
}

[[nodiscard]] ThermodynamicState thermodynamic_from_primitive(const Primitive& value) noexcept {
    ThermodynamicState result{};
    result.density = value.rho;
    result.velocity_x = value.u;
    result.velocity_y = value.v;
    result.pressure = value.p;
    return result;
}

[[nodiscard]] Vec2 zero_gradient() noexcept { return {0.0, 0.0}; }

void update_bounds(Primitive& minimum, Primitive& maximum, const Primitive& value) noexcept {
    minimum.rho = std::min(minimum.rho, value.rho);
    minimum.u = std::min(minimum.u, value.u);
    minimum.v = std::min(minimum.v, value.v);
    minimum.p = std::min(minimum.p, value.p);
    maximum.rho = std::max(maximum.rho, value.rho);
    maximum.u = std::max(maximum.u, value.u);
    maximum.v = std::max(maximum.v, value.v);
    maximum.p = std::max(maximum.p, value.p);
}

[[nodiscard]] bool finite_primitive_vector(const Primitive& value) noexcept {
    for (const Real item : components(value)) {
        if (!std::isfinite(item)) return false;
    }
    return true;
}

}  // namespace

PrimitiveGradients weighted_least_squares_gradients(const Vec2& cell_center,
                                                     const Primitive& cell_value,
                                                     const std::vector<PrimitiveSample>& samples,
                                                     bool* used_singular_fallback) {
    if (!finite(cell_center) || !finite_primitive_vector(cell_value)) {
        throw std::invalid_argument("weighted least squares requires finite center and primitive state");
    }
    Real mxx = 0.0;
    Real mxy = 0.0;
    Real myy = 0.0;
    std::array<Real, kStateVariables> bx{};
    std::array<Real, kStateVariables> by{};
    Vec2 strongest_direction{0.0, 0.0};
    Real strongest_distance = 0.0;
    const auto center_components = components(cell_value);
    for (const PrimitiveSample& sample : samples) {
        if (!finite(sample.location) || !finite_primitive_vector(sample.value)) {
            throw std::invalid_argument("weighted least squares sample is non-finite");
        }
        const Vec2 delta{sample.location[0] - cell_center[0], sample.location[1] - cell_center[1]};
        const Real distance2 = squared_norm(delta);
        if (!(distance2 > std::numeric_limits<Real>::epsilon())) continue;
        const Real weight = 1.0 / distance2;
        mxx += weight * delta[0] * delta[0];
        mxy += weight * delta[0] * delta[1];
        myy += weight * delta[1] * delta[1];
        if (distance2 > strongest_distance) {
            strongest_distance = distance2;
            strongest_direction = delta;
        }
        const auto sample_components = components(sample.value);
        for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
            const Real difference = sample_components[variable] - center_components[variable];
            bx[variable] += weight * delta[0] * difference;
            by[variable] += weight * delta[1] * difference;
        }
    }

    PrimitiveGradients result{};
    const Real trace = mxx + myy;
    const Real determinant = mxx * myy - mxy * mxy;
    const bool full_rank = trace > 0.0 && std::isfinite(determinant) &&
                           determinant > kRankTolerance * trace * trace;
    if (full_rank) {
        for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
            result[variable] = {(myy * bx[variable] - mxy * by[variable]) / determinant,
                                (mxx * by[variable] - mxy * bx[variable]) / determinant};
        }
        if (used_singular_fallback != nullptr) *used_singular_fallback = false;
        return result;
    }

    if (used_singular_fallback != nullptr) *used_singular_fallback = true;
    if (!(strongest_distance > 0.0)) {
        for (Vec2& gradient : result) gradient = zero_gradient();
        return result;
    }
    const Real inverse_length = 1.0 / std::sqrt(strongest_distance);
    const Vec2 direction{strongest_direction[0] * inverse_length, strongest_direction[1] * inverse_length};
    Real denominator = 0.0;
    std::array<Real, kStateVariables> numerator{};
    for (const PrimitiveSample& sample : samples) {
        const Vec2 delta{sample.location[0] - cell_center[0], sample.location[1] - cell_center[1]};
        const Real distance2 = squared_norm(delta);
        if (!(distance2 > std::numeric_limits<Real>::epsilon())) continue;
        const Real projected = dot(delta, direction);
        const Real weight = 1.0 / distance2;
        denominator += weight * projected * projected;
        const auto sample_components = components(sample.value);
        for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
            numerator[variable] += weight * projected * (sample_components[variable] - center_components[variable]);
        }
    }
    if (!(denominator > std::numeric_limits<Real>::epsilon())) {
        for (Vec2& gradient : result) gradient = zero_gradient();
        return result;
    }
    for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
        const Real directional_derivative = numerator[variable] / denominator;
        result[variable] = {directional_derivative * direction[0], directional_derivative * direction[1]};
    }
    return result;
}

ReconstructionBounds primitive_bounds(const Primitive& cell_value,
                                      const std::vector<PrimitiveSample>& samples,
                                      const std::vector<Primitive>& boundary_values) {
    if (!finite_primitive_vector(cell_value)) {
        throw std::invalid_argument("primitive bounds requires finite center value");
    }
    ReconstructionBounds result{cell_value, cell_value};
    for (const PrimitiveSample& sample : samples) {
        if (!finite_primitive_vector(sample.value)) throw std::invalid_argument("non-finite sample bound");
        update_bounds(result.minimum, result.maximum, sample.value);
    }
    for (const Primitive& boundary : boundary_values) {
        if (!finite_primitive_vector(boundary)) throw std::invalid_argument("non-finite boundary bound");
        update_bounds(result.minimum, result.maximum, boundary);
    }
    return result;
}

Real pressure_jump_flattening_factor(const Primitive& cell_value,
                                     const std::vector<PrimitiveSample>& samples,
                                     Real onset, Real full) {
    if (!finite_primitive_vector(cell_value) || !(cell_value.p > 0.0) ||
        !std::isfinite(onset) || !std::isfinite(full) || !(onset >= 0.0) ||
        !(full > onset)) {
        throw std::invalid_argument(
            "pressure-jump flattening requires positive pressure and 0 <= onset < full");
    }
    Real maximum_jump = 0.0;
    for (const PrimitiveSample& sample : samples) {
        if (!finite_primitive_vector(sample.value) || !(sample.value.p > 0.0)) {
            throw std::invalid_argument(
                "pressure-jump flattening sample requires positive finite pressure");
        }
        const Real scale = std::max(
            std::min(cell_value.p, sample.value.p),
            std::numeric_limits<Real>::min());
        maximum_jump = std::max(
            maximum_jump, std::abs(sample.value.p - cell_value.p) / scale);
    }
    if (maximum_jump <= onset) return 1.0;
    if (maximum_jump >= full) return 0.0;
    return (full - maximum_jump) / (full - onset);
}

ReconstructionResult reconstruct_limited_primitive(
    const Vec2& cell_center, const Primitive& cell_value,
    const std::vector<PrimitiveSample>& samples, const std::vector<Vec2>& face_locations,
    const std::vector<Primitive>& boundary_values) {
    ReconstructionResult result{};
    result.gradients = weighted_least_squares_gradients(cell_center, cell_value, samples,
                                                        &result.used_singular_fallback);
    const ReconstructionBounds bounds = primitive_bounds(cell_value, samples, boundary_values);
    const auto center = components(cell_value);
    const auto lower = components(bounds.minimum);
    const auto upper = components(bounds.maximum);
    for (const Vec2& face : face_locations) {
        if (!finite(face)) throw std::invalid_argument("face location is non-finite");
        const Vec2 delta{face[0] - cell_center[0], face[1] - cell_center[1]};
        for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
            const Real increment = dot(result.gradients[variable], delta);
            if (increment > 0.0) {
                result.limiter[variable] = std::min(result.limiter[variable],
                    std::clamp((upper[variable] - center[variable]) / increment, 0.0, 1.0));
            } else if (increment < 0.0) {
                result.limiter[variable] = std::min(result.limiter[variable],
                    std::clamp((lower[variable] - center[variable]) / increment, 0.0, 1.0));
            }
        }
    }
    for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
        result.gradients[variable][0] *= result.limiter[variable];
        result.gradients[variable][1] *= result.limiter[variable];
    }
    result.shock_flattening =
        pressure_jump_flattening_factor(cell_value, samples);
    for (Vec2& gradient : result.gradients) {
        gradient[0] *= result.shock_flattening;
        gradient[1] *= result.shock_flattening;
    }
    return result;
}

FaceReconstruction reconstruct_face_state(const Vec2& cell_center, const Primitive& cell_value,
                                          const PrimitiveGradients& gradients,
                                          const Vec2& face_location, const GasModel& gas) {
    if (!finite(cell_center) || !finite(face_location) || !finite_primitive_vector(cell_value)) {
        throw std::invalid_argument("face reconstruction requires finite input values");
    }
    if (!(cell_value.rho > gas.density_floor) || !(cell_value.p > gas.pressure_floor)) {
        throw std::invalid_argument("face reconstruction requires an admissible cell-center primitive state");
    }
    const Vec2 offset{face_location[0] - cell_center[0], face_location[1] - cell_center[1]};
    std::array<Real, kStateVariables> center = components(cell_value);
    std::array<Real, kStateVariables> candidate = center;
    for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
        if (!finite(gradients[variable])) throw std::invalid_argument("face reconstruction gradient is non-finite");
        candidate[variable] += dot(gradients[variable], offset);
    }
    FaceReconstruction result{};
    Real theta = 1.0;
    const auto bound_theta = [&](std::size_t component_index, Real floor) {
        if (candidate[component_index] <= floor) {
            const Real denominator = center[component_index] - candidate[component_index];
            if (!(center[component_index] > floor) || !(denominator > 0.0)) return Real{0.0};
            return std::clamp((center[component_index] - floor) / denominator, 0.0, 1.0);
        }
        return Real{1.0};
    };
    theta = std::min(theta, bound_theta(0, gas.density_floor));
    theta = std::min(theta, bound_theta(3, gas.pressure_floor));
    if (!std::isfinite(theta)) theta = 0.0;
    if (theta < 1.0) theta *= 0.95;
    for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
        candidate[variable] = center[variable] + theta * (candidate[variable] - center[variable]);
    }
    result.positivity_scale = theta;
    result.used_positivity_fallback = theta < 1.0;
    Primitive face = from_components(candidate);
    if (!finite_primitive_vector(face) || !(face.rho > gas.density_floor) ||
        !(face.p > gas.pressure_floor)) {
        face = cell_value;
        result.positivity_scale = 0.0;
        result.used_positivity_fallback = true;
    }
    try {
        result.conservative = encode_state(thermodynamic_from_primitive(face), gas);
        result.primitive = decode_state(result.conservative, gas);
    } catch (const std::exception&) {
        result.conservative = encode_state(thermodynamic_from_primitive(cell_value), gas);
        result.primitive = decode_state(result.conservative, gas);
        result.positivity_scale = 0.0;
        result.used_positivity_fallback = true;
    }
    return result;
}

Vec2 temperature_gradient(const Primitive& primitive, const PrimitiveGradients& gradients,
                          const GasModel& gas) {
    if (!(primitive.rho > gas.density_floor) || !(gas.gas_constant > 0.0) ||
        !finite(gradients[0]) || !finite(gradients[3])) {
        throw std::invalid_argument("temperature gradient requires positive density and finite gradients");
    }
    const Real inverse_rho_r = 1.0 / (primitive.rho * gas.gas_constant);
    const Real pressure_over_rho = primitive.p / primitive.rho;
    return {(gradients[3][0] - pressure_over_rho * gradients[0][0]) * inverse_rho_r,
            (gradients[3][1] - pressure_over_rho * gradients[0][1]) * inverse_rho_r};
}

Vec2 corrected_central_face_gradient(Real left_value, Real right_value,
                                     const Vec2& left_center, const Vec2& right_center,
                                     const Vec2& left_gradient, const Vec2& right_gradient) {
    const Vec2 connector{right_center[0] - left_center[0], right_center[1] - left_center[1]};
    const Real connector2 = squared_norm(connector);
    const Vec2 central{0.5 * (left_gradient[0] + right_gradient[0]),
                       0.5 * (left_gradient[1] + right_gradient[1])};
    if (!(connector2 > std::numeric_limits<Real>::epsilon()) || !finite(central)) return central;
    const Real correction = (right_value - left_value - dot(central, connector)) / connector2;
    return {central[0] + correction * connector[0], central[1] + correction * connector[1]};
}

PrimitiveGradients corrected_central_face_gradients(
    const Primitive& left_value, const Primitive& right_value, const Vec2& left_center,
    const Vec2& right_center, const PrimitiveGradients& left_gradients,
    const PrimitiveGradients& right_gradients) {
    PrimitiveGradients result{};
    const auto left = components(left_value);
    const auto right = components(right_value);
    for (std::size_t variable = 0; variable < kStateVariables; ++variable) {
        result[variable] = corrected_central_face_gradient(
            left[variable], right[variable], left_center, right_center,
            left_gradients[variable], right_gradients[variable]);
    }
    return result;
}

Primitive no_slip_wall_primitive(const Primitive& extrapolated) noexcept {
    Primitive result = extrapolated;
    result.u = 0.0;
    result.v = 0.0;
    return result;
}

Vec2 adiabatic_temperature_gradient(const Vec2& value, const Vec2& wall_normal) noexcept {
    const Real normal2 = squared_norm(wall_normal);
    if (!(normal2 > std::numeric_limits<Real>::epsilon()) || !finite(value) || !finite(wall_normal)) {
        return value;
    }
    const Real normal_component = dot(value, wall_normal) / normal2;
    return {value[0] - normal_component * wall_normal[0], value[1] - normal_component * wall_normal[1]};
}

}  // namespace cfd
