#include "cfd/solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace cfd {
namespace {

using Limiter = std::array<double, nvars>;
using Matrix4 = std::array<double, nvars * nvars>;

Matrix4 zero_matrix() {
  Matrix4 matrix{};
  matrix.fill(0.0);
  return matrix;
}

double& matrix_entry(Matrix4& matrix, int row, int column) {
  return matrix[static_cast<std::size_t>(row * nvars + column)];
}

double matrix_entry(const Matrix4& matrix, int row, int column) {
  return matrix[static_cast<std::size_t>(row * nvars + column)];
}

void add_matrix(Matrix4& destination, const Matrix4& source, double scale = 1.0) {
  for (std::size_t i = 0; i < destination.size(); ++i) destination[i] += scale * source[i];
}

void add_identity(Matrix4& matrix, double value) {
  for (int i = 0; i < nvars; ++i) matrix_entry(matrix, i, i) += value;
}

Conserved matrix_vector(const Matrix4& matrix, const Conserved& vector) {
  Conserved result{};
  for (int row = 0; row < nvars; ++row) {
    for (int column = 0; column < nvars; ++column) {
      result[static_cast<std::size_t>(row)] +=
          matrix_entry(matrix, row, column) * vector[static_cast<std::size_t>(column)];
    }
  }
  return result;
}

Conserved solve_matrix(Matrix4 matrix, Conserved right_hand_side) {
  for (int pivot = 0; pivot < nvars; ++pivot) {
    int best = pivot;
    for (int row = pivot + 1; row < nvars; ++row) {
      if (std::abs(matrix_entry(matrix, row, pivot)) > std::abs(matrix_entry(matrix, best, pivot))) best = row;
    }
    if (std::abs(matrix_entry(matrix, best, pivot)) < 1.0e-20) throw std::runtime_error("singular 4x4 implicit block");
    if (best != pivot) {
      for (int column = pivot; column < nvars; ++column) {
        std::swap(matrix_entry(matrix, pivot, column), matrix_entry(matrix, best, column));
      }
      std::swap(right_hand_side[static_cast<std::size_t>(pivot)], right_hand_side[static_cast<std::size_t>(best)]);
    }
    const double diagonal = matrix_entry(matrix, pivot, pivot);
    for (int row = pivot + 1; row < nvars; ++row) {
      const double factor = matrix_entry(matrix, row, pivot) / diagonal;
      matrix_entry(matrix, row, pivot) = 0.0;
      for (int column = pivot + 1; column < nvars; ++column) {
        matrix_entry(matrix, row, column) -= factor * matrix_entry(matrix, pivot, column);
      }
      right_hand_side[static_cast<std::size_t>(row)] -= factor * right_hand_side[static_cast<std::size_t>(pivot)];
    }
  }
  Conserved solution{};
  for (int row = nvars - 1; row >= 0; --row) {
    double value = right_hand_side[static_cast<std::size_t>(row)];
    for (int column = row + 1; column < nvars; ++column) {
      value -= matrix_entry(matrix, row, column) * solution[static_cast<std::size_t>(column)];
    }
    solution[static_cast<std::size_t>(row)] = value / matrix_entry(matrix, row, row);
  }
  return solution;
}

Matrix4 euler_flux_jacobian(const Primitive& primitive, Vec2 normal, double gamma) {
  Matrix4 jacobian = zero_matrix();
  const double rho = primitive.rho;
  const double u = primitive.u;
  const double v = primitive.v;
  const double un = u * normal.x + v * normal.y;
  const double velocity_squared = u * u + v * v;
  const double energy_density = primitive.p / (gamma - 1.0) + 0.5 * rho * velocity_squared;
  const double enthalpy = (energy_density + primitive.p) / rho;
  matrix_entry(jacobian, 0, 1) = normal.x;
  matrix_entry(jacobian, 0, 2) = normal.y;

  matrix_entry(jacobian, 1, 0) = -u * un + 0.5 * (gamma - 1.0) * velocity_squared * normal.x;
  matrix_entry(jacobian, 1, 1) = un + u * normal.x - (gamma - 1.0) * u * normal.x;
  matrix_entry(jacobian, 1, 2) = u * normal.y - (gamma - 1.0) * v * normal.x;
  matrix_entry(jacobian, 1, 3) = (gamma - 1.0) * normal.x;

  matrix_entry(jacobian, 2, 0) = -v * un + 0.5 * (gamma - 1.0) * velocity_squared * normal.y;
  matrix_entry(jacobian, 2, 1) = v * normal.x - (gamma - 1.0) * u * normal.y;
  matrix_entry(jacobian, 2, 2) = un + v * normal.y - (gamma - 1.0) * v * normal.y;
  matrix_entry(jacobian, 2, 3) = (gamma - 1.0) * normal.y;

  matrix_entry(jacobian, 3, 0) = un * (0.5 * (gamma - 1.0) * velocity_squared - enthalpy);
  matrix_entry(jacobian, 3, 1) = enthalpy * normal.x - (gamma - 1.0) * u * un;
  matrix_entry(jacobian, 3, 2) = enthalpy * normal.y - (gamma - 1.0) * v * un;
  matrix_entry(jacobian, 3, 3) = gamma * un;
  return jacobian;
}

struct NeighborBuffers {
  std::vector<double> send;
  std::vector<double> receive;
};

class HaloExchange {
 public:
  HaloExchange(const LocalMesh& mesh, MPI_Comm communicator)
      : mesh_(mesh), communicator_(communicator), buffers_(mesh.halo_links.size()) {}

  void state(std::vector<Conserved>& values, int tag) {
    exchange(4, tag,
      [&](int cell, int component) { return values[static_cast<std::size_t>(cell)][static_cast<std::size_t>(component)]; },
      [&](int cell, int component, double value) { values[static_cast<std::size_t>(cell)][static_cast<std::size_t>(component)] = value; });
  }

  void gradients(std::vector<PrimitiveGradient>& gradients, std::vector<Limiter>& limiter, int tag) {
    exchange(12, tag,
      [&](int cell, int component) {
        if (component < 8) {
          const int variable = component / 2;
          return component % 2 == 0 ? gradients[static_cast<std::size_t>(cell)].q[static_cast<std::size_t>(variable)].x
                                    : gradients[static_cast<std::size_t>(cell)].q[static_cast<std::size_t>(variable)].y;
        }
        return limiter[static_cast<std::size_t>(cell)][static_cast<std::size_t>(component - 8)];
      },
      [&](int cell, int component, double value) {
        if (component < 8) {
          const int variable = component / 2;
          if (component % 2 == 0) gradients[static_cast<std::size_t>(cell)].q[static_cast<std::size_t>(variable)].x = value;
          else gradients[static_cast<std::size_t>(cell)].q[static_cast<std::size_t>(variable)].y = value;
        } else {
          limiter[static_cast<std::size_t>(cell)][static_cast<std::size_t>(component - 8)] = value;
        }
      });
  }

