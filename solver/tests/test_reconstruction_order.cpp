#include "numerics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aerofv;

namespace {

struct Quadratic {
  double base;
  double ax;
  double ay;
  double xx;
  double xy;
  double yy;

  double value(const Vec2 &point) const {
    return base + ax * point.x + ay * point.y + xx * point.x * point.x +
           xy * point.x * point.y + yy * point.y * point.y;
  }
};

Primitive analytic_primitive(const Vec2 &point) {
  // All four fields increase throughout the small test stencil.  The test
  // therefore avoids smooth extrema, where a Barth limiter may deliberately
  // reduce formal order to enforce monotonicity.
  static const std::array<Quadratic, 4> fields{{
      {1.1, 0.40, 0.20, 0.06, 0.03, 0.02},
      {0.2, 0.31, 0.12, 0.04, 0.02, 0.01},
      {-0.1, 0.22, 0.18, 0.03, 0.01, 0.04},
      {1.4, 0.55, 0.33, 0.09, 0.02, 0.05},
  }};
  return {fields[0].value(point), fields[1].value(point), fields[2].value(point),
          fields[3].value(point)};
}

std::array<double, 4> values(const Primitive &q) {
  return {q.rho, q.u, q.v, q.p};
}

PrimitiveGradient weighted_least_squares_gradient(
    const Vec2 &center, const Primitive &center_value,
    const std::vector<Vec2> &neighbor_centers,
    const std::vector<Primitive> &neighbor_values) {
  if (neighbor_centers.size() != neighbor_values.size() || neighbor_centers.size() < 2) {
    throw std::invalid_argument("invalid least-squares stencil");
  }
  double a00 = 0.0;
  double a01 = 0.0;
  double a11 = 0.0;
  std::array<double, 4> b0{};
  std::array<double, 4> b1{};
  const auto qc = values(center_value);
  for (std::size_t i = 0; i < neighbor_centers.size(); ++i) {
    const Vec2 d = neighbor_centers[i] - center;
    const double distance_squared = dot(d, d);
    const double weight = 1.0 / distance_squared;
    a00 += weight * d.x * d.x;
    a01 += weight * d.x * d.y;
    a11 += weight * d.y * d.y;
    const auto qn = values(neighbor_values[i]);
    for (std::size_t component = 0; component < 4; ++component) {
      const double difference = qn[component] - qc[component];
      b0[component] += weight * d.x * difference;
      b1[component] += weight * d.y * difference;
    }
  }
  const double determinant = a00 * a11 - a01 * a01;
  if (!(determinant > 1.0e-14)) {
    throw std::runtime_error("singular weighted least-squares stencil");
  }
  PrimitiveGradient result{};
  for (std::size_t component = 0; component < 4; ++component) {
    result[component] = {(a11 * b0[component] - a01 * b1[component]) / determinant,
                         (a00 * b1[component] - a01 * b0[component]) / determinant};
  }
  return result;
}

struct Stencil {
  Vec2 center{0.5, 0.2};
  std::vector<Vec2> offsets;
  std::vector<Vec2> face_offsets;
};

Stencil perturbed_stencil(double h) {
  // This deliberately non-orthogonal ring is a cell-center stencil, not a
  // structured Cartesian cross.  Fixed coefficients make every refinement
  // geometrically similar and deterministic.
  const std::array<Vec2, 8> unit_offsets{{
      {1.18, 0.08}, {0.69, 0.92}, {-0.11, 1.27}, {-0.97, 0.61},
      {-1.23, -0.18}, {-0.59, -1.05}, {0.23, -1.25}, {1.08, -0.74},
  }};
  const std::array<Vec2, 4> unit_faces{{
      {0.28, 0.17}, {-0.18, 0.13}, {-0.12, -0.19}, {0.18, -0.14},
  }};
  Stencil stencil;
  stencil.offsets.reserve(unit_offsets.size());
  stencil.face_offsets.reserve(unit_faces.size());
  for (const Vec2 &offset : unit_offsets) {
    stencil.offsets.push_back(h * offset);
  }
  for (const Vec2 &offset : unit_faces) {
    stencil.face_offsets.push_back(h * offset);
  }
  return stencil;
}

double l2_face_error(double h, double *minimum_limiter) {
  const Stencil stencil = perturbed_stencil(h);
  const Primitive center_value = analytic_primitive(stencil.center);
  std::vector<Vec2> centers;
  std::vector<Primitive> neighbors;
  Primitive min_value = center_value;
  Primitive max_value = center_value;
  for (const Vec2 &offset : stencil.offsets) {
    const Primitive neighbor = analytic_primitive(stencil.center + offset);
    centers.push_back(stencil.center + offset);
    neighbors.push_back(neighbor);
    min_value.rho = std::min(min_value.rho, neighbor.rho);
    min_value.u = std::min(min_value.u, neighbor.u);
    min_value.v = std::min(min_value.v, neighbor.v);
    min_value.p = std::min(min_value.p, neighbor.p);
    max_value.rho = std::max(max_value.rho, neighbor.rho);
    max_value.u = std::max(max_value.u, neighbor.u);
    max_value.v = std::max(max_value.v, neighbor.v);
    max_value.p = std::max(max_value.p, neighbor.p);
  }
  const PrimitiveGradient gradient = weighted_least_squares_gradient(
      stencil.center, center_value, centers, neighbors);
  const auto limiter = barth_jespersen_limiters(center_value, gradient, min_value,
                                                max_value, stencil.face_offsets);
  *minimum_limiter = *std::min_element(limiter.begin(), limiter.end());
  double sum_squares = 0.0;
  for (const Vec2 &face_offset : stencil.face_offsets) {
    const Primitive reconstructed =
        reconstruct_primitive(center_value, gradient, face_offset, limiter);
    const Primitive exact = analytic_primitive(stencil.center + face_offset);
    const std::array<double, 4> error{
        reconstructed.rho - exact.rho, reconstructed.u - exact.u,
        reconstructed.v - exact.v, reconstructed.p - exact.p};
    for (double component_error : error) {
      sum_squares += component_error * component_error;
    }
  }
  return std::sqrt(sum_squares / (4.0 * stencil.face_offsets.size()));
}

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void verify_linear_reproduction() {
  const Stencil stencil = perturbed_stencil(0.37);
  const Primitive center{1.2, -0.2, 0.6, 2.3};
  const PrimitiveGradient exact_gradient{{{0.7, -0.4}, {-0.2, 0.9},
                                           {1.1, 0.3}, {-0.6, 0.8}}};
  std::vector<Vec2> centers;
  std::vector<Primitive> neighbors;
  Primitive minimum = center;
  Primitive maximum = center;
  for (const Vec2 &offset : stencil.offsets) {
    const Primitive q = reconstruct_primitive(center, exact_gradient, offset);
    centers.push_back(stencil.center + offset);
    neighbors.push_back(q);
    minimum.rho = std::min(minimum.rho, q.rho);
    minimum.u = std::min(minimum.u, q.u);
    minimum.v = std::min(minimum.v, q.v);
    minimum.p = std::min(minimum.p, q.p);
    maximum.rho = std::max(maximum.rho, q.rho);
    maximum.u = std::max(maximum.u, q.u);
    maximum.v = std::max(maximum.v, q.v);
    maximum.p = std::max(maximum.p, q.p);
  }
  const PrimitiveGradient recovered =
      weighted_least_squares_gradient(stencil.center, center, centers, neighbors);
  const auto limiter = barth_jespersen_limiters(center, recovered, minimum, maximum,
                                                stencil.face_offsets);
  for (std::size_t component = 0; component < 4; ++component) {
    require(std::abs(recovered[component].x - exact_gradient[component].x) < 1.0e-12,
            "weighted least squares did not reproduce a linear x gradient");
    require(std::abs(recovered[component].y - exact_gradient[component].y) < 1.0e-12,
            "weighted least squares did not reproduce a linear y gradient");
    require(std::abs(limiter[component] - 1.0) < 1.0e-12,
            "limiter unexpectedly changed a bounded linear reconstruction");
  }
  for (const Vec2 &offset : stencil.face_offsets) {
    const Primitive reconstructed = reconstruct_primitive(center, recovered, offset, limiter);
    const Primitive exact = reconstruct_primitive(center, exact_gradient, offset);
    const auto difference = values(reconstructed);
    const auto expected = values(exact);
    for (std::size_t component = 0; component < 4; ++component) {
      require(std::abs(difference[component] - expected[component]) < 1.0e-12,
              "linear face value was not reproduced on skew stencil");
    }
  }
}

void verify_positivity_fallback() {
  const GasModel gas{};
  const Conservative center = primitive_to_conservative({1.0, 0.2, -0.1, 1.0}, gas);
  Conservative invalid = center;
  invalid[0] = -0.5;
  invalid[3] = -1.0;
  const double theta = positivity_scale(center, invalid, gas);
  const Conservative corrected = positivity_limited_state(center, invalid, gas);
  require(theta >= 0.0 && theta < 1.0, "positivity scale did not limit an invalid state");
  require(physically_valid(corrected, gas),
          "positivity fallback did not return an admissible conservative state");
  const Primitive primitive_corrected = positivity_limited_reconstruction(
      {1.0, 0.2, -0.1, 1.0}, {-1.0, 100.0, -100.0, -1.0}, gas);
  require(physically_valid(primitive_corrected, gas),
          "primitive reconstruction positivity fallback returned an invalid state");
}

} // namespace

