#include "cfd/reconstruction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace cfd {
namespace {

std::array<double, 4> components(const Primitive& value) {
  return {value.rho, value.u, value.v, value.p};
}

Conservative load_state(const std::vector<double>& U, std::size_t cell) {
  return Conservative{U[cell * 4U], U[cell * 4U + 1U], U[cell * 4U + 2U],
                      U[cell * 4U + 3U]};
}

double venkatakrishnan_epsilon_geometric(double area,
                                         double reference_length) {
  constexpr double regularization_constant = 5.0;
  const double relative_length = std::clamp(
      regularization_constant * std::sqrt(std::max(area, 1.0e-300)) /
          reference_length,
      1.0e-6, 1.0);
  return relative_length * relative_length * relative_length;
}

}  // namespace

double venkatakrishnan_face_limiter(double allowed_change,
                                   double reconstructed_change,
                                   double epsilon_squared) noexcept {
  if (!std::isfinite(allowed_change) || !std::isfinite(reconstructed_change) ||
      !std::isfinite(epsilon_squared) || allowed_change < 0.0 ||
      epsilon_squared < 0.0) {
    return 0.0;
  }
  const double increment = std::abs(reconstructed_change);
  if (increment == 0.0) return 1.0;
  const double allowed_squared = allowed_change * allowed_change;
  const double increment_squared = increment * increment;
  const double numerator = allowed_squared + 2.0 * allowed_change * increment +
                           epsilon_squared;
  const double denominator = allowed_squared + allowed_change * increment +
                             2.0 * increment_squared + epsilon_squared;
  if (!(denominator > 0.0) || !std::isfinite(numerator) ||
      !std::isfinite(denominator)) {
    return 0.0;
  }
  return std::clamp(numerator / denominator, 0.0, 1.0);
}

double shock_limited_face_limiter(double venkatakrishnan,
                                 double barth_jespersen,
                                 double jump_ratio) noexcept {
  if (!std::isfinite(venkatakrishnan) ||
      !std::isfinite(barth_jespersen) || !std::isfinite(jump_ratio)) {
    return 0.0;
  }
  const double venkat = std::clamp(venkatakrishnan, 0.0, 1.0);
  const double barth = std::clamp(barth_jespersen, 0.0, 1.0);
  constexpr double transition_start = 1.5;
  constexpr double strong_jump_ratio = 1.75;
  const double coordinate = std::clamp(
      (jump_ratio - transition_start) /
          (strong_jump_ratio - transition_start),
      0.0, 1.0);
  const double blend = coordinate * coordinate * (3.0 - 2.0 * coordinate);
  return venkat + blend * (std::min(venkat, barth) - venkat);
}

PrimitiveReconstruction::PrimitiveReconstruction(const DistributedMesh& mesh,
                                                 const CaloricallyPerfectGas& gas,
                                                 const std::map<std::string, BoundaryCondition>& boundary_conditions,
                                                 const Primitive& freestream,
                                                 MPI_Comm communicator,
                                                 double reference_length)
    : mesh_(mesh),
      gas_(gas),
      boundary_conditions_(boundary_conditions),
      freestream_(freestream),
      communicator_(communicator),
      reference_length_(reference_length) {
  if (!(reference_length_ > 0.0) || !std::isfinite(reference_length_)) {
    throw std::invalid_argument("reconstruction reference length must be finite and positive");
  }
  data_.primitive.resize(mesh_.cells.size());
  data_.gradient.resize(mesh_.cells.size());
  data_.limiter.resize(mesh_.cells.size());
  data_.barth_limiter.resize(mesh_.cells.size());
  data_.minimum.resize(mesh_.cells.size());
  data_.maximum.resize(mesh_.cells.size());
  data_.shock_fallback.resize(mesh_.cells.size());
}