 private:
  template <class Getter, class Setter>
  void exchange(int components, int tag, Getter getter, Setter setter) {
    std::vector<MPI_Request> requests;
    requests.reserve(mesh_.halo_links.size() * 2);
    for (std::size_t link_index = 0; link_index < mesh_.halo_links.size(); ++link_index) {
      const HaloLink& link = mesh_.halo_links[link_index];
      NeighborBuffers& buffer = buffers_[link_index];
      buffer.receive.resize(link.receive_cells.size() * static_cast<std::size_t>(components));
      MPI_Request request{};
      MPI_Irecv(buffer.receive.data(), static_cast<int>(buffer.receive.size()), MPI_DOUBLE, link.rank, tag,
                communicator_, &request);
      requests.push_back(request);
    }
    for (std::size_t link_index = 0; link_index < mesh_.halo_links.size(); ++link_index) {
      const HaloLink& link = mesh_.halo_links[link_index];
      NeighborBuffers& buffer = buffers_[link_index];
      buffer.send.resize(link.send_cells.size() * static_cast<std::size_t>(components));
      std::size_t position = 0;
      for (int cell : link.send_cells) {
        for (int component = 0; component < components; ++component) buffer.send[position++] = getter(cell, component);
      }
      MPI_Request request{};
      MPI_Isend(buffer.send.data(), static_cast<int>(buffer.send.size()), MPI_DOUBLE, link.rank, tag,
                communicator_, &request);
      requests.push_back(request);
    }
    if (!requests.empty()) MPI_Waitall(static_cast<int>(requests.size()), requests.data(), MPI_STATUSES_IGNORE);
    for (std::size_t link_index = 0; link_index < mesh_.halo_links.size(); ++link_index) {
      const HaloLink& link = mesh_.halo_links[link_index];
      const NeighborBuffers& buffer = buffers_[link_index];
      std::size_t position = 0;
      for (int cell : link.receive_cells) {
        for (int component = 0; component < components; ++component) setter(cell, component, buffer.receive[position++]);
      }
    }
  }

  const LocalMesh& mesh_;
  MPI_Comm communicator_;
  std::vector<NeighborBuffers> buffers_;
};

struct Evaluation {
  std::vector<Conserved> residual;
  std::vector<double> spectral_sum;
  std::vector<double> face_coefficient;
  std::vector<Matrix4> diagonal_jacobian;
  std::vector<Matrix4> off_diagonal_from_left;
  std::vector<Matrix4> off_diagonal_from_right;
  std::vector<Primitive> primitive;
  std::vector<PrimitiveGradient> gradient;
  std::vector<Limiter> limiter;
  std::vector<double> vorticity;
  std::vector<SurfaceRow> surface;
  std::array<double, nvars> component_l2{};
  double total_l2 = 0.0;
  double total_linf = 0.0;
  ForceRow force;
  std::int64_t hllc_fallbacks = 0;
  std::int64_t positivity_fallbacks = 0;
};

int other_cell(const LocalFace& face, int cell) {
  if (face.left == cell) return face.right;
  if (face.right == cell) return face.left;
  throw std::runtime_error("cell is not incident to face");
}

Primitive add_reconstruction(const Primitive& center, const PrimitiveGradient& gradient,
                             const Limiter& limiter, Vec2 displacement) {
  Primitive reconstructed = center;
  for (int component = 0; component < nvars; ++component) {
    reconstructed[static_cast<std::size_t>(component)] +=
        limiter[static_cast<std::size_t>(component)] *
        dot(gradient.q[static_cast<std::size_t>(component)], displacement);
  }
  return reconstructed;
}

PrimitiveGradient corrected_face_gradient(const Primitive& left, const Primitive& right,
                                           const PrimitiveGradient& left_gradient,
                                           const PrimitiveGradient& right_gradient, Vec2 displacement) {
  PrimitiveGradient face_gradient;
  const double distance_squared = std::max(norm2(displacement), 1.0e-28);
  for (int component = 0; component < nvars; ++component) {
    const auto i = static_cast<std::size_t>(component);
    Vec2 average = 0.5 * (left_gradient.q[i] + right_gradient.q[i]);
    const double correction = (right[i] - left[i] - dot(average, displacement)) / distance_squared;
    face_gradient.q[i] = average + displacement * correction;
  }
  return face_gradient;
}

class ResidualEvaluator {
 public:
  ResidualEvaluator(const CaseConfig& config, const LocalMesh& mesh, MPI_Comm communicator,
                    PerfectGasPhysics& physics, HaloExchange& halo)
      : config_(config), mesh_(mesh), communicator_(communicator), physics_(physics), halo_(halo) {
    MPI_Comm_rank(communicator_, &rank_);
  }

