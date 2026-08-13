#include "numerics.hpp"

#include <algorithm>
#include <cmath>

namespace aerofv {

Primitive reconstruct_primitive(const Primitive &center,
                                const PrimitiveGradient &gradient,
                                const Vec2 &face_offset,
                                const std::array<double, 4> &limiter) {
  Primitive result = center;
  double *values[] = {&result.rho, &result.u, &result.v, &result.p};
  const double center_values[] = {center.rho, center.u, center.v, center.p};
  for (std::size_t component = 0; component < 4; ++component) {
    const double phi = std::clamp(limiter[component], 0.0, 1.0);
    const double delta = dot(gradient[component], face_offset);
    *values[component] = std::isfinite(delta)
                             ? center_values[component] + phi * delta
                             : center_values[component];
  }
  return result;
}

PrimitiveGradient corrected_primitive_face_gradient(
    const PrimitiveGradient &left_gradient,
    const PrimitiveGradient &right_gradient, const Primitive &left_center,
    const Primitive &right_center, const Vec2 &center_displacement,
    const Vec2 &unit_normal, double epsilon) {
  PrimitiveGradient result{};
  const double normal_length = norm(unit_normal);
  if (!std::isfinite(normal_length) || normal_length <= epsilon) {
    return result;
  }
  const Vec2 normal = unit_normal / normal_length;
  const double normal_distance = dot(center_displacement, normal);
  const double left_values[] = {left_center.rho, left_center.u, left_center.v,
                                left_center.p};
  const double right_values[] = {right_center.rho, right_center.u,
                                 right_center.v, right_center.p};
  for (std::size_t component = 0; component < 4; ++component) {
    const Vec2 average =
        0.5 * (left_gradient[component] + right_gradient[component]);
    result[component] = average;
    if (!std::isfinite(average.x) || !std::isfinite(average.y) ||
        !std::isfinite(left_values[component]) ||
        !std::isfinite(right_values[component]) ||
        !std::isfinite(normal_distance) ||
        std::abs(normal_distance) <= epsilon) {
      continue;
    }
    const double desired =
        (right_values[component] - left_values[component]) / normal_distance;
    if (std::isfinite(desired)) {
      result[component] += normal * (desired - dot(result[component], normal));
    }
  }
  return result;
}

double barth_jespersen_limiter(double center_value, const Vec2 &gradient,
                               double neighbor_min, double neighbor_max,
                               const std::vector<Vec2> &face_offsets,
                               double epsilon) {
  if (!std::isfinite(center_value) || !std::isfinite(neighbor_min) ||
      !std::isfinite(neighbor_max) || neighbor_min > center_value ||
      neighbor_max < center_value || !std::isfinite(gradient.x) ||
      !std::isfinite(gradient.y)) {
    return 0.0;
  }
  double limiter = 1.0;
  const double tolerance = std::max(epsilon, 0.0);
  for (const Vec2 &offset : face_offsets) {
    const double increment = dot(gradient, offset);
    if (!std::isfinite(increment)) {
      return 0.0;
    }
    if (increment > tolerance) {
      limiter = std::min(limiter, (neighbor_max - center_value) / increment);
    } else if (increment < -tolerance) {
      limiter = std::min(limiter, (neighbor_min - center_value) / increment);
    }
  }
  return std::clamp(limiter, 0.0, 1.0);
}

std::array<double, 4> barth_jespersen_limiters(
    const Primitive &center, const PrimitiveGradient &gradient,
    const Primitive &neighbor_min, const Primitive &neighbor_max,
    const std::vector<Vec2> &face_offsets, double epsilon) {
  const double values[] = {center.rho, center.u, center.v, center.p};
  const double mins[] = {neighbor_min.rho, neighbor_min.u, neighbor_min.v,
                         neighbor_min.p};
  const double maxs[] = {neighbor_max.rho, neighbor_max.u, neighbor_max.v,
                         neighbor_max.p};
  std::array<double, 4> limiters{};
  for (std::size_t component = 0; component < limiters.size(); ++component) {
    limiters[component] = barth_jespersen_limiter(
        values[component], gradient[component], mins[component], maxs[component],
        face_offsets, epsilon);
  }
  return limiters;
}

double positivity_scale(const Conservative &center,
                        const Conservative &candidate, const GasModel &gas,
                        unsigned bisection_iterations) {
  if (!physically_valid(center, gas)) {
    return 0.0;
  }
  if (physically_valid(candidate, gas)) {
    return 1.0;
  }
  if (!finite(candidate)) {
    return 0.0;
  }
  double low = 0.0;
  double high = 1.0;
  for (unsigned i = 0; i < bisection_iterations; ++i) {
    const double middle = 0.5 * (low + high);
    const Conservative trial = center + middle * (candidate - center);
    if (physically_valid(trial, gas)) {
      low = middle;
    } else {
      high = middle;
    }
  }
  return low;
}

Conservative positivity_limited_state(const Conservative &center,
                                      const Conservative &candidate,
                                      const GasModel &gas,
                                      unsigned bisection_iterations) {
  const Conservative base = sanitize_conservative(center, gas);
  const double theta = positivity_scale(base, candidate, gas, bisection_iterations);
  return sanitize_conservative(base + theta * (candidate - base), gas);
}

Primitive positivity_limited_reconstruction(
    const Primitive &center, const Primitive &candidate, const GasModel &gas,
    unsigned bisection_iterations) {
  (void)bisection_iterations;
  const Primitive base = sanitize_primitive(center, gas);
  if (!finite(candidate)) {
    return base;
  }
  const double rho_floor = 1.001 * gas.density_floor;
  const double pressure_floor = 1.001 * gas.pressure_floor;
  double theta = 1.0;
  if (candidate.rho < rho_floor) {
    theta = std::min(theta, (base.rho - rho_floor) /
                                std::max(base.rho - candidate.rho, 1.0e-300));
  }
  if (candidate.p < pressure_floor) {
    theta = std::min(theta, (base.p - pressure_floor) /
                                std::max(base.p - candidate.p, 1.0e-300));
  }
  theta = std::clamp(0.999 * theta, 0.0, 1.0);
  return sanitize_primitive(
      {base.rho + theta * (candidate.rho - base.rho),
       base.u + theta * (candidate.u - base.u),
       base.v + theta * (candidate.v - base.v),
       base.p + theta * (candidate.p - base.p)},
      gas);
}

} // namespace aerofv