const ReconstructionData& PrimitiveReconstruction::compute(const std::vector<double>& U) {
  if (U.size() != mesh_.cells.size() * 4U) {
    throw std::invalid_argument("reconstruction state width does not match local mesh");
  }
  data_.diagnostics = {};
  for (std::size_t cell = 0; cell < mesh_.cells.size(); ++cell) {
    data_.primitive[cell] = gas_.primitive(load_state(U, cell));
    data_.gradient[cell] = {};
    data_.limiter[cell] = {1.0, 1.0, 1.0, 1.0};
    data_.barth_limiter[cell] = {1.0, 1.0, 1.0, 1.0};
    data_.minimum[cell] = components(data_.primitive[cell]);
    data_.maximum[cell] = components(data_.primitive[cell]);
    data_.shock_fallback[cell] = false;
  }

  // Inverse-distance weighted least squares.  The same symmetric normal matrix
  // is reused for all four primitive components in a cell.
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const LocalCell& local = mesh_.cells[cell];
    double a00 = 0.0;
    double a01 = 0.0;
    double a11 = 0.0;
    std::array<double, 4> b0{};
    std::array<double, 4> b1{};
    const auto center_value = components(data_.primitive[cell]);
    const double length_squared = std::max(local.area, 1.0e-300);
    const double minimum_distance_squared = 1.0e-8 * length_squared;
    const double maximum_distance_squared = 1.0e8 * length_squared;
    auto add_sample = [&](const Vec2& displacement,
                          const Primitive& sample) {
      const double raw_distance_squared = dot(displacement, displacement);
      if (!(raw_distance_squared > 0.0) || !std::isfinite(raw_distance_squared)) return;
      const double distance_squared = std::clamp(
          raw_distance_squared, minimum_distance_squared,
          maximum_distance_squared);
      const double weight = 1.0 / distance_squared;
      a00 += weight * displacement.x * displacement.x;
      a01 += weight * displacement.x * displacement.y;
      a11 += weight * displacement.y * displacement.y;
      const auto sample_value = components(sample);
      for (std::size_t k = 0; k < 4U; ++k) {
        const double difference = sample_value[k] - center_value[k];
        b0[k] += weight * displacement.x * difference;
        b1[k] += weight * displacement.y * difference;
      }
    };
    for (LocalIndex face_index : local.faces) {
      const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
      LocalIndex neighbor = invalid_local_index;
      if (face.left_cell == static_cast<LocalIndex>(cell)) neighbor = face.right_cell;
      if (face.right_cell == static_cast<LocalIndex>(cell)) neighbor = face.left_cell;
      if (neighbor >= 0) {
        add_sample(mesh_.cells[static_cast<std::size_t>(neighbor)].center -
                       local.center,
                   data_.primitive[static_cast<std::size_t>(neighbor)]);
      } else {
        const Vec2 outward =
            face.left_cell == static_cast<LocalIndex>(cell)
                ? face.normal
                : -1.0 * face.normal;
        const double outward_length = norm(outward);
        if (!(outward_length > 0.0) || !std::isfinite(outward_length)) continue;
        const Vec2 unit_outward = outward / outward_length;
        const double normal_distance =
            dot(face.center - local.center, unit_outward);
        const Vec2 mirrored_displacement =
            2.0 * normal_distance * unit_outward;
        const Primitive exterior = boundary_exterior_state(
            boundary_conditions_.at(face.boundary), data_.primitive[cell],
            freestream_, unit_outward, gas_);
        add_sample(mirrored_displacement, exterior);
      }
    }
    const double determinant = a00 * a11 - a01 * a01;
    const double matrix_scale = std::max(1.0, a00 + a11);
    if (determinant > 64.0 * std::numeric_limits<double>::epsilon() *
                          matrix_scale * matrix_scale) {
      for (std::size_t k = 0; k < 4U; ++k) {
        data_.gradient[cell][k] =
            Vec2{(a11 * b0[k] - a01 * b1[k]) / determinant,
                 (a00 * b1[k] - a01 * b0[k]) / determinant};
      }
    }
  }

  exchange_buffer_.assign(mesh_.cells.size() * 8U, 0.0);
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) {
      exchange_buffer_[cell * 8U + 2U * k] = data_.gradient[cell][k].x;
      exchange_buffer_[cell * 8U + 2U * k + 1U] = data_.gradient[cell][k].y;
    }
  }
  exchange_halo(mesh_, exchange_buffer_, 8U, communicator_);
  for (std::size_t cell = 0; cell < mesh_.cells.size(); ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) {
      data_.gradient[cell][k] = Vec2{exchange_buffer_[cell * 8U + 2U * k],
                                    exchange_buffer_[cell * 8U + 2U * k + 1U]};
    }
  }

  // Venkatakrishnan limiter: extrema include the same interior and boundary
  // ghost samples used by least squares. For each face/component, with allowed
  // extremum distance a and unlimited increment b, the smooth multiplier is
  //   phi = (a^2 + 2 a |b| + eps^2) /
  //         (a^2 + a |b| + 2 b^2 + eps^2).
  // eps^2 = q_scale^2 (K h/L_ref)^3 (K=5) regularizes smooth extrema while
  // vanishing cubically under mesh refinement. As a rho/p jump approaches the
  // strong-shock threshold, a smooth transition tightens each multiplier
  // toward min(Venkatakrishnan, Barth--Jespersen).
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const auto center_value = components(data_.primitive[cell]);
    auto minimum = center_value;
    auto maximum = center_value;
    const LocalCell& local = mesh_.cells[cell];
    for (LocalIndex face_index : local.faces) {
      const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
      LocalIndex neighbor = face.left_cell == static_cast<LocalIndex>(cell)
                                ? face.right_cell
                                : face.left_cell;
      Primitive sample;
      if (neighbor >= 0) {
        sample = data_.primitive[static_cast<std::size_t>(neighbor)];
      } else {
        const Vec2 outward =
            face.left_cell == static_cast<LocalIndex>(cell)
                ? face.normal
                : -1.0 * face.normal;
        sample = boundary_exterior_state(
            boundary_conditions_.at(face.boundary), data_.primitive[cell],
            freestream_, outward, gas_);
      }
      const auto value = components(sample);
      for (std::size_t k = 0; k < 4U; ++k) {
        minimum[k] = std::min(minimum[k], value[k]);
        maximum[k] = std::max(maximum[k], value[k]);
      }
    }
    constexpr double strong_jump_ratio = 1.75;
    const double jump_ratio =
        minimum[0] > 0.0 && minimum[3] > 0.0
            ? std::max(maximum[0] / minimum[0],
                       maximum[3] / minimum[3])
            : std::numeric_limits<double>::infinity();
    const bool strong_jump = jump_ratio >= strong_jump_ratio;
    data_.minimum[cell] = minimum;
    data_.maximum[cell] = maximum;
    data_.shock_fallback[cell] = strong_jump;
    const double epsilon_geometric =
        venkatakrishnan_epsilon_geometric(local.area, reference_length_);
    for (LocalIndex face_index : local.faces) {
      const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
      const Vec2 offset = face.center - local.center;
      for (std::size_t k = 0; k < 4U; ++k) {
        const double change = dot(data_.gradient[cell][k], offset);
        if (change == 0.0) continue;
        const double allowed = change > 0.0
                                   ? maximum[k] - center_value[k]
                                   : center_value[k] - minimum[k];
        const double barth = std::clamp(allowed / std::abs(change), 0.0, 1.0);
        const double component_scale = std::max(
            {std::abs(center_value[k]), std::abs(minimum[k]),
             std::abs(maximum[k]), 1.0e-12});
        const double epsilon_squared =
            component_scale * component_scale * epsilon_geometric;
        data_.barth_limiter[cell][k] =
            std::min(data_.barth_limiter[cell][k], barth);
        const double venkat =
            venkatakrishnan_face_limiter(allowed, change, epsilon_squared);
        data_.limiter[cell][k] = std::min(data_.limiter[cell][k], venkat);
        if (!strong_jump &&
            venkat < 1.0 - 32.0 * std::numeric_limits<double>::epsilon()) {
          ++data_.diagnostics.venkatakrishnan_limited_face_components;
        }
      }
    }
    for (std::size_t k = 0; k < 4U; ++k) {
      data_.limiter[cell][k] = shock_limited_face_limiter(
          data_.limiter[cell][k], data_.barth_limiter[cell][k], jump_ratio);
    }
    if (strong_jump) {
      ++data_.diagnostics.shock_fallback_cells;
    }
  }

  exchange_buffer_.assign(mesh_.cells.size() * 17U, 1.0);
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) {
      exchange_buffer_[cell * 17U + k] = data_.limiter[cell][k];
      exchange_buffer_[cell * 17U + 4U + k] = data_.barth_limiter[cell][k];
      exchange_buffer_[cell * 17U + 8U + k] = data_.minimum[cell][k];
      exchange_buffer_[cell * 17U + 12U + k] = data_.maximum[cell][k];
    }
    exchange_buffer_[cell * 17U + 16U] =
        data_.shock_fallback[cell] ? 1.0 : 0.0;
  }
  exchange_halo(mesh_, exchange_buffer_, 17U, communicator_);
  for (std::size_t cell = 0; cell < mesh_.cells.size(); ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) {
      data_.limiter[cell][k] = exchange_buffer_[cell * 17U + k];
      data_.barth_limiter[cell][k] = exchange_buffer_[cell * 17U + 4U + k];
      data_.minimum[cell][k] = exchange_buffer_[cell * 17U + 8U + k];
      data_.maximum[cell][k] = exchange_buffer_[cell * 17U + 12U + k];
    }
    data_.shock_fallback[cell] = exchange_buffer_[cell * 17U + 16U] != 0.0;
  }
  return data_;
}