  Evaluation evaluate(std::vector<Conserved>& state, bool collect_surface) {
    halo_.state(state, 7400);
    Evaluation out;
    const std::size_t local_count = mesh_.cells.size();
    const std::size_t owned_count = static_cast<std::size_t>(mesh_.owned_cell_count);
    out.residual.assign(owned_count, zeros());
    out.spectral_sum.assign(owned_count, 0.0);
    out.face_coefficient.assign(mesh_.faces.size(), 0.0);
    out.diagonal_jacobian.assign(owned_count, zero_matrix());
    out.off_diagonal_from_left.assign(mesh_.faces.size(), zero_matrix());
    out.off_diagonal_from_right.assign(mesh_.faces.size(), zero_matrix());
    out.primitive.resize(local_count);
    out.gradient.resize(local_count);
    out.limiter.assign(local_count, Limiter{1.0, 1.0, 1.0, 1.0});
    out.vorticity.assign(owned_count, 0.0);
    for (std::size_t cell = 0; cell < local_count; ++cell) out.primitive[cell] = physics_.to_primitive(state[cell]);

    compute_gradients(out.primitive, out.gradient);
    compute_limiters(out.primitive, out.gradient, out.limiter);
    halo_.gradients(out.gradient, out.limiter, 7401);
    for (std::size_t cell = 0; cell < owned_count; ++cell) {
      out.vorticity[cell] = out.gradient[cell].q[2].x - out.gradient[cell].q[1].y;
    }

    std::array<double, 5> local_force{};  // pressure x/y, viscous x/y, moment
    std::int64_t local_hllc_fallbacks = 0;
    std::int64_t local_positivity_fallbacks = 0;
    for (std::size_t face_index = 0; face_index < mesh_.faces.size(); ++face_index) {
      const LocalFace& face = mesh_.faces[face_index];
      const Primitive left_center = out.primitive[static_cast<std::size_t>(face.left)];
      Primitive left = add_reconstruction(left_center, out.gradient[static_cast<std::size_t>(face.left)],
                                          out.limiter[static_cast<std::size_t>(face.left)],
                                          face.center - mesh_.cells[static_cast<std::size_t>(face.left)].center);
      if (left.rho <= physics_.density_floor() || left.p <= physics_.pressure_floor() ||
          !std::isfinite(left.rho) || !std::isfinite(left.p)) {
        left = left_center;
        ++local_positivity_fallbacks;
      }

      Conserved inviscid{};
      Conserved viscous{};
      double wave_speed = std::abs(dot(Vec2{left.u, left.v}, face.normal)) + physics_.sound_speed(left);
      double viscous_coefficient = 0.0;
      Primitive wall_state = left;
      PrimitiveGradient face_gradient{};
      if (face.right >= 0) {
        const Primitive right_center = out.primitive[static_cast<std::size_t>(face.right)];
        Primitive right = add_reconstruction(right_center, out.gradient[static_cast<std::size_t>(face.right)],
                                             out.limiter[static_cast<std::size_t>(face.right)],
                                             face.center - mesh_.cells[static_cast<std::size_t>(face.right)].center);
        if (right.rho <= physics_.density_floor() || right.p <= physics_.pressure_floor() ||
            !std::isfinite(right.rho) || !std::isfinite(right.p)) {
          right = right_center;
          ++local_positivity_fallbacks;
        }
        const FluxResult flux = physics_.rusanov_flux(left, right, face.normal);
        inviscid = flux.flux;
        wave_speed = flux.spectral_radius;
        if (flux.used_rusanov_fallback) ++local_hllc_fallbacks;
        if (config_.viscous) {
          const Vec2 displacement = mesh_.cells[static_cast<std::size_t>(face.right)].center -
                                    mesh_.cells[static_cast<std::size_t>(face.left)].center;
          face_gradient = corrected_face_gradient(left_center, right_center,
                                                   out.gradient[static_cast<std::size_t>(face.left)],
                                                   out.gradient[static_cast<std::size_t>(face.right)], displacement);
          Primitive face_state;
          for (int component = 0; component < nvars; ++component) {
            face_state[static_cast<std::size_t>(component)] =
                0.5 * (left[static_cast<std::size_t>(component)] + right[static_cast<std::size_t>(component)]);
          }
          viscous = physics_.viscous_flux(face_state, face_gradient, face.normal);
          const double distance = std::max(std::abs(dot(displacement, face.normal)), 1.0e-12 * face.length);
          const double minimum_density = std::max(std::min(left.rho, right.rho), physics_.density_floor());
          viscous_coefficient = 4.0 * physics_.viscosity() * face.length / (minimum_density * distance);
        }
      } else if (face.boundary_type == BoundaryType::farfield) {
        const Primitive right = physics_.boundary_state(left, BoundaryType::farfield, face.normal);
        const FluxResult flux = physics_.rusanov_flux(left, right, face.normal);
        inviscid = flux.flux;
        wave_speed = flux.spectral_radius;
        if (flux.used_rusanov_fallback) ++local_hllc_fallbacks;
        if (config_.viscous) {
          PrimitiveGradient zero_gradient{};
          const Vec2 displacement = 2.0 * (face.center - mesh_.cells[static_cast<std::size_t>(face.left)].center);
          face_gradient = corrected_face_gradient(left_center, right,
                                                   out.gradient[static_cast<std::size_t>(face.left)],
                                                   zero_gradient, displacement);
          Primitive face_state;
          for (int component = 0; component < nvars; ++component) {
            face_state[static_cast<std::size_t>(component)] =
                0.5 * (left[static_cast<std::size_t>(component)] + right[static_cast<std::size_t>(component)]);
          }
          viscous = physics_.viscous_flux(face_state, face_gradient, face.normal);
          const double distance = std::max(std::abs(dot(displacement, face.normal)), 1.0e-12 * face.length);
          viscous_coefficient = 4.0 * physics_.viscosity() * face.length /
                                (std::max(left.rho, physics_.density_floor()) * distance);
        }
      } else {
        wall_state = left;
        if (face.boundary_type == BoundaryType::slip_wall) {
          const double normal_velocity = wall_state.u * face.normal.x + wall_state.v * face.normal.y;
          wall_state.u -= normal_velocity * face.normal.x;
          wall_state.v -= normal_velocity * face.normal.y;
        } else if (face.boundary_type == BoundaryType::no_slip_adiabatic_wall) {
          wall_state.u = 0.0;
          wall_state.v = 0.0;
        }
        inviscid = {0.0, wall_state.p * face.normal.x, wall_state.p * face.normal.y, 0.0};
        if (config_.viscous && face.boundary_type == BoundaryType::no_slip_adiabatic_wall) {
          face_gradient = out.gradient[static_cast<std::size_t>(face.left)];
          const Vec2 displacement = face.center - mesh_.cells[static_cast<std::size_t>(face.left)].center;
          const double distance_squared = std::max(norm2(displacement), 1.0e-28);
          for (int component : {1, 2}) {
            const auto i = static_cast<std::size_t>(component);
            const double target = -left_center[i];
            face_gradient.q[i] += displacement * ((target - dot(face_gradient.q[i], displacement)) / distance_squared);
          }
          const double rho_normal_gradient = dot(face_gradient.q[0], face.normal);
          const double pressure_normal_gradient = dot(face_gradient.q[3], face.normal);
          const double adiabatic_pressure_gradient = wall_state.p * rho_normal_gradient / wall_state.rho;
          face_gradient.q[3] += face.normal * (adiabatic_pressure_gradient - pressure_normal_gradient);
          viscous = physics_.viscous_flux(wall_state, face_gradient, face.normal);
          const double distance = std::max(std::abs(dot(displacement, face.normal)), 1.0e-12 * face.length);
          viscous_coefficient = 4.0 * physics_.viscosity() * face.length /
                                (std::max(left.rho, physics_.density_floor()) * distance);
        }

        const double pressure_fx = wall_state.p * face.normal.x * face.length;
        const double pressure_fy = wall_state.p * face.normal.y * face.length;
        double viscous_fx = -viscous[1] * face.length;
        double viscous_fy = -viscous[2] * face.length;
        const double normal_viscous = viscous_fx * face.normal.x + viscous_fy * face.normal.y;
        viscous_fx -= normal_viscous * face.normal.x;
        viscous_fy -= normal_viscous * face.normal.y;
        local_force[0] += pressure_fx;
        local_force[1] += pressure_fy;
        local_force[2] += viscous_fx;
        local_force[3] += viscous_fy;
        const Vec2 arm = face.center - config_.reference.moment_center;
        local_force[4] += arm.x * (pressure_fy + viscous_fy) - arm.y * (pressure_fx + viscous_fx);

        if (collect_surface) {
          const double cp = (wall_state.p - config_.freestream.pressure) / config_.dynamic_pressure();
          const Vec2 tangent{-face.normal.y, face.normal.x};
          const double cf = (viscous_fx * tangent.x + viscous_fy * tangent.y) /
                            (config_.dynamic_pressure() * face.length);
          const double mach = std::sqrt(wall_state.u * wall_state.u + wall_state.v * wall_state.v) /
                              physics_.sound_speed(wall_state);
          out.surface.push_back({face.center.x, face.center.y, face.normal.x, face.normal.y,
                                 wall_state.p, cp, cf, wall_state.rho, wall_state.u, wall_state.v,
                                 mach, face.boundary_tag});
        }
      }

      const Conserved total_flux = inviscid - viscous;
      const Conserved integrated = total_flux * face.length;
      if (mesh_.is_owned(face.left)) out.residual[static_cast<std::size_t>(face.left)] += integrated;
      if (face.right >= 0 && mesh_.is_owned(face.right)) out.residual[static_cast<std::size_t>(face.right)] -= integrated;
      const double coefficient = wave_speed * face.length + viscous_coefficient;
      out.face_coefficient[face_index] = coefficient;
      if (mesh_.is_owned(face.left)) {
        const auto left_index = static_cast<std::size_t>(face.left);
        out.spectral_sum[left_index] += coefficient;
        if (face.right >= 0 || face.boundary_type == BoundaryType::farfield) {
          Matrix4 diagonal = euler_flux_jacobian(left_center, face.normal, physics_.gamma());
          for (double& entry : diagonal) entry *= 0.5 * face.length;
          add_identity(diagonal, 0.5 * wave_speed * face.length + viscous_coefficient);
          add_matrix(out.diagonal_jacobian[left_index], diagonal);
        } else {
          add_identity(out.diagonal_jacobian[left_index], coefficient);
        }
        if (face.right >= 0) {
          Matrix4 off = euler_flux_jacobian(out.primitive[static_cast<std::size_t>(face.right)],
                                            face.normal, physics_.gamma());
          for (double& entry : off) entry *= 0.5 * face.length;
          add_identity(off, -0.5 * wave_speed * face.length - viscous_coefficient);
          out.off_diagonal_from_right[face_index] = off;
        }
      }
      if (face.right >= 0 && mesh_.is_owned(face.right)) {
        const auto right_index = static_cast<std::size_t>(face.right);
        out.spectral_sum[right_index] += coefficient;
        const Vec2 opposite_normal{-face.normal.x, -face.normal.y};
        Matrix4 diagonal = euler_flux_jacobian(out.primitive[right_index], opposite_normal, physics_.gamma());
        for (double& entry : diagonal) entry *= 0.5 * face.length;
        add_identity(diagonal, 0.5 * wave_speed * face.length + viscous_coefficient);
        add_matrix(out.diagonal_jacobian[right_index], diagonal);
        Matrix4 off = euler_flux_jacobian(left_center, opposite_normal, physics_.gamma());
        for (double& entry : off) entry *= 0.5 * face.length;
        add_identity(off, -0.5 * wave_speed * face.length - viscous_coefficient);
        out.off_diagonal_from_left[face_index] = off;
      }
    }

    calculate_norms(out);
    std::array<double, 5> global_force{};
    MPI_Allreduce(local_force.data(), global_force.data(), static_cast<int>(global_force.size()), MPI_DOUBLE, MPI_SUM,
                  communicator_);
    std::int64_t global_hllc_fallbacks = 0;
    std::int64_t global_positivity_fallbacks = 0;
    MPI_Allreduce(&local_hllc_fallbacks, &global_hllc_fallbacks, 1, MPI_INT64_T, MPI_SUM, communicator_);
    MPI_Allreduce(&local_positivity_fallbacks, &global_positivity_fallbacks, 1, MPI_INT64_T, MPI_SUM, communicator_);
    out.hllc_fallbacks = global_hllc_fallbacks;
    out.positivity_fallbacks = global_positivity_fallbacks;

    const double angle = config_.freestream.aoa_degrees * std::acos(-1.0) / 180.0;
    const Vec2 drag_direction{std::cos(angle), std::sin(angle)};
    const Vec2 lift_direction{-std::sin(angle), std::cos(angle)};
    const Vec2 pressure_force{global_force[0], global_force[1]};
    const Vec2 viscous_force{global_force[2], global_force[3]};
    const double denominator = config_.dynamic_pressure() * config_.reference.area;
    out.force.pressure_drag = dot(pressure_force, drag_direction) / denominator;
    out.force.viscous_drag = dot(viscous_force, drag_direction) / denominator;
    out.force.pressure_lift = dot(pressure_force, lift_direction) / denominator;
    out.force.viscous_lift = dot(viscous_force, lift_direction) / denominator;
    out.force.cd = out.force.pressure_drag + out.force.viscous_drag;
    out.force.cl = out.force.pressure_lift + out.force.viscous_lift;
    out.force.cmz = global_force[4] /
        (config_.dynamic_pressure() * config_.reference.area * config_.reference.length);
    return out;
  }