int main() {
  try {
    verify_linear_reproduction();
    verify_positivity_fallback();

    const std::array<double, 4> spacings{{1.0 / 8.0, 1.0 / 16.0, 1.0 / 32.0,
                                           1.0 / 64.0}};
    std::array<double, 4> errors{};
    std::array<double, 4> minimum_limiters{};
    for (std::size_t i = 0; i < spacings.size(); ++i) {
      errors[i] = l2_face_error(spacings[i], &minimum_limiters[i]);
      require(std::isfinite(errors[i]) && errors[i] > 0.0,
              "non-finite reconstruction error");
      require(minimum_limiters[i] > 0.999999999,
              "monotone smooth-region limiter unexpectedly reduced reconstruction");
    }
    std::array<double, 3> orders{};
    for (std::size_t i = 0; i < orders.size(); ++i) {
      orders[i] = std::log(errors[i] / errors[i + 1]) / std::log(2.0);
      require(orders[i] > 1.7, "observed face-value reconstruction order is below 1.7");
    }
    std::cout << std::setprecision(12) << "reconstruction L2 errors:";
    for (double error : errors) {
      std::cout << ' ' << error;
    }
    std::cout << "\nobserved orders:";
    for (double order : orders) {
      std::cout << ' ' << order;
    }
    std::cout << "\nreconstruction-order test passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "reconstruction-order test failed: " << error.what() << '\n';
    return 1;
  }
}