Primitive PrimitiveReconstruction::face_value(LocalIndex cell_index,
                                               const Vec2& face_center) {
  return face_value_at(cell_index, face_center);
}

Primitive PrimitiveReconstruction::face_value(LocalIndex cell_index,
                                               LocalIndex face_index) {
  return face_value_at(
      cell_index, mesh_.faces.at(static_cast<std::size_t>(face_index)).center);
}

Primitive PrimitiveReconstruction::face_value_at(LocalIndex cell_index,
                                                  const Vec2& face_center) {
  const std::size_t cell = static_cast<std::size_t>(cell_index);
  const Primitive& center = data_.primitive.at(cell);
  const Vec2 offset = face_center - mesh_.cells.at(cell).center;
  const auto center_value = components(center);
  const double epsilon_geometric = venkatakrishnan_epsilon_geometric(
      mesh_.cells.at(cell).area, reference_length_);
  ComponentLimiter smooth_limiter{};
  ComponentLimiter barth_limiter{};
  std::array<double, 4> increment{};
  const double jump_ratio =
      data_.minimum[cell][0] > 0.0 && data_.minimum[cell][3] > 0.0
          ? std::max(data_.maximum[cell][0] / data_.minimum[cell][0],
                     data_.maximum[cell][3] / data_.minimum[cell][3])
          : (data_.shock_fallback[cell]
                 ? std::numeric_limits<double>::infinity()
                 : 1.0);
  for (std::size_t k = 0; k < 4U; ++k) {
    const double change = dot(data_.gradient[cell][k], offset);
    if (change == 0.0) {
      smooth_limiter[k] = 1.0;
      barth_limiter[k] = 1.0;
      continue;
    }
    const double allowed = change > 0.0
                               ? data_.maximum[cell][k] - center_value[k]
                               : center_value[k] - data_.minimum[cell][k];
    barth_limiter[k] =
        std::clamp(allowed / std::abs(change), 0.0, 1.0);
    const double component_scale = std::max(
        {std::abs(center_value[k]), std::abs(data_.minimum[cell][k]),
         std::abs(data_.maximum[cell][k]), 1.0e-12});
    const double epsilon_squared =
        component_scale * component_scale * epsilon_geometric;
    const double venkat = venkatakrishnan_face_limiter(
        allowed, change, epsilon_squared);
    smooth_limiter[k] = shock_limited_face_limiter(
        venkat, barth_limiter[k], jump_ratio);
    increment[k] = smooth_limiter[k] * change;
  }
  const double rho_floor = std::max(1.0e-14, 1.0e-12 * center.rho);
  const double pressure_floor = std::max(1.0e-14, 1.0e-12 * center.p);
  const bool smooth_candidate_inadmissible =
      !std::isfinite(center.rho + increment[0]) ||
      !std::isfinite(center.u + increment[1]) ||
      !std::isfinite(center.v + increment[2]) ||
      !std::isfinite(center.p + increment[3]) ||
      center.rho + increment[0] <= rho_floor ||
      center.p + increment[3] <= pressure_floor;
  if (smooth_candidate_inadmissible) {
    for (std::size_t k = 0; k < 4U; ++k) {
      increment[k] = barth_limiter[k] * dot(data_.gradient[cell][k], offset);
    }
    ++data_.diagnostics.positivity_barth_fallbacks;
  }
  double scale = 1.0;
  if (center.rho + increment[0] <= rho_floor) {
    scale = std::min(scale, (center.rho - rho_floor) / (-increment[0]));
  }
  if (center.p + increment[3] <= pressure_floor) {
    scale = std::min(scale, (center.p - pressure_floor) / (-increment[3]));
  }
  scale = std::clamp(scale, 0.0, 1.0);
  if (scale < 1.0) ++data_.diagnostics.positivity_scaled;
  try {
    return gas_.complete(center.rho + scale * increment[0],
                         center.u + scale * increment[1],
                         center.v + scale * increment[2],
                         center.p + scale * increment[3]);
  } catch (const std::exception&) {
    ++data_.diagnostics.first_order_fallbacks;
    return center;
  }
}

VelocityTemperatureGradients velocity_temperature_gradients(
    const Primitive& state, const PrimitiveGradient& gradient,
    const CaloricallyPerfectGas& gas) {
  const double denominator = state.rho * state.rho * gas.gas_constant();
  return VelocityTemperatureGradients{
      gradient[1], gradient[2],
      (state.rho * gradient[3] - state.p * gradient[0]) / denominator};
}

Vec2 corrected_face_gradient(const Vec2& left_gradient, const Vec2& right_gradient,
                             double left_value, double right_value,
                             const Vec2& left_to_right) {
  const double distance = norm(left_to_right);
  if (!(distance > 0.0)) return 0.5 * (left_gradient + right_gradient);
  const Vec2 direction = left_to_right / distance;
  const Vec2 average = 0.5 * (left_gradient + right_gradient);
  const double secant = (right_value - left_value) / distance;
  double correction = secant - dot(average, direction);
  const double bound = 2.0 * std::max({norm(left_gradient), norm(right_gradient),
                                      std::abs(secant), 1.0e-30});
  correction = std::clamp(correction, -bound, bound);
  return average + correction * direction;
}

}  // namespace cfd