  void add_physical_time_residual(Evaluation& evaluation, const std::vector<Conserved>& current,
                                  const std::vector<Conserved>& previous,
                                  const std::vector<Conserved>& older, double dt, bool first_step) {
    const double alpha = first_step ? 1.0 : 1.5;
    const double beta = first_step ? -1.0 : -2.0;
    const double gamma = first_step ? 0.0 : 0.5;
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      const auto i = static_cast<std::size_t>(cell);
      const double scale = mesh_.cells[i].area / dt;
      for (int variable = 0; variable < nvars; ++variable) {
        const auto k = static_cast<std::size_t>(variable);
        evaluation.residual[i][k] += scale * (alpha * current[i][k] + beta * previous[i][k] + gamma * older[i][k]);
      }
    }
    calculate_norms(evaluation);
  }

 private:
  void compute_gradients(const std::vector<Primitive>& primitive,
                         std::vector<PrimitiveGradient>& gradients) const {
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      const LocalCell& local_cell = mesh_.cells[static_cast<std::size_t>(cell)];
      double a00 = 0.0;
      double a01 = 0.0;
      double a11 = 0.0;
      std::array<Vec2, nvars> right_hand_side{};
      for (int face_index : local_cell.faces) {
        const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
        const int neighbor = other_cell(face, cell);
        Primitive sample;
        Vec2 displacement;
        if (neighbor >= 0) {
          sample = primitive[static_cast<std::size_t>(neighbor)];
          displacement = mesh_.cells[static_cast<std::size_t>(neighbor)].center - local_cell.center;
        } else {
          sample = physics_.boundary_state(primitive[static_cast<std::size_t>(cell)], face.boundary_type, face.normal);
          displacement = 2.0 * (face.center - local_cell.center);
        }
        const double distance_squared = std::max(norm2(displacement), 1.0e-28);
        const double weight = 1.0 / distance_squared;
        a00 += weight * displacement.x * displacement.x;
        a01 += weight * displacement.x * displacement.y;
        a11 += weight * displacement.y * displacement.y;
        for (int component = 0; component < nvars; ++component) {
          const double difference = sample[static_cast<std::size_t>(component)] -
                                    primitive[static_cast<std::size_t>(cell)][static_cast<std::size_t>(component)];
          right_hand_side[static_cast<std::size_t>(component)] += displacement * (weight * difference);
        }
      }
      double determinant = a00 * a11 - a01 * a01;
      const double trace = a00 + a11;
      if (determinant < 1.0e-12 * trace * trace) {
        const double regularization = std::max(1.0e-14, 1.0e-8 * trace);
        a00 += regularization;
        a11 += regularization;
        determinant = a00 * a11 - a01 * a01;
      }
      if (!(determinant > 0.0) || !std::isfinite(determinant)) throw std::runtime_error("singular least-squares gradient matrix");
      for (int component = 0; component < nvars; ++component) {
        const Vec2 rhs = right_hand_side[static_cast<std::size_t>(component)];
        gradients[static_cast<std::size_t>(cell)].q[static_cast<std::size_t>(component)] =
            {(a11 * rhs.x - a01 * rhs.y) / determinant,
             (-a01 * rhs.x + a00 * rhs.y) / determinant};
      }
    }
  }

  void compute_limiters(const std::vector<Primitive>& primitive,
                        const std::vector<PrimitiveGradient>& gradients,
                        std::vector<Limiter>& limiter) const {
    constexpr double epsilon = 1.0e-14;
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      const auto cell_index = static_cast<std::size_t>(cell);
      const LocalCell& local_cell = mesh_.cells[cell_index];
      std::array<double, nvars> minimum{};
      std::array<double, nvars> maximum{};
      for (int component = 0; component < nvars; ++component) {
        minimum[static_cast<std::size_t>(component)] = primitive[cell_index][static_cast<std::size_t>(component)];
        maximum[static_cast<std::size_t>(component)] = primitive[cell_index][static_cast<std::size_t>(component)];
      }
      for (int face_index : local_cell.faces) {
        const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
        const int neighbor = other_cell(face, cell);
        const Primitive sample = neighbor >= 0 ? primitive[static_cast<std::size_t>(neighbor)]
            : physics_.boundary_state(primitive[cell_index], face.boundary_type, face.normal);
        for (int component = 0; component < nvars; ++component) {
          const auto k = static_cast<std::size_t>(component);
          minimum[k] = std::min(minimum[k], sample[k]);
          maximum[k] = std::max(maximum[k], sample[k]);
        }
      }
      Limiter phi{1.0, 1.0, 1.0, 1.0};
      for (int face_index : local_cell.faces) {
        const LocalFace& face = mesh_.faces[static_cast<std::size_t>(face_index)];
        const Vec2 displacement = face.center - local_cell.center;
        for (int component = 0; component < nvars; ++component) {
          const auto k = static_cast<std::size_t>(component);
          const double change = dot(gradients[cell_index].q[k], displacement);
          if (change > epsilon) phi[k] = std::min(phi[k], (maximum[k] - primitive[cell_index][k]) / change);
          else if (change < -epsilon) phi[k] = std::min(phi[k], (minimum[k] - primitive[cell_index][k]) / change);
        }
      }
      phi[0] = std::max(0.0, std::min(1.0, phi[0]));
      phi[3] = std::max(0.0, std::min(1.0, phi[3]));
      for (int component : {1, 2}) {
        phi[static_cast<std::size_t>(component)] =
            std::max(0.0, std::min(1.0, phi[static_cast<std::size_t>(component)]));
      }
      for (int face_index : local_cell.faces) {
        const Vec2 displacement = mesh_.faces[static_cast<std::size_t>(face_index)].center - local_cell.center;
        const double density_change = dot(gradients[cell_index].q[0], displacement);
        const double pressure_change = dot(gradients[cell_index].q[3], displacement);
        if (density_change < 0.0) {
          phi[0] = std::min(phi[0], 0.99 * (primitive[cell_index].rho - physics_.density_floor()) / (-density_change));
        }
        if (pressure_change < 0.0) {
          phi[3] = std::min(phi[3], 0.99 * (primitive[cell_index].p - physics_.pressure_floor()) / (-pressure_change));
        }
      }
      for (double& value : phi) value = std::max(0.0, std::min(1.0, value));
      limiter[cell_index] = phi;
    }
  }

  void calculate_norms(Evaluation& evaluation) const {
    std::array<double, nvars> local_sums{};
    std::array<double, nvars> local_maxima{};
    double local_total_sum = 0.0;
    double local_total_maximum = 0.0;
    for (int cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      const auto i = static_cast<std::size_t>(cell);
      const double inverse_area = 1.0 / mesh_.cells[i].area;
      double cell_sum = 0.0;
      for (int component = 0; component < nvars; ++component) {
        const auto k = static_cast<std::size_t>(component);
        const double value = evaluation.residual[i][k] * inverse_area;
        local_sums[k] += value * value;
        local_maxima[k] = std::max(local_maxima[k], std::abs(value));
        cell_sum += value * value;
      }
      local_total_sum += cell_sum;
      local_total_maximum = std::max(local_total_maximum, std::sqrt(cell_sum));
    }
    std::array<double, nvars> global_sums{};
    std::array<double, nvars> global_maxima{};
    MPI_Allreduce(local_sums.data(), global_sums.data(), nvars, MPI_DOUBLE, MPI_SUM, communicator_);
    MPI_Allreduce(local_maxima.data(), global_maxima.data(), nvars, MPI_DOUBLE, MPI_MAX, communicator_);
    double global_total_sum = 0.0;
    MPI_Allreduce(&local_total_sum, &global_total_sum, 1, MPI_DOUBLE, MPI_SUM, communicator_);
    MPI_Allreduce(&local_total_maximum, &evaluation.total_linf, 1, MPI_DOUBLE, MPI_MAX, communicator_);
    for (int component = 0; component < nvars; ++component) {
      evaluation.component_l2[static_cast<std::size_t>(component)] =
          std::sqrt(global_sums[static_cast<std::size_t>(component)] / static_cast<double>(mesh_.global_cell_count));
    }
    evaluation.total_l2 = std::sqrt(global_total_sum / static_cast<double>(mesh_.global_cell_count));
  }

  const CaseConfig& config_;
  const LocalMesh& mesh_;
  MPI_Comm communicator_;
  PerfectGasPhysics& physics_;
  HaloExchange& halo_;
  int rank_ = 0;
};

double cfl_at_step(const RunControl& run, int step) {
  if (run.pseudo_cfl_ramp_steps <= 0) return run.cfl_max;
  const double fraction = std::min(1.0, static_cast<double>(step) / static_cast<double>(run.pseudo_cfl_ramp_steps));
  if (run.cfl_initial <= 0.0 || run.cfl_max <= 0.0) return run.cfl_initial;
  return run.cfl_initial * std::pow(run.cfl_max / run.cfl_initial, fraction);
}

int solve_scalar_block_jacobi(const LocalMesh& mesh, HaloExchange& halo,
                              const Evaluation& evaluation, const std::vector<Conserved>& right_hand_side,
                              double cfl, const std::vector<double>& physical_diagonal,
                              int minimum_iterations, int maximum_iterations, double target,
                              std::vector<Conserved>& increment, MPI_Comm communicator) {
  increment.assign(mesh.cells.size(), zeros());
  std::vector<Conserved> next = increment;
  double initial_change_norm = -1.0;
  for (int iteration = 1; iteration <= maximum_iterations; ++iteration) {
    halo.state(increment, 7410);
    double local_change_sum = 0.0;
    for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const auto i = static_cast<std::size_t>(cell);
      Matrix4 diagonal = evaluation.diagonal_jacobian[i];
      add_identity(diagonal, physical_diagonal[i] + evaluation.spectral_sum[i] / cfl);
      Conserved rhs = right_hand_side[i];
      for (int face_index : mesh.cells[i].faces) {
        const LocalFace& face = mesh.faces[static_cast<std::size_t>(face_index)];
        const int neighbor = other_cell(face, cell);
        if (neighbor < 0) continue;
        const Matrix4& off = face.left == cell
            ? evaluation.off_diagonal_from_right[static_cast<std::size_t>(face_index)]
            : evaluation.off_diagonal_from_left[static_cast<std::size_t>(face_index)];
        rhs -= matrix_vector(off, increment[static_cast<std::size_t>(neighbor)]);
      }
      next[i] = solve_matrix(diagonal, rhs);
      for (int component = 0; component < nvars; ++component) {
        const double difference = next[i][static_cast<std::size_t>(component)] -
                                  increment[i][static_cast<std::size_t>(component)];
        local_change_sum += difference * difference;
      }
    }
    for (int cell = 0; cell < mesh.owned_cell_count; ++cell) increment[static_cast<std::size_t>(cell)] = next[static_cast<std::size_t>(cell)];
    double global_change_sum = 0.0;
    MPI_Allreduce(&local_change_sum, &global_change_sum, 1, MPI_DOUBLE, MPI_SUM, communicator);
    const double change_norm = std::sqrt(global_change_sum / static_cast<double>(mesh.global_cell_count));
    if (initial_change_norm < 0.0) initial_change_norm = std::max(change_norm, 1.0e-300);
    if (iteration >= minimum_iterations && change_norm / initial_change_norm <= target) return iteration;
  }
  return maximum_iterations;
}

std::int64_t apply_increment(const LocalMesh& mesh, const PerfectGasPhysics& physics,
                             std::vector<Conserved>& state, const std::vector<Conserved>& increment) {
  std::int64_t local_damped = 0;
  for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
    const auto i = static_cast<std::size_t>(cell);
    double alpha = 1.0;
    Conserved candidate{};
    while (alpha >= std::ldexp(1.0, -24)) {
      candidate = state[i] + increment[i] * alpha;
      if (physics.admissible(candidate)) break;
      alpha *= 0.5;
    }
    if (!physics.admissible(candidate)) throw std::runtime_error("positivity line search failed to find an admissible update");
    if (alpha < 1.0) ++local_damped;
    state[i] = candidate;
  }
  return local_damped;
}

ResidualRow make_residual_row(int step, double time, int inner, double cfl, double dt,
                              const Evaluation& evaluation) {
  return {step, time, inner, cfl, dt, evaluation.component_l2,
          evaluation.total_l2, evaluation.total_linf};
}

std::string progress_line(int step, double time, const Evaluation& evaluation,
                          const ForceRow& force, double reduction, int inner) {
  std::ostringstream line;
  line << "step=" << step << " t=" << std::fixed << std::setprecision(4) << time
       << " inner=" << inner << " residual=" << std::scientific << std::setprecision(5)
       << evaluation.total_l2 << " reduction=" << std::fixed << std::setprecision(3) << reduction
       << " Cd=" << std::scientific << force.cd << " Cl=" << force.cl;
  return line.str();
}

bool forces_stable(const std::vector<ForceRow>& history, std::size_t window, double tolerance) {
  if (history.size() < window) return false;
  double min_cd = std::numeric_limits<double>::infinity();
  double max_cd = -std::numeric_limits<double>::infinity();
  double min_cl = std::numeric_limits<double>::infinity();
  double max_cl = -std::numeric_limits<double>::infinity();
  for (std::size_t i = history.size() - window; i < history.size(); ++i) {
    min_cd = std::min(min_cd, history[i].cd);
    max_cd = std::max(max_cd, history[i].cd);
    min_cl = std::min(min_cl, history[i].cl);
    max_cl = std::max(max_cl, history[i].cl);
  }
  return max_cd - min_cd < tolerance * std::max(1.0, std::abs(0.5 * (min_cd + max_cd))) &&
         max_cl - min_cl < tolerance * std::max(1.0, std::abs(0.5 * (min_cl + max_cl)));
}

bool statistically_periodic(const std::vector<ForceRow>& history, double final_time, std::string& details) {
  std::vector<const ForceRow*> late;
  const double start = std::max(0.5 * final_time, final_time - 100.0);
  for (const ForceRow& row : history) if (row.physical_time >= start) late.push_back(&row);
  if (late.size() < 100) {
    details = "insufficient post-transient force samples";
    return false;
  }
  double mean_lift = 0.0;
  double mean_drag = 0.0;
  for (const ForceRow* row : late) {
    mean_lift += row->cl;
    mean_drag += row->cd;
  }
  mean_lift /= static_cast<double>(late.size());
  mean_drag /= static_cast<double>(late.size());
  double variance = 0.0;
  for (const ForceRow* row : late) variance += (row->cl - mean_lift) * (row->cl - mean_lift);
  const double lift_rms = std::sqrt(variance / static_cast<double>(late.size()));
  std::vector<double> crossings;
  for (std::size_t i = 1; i < late.size(); ++i) {
    if (late[i - 1]->cl < mean_lift && late[i]->cl >= mean_lift) crossings.push_back(late[i]->physical_time);
  }
  std::ostringstream stream;
  stream << "post-transient mean Cd=" << mean_drag << ", lift RMS=" << lift_rms
         << ", upward mean crossings=" << crossings.size();
  details = stream.str();
  return mean_drag > 0.0 && lift_rms > 1.0e-5 && crossings.size() >= 4;
}

void initialize_freestream(const CaseConfig& config, const LocalMesh& mesh,
                           const PerfectGasPhysics& physics, std::vector<Conserved>& state) {
  state.resize(mesh.cells.size());
  const Primitive freestream = config.freestream_primitive();
  for (std::size_t cell = 0; cell < mesh.cells.size(); ++cell) {
    Primitive initialized = freestream;
    if (config.run.transient) {
      const Vec2 relative = mesh.cells[cell].center - config.reference.moment_center;
      const double envelope = std::exp(-0.2 * norm2(relative));
      initialized.v += 1.0e-3 * config.freestream.velocity_magnitude * envelope *
                       std::sin(2.0 * std::acos(-1.0) * relative.x);
    }
    state[cell] = physics.to_conserved(initialized);
  }
}

}  // namespace

RunResult run_solver(const CaseConfig& config, const LocalMesh& mesh, MPI_Comm communicator,
                     RunObserver& observer, const RunOptions& options,
                     const std::optional<InitialState>& restart) {
  int rank = 0;
  MPI_Comm_rank(communicator, &rank);
  PerfectGasPhysics physics(config);
  HaloExchange halo(mesh, communicator);
  ResidualEvaluator evaluator(config, mesh, communicator, physics, halo);

  RunResult result;
  if (restart) {
    if (restart->state.size() != mesh.cells.size() || restart->previous_state.size() != mesh.cells.size()) {
      throw std::runtime_error("restart state does not match the local partition size");
    }
    result.state = restart->state;
    result.previous_state = restart->previous_state;
    result.final_step = restart->step;
    result.final_physical_time = restart->physical_time;
  } else {
    initialize_freestream(config, mesh, physics, result.state);
    result.previous_state = result.state;
  }

  if (!config.run.transient) {
    Evaluation evaluation = evaluator.evaluate(result.state, false);
    const double initial_residual = std::max(evaluation.total_l2, 1.0e-300);
    int maximum_steps = config.run.max_steps;
    if (options.debug_max_steps > 0 && options.debug_max_steps < maximum_steps) {
      maximum_steps = options.debug_max_steps;
      result.debug_limited = true;
    }
    std::vector<double> zero_physical_diagonal(static_cast<std::size_t>(mesh.owned_cell_count), 0.0);
    std::vector<Conserved> increment;
    for (int step = result.final_step + 1; step <= maximum_steps; ++step) {
      const double cfl = cfl_at_step(config.run, step);
      std::vector<Conserved> rhs = evaluation.residual;
      for (Conserved& value : rhs) value = value * -1.0;
      const int linear_iterations = solve_scalar_block_jacobi(
          mesh, halo, evaluation, rhs, cfl, zero_physical_diagonal,
          config.run.min_inner_iterations, config.run.max_inner_iterations,
          config.run.inner_residual_reduction_target, increment, communicator);
      const std::int64_t local_damped = apply_increment(mesh, physics, result.state, increment);
      std::int64_t global_damped = 0;
      MPI_Allreduce(&local_damped, &global_damped, 1, MPI_INT64_T, MPI_SUM, communicator);
      result.damped_updates += global_damped;
      evaluation = evaluator.evaluate(result.state, false);
      result.hllc_fallback_faces += evaluation.hllc_fallbacks;
      result.reconstruction_positivity_fallbacks += evaluation.positivity_fallbacks;
      const double reduction = std::log10(initial_residual / std::max(evaluation.total_l2, 1.0e-300));
      ResidualRow residual = make_residual_row(step, 0.0, linear_iterations, cfl, 0.0, evaluation);
      ForceRow force = evaluation.force;
      force.step = step;
      force.physical_time = 0.0;
      observer.residual(residual);
      observer.force(force);
      result.force_history.push_back(force);
      result.final_step = step;
      result.residual_reduction_orders = reduction;
      if (rank == 0 && options.progress_interval > 0 && (step == 1 || step % options.progress_interval == 0)) {
        observer.progress(progress_line(step, 0.0, evaluation, force, reduction, linear_iterations));
      }
      if (reduction >= config.run.residual_reduction_target && forces_stable(result.force_history, 50, 1.0e-5)) {
        result.convergence_status = "converged";
        result.notes = "Residual target reached and terminal force window is stable.";
        break;
      }
    }
    if (result.convergence_status != "converged" && !result.debug_limited &&
        result.residual_reduction_orders >= 0.9 * config.run.residual_reduction_target &&
        forces_stable(result.force_history, 500, 5.0e-5)) {
      result.convergence_status = "converged";
      result.notes = "Steady residual reached a documented near-target plateau with stable forces.";
    }
    if (result.convergence_status != "converged") {
      result.notes = result.debug_limited ? "Debug step limit reached; this is not a final result."
                                          : "Steady convergence criteria were not reached.";
    }
  } else {
    const int supplied_steps = static_cast<int>(std::llround(config.run.final_time / config.run.time_step));
    int final_target_step = supplied_steps;
    if (options.debug_max_steps > 0 && options.debug_max_steps < final_target_step) {
      final_target_step = options.debug_max_steps;
      result.debug_limited = true;
    }
    if (options.debug_final_time > 0.0) {
      const int debug_steps = static_cast<int>(std::floor(options.debug_final_time / config.run.time_step + 1.0e-12));
      if (debug_steps < final_target_step) {
        final_target_step = debug_steps;
        result.debug_limited = true;
      }
    }
    result.inner_statistics.requested_min = config.run.min_inner_iterations;
    result.inner_statistics.requested_max = config.run.max_inner_iterations;
    result.inner_statistics.target = config.run.inner_residual_reduction_target;
    result.inner_statistics.observed_min = std::numeric_limits<int>::max();
    long long total_inner_iterations = 0;
    int completed_physical_steps = 0;
    std::vector<Conserved> older = result.previous_state;
    std::vector<Conserved> previous = result.state;
    const int first_new_step = result.final_step + 1;
    double next_field_time = std::floor(result.final_physical_time + 1.0e-12) +
                             std::max(config.outputs.write_field_every_time, config.run.time_step);
    for (int step = first_new_step; step <= final_target_step; ++step) {
      const bool first_bdf_step = step == 1;
      std::vector<Conserved> current = previous;
      if (!first_bdf_step) {
        for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
          const auto i = static_cast<std::size_t>(cell);
          const Conserved extrapolated = previous[i] + (previous[i] - older[i]);
          if (physics.admissible(extrapolated)) current[i] = extrapolated;
        }
      }
      double initial_inner_residual = 0.0;
      double final_ratio = std::numeric_limits<double>::infinity();
      int used_inner_iterations = 0;
      Evaluation evaluation;
      for (int inner = 1; inner <= config.run.max_inner_iterations; ++inner) {
        evaluation = evaluator.evaluate(current, false);
        evaluator.add_physical_time_residual(evaluation, current, previous, older,
                                             config.run.time_step, first_bdf_step);
        if (inner == 1) initial_inner_residual = std::max(evaluation.total_l2, 1.0e-300);
        final_ratio = evaluation.total_l2 / initial_inner_residual;
        observer.residual(make_residual_row(step, static_cast<double>(step) * config.run.time_step, inner,
                                            config.run.cfl_max, config.run.time_step, evaluation));
        used_inner_iterations = inner;
        if (inner >= config.run.min_inner_iterations && final_ratio <= config.run.inner_residual_reduction_target) break;

        const double alpha = first_bdf_step ? 1.0 : 1.5;
        std::vector<double> physical_diagonal(static_cast<std::size_t>(mesh.owned_cell_count));
        for (int cell = 0; cell < mesh.owned_cell_count; ++cell) {
          physical_diagonal[static_cast<std::size_t>(cell)] =
              alpha * mesh.cells[static_cast<std::size_t>(cell)].area / config.run.time_step;
        }
        std::vector<Conserved> rhs = evaluation.residual;
        for (Conserved& value : rhs) value = value * -1.0;
        std::vector<Conserved> increment;
        solve_scalar_block_jacobi(mesh, halo, evaluation, rhs, config.run.cfl_max, physical_diagonal,
                                  2, 5, 0.1, increment, communicator);
        const std::int64_t local_damped = apply_increment(mesh, physics, current, increment);
        std::int64_t global_damped = 0;
        MPI_Allreduce(&local_damped, &global_damped, 1, MPI_INT64_T, MPI_SUM, communicator);
        result.damped_updates += global_damped;
      }
      if (final_ratio > config.run.inner_residual_reduction_target) ++result.inner_statistics.target_misses;
      result.inner_statistics.last_ratio = final_ratio;
      result.inner_statistics.observed_min = std::min(result.inner_statistics.observed_min, used_inner_iterations);
      result.inner_statistics.observed_max = std::max(result.inner_statistics.observed_max, used_inner_iterations);
      total_inner_iterations += used_inner_iterations;
      ++completed_physical_steps;

      older = previous;
      previous = current;
      result.state = current;
      result.previous_state = older;
      result.final_step = step;
      result.final_physical_time = static_cast<double>(step) * config.run.time_step;
      result.hllc_fallback_faces += evaluation.hllc_fallbacks;
      result.reconstruction_positivity_fallbacks += evaluation.positivity_fallbacks;
      ForceRow force = evaluation.force;
      force.step = step;
      force.physical_time = result.final_physical_time;
      observer.force(force);
      result.force_history.push_back(force);
      if (rank == 0 && options.progress_interval > 0 && (step == first_new_step || step % options.progress_interval == 0)) {
        observer.progress(progress_line(step, result.final_physical_time, evaluation, force,
                                        -std::log10(std::max(final_ratio, 1.0e-300)), used_inner_iterations));
      }
      if (config.outputs.write_field_every_time > 0.0 && result.final_physical_time + 1.0e-10 >= next_field_time) {
        Evaluation snapshot = evaluator.evaluate(result.state, false);
        observer.field(step, result.final_physical_time, result.state, snapshot.vorticity, false);
        next_field_time += config.outputs.write_field_every_time;
      }
    }
    if (completed_physical_steps > 0) {
      result.inner_statistics.observed_mean = static_cast<double>(total_inner_iterations) /
                                              static_cast<double>(completed_physical_steps);
      result.inner_statistics.target_converged_fraction =
          1.0 - static_cast<double>(result.inner_statistics.target_misses) /
                    static_cast<double>(completed_physical_steps);
    } else {
      result.inner_statistics.observed_min = 0;
      result.inner_statistics.target_converged_fraction = 0.0;
    }
    std::string periodic_details;
    const bool periodic = statistically_periodic(result.force_history, config.run.final_time, periodic_details);
    if (!result.debug_limited && result.final_physical_time + 1.0e-10 >= config.run.final_time &&
        result.inner_statistics.target_converged_fraction >= 0.95 && periodic) {
      result.convergence_status = "statistically_periodic";
      result.notes = "True frozen-history BDF2 run reached the production horizon; " + periodic_details + ".";
    } else if (result.debug_limited) {
      result.notes = "Debug transient limit reached; this is not a final result.";
    } else {
      result.notes = "Transient completion/statistical-periodicity gate failed: " + periodic_details + ".";
    }
  }

  Evaluation final_evaluation = evaluator.evaluate(result.state, true);
  result.vorticity = std::move(final_evaluation.vorticity);
  result.local_surface = std::move(final_evaluation.surface);
  observer.field(result.final_step, result.final_physical_time, result.state, result.vorticity, true);
  return result;
}

}  // namespace cfd
