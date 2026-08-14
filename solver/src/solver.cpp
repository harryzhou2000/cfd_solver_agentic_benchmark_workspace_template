#include "cfd/solver.hpp"

#include "cfd/boundary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

namespace cfd {

const char* to_string(SteadyAcceptanceMode mode) noexcept {
  switch (mode) {
    case SteadyAcceptanceMode::none:
      return "none";
    case SteadyAcceptanceMode::jfnk:
      return "jfnk";
    case SteadyAcceptanceMode::implicit_trust_region_retry:
      return "implicit_trust_region_retry";
    case SteadyAcceptanceMode::newton_rescue:
      return "newton_rescue";
    case SteadyAcceptanceMode::implicit_pseudo_transient_bridge:
      return "implicit_pseudo_transient_bridge";
    case SteadyAcceptanceMode::pseudo_time_fallback:
      return "pseudo_time_fallback";
  }
  return "none";
}

namespace {

double owned_dot(const DistributedMesh& mesh, const std::vector<double>& left,
                 const std::vector<double>& right, MPI_Comm communicator) {
  if (left.size() < mesh.owned_cell_count * 4U ||
      right.size() < mesh.owned_cell_count * 4U) {
    throw std::invalid_argument("distributed vector is smaller than its owned range");
  }
  double local = 0.0;
  for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
    local += left[i] * right[i];
  }
  double global = 0.0;
  MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, communicator);
  return global;
}

double owned_norm(const DistributedMesh& mesh, const std::vector<double>& value,
                  MPI_Comm communicator) {
  if (value.size() < mesh.owned_cell_count * 4U) {
    throw std::invalid_argument(
        "distributed vector is smaller than its owned range");
  }
  long double local_squared = 0.0L;
  for (std::size_t index = 0; index < mesh.owned_cell_count * 4U; ++index) {
    const long double entry = static_cast<long double>(value[index]);
    local_squared += entry * entry;
  }
  long double global_squared = 0.0L;
  MPI_Allreduce(&local_squared, &global_squared, 1, MPI_LONG_DOUBLE, MPI_SUM,
                communicator);
  return static_cast<double>(std::sqrt(std::max(0.0L, global_squared)));
}

double vector_norm(const DistributedMesh& mesh, const std::vector<double>& value,
                   MPI_Comm communicator) {
  return owned_norm(mesh, value, communicator) /
         std::sqrt(static_cast<double>(mesh.global_cell_count));
}

std::array<double, 4> component_vector_norms(
    const DistributedMesh& mesh, const std::vector<double>& value,
    MPI_Comm communicator) {
  std::array<long double, 4> local{};
  for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const long double entry =
          static_cast<long double>(value[cell * 4U + component]);
      local[component] += entry * entry;
    }
  }
  std::array<long double, 4> global{};
  MPI_Allreduce(local.data(), global.data(), 4, MPI_LONG_DOUBLE, MPI_SUM,
                communicator);
  const long double count = static_cast<long double>(mesh.global_cell_count);
  std::array<double, 4> norms{};
  for (std::size_t component = 0; component < 4U; ++component) {
    norms[component] = static_cast<double>(
        std::sqrt(std::max(0.0L, global[component]) / count));
  }
  return norms;
}

void euler_jacobian(const Primitive& state, const Vec2& normal, double gamma,
                    std::array<double, 16>& matrix) {
  matrix.fill(0.0);
  const double velocity_squared = state.u * state.u + state.v * state.v;
  const double normal_velocity = state.u * normal.x + state.v * normal.y;
  const double enthalpy = gamma * state.p / ((gamma - 1.0) * state.rho) +
                          0.5 * velocity_squared;
  matrix[1] = normal.x;
  matrix[2] = normal.y;
  matrix[4] = -state.u * normal_velocity +
              0.5 * (gamma - 1.0) * velocity_squared * normal.x;
  matrix[5] = normal_velocity + (2.0 - gamma) * state.u * normal.x;
  matrix[6] = state.u * normal.y - (gamma - 1.0) * state.v * normal.x;
  matrix[7] = (gamma - 1.0) * normal.x;
  matrix[8] = -state.v * normal_velocity +
              0.5 * (gamma - 1.0) * velocity_squared * normal.y;
  matrix[9] = state.v * normal.x - (gamma - 1.0) * state.u * normal.y;
  matrix[10] = normal_velocity + (2.0 - gamma) * state.v * normal.y;
  matrix[11] = (gamma - 1.0) * normal.y;
  matrix[12] = normal_velocity *
               (0.5 * (gamma - 1.0) * velocity_squared - enthalpy);
  matrix[13] = enthalpy * normal.x - (gamma - 1.0) * state.u * normal_velocity;
  matrix[14] = enthalpy * normal.y - (gamma - 1.0) * state.v * normal_velocity;
  matrix[15] = gamma * normal_velocity;
}

bool invert_block(const double* source, double* inverse) {
  double augmented[4][8]{};
  double scale = 0.0;
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      augmented[row][column] = source[row * 4U + column];
      scale = std::max(scale, std::abs(augmented[row][column]));
    }
    augmented[row][row + 4U] = 1.0;
  }
  for (std::size_t column = 0; column < 4U; ++column) {
    std::size_t pivot = column;
    for (std::size_t row = column + 1U; row < 4U; ++row) {
      if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) {
        pivot = row;
      }
    }
    if (std::abs(augmented[pivot][column]) <= 1.0e-13 * std::max(scale, 1.0)) {
      return false;
    }
    if (pivot != column) {
      for (std::size_t entry = 0; entry < 8U; ++entry) {
        std::swap(augmented[pivot][entry], augmented[column][entry]);
      }
    }
    const double divisor = augmented[column][column];
    for (double& entry : augmented[column]) entry /= divisor;
    for (std::size_t row = 0; row < 4U; ++row) {
      if (row == column) continue;
      const double factor = augmented[row][column];
      for (std::size_t entry = 0; entry < 8U; ++entry) {
        augmented[row][entry] -= factor * augmented[column][entry];
      }
    }
  }
  for (std::size_t row = 0; row < 4U; ++row) {
    for (std::size_t column = 0; column < 4U; ++column) {
      inverse[row * 4U + column] = augmented[row][column + 4U];
    }
  }
  return true;
}

void build_block_jacobi_inverse(
    const DistributedMesh& mesh, const ResidualResult& frozen,
    const std::vector<double>& state, const CaloricallyPerfectGas& gas,
    const CaseConfig& config, const Conservative& freestream, double cfl,
    double physical_diagonal, std::vector<double>& cell_coupling,
    std::vector<double>& diagonal_block, std::vector<double>& inverse_block) {
  cell_coupling.assign(mesh.faces.size(), 0.0);
  diagonal_block.assign(mesh.owned_cell_count * 16U, 0.0);
  inverse_block.assign(mesh.owned_cell_count * 16U, 0.0);
  const Primitive freestream_primitive = gas.primitive(freestream);
  for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
    const double radius = frozen.spectral_radius[cell];
    double* block = diagonal_block.data() + cell * 16U;
    const double scalar = radius / cfl + physical_diagonal * mesh.cells[cell].area;
    for (std::size_t k = 0; k < 4U; ++k) block[k * 4U + k] = scalar;
    const Conservative conservative{state[cell * 4U], state[cell * 4U + 1U],
                                    state[cell * 4U + 2U], state[cell * 4U + 3U]};
    const Primitive primitive = gas.primitive(conservative);
    double represented_convective_radius = 0.0;
    for (LocalIndex face_index : mesh.cells[cell].faces) {
      const LocalFace& face = mesh.faces[static_cast<std::size_t>(face_index)];
      const LocalIndex neighbor = face.left_cell == static_cast<LocalIndex>(cell)
                                      ? face.right_cell
                                      : face.left_cell;
      const Vec2 outward = face.left_cell == static_cast<LocalIndex>(cell)
                               ? face.normal
                               : -1.0 * face.normal;
      if (neighbor >= 0 || config.boundary_conditions.at(face.boundary) ==
                               BoundaryCondition::farfield) {
        Primitive other = freestream_primitive;
        if (neighbor >= 0) {
          const std::size_t index = static_cast<std::size_t>(neighbor);
          other = gas.primitive(Conservative{
              state[index * 4U], state[index * 4U + 1U],
              state[index * 4U + 2U], state[index * 4U + 3U]});
        }
        const double wave = std::max(
            std::abs(primitive.u * outward.x + primitive.v * outward.y) + primitive.a,
            std::abs(other.u * outward.x + other.v * outward.y) + other.a);
        const double coupling = 0.5 * wave * face.length;
        represented_convective_radius += wave * face.length;
        if (neighbor >= 0) {
          cell_coupling[static_cast<std::size_t>(face_index)] = coupling;
        }
        for (std::size_t k = 0; k < 4U; ++k) block[k * 4U + k] += coupling;
        std::array<double, 16> jacobian{};
        euler_jacobian(primitive, outward, gas.gamma(), jacobian);
        for (std::size_t row = 0; row < 4U; ++row) {
          for (std::size_t column = 0; column < 4U; ++column) {
            block[row * 4U + column] +=
                0.5 * face.length * jacobian[row * 4U + column];
          }
        }
      } else {
        const double pressure_rho =
            0.5 * (gas.gamma() - 1.0) *
            (primitive.u * primitive.u + primitive.v * primitive.v);
        const std::array<double, 4> pressure_derivative{
            pressure_rho, -(gas.gamma() - 1.0) * primitive.u,
            -(gas.gamma() - 1.0) * primitive.v, gas.gamma() - 1.0};
        for (std::size_t column = 0; column < 4U; ++column) {
          block[1U * 4U + column] +=
              face.length * outward.x * pressure_derivative[column];
          block[2U * 4U + column] +=
              face.length * outward.y * pressure_derivative[column];
        }
      }
    }
    const double unrepresented =
        std::max(0.0, radius - represented_convective_radius);
    for (std::size_t k = 0; k < 4U; ++k) {
      block[k * 4U + k] += 0.5 * unrepresented;
    }
    double* inverse = inverse_block.data() + cell * 16U;
    if (!invert_block(block, inverse)) {
      std::fill(inverse, inverse + 16U, 0.0);
      for (std::size_t k = 0; k < 4U; ++k) {
        inverse[k * 4U + k] = 1.0 / std::max(block[k * 4U + k], 1.0e-300);
      }
    }
  }
}

[[maybe_unused]] InnerSolveStats block_jacobi(const DistributedMesh& mesh,
                            const ResidualResult& frozen,
                            const std::vector<double>& state,
                            const CaloricallyPerfectGas& gas,
                            const CaseConfig& config,
                            const Conservative& freestream,
                            const std::vector<double>& right_hand_side,
                            double cfl, double physical_diagonal,
                            int minimum_sweeps, int maximum_sweeps,
                            double target, MPI_Comm communicator,
                            std::vector<double>& correction,
                            std::vector<double>& next,
                            std::vector<double>& cell_coupling,
                            std::vector<double>& diagonal_block,
                            std::vector<double>& inverse_block) {
  InnerSolveStats stats;
  stats.linear_solver = transient_linear_solver_name;
  correction.assign(mesh.cells.size() * 4U, 0.0);
  next.assign(mesh.cells.size() * 4U, 0.0);
  build_block_jacobi_inverse(mesh, frozen, state, gas, config, freestream,
                             cfl, physical_diagonal, cell_coupling,
                             diagonal_block, inverse_block);
  auto defect_norm = [&]() {
    double local = 0.0;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double* block = diagonal_block.data() + cell * 16U;
      for (std::size_t row = 0; row < 4U; ++row) {
        double applied = 0.0;
        for (std::size_t column = 0; column < 4U; ++column) {
          applied += block[row * 4U + column] * correction[cell * 4U + column];
        }
        for (LocalIndex face_index : mesh.cells[cell].faces) {
          const LocalFace& face = mesh.faces[static_cast<std::size_t>(face_index)];
          const LocalIndex neighbor = face.left_cell == static_cast<LocalIndex>(cell)
                                          ? face.right_cell
                                          : face.left_cell;
          if (neighbor >= 0) {
            applied -= cell_coupling[static_cast<std::size_t>(face_index)] *
                       correction[static_cast<std::size_t>(neighbor) * 4U + row];
          }
        }
        const double defect = right_hand_side[cell * 4U + row] - applied;
        local += defect * defect;
      }
    }
    double global = 0.0;
    MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, communicator);
    return std::sqrt(global / static_cast<double>(mesh.global_cell_count));
  };

  stats.initial_defect = vector_norm(mesh, right_hand_side, communicator);
  stats.final_defect = stats.initial_defect;
  if (stats.initial_defect == 0.0) {
    stats.converged = true;
    stats.defect_ratio = 0.0;
    return stats;
  }
  for (int sweep = 1; sweep <= maximum_sweeps; ++sweep) {
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      std::array<double, 4> effective{};
      for (std::size_t component = 0; component < 4U; ++component) {
        effective[component] = right_hand_side[cell * 4U + component];
        for (LocalIndex face_index : mesh.cells[cell].faces) {
          const LocalFace& face = mesh.faces[static_cast<std::size_t>(face_index)];
          const LocalIndex neighbor = face.left_cell == static_cast<LocalIndex>(cell)
                                          ? face.right_cell
                                          : face.left_cell;
          if (neighbor >= 0) {
            effective[component] +=
                cell_coupling[static_cast<std::size_t>(face_index)] *
                correction[static_cast<std::size_t>(neighbor) * 4U + component];
          }
        }
      }
      const double* inverse = inverse_block.data() + cell * 16U;
      for (std::size_t row = 0; row < 4U; ++row) {
        next[cell * 4U + row] = 0.0;
        for (std::size_t column = 0; column < 4U; ++column) {
          next[cell * 4U + row] += inverse[row * 4U + column] * effective[column];
        }
      }
    }
    for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
      correction[i] = next[i];
    }
    exchange_halo(mesh, correction, 4U, communicator);
    stats.linear_sweeps = sweep;
    stats.total_linear_sweeps = sweep;
    const bool check = sweep == maximum_sweeps ||
                       (sweep >= minimum_sweeps &&
                        (sweep - minimum_sweeps) % 3 == 0);
    if (check) {
      stats.final_defect = defect_norm();
      stats.defect_ratio = stats.final_defect / stats.initial_defect;
      if (sweep >= minimum_sweeps && stats.defect_ratio <= target) {
        stats.converged = true;
        break;
      }
    }
  }
  return stats;
}

}  // namespace

FrozenRusanovLUSGSOperator::FrozenRusanovLUSGSOperator(
    const DistributedMesh& mesh, const CaseConfig& config,
    const std::vector<double>& state,
    const ResidualResult& first_order_residual, double cfl,
    MPI_Comm communicator, double physical_diagonal,
    double pseudo_time_diagonal_scale)
    : mesh_(mesh), communicator_(communicator) {
  if (state.size() != mesh_.cells.size() * 4U ||
      first_order_residual.spectral_radius.size() != mesh_.owned_cell_count ||
      !(cfl > 0.0) || !std::isfinite(cfl) ||
      !(physical_diagonal >= 0.0) || !std::isfinite(physical_diagonal) ||
      !(pseudo_time_diagonal_scale >= 0.0) ||
      !std::isfinite(pseudo_time_diagonal_scale)) {
    throw std::invalid_argument("invalid frozen Rusanov LU-SGS inputs");
  }
  const CaloricallyPerfectGas gas(config.gas);
  const Conservative freestream = gas.freestream(config.freestream);
  const Primitive freestream_primitive = gas.primitive(freestream);
  const double dissipation =
      config.run_control.rusanov_dissipation_scale.value_or(1.0);
  diagonal_.resize(mesh_.owned_cell_count);
  inverse_diagonal_.resize(mesh_.owned_cell_count);
  left_to_right_.resize(mesh_.faces.size());
  right_to_left_.resize(mesh_.faces.size());

  auto conservative_at = [&](LocalIndex cell) {
    const std::size_t offset = static_cast<std::size_t>(cell) * 4U;
    return Conservative{state[offset], state[offset + 1U], state[offset + 2U],
                        state[offset + 3U]};
  };
  auto add_block = [](ConservativeJacobian& destination,
                      const ConservativeJacobian& source) {
    for (std::size_t entry = 0; entry < 16U; ++entry) {
      destination[entry] += source[entry];
    }
  };

  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const double pseudo_diagonal = pseudo_time_diagonal_scale *
        first_order_residual.spectral_radius[cell] / cfl;
    if (!(pseudo_diagonal >= 0.0) || !std::isfinite(pseudo_diagonal)) {
      throw std::invalid_argument("invalid LU-SGS pseudo-time spectral diagonal");
    }
    for (std::size_t component = 0; component < 4U; ++component) {
      diagonal_[cell][component * 4U + component] =
          pseudo_diagonal + physical_diagonal * mesh_.cells[cell].area;
    }
  }

  for (std::size_t face_index = 0; face_index < mesh_.faces.size(); ++face_index) {
    const LocalFace& face = mesh_.faces[face_index];
    const std::size_t left = static_cast<std::size_t>(face.left_cell);
    const bool left_owned = left < mesh_.owned_cell_count;
    const Conservative left_state = conservative_at(face.left_cell);
    if (face.right_cell >= 0) {
      const std::size_t right = static_cast<std::size_t>(face.right_cell);
      const bool right_owned = right < mesh_.owned_cell_count;
      const RusanovFaceJacobian blocks = frozen_rusanov_face_jacobian(
          left_state, conservative_at(face.right_cell), face.normal,
          face.length, gas, dissipation);
      left_to_right_[face_index] = blocks.left_right;
      right_to_left_[face_index] = blocks.right_left;
      if (left_owned) add_block(diagonal_[left], blocks.left_left);
      if (right_owned) add_block(diagonal_[right], blocks.right_right);
      continue;
    }
    if (!left_owned) {
      throw std::logic_error("LU-SGS boundary face is not owned locally");
    }
    const BoundaryCondition condition = config.boundary_conditions.at(face.boundary);
    if (condition == BoundaryCondition::farfield) {
      const Primitive exterior = boundary_exterior_state(
          condition, gas.primitive(left_state), freestream_primitive,
          face.normal, gas);
      const RusanovFaceJacobian blocks = frozen_rusanov_face_jacobian(
          left_state, gas.conservative(exterior), face.normal, face.length,
          gas, dissipation);
      // The characteristic exterior state is frozen for the approximate
      // preconditioner, leaving a conservative local boundary contribution.
      add_block(diagonal_[left], blocks.left_left);
    } else {
      const Primitive primitive = gas.primitive(left_state);
      const double gamma_minus_one = gas.gamma() - 1.0;
      const std::array<double, 4> pressure_derivative{
          0.5 * gamma_minus_one *
              (primitive.u * primitive.u + primitive.v * primitive.v),
          -gamma_minus_one * primitive.u,
          -gamma_minus_one * primitive.v, gamma_minus_one};
      for (std::size_t column = 0; column < 4U; ++column) {
        diagonal_[left][1U * 4U + column] +=
            face.length * face.normal.x * pressure_derivative[column];
        diagonal_[left][2U * 4U + column] +=
            face.length * face.normal.y * pressure_derivative[column];
      }
    }
  }

  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    ConservativeJacobian regularized = diagonal_[cell];
    bool inverted = false;
    const double base = std::max(
        1.0, pseudo_time_diagonal_scale *
                 first_order_residual.spectral_radius[cell] / cfl +
                 physical_diagonal * mesh_.cells[cell].area);
    for (int attempt = 0; attempt < 8 && !inverted; ++attempt) {
      if (attempt > 0) {
        const double shift = base * std::pow(10.0, attempt - 13);
        regularized = diagonal_[cell];
        for (std::size_t component = 0; component < 4U; ++component) {
          regularized[component * 4U + component] += shift;
        }
      }
      inverted = invert_block(regularized.data(),
                              inverse_diagonal_[cell].data());
    }
    if (!inverted) {
      throw std::runtime_error("unable to invert full LU-SGS diagonal block");
    }
    diagonal_[cell] = regularized;
  }

  forward_order_.resize(mesh_.owned_cell_count);
  std::iota(forward_order_.begin(), forward_order_.end(), 0U);
  std::sort(forward_order_.begin(), forward_order_.end(),
            [&](std::size_t left, std::size_t right) {
              return mesh_.cells[left].global_id < mesh_.cells[right].global_id;
            });
}

void FrozenRusanovLUSGSOperator::apply(
    const std::vector<double>& direction, std::vector<double>& product) const {
  if (direction.size() != mesh_.cells.size() * 4U) {
    throw std::invalid_argument("LU-SGS operator direction dimension mismatch");
  }
  std::vector<double> exchanged = direction;
  exchange_halo(mesh_, exchanged, 4U, communicator_);
  product.assign(mesh_.cells.size() * 4U, 0.0);
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t row = 0; row < 4U; ++row) {
      for (std::size_t column = 0; column < 4U; ++column) {
        product[cell * 4U + row] +=
            diagonal_[cell][row * 4U + column] *
            exchanged[cell * 4U + column];
      }
    }
    for (LocalIndex local_face : mesh_.cells[cell].faces) {
      const std::size_t face_index = static_cast<std::size_t>(local_face);
      const LocalFace& face = mesh_.faces[face_index];
      const LocalIndex neighbor =
          face.left_cell == static_cast<LocalIndex>(cell)
              ? face.right_cell
              : face.left_cell;
      if (neighbor < 0) continue;
      const ConservativeJacobian& block =
          face.left_cell == static_cast<LocalIndex>(cell)
              ? left_to_right_[face_index]
              : right_to_left_[face_index];
      const std::size_t neighbor_offset =
          static_cast<std::size_t>(neighbor) * 4U;
      for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
          product[cell * 4U + row] +=
              block[row * 4U + column] *
              exchanged[neighbor_offset + column];
        }
      }
    }
  }
}

InnerSolveStats FrozenRusanovLUSGSOperator::solve(
    const std::vector<double>& right_hand_side, int minimum_sweeps,
    int maximum_sweeps, double relative_tolerance,
    std::vector<double>& correction) const {
  if (right_hand_side.size() != mesh_.cells.size() * 4U ||
      minimum_sweeps < 3 || maximum_sweeps < minimum_sweeps ||
      !(relative_tolerance >= 0.0) || !std::isfinite(relative_tolerance)) {
    throw std::invalid_argument("invalid LU-SGS solve controls");
  }
  InnerSolveStats stats;
  stats.linear_solver = steady_fallback_solver_name;
  correction.assign(mesh_.cells.size() * 4U, 0.0);
  stats.initial_defect = vector_norm(mesh_, right_hand_side, communicator_);
  stats.final_defect = stats.initial_defect;
  if (stats.initial_defect == 0.0) {
    stats.defect_ratio = 0.0;
    stats.converged = true;
    return stats;
  }

  auto update_cell = [&](std::size_t cell) {
    Conservative effective{};
    for (std::size_t component = 0; component < 4U; ++component) {
      effective[component] = right_hand_side[cell * 4U + component];
    }
    for (LocalIndex local_face : mesh_.cells[cell].faces) {
      const std::size_t face_index = static_cast<std::size_t>(local_face);
      const LocalFace& face = mesh_.faces[face_index];
      const LocalIndex neighbor =
          face.left_cell == static_cast<LocalIndex>(cell)
              ? face.right_cell
              : face.left_cell;
      if (neighbor < 0) continue;
      const ConservativeJacobian& block =
          face.left_cell == static_cast<LocalIndex>(cell)
              ? left_to_right_[face_index]
              : right_to_left_[face_index];
      const std::size_t neighbor_offset =
          static_cast<std::size_t>(neighbor) * 4U;
      for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
          effective[row] -= block[row * 4U + column] *
                            correction[neighbor_offset + column];
        }
      }
    }
    for (std::size_t row = 0; row < 4U; ++row) {
      double value = 0.0;
      for (std::size_t column = 0; column < 4U; ++column) {
        value += inverse_diagonal_[cell][row * 4U + column] *
                 effective[column];
      }
      correction[cell * 4U + row] = value;
    }
  };

  std::vector<double> applied;
  for (int sweep = 1; sweep <= maximum_sweeps; ++sweep) {
    // Remote blocks use the previous completed half-sweep. Local rows consume
    // updates immediately in globally deterministic cell-ID order.
    exchange_halo(mesh_, correction, 4U, communicator_);
    for (std::size_t cell : forward_order_) update_cell(cell);
    exchange_halo(mesh_, correction, 4U, communicator_);
    for (auto iterator = forward_order_.rbegin();
         iterator != forward_order_.rend(); ++iterator) {
      update_cell(*iterator);
    }
    exchange_halo(mesh_, correction, 4U, communicator_);
    stats.linear_sweeps = sweep;
    stats.total_linear_sweeps = sweep;
    if (sweep >= minimum_sweeps) {
      apply(correction, applied);
      double local_squared = 0.0;
      for (std::size_t entry = 0;
           entry < mesh_.owned_cell_count * 4U; ++entry) {
        const double defect = right_hand_side[entry] - applied[entry];
        local_squared += defect * defect;
      }
      double global_squared = 0.0;
      MPI_Allreduce(&local_squared, &global_squared, 1, MPI_DOUBLE, MPI_SUM,
                    communicator_);
      stats.final_defect = std::sqrt(
          global_squared / static_cast<double>(mesh_.global_cell_count));
      stats.defect_ratio = stats.final_defect / stats.initial_defect;
      if (stats.defect_ratio <= relative_tolerance) {
        stats.converged = true;
        break;
      }
    }
  }
  return stats;
}

double steady_jfnk_epsilon_multiplier(double nonlinear_residual,
                                      double reference_residual) noexcept {
  if (!(nonlinear_residual >= 0.0) || !std::isfinite(nonlinear_residual) ||
      !(reference_residual > 0.0) || !std::isfinite(reference_residual)) {
    return 1.0;
  }
  if (nonlinear_residual >= reference_residual) return 1.0;
  const double multiplier =
      std::sqrt(nonlinear_residual) / std::sqrt(reference_residual);
  return std::clamp(multiplier, steady_jfnk_minimum_epsilon_multiplier, 1.0);
}

SteadyMatrixFreeOperator::SteadyMatrixFreeOperator(
    const DistributedMesh& mesh, ResidualOperator& residual,
    const std::vector<double>& state, const ResidualResult& base_residual,
    double cfl, MPI_Comm communicator, double reconstruction_blend)
    : mesh_(mesh),
      residual_(residual),
      state_(state),
      base_residual_(base_residual),
      cfl_(cfl),
      reconstruction_blend_(reconstruction_blend),
      communicator_(communicator) {
  if (state_.size() != mesh_.cells.size() * 4U ||
      base_residual_.value.size() != mesh_.owned_cell_count * 4U ||
      base_residual_.spectral_radius.size() != mesh_.owned_cell_count ||
      !(cfl_ > 0.0) || !std::isfinite(cfl_) ||
      !std::isfinite(reconstruction_blend_) || reconstruction_blend_ < 0.0 ||
      reconstruction_blend_ > 1.0) {
    throw std::invalid_argument("invalid steady matrix-free operator inputs");
  }
  std::array<double, 4> local_squared{};
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const double value = state_[cell * 4U + component];
      local_squared[component] += value * value;
    }
  }
  std::array<double, 4> global_squared{};
  MPI_Allreduce(local_squared.data(), global_squared.data(), 4, MPI_DOUBLE,
                MPI_SUM, communicator_);
  double maximum_rms = 0.0;
  for (std::size_t component = 0; component < 4U; ++component) {
    component_scale_[component] = std::sqrt(
        global_squared[component] /
        static_cast<double>(mesh_.global_cell_count));
    maximum_rms = std::max(maximum_rms, component_scale_[component]);
  }
  const double scale_floor = std::max(1.0e-14, 1.0e-3 * maximum_rms);
  scaled_state_norm_ = 0.0;
  for (std::size_t component = 0; component < 4U; ++component) {
    component_scale_[component] =
        std::max(component_scale_[component], scale_floor);
    scaled_state_norm_ +=
        global_squared[component] /
        (component_scale_[component] * component_scale_[component]);
  }
  scaled_state_norm_ = std::sqrt(scaled_state_norm_);
}

void SteadyMatrixFreeOperator::apply(const std::vector<double>& direction,
                                     std::vector<double>& product,
                                     double epsilon_multiplier) {
  if (direction.size() != state_.size() || !(epsilon_multiplier > 0.0) ||
      !std::isfinite(epsilon_multiplier)) {
    throw std::invalid_argument("matrix-free direction dimension mismatch");
  }
  product.assign(state_.size(), 0.0);
  double local_scaled_direction_squared = 0.0;
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const double scaled = direction[cell * 4U + component] /
                            component_scale_[component];
      local_scaled_direction_squared += scaled * scaled;
    }
  }
  double global_scaled_direction_squared = 0.0;
  MPI_Allreduce(&local_scaled_direction_squared,
                &global_scaled_direction_squared, 1, MPI_DOUBLE, MPI_SUM,
                communicator_);
  const double scaled_direction_norm =
      std::sqrt(global_scaled_direction_squared);
  if (scaled_direction_norm == 0.0) {
    last_epsilon_ = 0.0;
    last_epsilon_halvings_ = 0;
    return;
  }

  // Centered differences balance O(epsilon^2) truncation against roundoff at
  // cube-root machine epsilon. Component scaling prevents total energy from
  // dictating perturbations for density and momentum.
  double epsilon = epsilon_multiplier *
                   std::cbrt(std::numeric_limits<double>::epsilon()) *
                   (1.0 + scaled_state_norm_) / scaled_direction_norm;
  if (!(epsilon > 0.0) || !std::isfinite(epsilon)) {
    throw std::runtime_error("matrix-free perturbation scale is invalid");
  }
  bool admissible = false;
  last_epsilon_halvings_ = 0;
  for (; last_epsilon_halvings_ <= 30; ++last_epsilon_halvings_) {
    perturbed_state_ = state_;
    int local_admissible = 1;
    for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      Conservative positive{};
      Conservative negative{};
      for (std::size_t component = 0; component < 4U; ++component) {
        const std::size_t index = cell * 4U + component;
        perturbed_state_[index] += epsilon * direction[index];
        positive[component] = perturbed_state_[index];
        negative[component] = state_[index] - epsilon * direction[index];
      }
      if (!residual_.gas().admissible(positive) ||
          !residual_.gas().admissible(negative)) {
        local_admissible = 0;
        break;
      }
    }
    int global_admissible = 0;
    MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT, MPI_MIN,
                  communicator_);
    if (global_admissible != 0) {
      admissible = true;
      break;
    }
    epsilon *= 0.5;
  }
  if (!admissible) {
    throw std::runtime_error(
        "unable to construct a globally admissible matrix-free perturbation");
  }
  last_epsilon_ = epsilon;

  ResidualEvaluationOptions options;
  options.reconstruction_blend = reconstruction_blend_;
  options.collective_preflight = false;
  options.compute_global_norms = false;
  options.compute_global_diagnostics = false;
  options.collect_surface = false;
  residual_.evaluate_into(perturbed_state_, cfl_, perturbed_residual_, options);
  positive_residual_value_ = perturbed_residual_.value;
  perturbed_state_ = state_;
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const std::size_t index = cell * 4U + component;
      perturbed_state_[index] -= epsilon * direction[index];
    }
  }
  residual_.evaluate_into(perturbed_state_, cfl_, perturbed_residual_, options);
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const double pseudo_diagonal = base_residual_.spectral_radius[cell] / cfl_;
    for (std::size_t component = 0; component < 4U; ++component) {
      const std::size_t index = cell * 4U + component;
      product[index] =
          (positive_residual_value_[index] - perturbed_residual_.value[index]) /
              (2.0 * epsilon) +
          pseudo_diagonal * direction[index];
    }
  }
}

TransientMatrixFreeOperator::TransientMatrixFreeOperator(
    const DistributedMesh& mesh, ResidualOperator& residual,
    const std::vector<double>& state, const ResidualResult& base_residual,
    const std::vector<double>& history_n,
    const std::vector<double>& history_nm1, double time_step, int bdf_order,
    double cfl, MPI_Comm communicator)
    : mesh_(mesh),
      residual_(residual),
      state_(state),
      history_n_(history_n),
      history_nm1_(history_nm1),
      base_residual_(base_residual),
      cfl_(cfl),
      physical_diagonal_((bdf_order == 2 ? 1.5 : 1.0) / time_step),
      communicator_(communicator) {
  const std::size_t expected = mesh_.cells.size() * 4U;
  if (state.size() != expected || history_n_.size() != expected ||
      history_nm1_.size() != expected || !(time_step > 0.0) ||
      !std::isfinite(time_step) || (bdf_order != 1 && bdf_order != 2) ||
      base_residual_.value.size() != mesh_.owned_cell_count * 4U ||
      !(cfl_ > 0.0) || !std::isfinite(cfl_)) {
    throw std::invalid_argument("invalid transient matrix-free operator inputs");
  }
  std::array<double, 4> local_squared{};
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const double value = state_[cell * 4U + component];
      local_squared[component] += value * value;
    }
  }
  std::array<double, 4> global_squared{};
  MPI_Allreduce(local_squared.data(), global_squared.data(), 4, MPI_DOUBLE,
                MPI_SUM, communicator_);
  double maximum_rms = 0.0;
  for (std::size_t component = 0; component < 4U; ++component) {
    component_scale_[component] = std::sqrt(
        global_squared[component] /
        static_cast<double>(mesh_.global_cell_count));
    maximum_rms = std::max(maximum_rms, component_scale_[component]);
  }
  const double scale_floor = std::max(1.0e-14, 1.0e-3 * maximum_rms);
  for (std::size_t component = 0; component < 4U; ++component) {
    component_scale_[component] =
        std::max(component_scale_[component], scale_floor);
    scaled_state_norm_ += global_squared[component] /
        (component_scale_[component] * component_scale_[component]);
  }
  scaled_state_norm_ = std::sqrt(scaled_state_norm_);
}

void TransientMatrixFreeOperator::apply(
    const std::vector<double>& direction, std::vector<double>& product,
    double epsilon_multiplier) {
  if (direction.size() != state_.size() || !(epsilon_multiplier > 0.0) ||
      !std::isfinite(epsilon_multiplier)) {
    throw std::invalid_argument("transient matrix-free direction dimension mismatch");
  }
  (void)history_n_;
  (void)history_nm1_;
  product.assign(state_.size(), 0.0);
  double local_scaled_direction_squared = 0.0;
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const double scaled = direction[cell * 4U + component] /
                            component_scale_[component];
      local_scaled_direction_squared += scaled * scaled;
    }
  }
  double global_scaled_direction_squared = 0.0;
  MPI_Allreduce(&local_scaled_direction_squared,
                &global_scaled_direction_squared, 1, MPI_DOUBLE, MPI_SUM,
                communicator_);
  const double scaled_direction_norm =
      std::sqrt(global_scaled_direction_squared);
  if (scaled_direction_norm == 0.0) {
    last_epsilon_ = 0.0;
    last_epsilon_halvings_ = 0;
    return;
  }
  double epsilon = epsilon_multiplier *
      std::sqrt(std::numeric_limits<double>::epsilon()) *
      (1.0 + scaled_state_norm_) / scaled_direction_norm;
  if (!(epsilon > 0.0) || !std::isfinite(epsilon)) {
    throw std::runtime_error("transient matrix-free perturbation scale is invalid");
  }
  bool admissible = false;
  last_epsilon_halvings_ = 0;
  for (; last_epsilon_halvings_ <= 30; ++last_epsilon_halvings_) {
    perturbed_state_ = state_;
    int local_admissible = 1;
    for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      Conservative candidate{};
      for (std::size_t component = 0; component < 4U; ++component) {
        const std::size_t index = cell * 4U + component;
        perturbed_state_[index] += epsilon * direction[index];
        candidate[component] = perturbed_state_[index];
      }
      if (!residual_.gas().admissible(candidate)) {
        local_admissible = 0;
        break;
      }
    }
    int global_admissible = 0;
    MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT, MPI_MIN,
                  communicator_);
    if (global_admissible != 0) {
      admissible = true;
      break;
    }
    epsilon *= 0.5;
  }
  if (!admissible) {
    throw std::runtime_error(
        "unable to construct an admissible transient matrix-free perturbation");
  }
  last_epsilon_ = epsilon;
  ResidualEvaluationOptions options;
  options.reconstruction_blend = 1.0;
  options.collective_preflight = false;
  options.compute_global_norms = false;
  options.compute_global_diagnostics = false;
  options.collect_surface = false;
  residual_.evaluate_into(perturbed_state_, cfl_, perturbed_residual_, options);
  for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
    const double diagonal = physical_diagonal_ * mesh_.cells[cell].area;
    for (std::size_t component = 0; component < 4U; ++component) {
      const std::size_t index = cell * 4U + component;
      product[index] =
          (perturbed_residual_.value[index] - base_residual_.value[index]) /
              epsilon +
          diagonal * direction[index];
    }
  }
}

MatrixFreeGmresResult restarted_gmres(
    const DistributedMesh& mesh, const std::vector<double>& right_hand_side,
    const DistributedLinearAction& apply_operator,
    const DistributedLinearAction& apply_right_preconditioner,
    const MatrixFreeGmresOptions& options, MPI_Comm communicator,
    std::vector<double>& solution) {
  const std::size_t vector_size = mesh.cells.size() * 4U;
  if (right_hand_side.size() != vector_size || !apply_operator ||
      !apply_right_preconditioner || options.restart < 1 ||
      options.minimum_iterations < 0 ||
      options.maximum_iterations < std::max(1, options.minimum_iterations) ||
      !(options.relative_tolerance > 0.0) ||
      !(options.relative_tolerance < 1.0) ||
      !std::isfinite(options.relative_tolerance)) {
    throw std::invalid_argument("invalid restarted GMRES inputs");
  }

  MatrixFreeGmresResult result;
  solution.assign(vector_size, 0.0);
  std::vector<double> residual = right_hand_side;
  result.initial_residual = owned_norm(mesh, residual, communicator);
  result.final_residual = result.initial_residual;
  if (result.initial_residual == 0.0) {
    result.residual_ratio = 0.0;
    result.converged = true;
    return result;
  }

  std::vector<double> applied(vector_size, 0.0);
  while (result.iterations < options.maximum_iterations) {
    const int cycle_size =
        std::min(options.restart, options.maximum_iterations - result.iterations);
    const double beta = owned_norm(mesh, residual, communicator);
    if (beta == 0.0) {
      result.final_residual = 0.0;
      result.residual_ratio = 0.0;
      result.converged = result.iterations >= options.minimum_iterations;
      break;
    }
    std::vector<std::vector<double>> basis(
        static_cast<std::size_t>(cycle_size + 1),
        std::vector<double>(vector_size, 0.0));
    std::vector<std::vector<double>> preconditioned(
        static_cast<std::size_t>(cycle_size),
        std::vector<double>(vector_size, 0.0));
    for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
      basis[0][i] = residual[i] / beta;
    }
    const std::size_t leading = static_cast<std::size_t>(cycle_size + 1);
    std::vector<double> hessenberg(
        leading * static_cast<std::size_t>(cycle_size), 0.0);
    std::vector<double> cosine(static_cast<std::size_t>(cycle_size), 0.0);
    std::vector<double> sine(static_cast<std::size_t>(cycle_size), 0.0);
    std::vector<double> transformed_rhs(
        static_cast<std::size_t>(cycle_size + 1), 0.0);
    transformed_rhs[0] = beta;
    int used = 0;
    bool predicted_converged = false;

    for (int column = 0; column < cycle_size; ++column) {
      apply_right_preconditioner(
          basis[static_cast<std::size_t>(column)],
          preconditioned[static_cast<std::size_t>(column)]);
      apply_operator(preconditioned[static_cast<std::size_t>(column)], applied);
      if (applied.size() != vector_size ||
          preconditioned[static_cast<std::size_t>(column)].size() != vector_size) {
        throw std::runtime_error("GMRES action returned an invalid vector size");
      }

      // Two-pass modified Gram-Schmidt is materially more robust for the
      // strongly scaled conservative variables than a single classical pass.
      for (int pass = 0; pass < 2; ++pass) {
        for (int row = 0; row <= column; ++row) {
          const double projection = owned_dot(
              mesh, applied, basis[static_cast<std::size_t>(row)], communicator);
          hessenberg[static_cast<std::size_t>(row) +
                     leading * static_cast<std::size_t>(column)] += projection;
          for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
            applied[i] -= projection * basis[static_cast<std::size_t>(row)][i];
          }
        }
      }
      const double next_norm = owned_norm(mesh, applied, communicator);
      hessenberg[static_cast<std::size_t>(column + 1) +
                 leading * static_cast<std::size_t>(column)] = next_norm;
      if (next_norm > 10.0 * std::numeric_limits<double>::epsilon()) {
        for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
          basis[static_cast<std::size_t>(column + 1)][i] = applied[i] / next_norm;
        }
      }

      for (int rotation = 0; rotation < column; ++rotation) {
        double& upper = hessenberg[static_cast<std::size_t>(rotation) +
                                   leading * static_cast<std::size_t>(column)];
        double& lower = hessenberg[static_cast<std::size_t>(rotation + 1) +
                                   leading * static_cast<std::size_t>(column)];
        const double rotated_upper =
            cosine[static_cast<std::size_t>(rotation)] * upper +
            sine[static_cast<std::size_t>(rotation)] * lower;
        lower = -sine[static_cast<std::size_t>(rotation)] * upper +
                cosine[static_cast<std::size_t>(rotation)] * lower;
        upper = rotated_upper;
      }
      double& diagonal = hessenberg[static_cast<std::size_t>(column) +
                                    leading * static_cast<std::size_t>(column)];
      double& subdiagonal = hessenberg[static_cast<std::size_t>(column + 1) +
                                       leading * static_cast<std::size_t>(column)];
      const double magnitude = std::hypot(diagonal, subdiagonal);
      cosine[static_cast<std::size_t>(column)] =
          magnitude == 0.0 ? 1.0 : diagonal / magnitude;
      sine[static_cast<std::size_t>(column)] =
          magnitude == 0.0 ? 0.0 : subdiagonal / magnitude;
      diagonal = magnitude;
      subdiagonal = 0.0;
      transformed_rhs[static_cast<std::size_t>(column + 1)] =
          -sine[static_cast<std::size_t>(column)] *
          transformed_rhs[static_cast<std::size_t>(column)];
      transformed_rhs[static_cast<std::size_t>(column)] *=
          cosine[static_cast<std::size_t>(column)];
      ++result.iterations;
      used = column + 1;
      const double estimate =
          std::abs(transformed_rhs[static_cast<std::size_t>(column + 1)]);
      predicted_converged =
          result.iterations >= options.minimum_iterations &&
          estimate <= options.relative_tolerance * result.initial_residual;
      const bool arnoldi_breakdown =
          next_norm <= 10.0 * std::numeric_limits<double>::epsilon();
      if (predicted_converged || arnoldi_breakdown ||
          result.iterations == options.maximum_iterations) {
        break;
      }
    }

    std::vector<double> coefficients(static_cast<std::size_t>(used), 0.0);
    for (int row = used - 1; row >= 0; --row) {
      double value = transformed_rhs[static_cast<std::size_t>(row)];
      for (int column = row + 1; column < used; ++column) {
        value -= hessenberg[static_cast<std::size_t>(row) +
                            leading * static_cast<std::size_t>(column)] *
                 coefficients[static_cast<std::size_t>(column)];
      }
      const double diagonal =
          hessenberg[static_cast<std::size_t>(row) +
                      leading * static_cast<std::size_t>(row)];
      if (std::abs(diagonal) <= std::numeric_limits<double>::min()) {
        coefficients[static_cast<std::size_t>(row)] = 0.0;
      } else {
        coefficients[static_cast<std::size_t>(row)] = value / diagonal;
      }
    }
    for (int column = 0; column < used; ++column) {
      for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
        solution[i] += coefficients[static_cast<std::size_t>(column)] *
                       preconditioned[static_cast<std::size_t>(column)][i];
      }
    }
    exchange_halo(mesh, solution, 4U, communicator);
    apply_operator(solution, applied);
    residual.assign(vector_size, 0.0);
    for (std::size_t i = 0; i < mesh.owned_cell_count * 4U; ++i) {
      residual[i] = right_hand_side[i] - applied[i];
    }
    result.final_residual = owned_norm(mesh, residual, communicator);
    result.residual_ratio = result.final_residual / result.initial_residual;
    result.converged = result.iterations >= options.minimum_iterations &&
                       result.residual_ratio <= options.relative_tolerance;
    if (result.converged) break;
    if (used == 0 || (!predicted_converged && result.iterations >= options.maximum_iterations)) {
      break;
    }
  }
  return result;
}

Conservative bdf_physical_residual(const Conservative& Ustar,
                                   const Conservative& Un,
                                   const Conservative& Unm1,
                                   double cell_volume, double time_step,
                                   int order) {
  if (!(cell_volume > 0.0) || !(time_step > 0.0) ||
      !std::isfinite(cell_volume) || !std::isfinite(time_step)) {
    throw std::invalid_argument("BDF volume and time step must be finite and positive");
  }
  if (order != 1 && order != 2) {
    throw std::invalid_argument("BDF order must be one or two");
  }
  Conservative result{};
  for (std::size_t k = 0; k < 4U; ++k) {
    result[k] = order == 1
                    ? cell_volume * (Ustar[k] - Un[k]) / time_step
                    : cell_volume *
                          (3.0 * Ustar[k] - 4.0 * Un[k] + Unm1[k]) /
                          (2.0 * time_step);
  }
  return result;
}

double steady_residual_growth_fraction(double cfl) noexcept {
  if (!(cfl > 0.0) || !std::isfinite(cfl)) return 0.0;
  return std::clamp(1.0e-3 * cfl, 1.0e-5, 1.0e-3);
}

SteadySpatialOrderSchedule steady_spatial_order_schedule(
    int pseudo_cfl_ramp_steps) noexcept {
  const std::size_t derived = static_cast<std::size_t>(
      std::max(1, pseudo_cfl_ramp_steps > 0 ? pseudo_cfl_ramp_steps / 4 : 1));
  const std::size_t bounded = std::min<std::size_t>(500U, derived);
  return SteadySpatialOrderSchedule{bounded, bounded};
}

std::size_t steady_full_order_minimum_steps(
    int pseudo_cfl_ramp_steps) noexcept {
  const int scaled = std::max(0, pseudo_cfl_ramp_steps) / 10;
  return static_cast<std::size_t>(std::max(50, std::min(250, scaled)));
}

bool steady_convergence_gate(double reconstruction_blend,
                             std::size_t full_order_accepted_steps,
                             int pseudo_cfl_ramp_steps,
                             double original_initial_residual,
                             double current_residual,
                             double residual_reduction_target) noexcept {
  if (reconstruction_blend != 1.0 ||
      full_order_accepted_steps <
          steady_full_order_minimum_steps(pseudo_cfl_ramp_steps) ||
      !(original_initial_residual >= 0.0) || !(current_residual >= 0.0) ||
      !(residual_reduction_target >= 0.0) ||
      !std::isfinite(original_initial_residual) ||
      !std::isfinite(current_residual) ||
      !std::isfinite(residual_reduction_target)) {
    return false;
  }
  if (original_initial_residual == 0.0) return current_residual == 0.0;
  const double required_ratio = std::pow(10.0, -residual_reduction_target);
  return current_residual <= original_initial_residual * required_ratio;
}

double smooth_reconstruction_blend(std::size_t ramp_step,
                                   std::size_t ramp_steps) noexcept {
  if (ramp_steps == 0U || ramp_step >= ramp_steps) return 1.0;
  const double fraction = static_cast<double>(ramp_step) /
                          static_cast<double>(ramp_steps);
  return fraction * fraction * (3.0 - 2.0 * fraction);
}

bool steady_residual_within_envelope(double current_residual,
                                     double trial_residual,
                                     double best_residual,
                                     double cfl) noexcept {
  if (!(current_residual >= 0.0) || !(trial_residual >= 0.0) ||
      !std::isfinite(current_residual) || !std::isfinite(trial_residual)) {
    return false;
  }
  const double growth = steady_residual_growth_fraction(cfl);
  const double local_bound = current_residual * (1.0 + growth);
  const double reference = best_residual >= 0.0 && std::isfinite(best_residual)
                               ? std::min(best_residual, current_residual)
                               : current_residual;
  const double runaway_bound = reference * 1.01;
  return trial_residual <= std::min(local_bound, runaway_bound);
}

double steady_fallback_runaway_bound(
    const std::vector<double>& recent_residuals,
    double initial_residual, double best_residual,
    double current_residual) noexcept {
  if (!(current_residual >= 0.0) || !std::isfinite(current_residual)) {
    return 0.0;
  }
  double recent_maximum = current_residual;
  for (double residual : recent_residuals) {
    if (residual >= 0.0 && std::isfinite(residual)) {
      recent_maximum = std::max(recent_maximum, residual);
    }
  }
  double trusted_scale = current_residual;
  if (initial_residual >= 0.0 && std::isfinite(initial_residual)) {
    trusted_scale = initial_residual;
  }
  if (best_residual >= 0.0 && std::isfinite(best_residual)) {
    trusted_scale = std::max(trusted_scale, best_residual);
  }
  return std::min(2.0 * recent_maximum, 10.0 * trusted_scale);
}

bool steady_fallback_within_runaway_bound(
    const std::vector<double>& recent_residuals,
    double initial_residual, double best_residual,
    double current_residual, double trial_residual) noexcept {
  if (!(trial_residual >= 0.0) || !std::isfinite(trial_residual)) {
    return false;
  }
  const double bound = steady_fallback_runaway_bound(
      recent_residuals, initial_residual, best_residual, current_residual);
  const double roundoff = 1.0e-12 * std::max(1.0, bound);
  return trial_residual <= bound + roundoff;
}

std::vector<double> steady_trust_region_retry_cfls(double current_cfl,
                                                   double maximum_cfl) {
  if (!(current_cfl > 0.0) || !(maximum_cfl > 0.0) ||
      !std::isfinite(current_cfl) || !std::isfinite(maximum_cfl)) {
    throw std::invalid_argument(
        "trust-region retry CFL bounds must be finite and positive");
  }
  std::vector<double> result;
  double candidate = 0.1;
  while (candidate <= current_cfl * (1.0 + 1.0e-12) &&
         candidate < maximum_cfl) {
    candidate *= 10.0;
  }
  while (candidate < maximum_cfl * (1.0 - 1.0e-12)) {
    result.push_back(candidate);
    if (candidate > maximum_cfl / 10.0) break;
    candidate *= 10.0;
  }
  return result;
}

std::optional<std::size_t> best_strict_residual_decrease(
    double initial_residual,
    const std::vector<double>& trial_residuals) noexcept {
  if (!(initial_residual >= 0.0) || !std::isfinite(initial_residual)) {
    return std::nullopt;
  }
  std::optional<std::size_t> best;
  for (std::size_t i = 0; i < trial_residuals.size(); ++i) {
    const double residual = trial_residuals[i];
    if (std::isfinite(residual) && residual < initial_residual &&
        (!best.has_value() || residual < trial_residuals[*best])) {
      best = i;
    }
  }
  return best;
}

bool strict_steady_merit_decrease(double initial_residual,
                                  double trial_residual,
                                  double predicted_merit_reduction) noexcept {
  if (!(initial_residual >= 0.0) || !std::isfinite(initial_residual) ||
      !(trial_residual < initial_residual) ||
      !std::isfinite(trial_residual) ||
      !(predicted_merit_reduction > 0.0) ||
      !std::isfinite(predicted_merit_reduction)) {
    return false;
  }
  // Form the merit difference without subtracting two nearly equal squares.
  const double actual_reduction =
      0.5 * (initial_residual - trial_residual) *
      (initial_residual + trial_residual);
  const double merit_scale = 0.5 * std::max(initial_residual * initial_residual,
                                            trial_residual * trial_residual);
  const double roundoff = 64.0 * std::numeric_limits<double>::epsilon() *
                          merit_scale;
  return actual_reduction + roundoff >=
         1.0e-4 * predicted_merit_reduction;
}

bool steady_implicit_fallback_allowed(double reconstruction_blend,
                                      bool disabled,
                                      std::size_t consecutive_steps) noexcept {
  return std::isfinite(reconstruction_blend) &&
         reconstruction_blend >= 0.0 && reconstruction_blend <= 1.0 &&
         !disabled &&
         consecutive_steps < steady_fallback_maximum_bridge_steps;
}

bool steady_fallback_mode_has_history(bool requested_mode, bool disabled,
                                      std::size_t residual_samples) noexcept {
  return requested_mode && !disabled && residual_samples > 0U;
}

void steady_nonmonotone_push_residual(std::vector<double>& window,
                                      double residual) {
  if (!(residual >= 0.0) || !std::isfinite(residual)) {
    throw std::invalid_argument(
        "nonmonotone residual-window sample must be finite and nonnegative");
  }
  window.push_back(residual);
  if (window.size() > steady_nonmonotone_window_capacity) {
    window.erase(window.begin(),
                 window.begin() + static_cast<std::ptrdiff_t>(
                                      window.size() -
                                      steady_nonmonotone_window_capacity));
  }
}

double steady_nonmonotone_reference(const std::vector<double>& window,
                                    double current_residual) noexcept {
  if (!(current_residual >= 0.0) || !std::isfinite(current_residual)) {
    return -1.0;
  }
  double reference = current_residual;
  for (double residual : window) {
    if (!(residual >= 0.0) || !std::isfinite(residual)) return -1.0;
    reference = std::max(reference, residual);
  }
  return reference;
}

bool steady_nonmonotone_trial_acceptable(
    double reference_residual, double trial_residual, double best_residual,
    double predicted_merit_reduction) noexcept {
  if (!(reference_residual >= 0.0) || !std::isfinite(reference_residual) ||
      !(trial_residual >= 0.0) || !std::isfinite(trial_residual) ||
      !(best_residual >= 0.0) || !std::isfinite(best_residual) ||
      !(predicted_merit_reduction > 0.0) ||
      !std::isfinite(predicted_merit_reduction) ||
      trial_residual > steady_nonmonotone_best_residual_cap * best_residual) {
    return false;
  }
  const double merit_reduction =
      0.5 * (reference_residual - trial_residual) *
      (reference_residual + trial_residual);
  const double merit_scale =
      0.5 * std::max(reference_residual * reference_residual,
                     trial_residual * trial_residual);
  const double roundoff = 64.0 * std::numeric_limits<double>::epsilon() *
                          std::max(1.0e-300, merit_scale);
  return merit_reduction + roundoff >=
         1.0e-4 * predicted_merit_reduction;
}

bool steady_nonmonotone_eligible(
    bool full_order_implicit, bool bridge_allowed, double best_residual,
    const std::vector<double>& residual_window) noexcept {
  return full_order_implicit && bridge_allowed && best_residual >= 0.0 &&
         std::isfinite(best_residual) && !residual_window.empty() &&
         steady_nonmonotone_reference(residual_window, best_residual) >= 0.0;
}

bool steady_meaningful_best_decrease(double best_residual,
                                     double trial_residual) noexcept {
  if (!(best_residual > 0.0) || !std::isfinite(best_residual) ||
      !(trial_residual >= 0.0) || !std::isfinite(trial_residual) ||
      !(trial_residual < best_residual)) {
    return false;
  }
  const double relative_decrease =
      (best_residual - trial_residual) / best_residual;
  const double threshold = std::max(
      256.0 * std::numeric_limits<double>::epsilon(),
      steady_meaningful_best_relative_decrease);
  return relative_decrease > threshold;
}

std::size_t steady_nonmonotone_stagnation_after_attempt(
    std::size_t current_streak, bool full_order_implicit,
    bool meaningful_best_improvement) noexcept {
  if (!full_order_implicit) return 0U;
  if (meaningful_best_improvement) return 0U;
  return std::min(steady_nonmonotone_stagnation_attempts,
                  current_streak + 1U);
}

bool steady_limiter_nonlinearity_active(
    const GlobalDiagnostics& diagnostics) noexcept {
  return diagnostics.venkatakrishnan_limited_face_components > 0U ||
         diagnostics.shock_fallback_cells > 0U ||
         diagnostics.positivity_barth_fallbacks > 0U ||
         diagnostics.positivity_scaled > 0U ||
         diagnostics.first_order_fallbacks > 0U;
}

bool steady_nonmonotone_descent_bypass_allowed(
    bool nonmonotone_eligible, bool gmres_converged,
    double merit_slope, double residual_norm) noexcept {
  if (!nonmonotone_eligible || !gmres_converged ||
      !(residual_norm > 0.0) || !std::isfinite(residual_norm)) {
    return false;
  }
  const double reliable_descent =
      -256.0 * std::numeric_limits<double>::epsilon() *
      residual_norm * residual_norm;
  return !std::isfinite(merit_slope) || merit_slope >= reliable_descent;
}

double steady_nonmonotone_surrogate_slope(
    double residual_norm, double gmres_residual_ratio) noexcept {
  if (!(residual_norm > 0.0) || !std::isfinite(residual_norm) ||
      !std::isfinite(gmres_residual_ratio)) {
    return 0.0;
  }
  const double ratio = std::clamp(gmres_residual_ratio, 0.0, 1.0);
  const double fraction = std::max(
      steady_nonmonotone_surrogate_fraction * (1.0 - ratio),
      steady_nonmonotone_surrogate_minimum_fraction);
  return -fraction * residual_norm * residual_norm;
}

double steady_nonmonotone_activation_reference(double best_residual) noexcept {
  if (!(best_residual >= 0.0) || !std::isfinite(best_residual)) return -1.0;
  return best_residual *
         (1.0 + steady_nonmonotone_activation_envelope_fraction);
}

double steady_nonmonotone_update_envelope_reference(
    double current_reference, double best_residual, bool seed_now) noexcept {
  if (!seed_now) return current_reference;
  return steady_nonmonotone_activation_reference(best_residual);
}

double steady_nonmonotone_bounded_reference(
    const std::vector<double>& window, double current_residual,
    double best_residual, double envelope_reference) noexcept {
  const double window_reference =
      steady_nonmonotone_reference(window, current_residual);
  const double hard_envelope =
      steady_nonmonotone_activation_reference(best_residual);
  if (!(window_reference >= 0.0) || !(hard_envelope >= 0.0) ||
      !(envelope_reference >= best_residual) ||
      !std::isfinite(envelope_reference)) {
    return -1.0;
  }
  // The seed is fixed for an activation cycle. Accepted residuals enter the
  // real window, but can never ratchet this virtual reference upward.
  return std::min(hard_envelope,
                  std::max(window_reference, envelope_reference));
}

double steady_implicit_bridge_bounded_initial_cfl(
    double minimum_cfl, double maximum_cfl) noexcept {
  if (!(minimum_cfl > 0.0) || !std::isfinite(minimum_cfl) ||
      !(maximum_cfl >= minimum_cfl) || !std::isfinite(maximum_cfl)) {
    return 0.0;
  }
  return std::clamp(steady_implicit_bridge_initial_cfl, minimum_cfl,
                    maximum_cfl);
}

bool steady_implicit_bridge_residual_within_cap(
    double entry_best_residual, double trial_residual) noexcept {
  return entry_best_residual >= 0.0 && std::isfinite(entry_best_residual) &&
         trial_residual >= 0.0 && std::isfinite(trial_residual) &&
         trial_residual <=
             steady_implicit_bridge_residual_cap * entry_best_residual;
}

bool steady_implicit_bridge_eligible(
    bool disabled, bool active, std::size_t stagnation_attempts,
    double best_residual) noexcept {
  return !disabled &&
         (active ||
          stagnation_attempts >= steady_nonmonotone_stagnation_attempts) &&
         best_residual >= 0.0 && std::isfinite(best_residual);
}

double steady_implicit_bridge_epoch_reference(
    double best_residual, double previous_residual) noexcept {
  if (!(best_residual >= 0.0) || !std::isfinite(best_residual)) return -1.0;
  if (!(previous_residual >= 0.0) || !std::isfinite(previous_residual)) {
    return best_residual;
  }
  return std::max(best_residual, previous_residual);
}

double steady_phase_best_with_entry_evidence(
    double best_residual, double evaluated_entry_residual) noexcept {
  if (best_residual >= 0.0 && std::isfinite(best_residual)) {
    return best_residual;
  }
  if (evaluated_entry_residual >= 0.0 &&
      std::isfinite(evaluated_entry_residual)) {
    return evaluated_entry_residual;
  }
  return -1.0;
}

double steady_largest_positivity_safe_scale(
    const std::vector<double>& state, const std::vector<double>& direction,
    std::size_t owned_cell_count,
    const CaloricallyPerfectGas& gas) noexcept {
  if (state.size() != direction.size() ||
      state.size() < owned_cell_count * 4U) {
    return 0.0;
  }
  constexpr double minimum_scale = 1.0 / 1048576.0;
  for (double scale = 1.0; scale >= minimum_scale; scale *= 0.5) {
    bool admissible = true;
    for (std::size_t cell = 0; cell < owned_cell_count; ++cell) {
      Conservative trial{};
      for (std::size_t component = 0; component < 4U; ++component) {
        const std::size_t index = cell * 4U + component;
        trial[component] = state[index] + scale * direction[index];
      }
      if (!gas.admissible(trial)) {
        admissible = false;
        break;
      }
    }
    if (admissible) return scale;
  }
  return 0.0;
}

void update_steady_implicit_bridge(
    SteadyImplicitBridgeState& state, bool accepted,
    bool meaningful_best_improvement, double previous_residual,
    double trial_residual, double minimum_cfl, double maximum_cfl) noexcept {
  if (!(minimum_cfl > 0.0) || !(maximum_cfl >= minimum_cfl) ||
      !std::isfinite(minimum_cfl) || !std::isfinite(maximum_cfl)) {
    return;
  }
  state.cfl = std::clamp(state.cfl, minimum_cfl, maximum_cfl);
  if (!accepted) {
    state.cfl = std::max(minimum_cfl, 0.5 * state.cfl);
    const bool at_cfl_floor =
        state.cfl <= minimum_cfl *
                         (1.0 + 64.0 * std::numeric_limits<double>::epsilon());
    if (at_cfl_floor && state.accepted_steps_since_best > 0U &&
        state.maximum_relative_growth <=
            steady_implicit_bridge_growth_contraction_threshold - 1.0) {
      // A mild pseudo-time trajectory can reach its cycle cap before the
      // physical transient reaches its residual peak. End this bounded cycle
      // at the CFL floor; re-entry uses the current accepted residual as the
      // next cycle reference instead of rejecting the same state forever.
      state.active = false;
      state.accepted_steps_since_best = 0U;
      ++state.watchdog_stops;
    }
    return;
  }
  ++state.accepted_steps;
  if (state.residual_minimum < 0.0 || trial_residual < state.residual_minimum) {
    state.residual_minimum = trial_residual;
  }
  state.residual_maximum = std::max(state.residual_maximum, trial_residual);
  if (previous_residual > 0.0 && std::isfinite(previous_residual) &&
      trial_residual >= 0.0 && std::isfinite(trial_residual)) {
    const double ratio = trial_residual / previous_residual;
    state.maximum_relative_growth =
        std::max(state.maximum_relative_growth, std::max(0.0, ratio - 1.0));
    if (ratio > steady_implicit_bridge_growth_contraction_threshold) {
      state.cfl = std::max(minimum_cfl, 0.5 * state.cfl);
    } else if (ratio < 1.0) {
      state.cfl = std::min(maximum_cfl, 1.2 * state.cfl);
    }
  }
  if (meaningful_best_improvement) {
    ++state.meaningful_best_improvements;
    state.accepted_steps_since_best = 0U;
  } else {
    ++state.accepted_steps_since_best;
  }
  if (state.accepted_steps_since_best >=
          steady_implicit_bridge_watchdog_steps ||
      state.accepted_steps_since_best >=
          steady_implicit_bridge_maximum_steps) {
    state.active = false;
    state.accepted_steps_since_best = 0U;
    ++state.watchdog_stops;
  }
}

void note_steady_implicit_bridge_external_best(
    SteadyImplicitBridgeState& state) noexcept {
  if (state.active) state.accepted_steps_since_best = 0U;
}

bool steady_nonmonotone_watchdog_expired(
    std::size_t accepted_steps_since_strict_best) noexcept {
  return accepted_steps_since_strict_best >=
         steady_nonmonotone_watchdog_steps;
}

void update_steady_nonmonotone_watchdog(
    SteadyNonmonotoneWatchdogState& state, bool accepted_step,
    bool strict_best_improvement) noexcept {
  if (!state.active || !accepted_step) return;
  if (strict_best_improvement) {
    state.accepted_steps_since_strict_best = 0U;
    return;
  }
  ++state.accepted_steps_since_strict_best;
  if (steady_nonmonotone_watchdog_expired(
          state.accepted_steps_since_strict_best)) {
    state.active = false;
    state.disabled = true;
    state.accepted_steps_since_strict_best = 0U;
    ++state.resets;
  }
}

void update_steady_cfl_adaptation(SteadyCflAdaptationState& state,
                                  bool accepted,
                                  bool residual_increased,
                                  bool tiny_line_scale,
                                  double residual,
                                  double cfl_floor,
                                  double scheduled_cfl) noexcept {
  if (!accepted) {
    state.cfl = std::max(cfl_floor, 0.5 * state.cfl);
    state.trend_reference_residual = -1.0;
    state.trend_samples = 0;
    state.probe_active = false;
    state.recovery_probe_cfl = 0.0;
    state.recovery_restore_pending = false;
    ++state.rejected_attempts;
    return;
  }

  // A pending probe applies to this accepted attempt and is consumed here.
  state.probe_active = false;
  state.recovery_restore_pending = false;
  state.rejected_attempts = 0;
  if (tiny_line_scale) {
    state.cfl = std::max(cfl_floor, 0.5 * state.cfl);
  } else if (residual_increased) {
    // Envelope-safe increases remain useful pseudo-time steps, but contract the
    // trust region before another attempt.
    state.cfl = std::max(cfl_floor, 0.9 * state.cfl);
  }

  if (!(residual >= 0.0) || !std::isfinite(residual)) return;
  if (!(state.trend_reference_residual > 0.0) ||
      !std::isfinite(state.trend_reference_residual)) {
    state.trend_reference_residual = residual;
    state.trend_samples = 0;
    state.cfl = std::clamp(state.cfl, cfl_floor, scheduled_cfl);
    return;
  }

  ++state.trend_samples;
  constexpr int trend_window = 4;
  if (state.trend_samples >= trend_window) {
    const double relative_trend =
        residual / state.trend_reference_residual - 1.0;
    if (relative_trend <= -1.0e-4) {
      // A robust 4-sample decrease of 0.01% is enough to regrow; the old
      // per-step 0.5% threshold was too coarse at low CFL.
      state.cfl = std::min(scheduled_cfl, 1.25 * state.cfl);
    }
    state.trend_reference_residual = residual;
    state.trend_samples = 0;
  }
  state.cfl = std::clamp(state.cfl, cfl_floor, scheduled_cfl);
}

FlowSolver::FlowSolver(const DistributedMesh& mesh, const CaseConfig& config,
                       MPI_Comm communicator)
    : mesh_(mesh),
      config_(config),
      communicator_(communicator),
      residual_(mesh, config, communicator),
      cfl_(config.run_control.cfl_initial),
      steady_reconstruction_blend_(
          config.run_control.type == RunType::steady ? 0.0 : 1.0) {
  if (config_.run_control.type == RunType::transient) {
    if (!config_.run_control.time_step.has_value() ||
        !config_.run_control.final_time.has_value()) {
      throw std::invalid_argument("transient run requires time_step and final_time");
    }
    if (config_.run_control.min_inner_iterations < 5 ||
        config_.run_control.max_inner_iterations > 1000 ||
        config_.run_control.inner_residual_reduction_target > 1.0e-3) {
      throw std::invalid_argument(
          "transient controls require min>=5, max<=1000, and target<=1e-3");
    }
    if (config_.run_control.cfl_initial != config_.run_control.cfl_max ||
        config_.run_control.pseudo_cfl_ramp_steps != 0) {
      throw std::invalid_argument("transient pseudo-time CFL must be fixed");
    }
  }
}

FlowSolverContinuation FlowSolver::continuation_state() const noexcept {
  FlowSolverContinuation state;
  state.cfl = cfl_;
  state.nonlinear_steps = nonlinear_steps_;
  state.steady_previous_residual = steady_previous_residual_;
  state.steady_best_residual = steady_best_residual_;
  state.steady_trend_reference_residual = steady_trend_reference_residual_;
  state.steady_trend_samples = steady_trend_samples_;
  state.steady_probe_active = steady_probe_active_;
  state.steady_rejected_attempts = steady_rejected_attempts_;
  state.steady_recovery_probe_cfl = steady_recovery_probe_cfl_;
  state.steady_recovery_restore_pending = steady_recovery_restore_pending_;
  state.steady_jfnk_accepted_steps = steady_jfnk_accepted_steps_;
  state.steady_fallback_accepted_steps = steady_fallback_accepted_steps_;
  state.steady_fallback_attempts = steady_fallback_attempts_;
  state.steady_fallback_rejected_steps = steady_fallback_rejected_steps_;
  state.steady_fallback_cfl_halvings = steady_fallback_cfl_halvings_;
  state.steady_last_fallback_cfl = steady_last_fallback_cfl_;
  state.steady_fallback_mode = steady_fallback_mode_;
  state.steady_fallback_cfl = steady_fallback_cfl_;
  state.steady_fallback_residual_window = steady_fallback_residual_window_;
  state.steady_fallback_steps_since_jfnk =
      steady_fallback_steps_since_jfnk_;
  state.steady_jfnk_failure_streak = steady_jfnk_failure_streak_;
  state.steady_jfnk_attempts = steady_jfnk_attempts_;
  state.steady_initial_residual_scale = steady_initial_residual_scale_;
  state.steady_initial_residual_is_original_run = true;
  state.steady_jfnk_epsilon_reference_residual =
      steady_jfnk_epsilon_reference_residual_;
  state.steady_jfnk_epsilon_multiplier = steady_jfnk_epsilon_multiplier_;
  state.steady_jfnk_last_epsilon = steady_jfnk_last_epsilon_;
  state.steady_jfnk_last_epsilon_halvings =
      steady_jfnk_last_epsilon_halvings_;
  state.steady_reconstruction_blend = steady_reconstruction_blend_;
  state.steady_first_order_accepted_steps =
      steady_first_order_accepted_steps_;
  state.steady_order_ramp_accepted_steps =
      steady_order_ramp_accepted_steps_;
  state.steady_full_order_accepted_steps =
      steady_full_order_accepted_steps_;
  state.steady_full_order_initial_residual =
      steady_full_order_initial_residual_;
  state.steady_full_order_best_residual =
      steady_full_order_best_residual_;
  state.steady_order_rescue_promoted = steady_order_rescue_promoted_;
  state.steady_rescue_attempts = steady_rescue_attempts_;
  state.steady_rescue_accepted_steps = steady_rescue_accepted_steps_;
  state.steady_rescue_total_gmres_iterations =
      steady_rescue_total_gmres_iterations_;
  state.steady_rescue_last_gmres_iterations =
      steady_rescue_last_gmres_iterations_;
  state.steady_rescue_max_gmres_iterations =
      steady_rescue_max_gmres_iterations_;
  state.steady_rescue_last_gmres_ratio = steady_rescue_last_gmres_ratio_;
  state.steady_rescue_last_cfl = steady_rescue_last_cfl_;
  state.steady_rescue_last_line_scale = steady_rescue_last_line_scale_;
  state.steady_rescue_reference_residual =
      steady_rescue_reference_residual_;
  state.steady_rescue_stagnation_count =
      steady_rescue_stagnation_count_;
  state.steady_rescue_cooldown_attempts =
      steady_rescue_cooldown_attempts_;
  state.steady_trust_region_retry_batches =
      steady_trust_region_retry_batches_;
  state.steady_trust_region_retry_candidates =
      steady_trust_region_retry_candidates_;
  state.steady_trust_region_retry_accepted_steps =
      steady_trust_region_retry_accepted_steps_;
  state.steady_trust_region_retry_total_gmres_iterations =
      steady_trust_region_retry_total_gmres_iterations_;
  state.steady_trust_region_retry_last_candidate_count =
      steady_trust_region_retry_last_candidate_count_;
  state.steady_trust_region_retry_last_total_gmres_iterations =
      steady_trust_region_retry_last_total_gmres_iterations_;
  state.steady_trust_region_retry_last_accepted_gmres_iterations =
      steady_trust_region_retry_last_accepted_gmres_iterations_;
  state.steady_trust_region_retry_last_accepted_cfl =
      steady_trust_region_retry_last_accepted_cfl_;
  state.steady_trust_region_retry_last_line_scale =
      steady_trust_region_retry_last_line_scale_;
  state.steady_trust_region_retry_last_initial_residual =
      steady_trust_region_retry_last_initial_residual_;
  state.steady_trust_region_retry_last_final_residual =
      steady_trust_region_retry_last_final_residual_;
  state.steady_trust_region_retry_cooldown_attempts =
      steady_trust_region_retry_cooldown_attempts_;
  state.steady_fallback_disabled = steady_fallback_disabled_;
  state.steady_fallback_consecutive_accepted_steps =
      steady_fallback_consecutive_accepted_steps_;
  state.steady_fallback_growth_disables =
      steady_fallback_growth_disables_;
  state.steady_lusgs_preconditioner_applications =
      steady_lusgs_preconditioner_applications_;
  state.steady_lusgs_preconditioner_sweeps =
      steady_lusgs_preconditioner_sweeps_;
  state.steady_lusgs_last_defect_ratio =
      steady_lusgs_last_defect_ratio_;
  state.steady_nonmonotone_residual_window =
      steady_nonmonotone_residual_window_;
  state.steady_strict_decrease_stagnation_streak =
      steady_strict_decrease_stagnation_streak_;
  state.steady_nonmonotone_bridge_active =
      steady_nonmonotone_bridge_active_;
  state.steady_nonmonotone_bridge_disabled =
      steady_nonmonotone_bridge_disabled_;
  state.steady_nonmonotone_steps_since_strict_best =
      steady_nonmonotone_steps_since_strict_best_;
  state.steady_nonmonotone_accepted_steps =
      steady_nonmonotone_accepted_steps_;
  state.steady_nonmonotone_max_relative_increase =
      steady_nonmonotone_max_relative_increase_;
  state.steady_nonmonotone_strict_best_improvements =
      steady_nonmonotone_strict_best_improvements_;
  state.steady_nonmonotone_watchdog_resets =
      steady_nonmonotone_watchdog_resets_;
  state.steady_nonmonotone_bypass_attempts =
      steady_nonmonotone_bypass_attempts_;
  state.steady_nonmonotone_bypass_accepted_steps =
      steady_nonmonotone_bypass_accepted_steps_;
  state.steady_nonmonotone_bypass_trial_evaluations =
      steady_nonmonotone_bypass_trial_evaluations_;
  state.steady_nonmonotone_bypass_last_actual_trial_residual =
      steady_nonmonotone_bypass_last_actual_trial_residual_;
  state.steady_nonmonotone_bypass_last_gmres_ratio =
      steady_nonmonotone_bypass_last_gmres_ratio_;
  state.steady_nonmonotone_envelope_reference =
      steady_nonmonotone_envelope_reference_;
  state.steady_nonmonotone_envelope_accepted_steps =
      steady_nonmonotone_envelope_accepted_steps_;
  state.steady_nonmonotone_envelope_max_relative_increase =
      steady_nonmonotone_envelope_max_relative_increase_;
  state.steady_implicit_bridge = steady_implicit_bridge_;
  state.transient_stats = transient_stats_;
  state.transient_samples = transient_samples_;
  state.transient_iteration_sum = transient_iteration_sum_;
  state.steady_target_met = steady_target_met_;
  return state;
}

void FlowSolver::restore_continuation_state(const FlowSolverContinuation& state) {
  const auto valid_steady_residual = [](double value) {
    return std::isfinite(value) && (value >= 0.0 || value == -1.0);
  };
  const double bridge_minimum_cfl = std::max(
      steady_fallback_minimum_cfl,
      0.01 * config_.run_control.cfl_initial);
  const SteadyImplicitBridgeState& bridge = state.steady_implicit_bridge;
  const SteadySpatialOrderSchedule order_schedule =
      steady_spatial_order_schedule(config_.run_control.pseudo_cfl_ramp_steps);
  bool invalid_steady_phase = false;
  if (config_.run_control.type == RunType::steady) {
    if (state.steady_order_rescue_promoted ||
        state.steady_first_order_accepted_steps >
            order_schedule.first_order_steps ||
        state.steady_order_ramp_accepted_steps > order_schedule.ramp_steps) {
      invalid_steady_phase = true;
    } else if (state.steady_first_order_accepted_steps <
               order_schedule.first_order_steps) {
      invalid_steady_phase = state.steady_order_ramp_accepted_steps != 0U ||
          state.steady_full_order_accepted_steps != 0U ||
          state.steady_reconstruction_blend != 0.0;
    } else if (state.steady_order_ramp_accepted_steps <
               order_schedule.ramp_steps) {
      const double expected_blend = smooth_reconstruction_blend(
          state.steady_order_ramp_accepted_steps + 1U,
          order_schedule.ramp_steps + 1U);
      invalid_steady_phase = state.steady_full_order_accepted_steps != 0U ||
          state.steady_reconstruction_blend != expected_blend;
    } else {
      invalid_steady_phase = state.steady_reconstruction_blend != 1.0;
    }
  }
  const bool invalid_spatial_order_state =
      !std::isfinite(state.steady_reconstruction_blend) ||
      state.steady_reconstruction_blend < 0.0 ||
      state.steady_reconstruction_blend > 1.0 ||
      (config_.run_control.type == RunType::transient
           ? state.steady_reconstruction_blend != 1.0 ||
                 state.steady_first_order_accepted_steps != 0U ||
                 state.steady_order_ramp_accepted_steps != 0U ||
                  state.steady_full_order_accepted_steps != 0U ||
                  state.steady_full_order_initial_residual != -1.0 ||
                   state.steady_full_order_best_residual != -1.0 ||
                   state.steady_order_rescue_promoted ||
                   state.steady_trust_region_retry_batches != 0U ||
                   state.steady_target_met
            : invalid_steady_phase ||
                  state.steady_full_order_accepted_steps >
                      state.nonlinear_steps ||
                 !valid_steady_residual(
                     state.steady_full_order_initial_residual) ||
                 !valid_steady_residual(
                     state.steady_full_order_best_residual) ||
                 ((state.steady_full_order_initial_residual < 0.0) !=
                  (state.steady_full_order_best_residual < 0.0)) ||
                 ((state.steady_full_order_accepted_steps == 0U) !=
                  (state.steady_full_order_initial_residual < 0.0)) ||
                   (state.steady_full_order_accepted_steps > 0U &&
                    (state.steady_reconstruction_blend != 1.0 ||
                     state.steady_order_ramp_accepted_steps !=
                         order_schedule.ramp_steps)) ||
                 (state.steady_full_order_best_residual >= 0.0 &&
                  state.steady_full_order_best_residual >
                      state.steady_full_order_initial_residual) ||
                  (state.steady_target_met &&
                   state.steady_initial_residual_is_original_run &&
                   (!config_.run_control.residual_reduction_target.has_value() ||
                    !steady_convergence_gate(
                        state.steady_reconstruction_blend,
                        state.steady_full_order_accepted_steps,
                        config_.run_control.pseudo_cfl_ramp_steps,
                        state.steady_initial_residual_scale,
                        state.steady_previous_residual,
                        *config_.run_control.residual_reduction_target))));
  if (!(state.cfl > 0.0) || !std::isfinite(state.cfl) ||
      !valid_steady_residual(state.steady_previous_residual) ||
      !valid_steady_residual(state.steady_best_residual) ||
      !valid_steady_residual(state.steady_trend_reference_residual) ||
      state.steady_trend_samples < 0 || state.steady_trend_samples > 3 ||
      (state.steady_trend_reference_residual < 0.0 &&
       state.steady_trend_samples != 0) ||
      state.steady_rejected_attempts < 0 ||
      !(state.steady_recovery_probe_cfl >= 0.0) ||
      !std::isfinite(state.steady_recovery_probe_cfl) ||
      state.steady_probe_active ||
      state.steady_recovery_restore_pending ||
      state.steady_recovery_probe_cfl != 0.0 ||
      ((state.steady_previous_residual < 0.0) !=
       (state.steady_best_residual < 0.0)) ||
      (state.steady_best_residual >= 0.0 &&
       state.steady_best_residual > state.steady_previous_residual) ||
      state.steady_fallback_accepted_steps >
          state.steady_fallback_attempts ||
      state.steady_fallback_rejected_steps >
          state.steady_fallback_attempts ||
      state.steady_fallback_rejected_steps !=
          state.steady_fallback_attempts -
              state.steady_fallback_accepted_steps ||
      state.steady_jfnk_accepted_steps > state.nonlinear_steps ||
      state.steady_fallback_accepted_steps >
          state.nonlinear_steps - state.steady_jfnk_accepted_steps ||
       state.steady_jfnk_accepted_steps > state.steady_jfnk_attempts ||
       state.steady_rescue_accepted_steps > state.steady_rescue_attempts ||
       state.steady_rescue_accepted_steps > state.nonlinear_steps ||
       state.steady_trust_region_retry_accepted_steps >
           state.nonlinear_steps ||
       state.steady_jfnk_accepted_steps +
               state.steady_fallback_accepted_steps +
               state.steady_rescue_accepted_steps +
               state.steady_trust_region_retry_accepted_steps >
           state.nonlinear_steps ||
       state.steady_rescue_last_gmres_iterations < 0 ||
       state.steady_rescue_last_gmres_iterations > 50 ||
       state.steady_rescue_max_gmres_iterations <
           state.steady_rescue_last_gmres_iterations ||
       state.steady_rescue_max_gmres_iterations > 50 ||
       !std::isfinite(state.steady_rescue_last_gmres_ratio) ||
       state.steady_rescue_last_gmres_ratio < 0.0 ||
       !std::isfinite(state.steady_rescue_last_cfl) ||
       state.steady_rescue_last_cfl < 0.0 ||
       state.steady_rescue_last_cfl > config_.run_control.cfl_max ||
       !std::isfinite(state.steady_rescue_last_line_scale) ||
       state.steady_rescue_last_line_scale < 0.0 ||
       state.steady_rescue_last_line_scale > 1.0 ||
       (state.steady_rescue_attempts == 0U &&
        (state.steady_rescue_last_gmres_iterations != 0 ||
         state.steady_rescue_max_gmres_iterations != 0 ||
         state.steady_rescue_total_gmres_iterations != 0U ||
         state.steady_rescue_last_cfl != 0.0 ||
         state.steady_rescue_last_line_scale != 0.0)) ||
       (state.steady_rescue_attempts > 0U &&
        !(state.steady_rescue_last_cfl > 0.0)) ||
       !valid_steady_residual(state.steady_rescue_reference_residual) ||
       (state.steady_rescue_reference_residual < 0.0 &&
        state.steady_rescue_stagnation_count != 0U) ||
        state.steady_rescue_cooldown_attempts >
            steady_rescue_retry_interval ||
        state.steady_trust_region_retry_accepted_steps >
            state.steady_trust_region_retry_batches ||
        state.steady_trust_region_retry_candidates <
            state.steady_trust_region_retry_batches ||
        state.steady_trust_region_retry_last_candidate_count >
            state.steady_trust_region_retry_candidates ||
        state.steady_trust_region_retry_last_total_gmres_iterations < 0 ||
        state.steady_trust_region_retry_last_accepted_gmres_iterations < 0 ||
        static_cast<std::size_t>(
            state.steady_trust_region_retry_last_total_gmres_iterations) >
            state.steady_trust_region_retry_total_gmres_iterations ||
        state.steady_trust_region_retry_last_accepted_gmres_iterations > 50 ||
        !std::isfinite(state.steady_trust_region_retry_last_accepted_cfl) ||
        state.steady_trust_region_retry_last_accepted_cfl < 0.0 ||
        state.steady_trust_region_retry_last_accepted_cfl >=
            config_.run_control.cfl_max ||
        !std::isfinite(state.steady_trust_region_retry_last_line_scale) ||
        state.steady_trust_region_retry_last_line_scale < 0.0 ||
        state.steady_trust_region_retry_last_line_scale > 1.0 ||
        !valid_steady_residual(
            state.steady_trust_region_retry_last_initial_residual) ||
        !valid_steady_residual(
            state.steady_trust_region_retry_last_final_residual) ||
        state.steady_trust_region_retry_cooldown_attempts >
            steady_trust_region_retry_interval ||
        (state.steady_trust_region_retry_batches == 0U &&
         (state.steady_trust_region_retry_candidates != 0U ||
          state.steady_trust_region_retry_accepted_steps != 0U ||
          state.steady_trust_region_retry_total_gmres_iterations != 0U ||
          state.steady_trust_region_retry_last_candidate_count != 0U ||
          state.steady_trust_region_retry_last_total_gmres_iterations != 0 ||
          state.steady_trust_region_retry_last_accepted_gmres_iterations != 0 ||
          state.steady_trust_region_retry_last_accepted_cfl != 0.0 ||
          state.steady_trust_region_retry_last_line_scale != 0.0 ||
          state.steady_trust_region_retry_last_initial_residual != -1.0 ||
          state.steady_trust_region_retry_last_final_residual != -1.0 ||
          state.steady_trust_region_retry_cooldown_attempts != 0U)) ||
        (state.steady_trust_region_retry_batches > 0U &&
         state.steady_trust_region_retry_last_candidate_count == 0U) ||
        (state.steady_trust_region_retry_accepted_steps == 0U &&
         (state.steady_trust_region_retry_last_accepted_cfl != 0.0 ||
          state.steady_trust_region_retry_last_line_scale != 0.0 ||
          state.steady_trust_region_retry_last_accepted_gmres_iterations != 0 ||
          state.steady_trust_region_retry_last_initial_residual != -1.0 ||
          state.steady_trust_region_retry_last_final_residual != -1.0)) ||
        (state.steady_trust_region_retry_accepted_steps > 0U &&
         state.steady_trust_region_retry_last_accepted_cfl > 0.0 &&
         (state.steady_trust_region_retry_last_final_residual >=
              state.steady_trust_region_retry_last_initial_residual ||
          state.steady_trust_region_retry_last_initial_residual < 0.0 ||
          state.steady_trust_region_retry_last_line_scale <= 0.0 ||
          state.steady_trust_region_retry_last_accepted_gmres_iterations <= 0)) ||
        // Early CFDRST9 diagnostics overwrote successful-candidate details with
        // a later failed batch (all-zero accepted fields and equal residuals).
        // Keep those checkpoints readable while all new writes preserve the
        // most recent successful candidate below.
        (state.steady_trust_region_retry_accepted_steps > 0U &&
         state.steady_trust_region_retry_last_accepted_cfl == 0.0 &&
         (state.steady_trust_region_retry_last_line_scale != 0.0 ||
          state.steady_trust_region_retry_last_accepted_gmres_iterations != 0 ||
          state.steady_trust_region_retry_last_initial_residual < 0.0 ||
          state.steady_trust_region_retry_last_final_residual !=
              state.steady_trust_region_retry_last_initial_residual)) ||
        state.steady_fallback_consecutive_accepted_steps >
           steady_fallback_maximum_bridge_steps ||
       state.steady_fallback_consecutive_accepted_steps >
           state.steady_fallback_accepted_steps ||
       (state.steady_fallback_disabled && state.steady_fallback_mode) ||
      !(state.steady_last_fallback_cfl >= 0.0) ||
      !std::isfinite(state.steady_last_fallback_cfl) ||
      state.steady_last_fallback_cfl > 0.1 ||
      (state.steady_fallback_attempts == 0U &&
       state.steady_last_fallback_cfl != 0.0) ||
      (state.steady_fallback_attempts > 0U &&
       !(state.steady_last_fallback_cfl > 0.0)) ||
      state.steady_fallback_cfl < steady_fallback_minimum_cfl ||
      !std::isfinite(state.steady_fallback_cfl) ||
      state.steady_fallback_cfl > 0.1 ||
      state.steady_fallback_residual_window.size() >
          steady_fallback_window_capacity ||
      !std::all_of(state.steady_fallback_residual_window.begin(),
                   state.steady_fallback_residual_window.end(),
                   [](double residual) {
                     return residual >= 0.0 && std::isfinite(residual);
                   }) ||
      state.steady_fallback_steps_since_jfnk >
          steady_fallback_jfnk_retry_interval ||
      state.steady_jfnk_failure_streak < 0 ||
       state.steady_jfnk_failure_streak > 2 ||
       state.steady_lusgs_preconditioner_sweeps <
           3U * state.steady_lusgs_preconditioner_applications ||
       !std::isfinite(state.steady_lusgs_last_defect_ratio) ||
       state.steady_lusgs_last_defect_ratio < 0.0 ||
      (state.steady_fallback_mode &&
       (state.steady_jfnk_failure_streak < 2 ||
        state.steady_fallback_residual_window.empty())) ||
       !valid_steady_residual(state.steady_initial_residual_scale) ||
       !valid_steady_residual(
           state.steady_jfnk_epsilon_reference_residual) ||
       !std::isfinite(state.steady_jfnk_epsilon_multiplier) ||
       state.steady_jfnk_epsilon_multiplier <
           steady_jfnk_minimum_epsilon_multiplier ||
       state.steady_jfnk_epsilon_multiplier > 1.0 ||
       !std::isfinite(state.steady_jfnk_last_epsilon) ||
       state.steady_jfnk_last_epsilon < 0.0 ||
       state.steady_jfnk_last_epsilon_halvings < 0 ||
       state.steady_jfnk_last_epsilon_halvings > 30 ||
       (state.steady_jfnk_epsilon_reference_residual < 0.0 &&
        (state.steady_jfnk_epsilon_multiplier != 1.0 ||
          state.steady_jfnk_last_epsilon != 0.0 ||
          state.steady_jfnk_last_epsilon_halvings != 0)) ||
        state.steady_nonmonotone_residual_window.size() >
            steady_nonmonotone_window_capacity ||
        !std::all_of(state.steady_nonmonotone_residual_window.begin(),
                     state.steady_nonmonotone_residual_window.end(),
                     [](double residual) {
                       return residual >= 0.0 && std::isfinite(residual);
                     }) ||
        state.steady_strict_decrease_stagnation_streak >
            steady_nonmonotone_stagnation_attempts ||
        (state.steady_nonmonotone_bridge_active &&
         state.steady_nonmonotone_bridge_disabled) ||
        state.steady_nonmonotone_steps_since_strict_best >=
            steady_nonmonotone_watchdog_steps ||
        state.steady_nonmonotone_accepted_steps > state.nonlinear_steps ||
        !std::isfinite(state.steady_nonmonotone_max_relative_increase) ||
        state.steady_nonmonotone_max_relative_increase < 0.0 ||
         state.steady_nonmonotone_max_relative_increase >
             steady_nonmonotone_best_residual_cap - 1.0 + 1.0e-12 ||
         state.steady_nonmonotone_bypass_accepted_steps >
             state.steady_nonmonotone_bypass_attempts ||
         state.steady_nonmonotone_bypass_accepted_steps >
             state.steady_nonmonotone_accepted_steps ||
         !valid_steady_residual(
             state.steady_nonmonotone_bypass_last_actual_trial_residual) ||
         !std::isfinite(
             state.steady_nonmonotone_bypass_last_gmres_ratio) ||
         state.steady_nonmonotone_bypass_last_gmres_ratio < 0.0 ||
         (state.steady_nonmonotone_bypass_attempts == 0U &&
          (state.steady_nonmonotone_bypass_accepted_steps != 0U ||
           state.steady_nonmonotone_bypass_trial_evaluations != 0U ||
           state.steady_nonmonotone_bypass_last_actual_trial_residual != -1.0 ||
           state.steady_nonmonotone_bypass_last_gmres_ratio != 1.0)) ||
         !valid_steady_residual(
             state.steady_nonmonotone_envelope_reference) ||
         state.steady_nonmonotone_envelope_accepted_steps >
             state.steady_nonmonotone_accepted_steps ||
         !std::isfinite(
             state.steady_nonmonotone_envelope_max_relative_increase) ||
         state.steady_nonmonotone_envelope_max_relative_increase < 0.0 ||
         state.steady_nonmonotone_envelope_max_relative_increase >
             steady_nonmonotone_activation_envelope_fraction + 1.0e-12 ||
         (state.steady_nonmonotone_envelope_accepted_steps == 0U &&
          state.steady_nonmonotone_envelope_max_relative_increase != 0.0) ||
         (state.steady_nonmonotone_envelope_reference >= 0.0 &&
          (state.steady_best_residual < 0.0 ||
           state.steady_nonmonotone_envelope_reference <
               state.steady_best_residual ||
           state.steady_nonmonotone_envelope_reference >
               steady_nonmonotone_activation_reference(
                   state.steady_best_residual) *
                   (1.0 + 64.0 * std::numeric_limits<double>::epsilon()) ||
           state.steady_nonmonotone_bridge_disabled ||
           state.steady_reconstruction_blend != 1.0)) ||
         (bridge.active && bridge.disabled) ||
         !std::isfinite(bridge.cfl) ||
         (bridge.attempts > 0U &&
          (bridge.cfl < bridge_minimum_cfl ||
           bridge.cfl > config_.run_control.cfl_max)) ||
         !valid_steady_residual(bridge.entry_best_residual) ||
         bridge.accepted_steps > bridge.attempts ||
         bridge.rejected_steps != bridge.attempts - bridge.accepted_steps ||
         bridge.accepted_steps_since_best >
             steady_implicit_bridge_watchdog_steps ||
         bridge.meaningful_best_improvements > bridge.accepted_steps ||
         !valid_steady_residual(bridge.residual_minimum) ||
         !valid_steady_residual(bridge.residual_maximum) ||
         !std::isfinite(bridge.maximum_relative_growth) ||
         bridge.maximum_relative_growth < 0.0 ||
         bridge.linear_sweeps < 3U * bridge.attempts ||
         (bridge.accepted_steps == 0U &&
          (bridge.residual_minimum != -1.0 ||
           bridge.residual_maximum != -1.0 ||
           bridge.maximum_relative_growth != 0.0 ||
           bridge.meaningful_best_improvements != 0U)) ||
         (bridge.accepted_steps > 0U &&
          (bridge.residual_minimum < 0.0 ||
           bridge.residual_maximum < bridge.residual_minimum ||
           bridge.entry_best_residual < 0.0)) ||
         (bridge.active &&
          (bridge.entry_best_residual < 0.0 ||
           config_.run_control.type != RunType::steady)) ||
         (bridge.disabled && bridge.active) ||
        (state.steady_nonmonotone_bridge_active &&
         (state.steady_reconstruction_blend != 1.0 ||
          state.steady_nonmonotone_residual_window.empty())) ||
        (config_.run_control.type == RunType::transient &&
         (!state.steady_nonmonotone_residual_window.empty() ||
          state.steady_strict_decrease_stagnation_streak != 0U ||
          state.steady_nonmonotone_bridge_active ||
          state.steady_nonmonotone_bridge_disabled ||
          state.steady_nonmonotone_steps_since_strict_best != 0U ||
          state.steady_nonmonotone_accepted_steps != 0U ||
          state.steady_nonmonotone_max_relative_increase != 0.0 ||
          state.steady_nonmonotone_strict_best_improvements != 0U ||
          state.steady_nonmonotone_watchdog_resets != 0U)) ||
         invalid_spatial_order_state ||
      !std::isfinite(state.transient_iteration_sum) ||
      !std::isfinite(state.transient_stats.observed_mean) ||
      !std::isfinite(state.transient_stats.target_met_fraction) ||
      !std::isfinite(state.transient_stats.last_ratio) ||
      state.transient_stats.target_misses > state.transient_samples) {
    throw std::invalid_argument("invalid FlowSolver continuation state");
  }
  cfl_ = state.cfl;
  nonlinear_steps_ = state.nonlinear_steps;
  transient_stats_ = state.transient_stats;
  transient_samples_ = state.transient_samples;
  transient_iteration_sum_ = state.transient_iteration_sum;
  steady_target_met_ = false;
  steady_previous_residual_ = state.steady_previous_residual;
  steady_best_residual_ = state.steady_best_residual;
  steady_trend_reference_residual_ = state.steady_trend_reference_residual;
  steady_trend_samples_ = state.steady_trend_samples;
  steady_probe_active_ = state.steady_probe_active;
  steady_rejected_attempts_ = state.steady_rejected_attempts;
  steady_recovery_probe_cfl_ = state.steady_recovery_probe_cfl;
  steady_recovery_restore_pending_ = state.steady_recovery_restore_pending;
  steady_jfnk_accepted_steps_ = state.steady_jfnk_accepted_steps;
  steady_fallback_accepted_steps_ = state.steady_fallback_accepted_steps;
  steady_fallback_attempts_ = state.steady_fallback_attempts;
  steady_fallback_rejected_steps_ = state.steady_fallback_rejected_steps;
  steady_fallback_cfl_halvings_ = state.steady_fallback_cfl_halvings;
  steady_last_fallback_cfl_ = state.steady_last_fallback_cfl;
  steady_fallback_mode_ = state.steady_fallback_mode;
  steady_fallback_cfl_ = state.steady_fallback_cfl;
  steady_fallback_residual_window_ =
      state.steady_fallback_residual_window;
  steady_fallback_steps_since_jfnk_ =
      state.steady_fallback_steps_since_jfnk;
  steady_jfnk_failure_streak_ = state.steady_jfnk_failure_streak;
  steady_jfnk_attempts_ = state.steady_jfnk_attempts;
  steady_initial_residual_scale_ = state.steady_initial_residual_scale;
  if (config_.run_control.type == RunType::steady &&
      !state.steady_initial_residual_is_original_run &&
      state.nonlinear_steps > 0U) {
    RestartableSolution original = uniform_initial_solution();
    ResidualEvaluationOptions options;
    options.reconstruction_blend = 0.0;
    const ResidualResult initial = residual_.evaluate(
        original.U, config_.run_control.cfl_initial, options);
    steady_initial_residual_scale_ = initial.norms.total_l2;
  }
  steady_jfnk_epsilon_reference_residual_ =
      state.steady_jfnk_epsilon_reference_residual;
  steady_jfnk_epsilon_multiplier_ =
      state.steady_jfnk_epsilon_multiplier;
  steady_jfnk_last_epsilon_ = state.steady_jfnk_last_epsilon;
  steady_jfnk_last_epsilon_halvings_ =
      state.steady_jfnk_last_epsilon_halvings;
  steady_reconstruction_blend_ = state.steady_reconstruction_blend;
  steady_first_order_accepted_steps_ =
      state.steady_first_order_accepted_steps;
  steady_order_ramp_accepted_steps_ =
      state.steady_order_ramp_accepted_steps;
  steady_full_order_accepted_steps_ =
      state.steady_full_order_accepted_steps;
  steady_full_order_initial_residual_ =
      state.steady_full_order_initial_residual;
  steady_full_order_best_residual_ =
      state.steady_full_order_best_residual;
  steady_order_rescue_promoted_ = state.steady_order_rescue_promoted;
  steady_rescue_attempts_ = state.steady_rescue_attempts;
  steady_rescue_accepted_steps_ = state.steady_rescue_accepted_steps;
  steady_rescue_total_gmres_iterations_ =
      state.steady_rescue_total_gmres_iterations;
  steady_rescue_last_gmres_iterations_ =
      state.steady_rescue_last_gmres_iterations;
  steady_rescue_max_gmres_iterations_ =
      state.steady_rescue_max_gmres_iterations;
  steady_rescue_last_gmres_ratio_ = state.steady_rescue_last_gmres_ratio;
  steady_rescue_last_cfl_ = state.steady_rescue_last_cfl;
  steady_rescue_last_line_scale_ = state.steady_rescue_last_line_scale;
  steady_rescue_reference_residual_ =
      state.steady_rescue_reference_residual;
  steady_rescue_stagnation_count_ = state.steady_rescue_stagnation_count;
  steady_rescue_cooldown_attempts_ =
      state.steady_rescue_cooldown_attempts;
  steady_trust_region_retry_batches_ =
      state.steady_trust_region_retry_batches;
  steady_trust_region_retry_candidates_ =
      state.steady_trust_region_retry_candidates;
  steady_trust_region_retry_accepted_steps_ =
      state.steady_trust_region_retry_accepted_steps;
  steady_trust_region_retry_total_gmres_iterations_ =
      state.steady_trust_region_retry_total_gmres_iterations;
  steady_trust_region_retry_last_candidate_count_ =
      state.steady_trust_region_retry_last_candidate_count;
  steady_trust_region_retry_last_total_gmres_iterations_ =
      state.steady_trust_region_retry_last_total_gmres_iterations;
  steady_trust_region_retry_last_accepted_gmres_iterations_ =
      state.steady_trust_region_retry_last_accepted_gmres_iterations;
  steady_trust_region_retry_last_accepted_cfl_ =
      state.steady_trust_region_retry_last_accepted_cfl;
  steady_trust_region_retry_last_line_scale_ =
      state.steady_trust_region_retry_last_line_scale;
  steady_trust_region_retry_last_initial_residual_ =
      state.steady_trust_region_retry_last_initial_residual;
  steady_trust_region_retry_last_final_residual_ =
      state.steady_trust_region_retry_last_final_residual;
  steady_trust_region_retry_cooldown_attempts_ =
      state.steady_trust_region_retry_cooldown_attempts;
  steady_fallback_disabled_ = state.steady_fallback_disabled;
  steady_fallback_consecutive_accepted_steps_ =
      state.steady_fallback_consecutive_accepted_steps;
  steady_fallback_growth_disables_ =
      state.steady_fallback_growth_disables;
  steady_lusgs_preconditioner_applications_ =
      state.steady_lusgs_preconditioner_applications;
  steady_lusgs_preconditioner_sweeps_ =
      state.steady_lusgs_preconditioner_sweeps;
  steady_lusgs_last_defect_ratio_ =
      state.steady_lusgs_last_defect_ratio;
  steady_nonmonotone_residual_window_ =
      state.steady_nonmonotone_residual_window;
  steady_strict_decrease_stagnation_streak_ =
      state.steady_strict_decrease_stagnation_streak;
  steady_nonmonotone_bridge_active_ =
      state.steady_nonmonotone_bridge_active;
  steady_nonmonotone_bridge_disabled_ =
      state.steady_nonmonotone_bridge_disabled;
  steady_nonmonotone_steps_since_strict_best_ =
      state.steady_nonmonotone_steps_since_strict_best;
  steady_nonmonotone_accepted_steps_ =
      state.steady_nonmonotone_accepted_steps;
  steady_nonmonotone_max_relative_increase_ =
      state.steady_nonmonotone_max_relative_increase;
  steady_nonmonotone_strict_best_improvements_ =
      state.steady_nonmonotone_strict_best_improvements;
  steady_nonmonotone_watchdog_resets_ =
      state.steady_nonmonotone_watchdog_resets;
  steady_nonmonotone_bypass_attempts_ =
      state.steady_nonmonotone_bypass_attempts;
  steady_nonmonotone_bypass_accepted_steps_ =
      state.steady_nonmonotone_bypass_accepted_steps;
  steady_nonmonotone_bypass_trial_evaluations_ =
      state.steady_nonmonotone_bypass_trial_evaluations;
  steady_nonmonotone_bypass_last_actual_trial_residual_ =
      state.steady_nonmonotone_bypass_last_actual_trial_residual;
  steady_nonmonotone_bypass_last_gmres_ratio_ =
      state.steady_nonmonotone_bypass_last_gmres_ratio;
  steady_nonmonotone_envelope_reference_ =
      state.steady_nonmonotone_envelope_reference;
  steady_nonmonotone_envelope_accepted_steps_ =
      state.steady_nonmonotone_envelope_accepted_steps;
  steady_nonmonotone_envelope_max_relative_increase_ =
      state.steady_nonmonotone_envelope_max_relative_increase;
  steady_implicit_bridge_ = state.steady_implicit_bridge;
  if (steady_implicit_bridge_.attempts == 0U) {
    steady_implicit_bridge_.cfl = steady_implicit_bridge_bounded_initial_cfl(
        bridge_minimum_cfl, config_.run_control.cfl_max);
  }
  if (config_.run_control.type == RunType::steady &&
      config_.run_control.residual_reduction_target.has_value()) {
    steady_target_met_ = steady_convergence_gate(
        steady_reconstruction_blend_, steady_full_order_accepted_steps_,
        config_.run_control.pseudo_cfl_ramp_steps,
        steady_initial_residual_scale_, steady_previous_residual_,
        *config_.run_control.residual_reduction_target);
  }
}

RestartableSolution FlowSolver::uniform_initial_solution() const {
  RestartableSolution result;
  result.U.resize(mesh_.cells.size() * 4U);
  const Conservative freestream = residual_.freestream();
  for (std::size_t cell = 0; cell < mesh_.cells.size(); ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) result.U[cell * 4U + k] = freestream[k];
  }
  result.U_n = result.U;
  result.U_nm1 = result.U;
  result.U_best = result.U;
  return result;
}

void FlowSolver::validate_solution(const RestartableSolution& solution) const {
  const std::size_t expected = mesh_.cells.size() * 4U;
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(communicator_, &rank);
  MPI_Comm_size(communicator_, &size);
  int local_code = 0;
  if (solution.U.size() != expected || solution.U_n.size() != expected ||
      solution.U_nm1.size() != expected || solution.U_best.size() != expected) {
    local_code = 1;
  } else {
    const std::array<const std::vector<double>*, 4> states{
        &solution.U, &solution.U_n, &solution.U_nm1, &solution.U_best};
    for (const std::vector<double>* state : states) {
      for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
        const Conservative value{(*state)[cell * 4U], (*state)[cell * 4U + 1U],
                                 (*state)[cell * 4U + 2U], (*state)[cell * 4U + 3U]};
        if (!residual_.gas().admissible(value)) {
          local_code = 2;
          break;
        }
      }
      if (local_code != 0) break;
    }
  }
  const int candidate_rank = local_code == 0 ? size : rank;
  int failure_rank = size;
  MPI_Allreduce(&candidate_rank, &failure_rank, 1, MPI_INT, MPI_MIN, communicator_);
  if (failure_rank == size) return;
  int failure_code = rank == failure_rank ? local_code : 0;
  MPI_Bcast(&failure_code, 1, MPI_INT, failure_rank, communicator_);
  throw std::invalid_argument(
      "collective solution preflight failed on rank " + std::to_string(failure_rank) +
      (failure_code == 1 ? ": restart vector dimension mismatch"
                         : ": inadmissible owned restart state"));
}

StepResult FlowSolver::implicit_step(RestartableSolution& solution,
                                     bool physical_time,
                                     bool attempt_steady_jfnk,
                                     bool attempt_trust_region_retry,
                                     bool attempt_newton_rescue,
                                     bool allow_steady_fallback,
                                     bool allow_steady_nonmonotone,
                                     bool nonmonotone_envelope_seeded,
                                     bool allow_steady_implicit_bridge) {
  validate_solution(solution);
  rollback_u_workspace_ = solution.U;
  const std::size_t entry_step = solution.physical_step;
  const double entry_time = solution.time;
  const double dt = physical_time ? *config_.run_control.time_step : 0.0;
  const double active_reconstruction_blend =
      physical_time ? 1.0 : steady_reconstruction_blend_;
  const bool bdf2 = physical_time && solution.physical_step > 0U;
  const double physical_diagonal = physical_time ? (bdf2 ? 1.5 / dt : 1.0 / dt) : 0.0;
  const int nonlinear_max = physical_time ? config_.run_control.max_inner_iterations : 1;
  const int nonlinear_min = physical_time ? config_.run_control.min_inner_iterations : 1;
  const std::vector<double>& frozen_un = solution.U_n;
  const std::vector<double>& frozen_unm1 = solution.U_nm1;
  double initial_nonlinear_norm = -1.0;
  double last_nonlinear_norm = 0.0;
  double last_nonlinear_ratio = 1.0;
  int nonlinear_iterations = 0;
  int total_linear_sweeps = 0;
  InnerSolveStats latest_linear{};
  ResidualResult& current = current_residual_workspace_;
  ResidualResult& trial_residual = trial_residual_workspace_;
  double accepted_scale = 0.0;
  bool update_accepted = false;
  bool target_met = false;
  bool accepted_residual_increase = false;
  SteadyAcceptanceMode steady_acceptance = SteadyAcceptanceMode::none;
  bool fallback_attempted = false;
  double fallback_cfl_used = 0.0;
  int fallback_sweeps = 0;
  int fallback_cfl_halvings = 0;
  int jfnk_iterations = 0;
  bool rescue_attempted = false;
  bool rescue_accepted = false;
  double rescue_cfl = 0.0;
  int rescue_gmres_iterations = 0;
  double rescue_gmres_ratio = 1.0;
  double rescue_initial_residual = 0.0;
  double rescue_final_residual = 0.0;
  bool trust_region_retry_attempted = false;
  bool trust_region_retry_accepted = false;
  int trust_region_retry_candidates = 0;
  int trust_region_retry_total_gmres_iterations = 0;
  int trust_region_retry_accepted_gmres_iterations = 0;
  double trust_region_retry_accepted_cfl = 0.0;
  double trust_region_retry_initial_residual = 0.0;
  double trust_region_retry_final_residual = 0.0;
  double jfnk_epsilon_reference_residual = -1.0;
  double jfnk_epsilon_multiplier = 1.0;
  double jfnk_last_epsilon = 0.0;
  int jfnk_last_epsilon_halvings = 0;
  std::vector<ImplicitTrustRegionCandidateDiagnostics>
      trust_region_retry_diagnostics;
  int lusgs_preconditioner_applications = 0;
  int lusgs_preconditioner_sweeps = 0;
  double lusgs_last_defect_ratio = 1.0;
  bool limiter_active = false;
  bool nonmonotone_bridge_attempted = false;
  bool nonmonotone_bridge_accepted = false;
  bool nonmonotone_direction_descent = false;
  bool nonmonotone_descent_bypass_attempted = false;
  bool nonmonotone_descent_bypass_accepted = false;
  int nonmonotone_trial_evaluations = 0;
  double nonmonotone_best_trial_residual =
      std::numeric_limits<double>::infinity();
  double nonmonotone_reference_residual = 0.0;
  double nonmonotone_relative_increase = 0.0;
  bool nonmonotone_envelope_accepted = false;
  double nonmonotone_envelope_relative_increase = 0.0;
  bool implicit_bridge_attempted = false;
  bool implicit_bridge_accepted = false;
  double implicit_bridge_cfl = 0.0;
  double implicit_bridge_line_scale = 0.0;
  double implicit_bridge_initial_residual = -1.0;
  double implicit_bridge_final_residual = -1.0;
  double implicit_bridge_relative_growth = 0.0;
  int implicit_bridge_linear_sweeps = 0;
  const bool jfnk_attempted = physical_time || attempt_steady_jfnk;
  bool cached_trial = false;
  double cached_trial_norm = 0.0;
  ResidualEvaluationOptions fast_options;
  fast_options.reconstruction_blend = active_reconstruction_blend;
  fast_options.collective_preflight = false;  // validate_solution/line search already did it.
  fast_options.compute_global_norms = false;
  fast_options.compute_global_diagnostics = false;
  fast_options.collect_surface = true;

  auto add_physical_residual = [&](std::vector<double>& value,
                                   const std::vector<double>& state) {
    if (!physical_time) return;
    for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
      Conservative Ustar{};
      Conservative Un{};
      Conservative Unm1{};
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        Ustar[k] = state[index];
        Un[k] = frozen_un[index];
        Unm1[k] = frozen_unm1[index];
      }
      const Conservative physical = bdf_physical_residual(
          Ustar, Un, Unm1, mesh_.cells[cell].area, dt, bdf2 ? 2 : 1);
      for (std::size_t k = 0; k < 4U; ++k) {
        value[cell * 4U + k] += physical[k];
      }
    }
  };

  std::vector<double>& nonlinear_residual = nonlinear_residual_workspace_;
  std::vector<double>& rhs = rhs_workspace_;
  std::vector<double>& trial_total = trial_total_workspace_;
  std::vector<double>& cached_total = cached_total_workspace_;
  std::vector<double>& original = original_u_workspace_;
  while (nonlinear_iterations < nonlinear_max) {
    double nonlinear_norm = 0.0;
    if (cached_trial) {
      nonlinear_residual.swap(cached_total);
      nonlinear_norm = cached_trial_norm;
      cached_trial = false;
    } else {
      ResidualEvaluationOptions base_options = fast_options;
      base_options.compute_global_diagnostics =
          !physical_time && active_reconstruction_blend == 1.0;
      residual_.evaluate_into(solution.U, cfl_, current, base_options);
      nonlinear_residual = current.value;
      add_physical_residual(nonlinear_residual, solution.U);
      nonlinear_norm = vector_norm(mesh_, nonlinear_residual, communicator_);
      if (base_options.compute_global_diagnostics) {
        limiter_active =
            steady_limiter_nonlinearity_active(current.diagnostics);
      }
    }
    if (initial_nonlinear_norm < 0.0) initial_nonlinear_norm = nonlinear_norm;
    const double nonlinear_ratio = initial_nonlinear_norm == 0.0
                                       ? 0.0
                                       : nonlinear_norm / initial_nonlinear_norm;
    last_nonlinear_norm = nonlinear_norm;
    last_nonlinear_ratio = nonlinear_ratio;
    if (!physical_time && steady_initial_residual_scale_ < 0.0) {
      steady_initial_residual_scale_ = nonlinear_norm;
    }
    const double active_epsilon_reference = steady_initial_residual_scale_;
    const double active_epsilon_multiplier =
        physical_time
            ? 1.0
            : steady_jfnk_epsilon_multiplier(nonlinear_norm,
                                             active_epsilon_reference);
    if (physical_time &&
        nonlinear_ratio <= config_.run_control.inner_residual_reduction_target) {
      if (nonlinear_iterations >= nonlinear_min) {
        target_met = true;
        break;
      }
      // A Newton update may reach the target before the configured minimum.
      // Count deterministic residual-verification passes rather than forcing a
      // meaningless near-zero correction that positivity/change guards reject.
      ++nonlinear_iterations;
      if (nonlinear_iterations >= nonlinear_min) {
        target_met = true;
        break;
      }
      continue;
    }
    if (!physical_time && nonlinear_norm == 0.0) {
      latest_linear.linear_solver = steady_linear_solver_name;
      latest_linear.converged = true;
      latest_linear.defect_ratio = 0.0;
      steady_acceptance = SteadyAcceptanceMode::jfnk;
      update_accepted = true;
      accepted_scale = 0.0;
      ++nonlinear_iterations;
      break;
    }

    rhs.assign(mesh_.cells.size() * 4U, 0.0);
    for (std::size_t i = 0; i < nonlinear_residual.size(); ++i) {
      rhs[i] = -nonlinear_residual[i];
    }
    std::unique_ptr<SteadyMatrixFreeOperator> steady_matrix_free;
    std::unique_ptr<FrozenRusanovLUSGSOperator> steady_lusgs;
    if (physical_time) {
      // The preconditioner constructs its own frozen first-order face blocks.
      // With no pseudo-time diagonal it needs only the current residual's
      // correctly sized spectral storage, avoiding a redundant reconstruction.
      FrozenRusanovLUSGSOperator transient_lusgs(
          mesh_, config_, solution.U, current, cfl_, communicator_,
          physical_diagonal, 0.0);
      TransientMatrixFreeOperator transient_matrix_free(
          mesh_, residual_, solution.U, current, frozen_un, frozen_unm1, dt,
          bdf2 ? 2 : 1, cfl_, communicator_);
      const DistributedLinearAction apply_operator =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            transient_matrix_free.apply(input, output);
          };
      const DistributedLinearAction apply_preconditioner =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            const InnerSolveStats preconditioner =
                transient_lusgs.solve(input, 3, 3, 0.0, output);
            ++lusgs_preconditioner_applications;
            lusgs_preconditioner_sweeps += preconditioner.linear_sweeps;
            lusgs_last_defect_ratio = preconditioner.defect_ratio;
          };
      MatrixFreeGmresOptions gmres_options;
      gmres_options.restart = 12;
      gmres_options.minimum_iterations = 3;
      gmres_options.maximum_iterations = 20;
      gmres_options.relative_tolerance = 0.1;
      const MatrixFreeGmresResult gmres = restarted_gmres(
          mesh_, rhs, apply_operator, apply_preconditioner, gmres_options,
          communicator_, correction_workspace_);
      latest_linear.linear_solver = transient_linear_solver_name;
      latest_linear.linear_sweeps = gmres.iterations;
      latest_linear.total_linear_sweeps = gmres.iterations;
      jfnk_iterations += gmres.iterations;
      const double normalization =
          std::sqrt(static_cast<double>(mesh_.global_cell_count));
      latest_linear.initial_defect = gmres.initial_residual / normalization;
      latest_linear.final_defect = gmres.final_residual / normalization;
      latest_linear.defect_ratio = gmres.residual_ratio;
      latest_linear.converged = gmres.converged;
      jfnk_last_epsilon = transient_matrix_free.last_epsilon();
      jfnk_last_epsilon_halvings =
          transient_matrix_free.last_epsilon_halvings();
    } else if (attempt_steady_jfnk) {
      ResidualResult first_order_frozen;
      ResidualEvaluationOptions frozen_options = fast_options;
      frozen_options.reconstruction_blend = 0.0;
      frozen_options.collect_surface = false;
      residual_.evaluate_into(solution.U, cfl_, first_order_frozen,
                              frozen_options);
      steady_lusgs = std::make_unique<FrozenRusanovLUSGSOperator>(
          mesh_, config_, solution.U, first_order_frozen, cfl_, communicator_);
      steady_matrix_free = std::make_unique<SteadyMatrixFreeOperator>(
          mesh_, residual_, solution.U, current, cfl_, communicator_,
          active_reconstruction_blend);
      const DistributedLinearAction apply_operator =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            steady_matrix_free->apply(input, output,
                                      active_epsilon_multiplier);
          };
      const DistributedLinearAction apply_preconditioner =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            const InnerSolveStats preconditioner =
                steady_lusgs->solve(input, 3, 3, 0.0, output);
            ++lusgs_preconditioner_applications;
            lusgs_preconditioner_sweeps += preconditioner.linear_sweeps;
            lusgs_last_defect_ratio = preconditioner.defect_ratio;
          };
      MatrixFreeGmresOptions gmres_options;
      gmres_options.restart = 16;
      gmres_options.minimum_iterations = 3;
      gmres_options.maximum_iterations = 40;
      // Loose early solves avoid oversolving a strongly pseudo-transient
      // system; the forcing term tightens as CFL removes that regularization.
      gmres_options.relative_tolerance = std::clamp(
          0.25 / std::sqrt(std::max(1.0, cfl_)), 0.03, 0.25);
      const MatrixFreeGmresResult gmres = restarted_gmres(
          mesh_, rhs, apply_operator, apply_preconditioner, gmres_options,
          communicator_, correction_workspace_);
      latest_linear.linear_solver =
          steady_linear_solver_name;
      latest_linear.linear_sweeps = gmres.iterations;
      latest_linear.total_linear_sweeps = gmres.iterations;
      jfnk_iterations = gmres.iterations;
      const double normalization =
          std::sqrt(static_cast<double>(mesh_.global_cell_count));
      latest_linear.initial_defect = gmres.initial_residual / normalization;
      latest_linear.final_defect = gmres.final_residual / normalization;
      latest_linear.defect_ratio = gmres.residual_ratio;
      latest_linear.converged = gmres.converged;
      jfnk_epsilon_reference_residual = active_epsilon_reference;
      jfnk_epsilon_multiplier = active_epsilon_multiplier;
      jfnk_last_epsilon = steady_matrix_free->last_epsilon();
      jfnk_last_epsilon_halvings =
          steady_matrix_free->last_epsilon_halvings();
    } else {
      latest_linear.linear_solver = allow_steady_fallback
                                        ? steady_fallback_solver_name
                                        : steady_linear_solver_name;
    }
    total_linear_sweeps += latest_linear.linear_sweeps;

    original = solution.U;
    struct StrictImplicitCandidateResult {
      ImplicitTrustRegionCandidateDiagnostics diagnostics;
      InnerSolveStats linear;
      bool accepted{};
      double initial_residual{};
      double final_residual{};
      double line_scale{};
      std::vector<double> state;
      ResidualResult residual;
    };
    auto evaluate_strict_implicit_candidate = [&](double candidate_cfl) {
      StrictImplicitCandidateResult outcome;
      outcome.diagnostics.cfl = candidate_cfl;
      outcome.state = original;

      ResidualEvaluationOptions candidate_options = fast_options;
      candidate_options.reconstruction_blend = active_reconstruction_blend;
      ResidualResult candidate_base;
      residual_.evaluate_into(original, candidate_cfl, candidate_base,
                              candidate_options);
      std::vector<double> candidate_rhs(mesh_.cells.size() * 4U, 0.0);
      for (std::size_t i = 0; i < candidate_base.value.size(); ++i) {
        candidate_rhs[i] = -candidate_base.value[i];
      }
      outcome.initial_residual =
          vector_norm(mesh_, candidate_base.value, communicator_);
      outcome.final_residual = outcome.initial_residual;
      outcome.diagnostics.best_trial_residual = outcome.initial_residual;
      outcome.diagnostics.initial_component_l2 = component_vector_norms(
          mesh_, candidate_base.value, communicator_);
      outcome.diagnostics.best_component_l2 =
          outcome.diagnostics.initial_component_l2;
      outcome.diagnostics.epsilon_reference_residual =
          active_epsilon_reference;
      outcome.diagnostics.epsilon_multiplier = steady_jfnk_epsilon_multiplier(
          outcome.initial_residual, active_epsilon_reference);
      outcome.residual = candidate_base;

      ResidualResult first_order_frozen;
      ResidualEvaluationOptions frozen_options = candidate_options;
      frozen_options.reconstruction_blend = 0.0;
      frozen_options.collect_surface = false;
      residual_.evaluate_into(original, candidate_cfl, first_order_frozen,
                              frozen_options);
      FrozenRusanovLUSGSOperator candidate_lusgs(
          mesh_, config_, original, first_order_frozen, candidate_cfl,
          communicator_);
      SteadyMatrixFreeOperator candidate_operator(
          mesh_, residual_, original, candidate_base, candidate_cfl,
          communicator_, active_reconstruction_blend);
      const DistributedLinearAction apply_candidate_operator =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            candidate_operator.apply(
                input, output, outcome.diagnostics.epsilon_multiplier);
          };
      const DistributedLinearAction apply_candidate_preconditioner =
          [&](const std::vector<double>& input, std::vector<double>& output) {
            const InnerSolveStats preconditioner =
                candidate_lusgs.solve(input, 3, 3, 0.0, output);
            ++lusgs_preconditioner_applications;
            lusgs_preconditioner_sweeps += preconditioner.linear_sweeps;
            lusgs_last_defect_ratio = preconditioner.defect_ratio;
          };
      MatrixFreeGmresOptions candidate_gmres_options;
      candidate_gmres_options.restart = 50;
      candidate_gmres_options.minimum_iterations = 3;
      candidate_gmres_options.maximum_iterations = 50;
      candidate_gmres_options.relative_tolerance = 1.0e-2;
      const MatrixFreeGmresResult candidate_gmres = restarted_gmres(
          mesh_, candidate_rhs, apply_candidate_operator,
          apply_candidate_preconditioner, candidate_gmres_options,
          communicator_, correction_workspace_);
      outcome.diagnostics.gmres_iterations = candidate_gmres.iterations;
      outcome.diagnostics.gmres_ratio = candidate_gmres.residual_ratio;
      outcome.diagnostics.gmres_converged = candidate_gmres.converged;
      const double normalization =
          std::sqrt(static_cast<double>(mesh_.global_cell_count));
      outcome.linear.linear_sweeps = candidate_gmres.iterations;
      outcome.linear.total_linear_sweeps = candidate_gmres.iterations;
      outcome.linear.initial_defect =
          candidate_gmres.initial_residual / normalization;
      outcome.linear.final_defect =
          candidate_gmres.final_residual / normalization;
      outcome.linear.defect_ratio = candidate_gmres.residual_ratio;
      outcome.linear.converged = candidate_gmres.converged;
      outcome.diagnostics.last_epsilon = candidate_operator.last_epsilon();
      outcome.diagnostics.last_epsilon_halvings =
          candidate_operator.last_epsilon_halvings();
      jfnk_epsilon_reference_residual =
          outcome.diagnostics.epsilon_reference_residual;
      jfnk_epsilon_multiplier = outcome.diagnostics.epsilon_multiplier;
      jfnk_last_epsilon = outcome.diagnostics.last_epsilon;
      jfnk_last_epsilon_halvings =
          outcome.diagnostics.last_epsilon_halvings;
      total_linear_sweeps += candidate_gmres.iterations;

      std::vector<double> best_total = candidate_base.value;
      if (candidate_gmres.converged && outcome.initial_residual > 0.0 &&
          std::isfinite(outcome.initial_residual)) {
        double scale = 1.0;
        constexpr double minimum_candidate_scale = 1.0 / 1048576.0;
        while (scale >= minimum_candidate_scale) {
          solution.U = original;
          int local_admissible = 1;
          int local_changed = 0;
          for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
            Conservative candidate{};
            for (std::size_t k = 0; k < 4U; ++k) {
              const std::size_t index = cell * 4U + k;
              const double update = scale * correction_workspace_[index];
              solution.U[index] += update;
              candidate[k] = solution.U[index];
              const double meaningful_change =
                  32.0 * std::numeric_limits<double>::epsilon() *
                  std::max(1.0, std::abs(original[index]));
              if (std::abs(update) > meaningful_change) local_changed = 1;
            }
            if (!residual_.gas().admissible(candidate)) {
              local_admissible = 0;
              break;
            }
          }
          int global_admissible = 0;
          int global_changed = 0;
          MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT,
                        MPI_MIN, communicator_);
          MPI_Allreduce(&local_changed, &global_changed, 1, MPI_INT, MPI_MAX,
                        communicator_);
          if (global_admissible != 0 && global_changed != 0) {
            ResidualResult candidate_trial;
            residual_.evaluate_into(solution.U, candidate_cfl, candidate_trial,
                                    candidate_options);
            ++outcome.diagnostics.line_search_evaluations;
            const double candidate_norm =
                vector_norm(mesh_, candidate_trial.value, communicator_);
            if (std::isfinite(candidate_norm) &&
                candidate_norm < outcome.final_residual) {
              outcome.final_residual = candidate_norm;
              outcome.line_scale = scale;
              outcome.state = solution.U;
              outcome.residual = std::move(candidate_trial);
              best_total = outcome.residual.value;
              outcome.diagnostics.best_line_scale = scale;
              outcome.diagnostics.best_trial_residual = candidate_norm;
              outcome.diagnostics.best_update_norm =
                  scale * vector_norm(mesh_, correction_workspace_,
                                      communicator_);
            }
          }
          scale *= 0.5;
        }
      }
      outcome.accepted =
          std::isfinite(outcome.final_residual) &&
          outcome.final_residual < outcome.initial_residual;
      outcome.diagnostics.strictly_decreasing = outcome.accepted;
      outcome.diagnostics.best_component_l2 =
          component_vector_norms(mesh_, best_total, communicator_);
      solution.U = original;
      return outcome;
    };

    auto try_line_search = [&](const std::vector<double>& direction) {
      solution.U = original;
      const bool nonmonotone_eligible = steady_nonmonotone_eligible(
          !physical_time && active_reconstruction_blend == 1.0,
          allow_steady_nonmonotone, steady_best_residual_,
          steady_nonmonotone_residual_window_);
      const double nonmonotone_reference =
          nonmonotone_eligible
              ? steady_nonmonotone_bounded_reference(
                    steady_nonmonotone_residual_window_, nonlinear_norm,
                    steady_best_residual_,
                    steady_nonmonotone_envelope_reference_)
              : -1.0;
      if (nonmonotone_eligible) {
        // Eligibility is driven by full-order strict-globalization stagnation,
        // not by shock/positivity fallback counters. Count entry even when the
        // direction or every bounded trial is rejected.
        nonmonotone_bridge_attempted = true;
        nonmonotone_reference_residual = nonmonotone_reference;
      }
      bool have_nonmonotone_candidate = false;
      double best_nonmonotone_norm =
          std::numeric_limits<double>::infinity();
      double best_nonmonotone_scale = 0.0;
      std::vector<double> best_nonmonotone_state;
      std::vector<double> best_nonmonotone_total;
      ResidualResult best_nonmonotone_residual;
      double merit_slope = 0.0;
      double linearized_norm_squared = 0.0;
      if (!physical_time) {
        std::vector<double> linearized;
        steady_matrix_free->apply(direction, linearized,
                                  active_epsilon_multiplier);
        jfnk_epsilon_reference_residual = active_epsilon_reference;
        jfnk_epsilon_multiplier = active_epsilon_multiplier;
        jfnk_last_epsilon = steady_matrix_free->last_epsilon();
        jfnk_last_epsilon_halvings =
            steady_matrix_free->last_epsilon_halvings();
        for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
          const double pseudo_diagonal = current.spectral_radius[cell] / cfl_;
          for (std::size_t k = 0; k < 4U; ++k) {
            linearized[cell * 4U + k] -=
                pseudo_diagonal * direction[cell * 4U + k];
          }
        }
        const double cell_count = static_cast<double>(mesh_.global_cell_count);
        merit_slope = owned_dot(mesh_, nonlinear_residual, linearized,
                                communicator_) /
                      cell_count;
        linearized_norm_squared =
            owned_dot(mesh_, linearized, linearized, communicator_) /
            cell_count;
        const bool reliable_descent =
            std::isfinite(merit_slope) && merit_slope <
                -256.0 * std::numeric_limits<double>::epsilon() *
                    nonlinear_norm * nonlinear_norm &&
            std::isfinite(linearized_norm_squared);
        const bool bypass_descent_gate =
            steady_nonmonotone_descent_bypass_allowed(
                nonmonotone_eligible, latest_linear.converged, merit_slope,
                nonlinear_norm) && std::isfinite(linearized_norm_squared);
        if (!reliable_descent && !bypass_descent_gate) {
          return false;
        }
        if (nonmonotone_eligible) {
          nonmonotone_direction_descent = reliable_descent;
          nonmonotone_descent_bypass_attempted = bypass_descent_gate;
        }
      }
      double scale = 1.0;
      constexpr double minimum_scale = 1.0 / 1048576.0;
      while (scale >= minimum_scale) {
        int local_admissible = 1;
        int local_changed = 0;
        for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
          Conservative trial{};
          for (std::size_t k = 0; k < 4U; ++k) {
            const std::size_t index = cell * 4U + k;
            const double update = scale * direction[index];
            trial[k] = original[index] + update;
            const double meaningful_change =
                32.0 * std::numeric_limits<double>::epsilon() *
                std::max(1.0, std::abs(original[index]));
            if (std::abs(update) > meaningful_change) local_changed = 1;
          }
          if (!residual_.gas().admissible(trial)) {
            local_admissible = 0;
            break;
          }
        }
        int global_admissible = 0;
        int global_changed = 0;
        MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT, MPI_MIN,
                      communicator_);
        MPI_Allreduce(&local_changed, &global_changed, 1, MPI_INT, MPI_MAX,
                      communicator_);
        if (global_admissible == 0 || global_changed == 0) {
          scale *= 0.5;
          continue;
        }
        solution.U = original;
        for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
          for (std::size_t k = 0; k < 4U; ++k) {
            solution.U[cell * 4U + k] += scale * direction[cell * 4U + k];
          }
        }
        residual_.evaluate_into(solution.U, cfl_, trial_residual, fast_options);
        trial_total = trial_residual.value;
        add_physical_residual(trial_total, solution.U);
        const double trial_norm = vector_norm(mesh_, trial_total, communicator_);
        if (nonmonotone_eligible) {
          ++nonmonotone_trial_evaluations;
          if (std::isfinite(trial_norm)) {
            nonmonotone_best_trial_residual =
                std::min(nonmonotone_best_trial_residual, trial_norm);
          }
        }
        const double strict_predicted_reduction =
            physical_time
                ? 0.0
                : -(scale * merit_slope +
                    0.5 * scale * scale * linearized_norm_squared);
        const double nonmonotone_predicted_reduction =
            nonmonotone_descent_bypass_attempted
                ? -scale * steady_nonmonotone_surrogate_slope(
                               nonlinear_norm, latest_linear.defect_ratio)
                : strict_predicted_reduction;
        const bool residual_accepted = physical_time
            ? std::isfinite(trial_norm) &&
                  trial_norm <= 1.05 * nonlinear_norm +
                      std::sqrt(1.0e-13 *
                                std::max(1.0,
                                         nonlinear_norm * nonlinear_norm))
            : [&] {
                if (!std::isfinite(trial_norm)) return false;
                 return strict_steady_merit_decrease(
                            nonlinear_norm, trial_norm,
                            strict_predicted_reduction) &&
                       (steady_best_residual_ < 0.0 ||
                         trial_norm <= 1.01 * steady_best_residual_);
              }();
        if (residual_accepted) {
          accepted_residual_increase = !physical_time && trial_norm > nonlinear_norm;
          accepted_scale = scale;
          std::swap(current, trial_residual);
          cached_total.swap(trial_total);
          cached_trial_norm = trial_norm;
          cached_trial = true;
          return true;
        }
        if (nonmonotone_eligible &&
            steady_nonmonotone_trial_acceptable(
                nonmonotone_reference, trial_norm, steady_best_residual_,
                nonmonotone_predicted_reduction) &&
            trial_norm < best_nonmonotone_norm) {
          have_nonmonotone_candidate = true;
          best_nonmonotone_norm = trial_norm;
          best_nonmonotone_scale = scale;
          best_nonmonotone_state = solution.U;
          best_nonmonotone_total = trial_total;
          best_nonmonotone_residual = trial_residual;
        }
        scale *= 0.5;
      }
      if (have_nonmonotone_candidate) {
        solution.U = std::move(best_nonmonotone_state);
        current = std::move(best_nonmonotone_residual);
        cached_total = std::move(best_nonmonotone_total);
        cached_trial_norm = best_nonmonotone_norm;
        cached_trial = true;
        accepted_scale = best_nonmonotone_scale;
        accepted_residual_increase =
            best_nonmonotone_norm > nonlinear_norm;
        nonmonotone_bridge_accepted = true;
        nonmonotone_descent_bypass_accepted =
            nonmonotone_descent_bypass_attempted;
        nonmonotone_relative_increase =
            nonlinear_norm > 0.0
                ? std::max(0.0,
                           best_nonmonotone_norm / nonlinear_norm - 1.0)
                : 0.0;
        nonmonotone_envelope_accepted =
            steady_nonmonotone_envelope_reference_ >= 0.0;
        nonmonotone_envelope_relative_increase =
            steady_best_residual_ > 0.0
                ? std::max(0.0,
                           best_nonmonotone_norm / steady_best_residual_ - 1.0)
                : 0.0;
        return true;
      }
      solution.U = original;
      return false;
    };
    bool line_accepted = false;
    if (physical_time || attempt_steady_jfnk) {
      line_accepted = try_line_search(correction_workspace_);
    }
    if (line_accepted && !physical_time) {
      steady_acceptance = SteadyAcceptanceMode::jfnk;
    }
    if (!line_accepted && !physical_time && attempt_trust_region_retry) {
      const std::vector<double> retry_cfls =
          steady_trust_region_retry_cfls(cfl_, config_.run_control.cfl_max);
      trust_region_retry_attempted = !retry_cfls.empty();
      std::optional<StrictImplicitCandidateResult> best_retry;
      for (double retry_cfl : retry_cfls) {
        StrictImplicitCandidateResult candidate =
            evaluate_strict_implicit_candidate(retry_cfl);
        candidate.linear.linear_solver =
            steady_trust_region_retry_solver_name;
        ++trust_region_retry_candidates;
        trust_region_retry_total_gmres_iterations +=
            candidate.diagnostics.gmres_iterations;
        if (trust_region_retry_candidates == 1) {
          trust_region_retry_initial_residual = candidate.initial_residual;
        }
        trust_region_retry_diagnostics.push_back(candidate.diagnostics);
        latest_linear = candidate.linear;
        if (candidate.accepted &&
            (!best_retry.has_value() ||
             candidate.final_residual < best_retry->final_residual)) {
          best_retry = std::move(candidate);
        }
      }
      trust_region_retry_final_residual = trust_region_retry_initial_residual;
      if (best_retry.has_value()) {
        trust_region_retry_accepted = true;
        trust_region_retry_accepted_cfl = best_retry->diagnostics.cfl;
        trust_region_retry_accepted_gmres_iterations =
            best_retry->diagnostics.gmres_iterations;
        trust_region_retry_final_residual = best_retry->final_residual;
        line_accepted = true;
        accepted_scale = best_retry->line_scale;
        accepted_residual_increase = false;
        steady_acceptance =
            SteadyAcceptanceMode::implicit_trust_region_retry;
        solution.U = std::move(best_retry->state);
        current = std::move(best_retry->residual);
        cached_total = current.value;
        cached_trial_norm = best_retry->final_residual;
        cached_trial = true;
        initial_nonlinear_norm = best_retry->initial_residual;
        last_nonlinear_norm = best_retry->final_residual;
        latest_linear = best_retry->linear;
      } else {
        solution.U = original;
      }
    }
    if (!line_accepted && !physical_time && attempt_newton_rescue) {
      // The endpoint rescue remains secondary to the deterministic intermediate
      // trust-region scan and accepts only strict actual residual decrease.
      rescue_attempted = true;
      rescue_cfl = config_.run_control.cfl_max;
      StrictImplicitCandidateResult candidate =
          evaluate_strict_implicit_candidate(rescue_cfl);
      candidate.linear.linear_solver = steady_rescue_solver_name;
      rescue_gmres_iterations = candidate.diagnostics.gmres_iterations;
      rescue_gmres_ratio = candidate.diagnostics.gmres_ratio;
      rescue_initial_residual = candidate.initial_residual;
      rescue_final_residual = candidate.accepted
                                  ? candidate.final_residual
                                  : candidate.initial_residual;
      latest_linear = candidate.linear;
      if (candidate.accepted) {
        rescue_accepted = true;
        line_accepted = true;
        accepted_scale = candidate.line_scale;
        accepted_residual_increase = false;
        steady_acceptance = SteadyAcceptanceMode::newton_rescue;
        solution.U = std::move(candidate.state);
        current = std::move(candidate.residual);
        cached_total = current.value;
        cached_trial_norm = candidate.final_residual;
        cached_trial = true;
        initial_nonlinear_norm = candidate.initial_residual;
        last_nonlinear_norm = candidate.final_residual;
      } else {
        solution.U = original;
      }
    }
    if (!line_accepted && !physical_time && allow_steady_implicit_bridge) {
      // This is a local pseudo-time step, not an explicit update and not a
      // nonlinear line search. The full-block frozen Rusanov operator contains
      // sigma/CFL I + J_frozen; its solution is applied at the largest globally
      // positivity-safe geometric scale. Only finiteness, positivity, and the
      // fixed bridge-entry safety cap gate acceptance.
      implicit_bridge_attempted = true;
      implicit_bridge_cfl = steady_implicit_bridge_.cfl;
      implicit_bridge_initial_residual = nonlinear_norm;
      ResidualResult bridge_first_order;
      ResidualEvaluationOptions bridge_frozen_options = fast_options;
      bridge_frozen_options.reconstruction_blend = 0.0;
      bridge_frozen_options.collect_surface = false;
      residual_.evaluate_into(original, implicit_bridge_cfl,
                              bridge_first_order, bridge_frozen_options);
      FrozenRusanovLUSGSOperator bridge_lusgs(
          mesh_, config_, original, bridge_first_order, implicit_bridge_cfl,
          communicator_);
      latest_linear = bridge_lusgs.solve(rhs, 3, 8, 0.05,
                                         correction_workspace_);
      latest_linear.linear_solver = steady_implicit_bridge_solver_name;
      implicit_bridge_linear_sweeps = latest_linear.linear_sweeps;
      ++lusgs_preconditioner_applications;
      lusgs_preconditioner_sweeps += latest_linear.linear_sweeps;
      lusgs_last_defect_ratio = latest_linear.defect_ratio;

      const double local_scale = steady_largest_positivity_safe_scale(
          original, correction_workspace_, mesh_.owned_cell_count,
          residual_.gas());
      double scale = 0.0;
      MPI_Allreduce(&local_scale, &scale, 1, MPI_DOUBLE, MPI_MIN,
                    communicator_);
      int local_changed = 0;
      if (scale > 0.0) {
        for (std::size_t index = 0;
             index < mesh_.owned_cell_count * 4U; ++index) {
          const double threshold =
              32.0 * std::numeric_limits<double>::epsilon() *
              std::max(1.0, std::abs(original[index]));
          if (std::abs(scale * correction_workspace_[index]) > threshold) {
            local_changed = 1;
            break;
          }
        }
      }
      int global_changed = 0;
      MPI_Allreduce(&local_changed, &global_changed, 1, MPI_INT, MPI_MAX,
                    communicator_);
      if (scale > 0.0 && global_changed != 0) {
        solution.U = original;
        for (std::size_t index = 0;
             index < mesh_.owned_cell_count * 4U; ++index) {
          solution.U[index] += scale * correction_workspace_[index];
        }
        residual_.evaluate_into(solution.U, implicit_bridge_cfl,
                                trial_residual, fast_options);
        trial_total = trial_residual.value;
        const double trial_norm =
            vector_norm(mesh_, trial_total, communicator_);
        implicit_bridge_final_residual = trial_norm;
        if (nonlinear_norm > 0.0 && std::isfinite(trial_norm)) {
          implicit_bridge_relative_growth =
              trial_norm / nonlinear_norm - 1.0;
        }
        if (steady_implicit_bridge_residual_within_cap(
                steady_implicit_bridge_.entry_best_residual, trial_norm)) {
          implicit_bridge_accepted = true;
          line_accepted = true;
          accepted_scale = scale;
          implicit_bridge_line_scale = scale;
          accepted_residual_increase = trial_norm > nonlinear_norm;
          steady_acceptance =
              SteadyAcceptanceMode::implicit_pseudo_transient_bridge;
          std::swap(current, trial_residual);
          cached_total.swap(trial_total);
          cached_trial_norm = trial_norm;
          cached_trial = true;
        }
      }
      if (!line_accepted) solution.U = original;
    }
    if (!line_accepted && !physical_time && !rescue_attempted &&
        allow_steady_fallback) {
      // Standalone LU-SGS recovery is globalized exactly like an implicit
      // nonlinear update: every accepted state is positive and decreases the
      // actual residual for the active continuation operator.
      fallback_attempted = true;
      constexpr int maximum_fallback_attempts = 8;
      double fallback_cfl = std::clamp(
          steady_fallback_cfl_, steady_fallback_minimum_cfl, 0.1);
      for (int attempt = 0; attempt < maximum_fallback_attempts; ++attempt) {
        ResidualResult fallback_first_order;
        ResidualEvaluationOptions fallback_frozen_options = fast_options;
        fallback_frozen_options.reconstruction_blend = 0.0;
        fallback_frozen_options.collect_surface = false;
        residual_.evaluate_into(original, fallback_cfl, fallback_first_order,
                                fallback_frozen_options);
        FrozenRusanovLUSGSOperator fallback_lusgs(
            mesh_, config_, original, fallback_first_order, fallback_cfl,
            communicator_);
        const InnerSolveStats fallback_linear = fallback_lusgs.solve(
            rhs, 3, 8, 0.05, correction_workspace_);
        fallback_sweeps += fallback_linear.total_linear_sweeps;
        fallback_cfl_used = fallback_cfl;
        latest_linear = fallback_linear;
        ++lusgs_preconditioner_applications;
        lusgs_preconditioner_sweeps += fallback_linear.linear_sweeps;
        lusgs_last_defect_ratio = fallback_linear.defect_ratio;

        double scale = 1.0;
        constexpr double minimum_fallback_scale = 1.0 / 1048576.0;
        while (scale >= minimum_fallback_scale) {
          solution.U = original;
          int local_admissible = 1;
          int local_changed = 0;
          for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
            Conservative trial{};
            for (std::size_t k = 0; k < 4U; ++k) {
              const std::size_t index = cell * 4U + k;
              const double update = scale * correction_workspace_[index];
              solution.U[index] += update;
              trial[k] = solution.U[index];
              const double meaningful_change =
                  32.0 * std::numeric_limits<double>::epsilon() *
                  std::max(1.0, std::abs(original[index]));
              if (std::abs(update) > meaningful_change) local_changed = 1;
            }
            if (!residual_.gas().admissible(trial)) {
              local_admissible = 0;
              break;
            }
          }
          int global_admissible = 0;
          int global_changed = 0;
          MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT,
                        MPI_MIN, communicator_);
          MPI_Allreduce(&local_changed, &global_changed, 1, MPI_INT,
                        MPI_MAX, communicator_);
          if (global_admissible == 0 || global_changed == 0) {
            scale *= 0.5;
            continue;
          }
          residual_.evaluate_into(solution.U, fallback_cfl, trial_residual,
                                  fast_options);
          trial_total = trial_residual.value;
          const double trial_norm =
              vector_norm(mesh_, trial_total, communicator_);
          if (std::isfinite(trial_norm) && trial_norm < nonlinear_norm) {
            accepted_residual_increase = false;
            accepted_scale = scale;
            std::swap(current, trial_residual);
            cached_total.swap(trial_total);
            cached_trial_norm = trial_norm;
            cached_trial = true;
            line_accepted = true;
            steady_acceptance = SteadyAcceptanceMode::pseudo_time_fallback;
            break;
          }
          scale *= 0.5;
        }
        if (line_accepted) break;

        solution.U = original;
        if (attempt + 1 < maximum_fallback_attempts &&
            0.5 * fallback_cfl >= steady_fallback_minimum_cfl) {
          fallback_cfl *= 0.5;
          ++fallback_cfl_halvings;
        } else {
          break;
        }
      }
    }
    if (!line_accepted) {
      update_accepted = false;
      break;
    }
    update_accepted = true;
    ++nonlinear_iterations;
    if (!physical_time) break;
  }

  if (physical_time && update_accepted && !target_met) {
    if (cached_trial) {
      nonlinear_residual.swap(cached_total);
      last_nonlinear_norm = cached_trial_norm;
      cached_trial = false;
    } else {
      nonlinear_residual = current.value;
      add_physical_residual(nonlinear_residual, solution.U);
      last_nonlinear_norm = vector_norm(mesh_, nonlinear_residual, communicator_);
    }
    last_nonlinear_ratio = initial_nonlinear_norm == 0.0
                               ? 0.0
                               : last_nonlinear_norm / initial_nonlinear_norm;
    target_met = nonlinear_iterations >= nonlinear_min &&
        last_nonlinear_ratio <= config_.run_control.inner_residual_reduction_target;
  }

  const bool accepted = physical_time ? (update_accepted && target_met) : update_accepted;
  StepResult result;
  result.accepted = accepted;
  result.target_met = physical_time && target_met;
  result.inner = latest_linear;
  result.inner.nonlinear_iterations = physical_time ? nonlinear_iterations
                                                    : (update_accepted ? 1 : 0);
  result.inner.total_linear_sweeps = total_linear_sweeps + fallback_sweeps;
  result.cfl = cfl_;
  result.line_search_scale = accepted_scale;
  result.nonmonotone_residual_increase = accepted_residual_increase;
  result.steady_acceptance = steady_acceptance;
  result.fallback_attempted = fallback_attempted;
  result.fallback_cfl = fallback_cfl_used;
  result.fallback_sweeps = fallback_sweeps;
  result.fallback_cfl_halvings = fallback_cfl_halvings;
  result.jfnk_iterations = jfnk_iterations;
  result.jfnk_attempted = jfnk_attempted;
  result.jfnk_epsilon_reference_residual =
      jfnk_epsilon_reference_residual;
  result.jfnk_epsilon_multiplier = jfnk_epsilon_multiplier;
  result.jfnk_last_epsilon = jfnk_last_epsilon;
  result.jfnk_last_epsilon_halvings = jfnk_last_epsilon_halvings;
  result.rescue_attempted = rescue_attempted;
  result.rescue_accepted = rescue_accepted;
  result.rescue_cfl = rescue_cfl;
  result.rescue_gmres_iterations = rescue_gmres_iterations;
  result.rescue_gmres_ratio = rescue_gmres_ratio;
  result.rescue_initial_residual = rescue_initial_residual;
  result.rescue_final_residual = rescue_final_residual;
  result.trust_region_retry_attempted = trust_region_retry_attempted;
  result.trust_region_retry_accepted = trust_region_retry_accepted;
  result.trust_region_retry_candidates = trust_region_retry_candidates;
  result.trust_region_retry_total_gmres_iterations =
      trust_region_retry_total_gmres_iterations;
  result.trust_region_retry_accepted_gmres_iterations =
      trust_region_retry_accepted_gmres_iterations;
  result.trust_region_retry_accepted_cfl = trust_region_retry_accepted_cfl;
  result.trust_region_retry_initial_residual =
      trust_region_retry_initial_residual;
  result.trust_region_retry_final_residual =
      trust_region_retry_final_residual;
  result.trust_region_retry_diagnostics =
      std::move(trust_region_retry_diagnostics);
  result.lusgs_preconditioner_applications =
      lusgs_preconditioner_applications;
  result.lusgs_preconditioner_sweeps = lusgs_preconditioner_sweeps;
  result.lusgs_last_defect_ratio = lusgs_last_defect_ratio;
  result.limiter_active = limiter_active;
  result.nonmonotone_bridge_attempted = nonmonotone_bridge_attempted;
  result.nonmonotone_bridge_accepted = nonmonotone_bridge_accepted;
  result.nonmonotone_direction_descent = nonmonotone_direction_descent;
  result.nonmonotone_descent_bypass_attempted =
      nonmonotone_descent_bypass_attempted;
  result.nonmonotone_descent_bypass_accepted =
      nonmonotone_descent_bypass_accepted;
  result.nonmonotone_trial_evaluations = nonmonotone_trial_evaluations;
  result.nonmonotone_best_trial_residual =
      std::isfinite(nonmonotone_best_trial_residual)
          ? nonmonotone_best_trial_residual
          : -1.0;
  result.nonmonotone_bypass_gmres_ratio = latest_linear.defect_ratio;
  result.nonmonotone_envelope_seeded = nonmonotone_envelope_seeded;
  result.nonmonotone_envelope_accepted = nonmonotone_envelope_accepted;
  result.nonmonotone_envelope_reference =
      steady_nonmonotone_envelope_reference_;
  result.nonmonotone_envelope_relative_increase =
      nonmonotone_envelope_relative_increase;
  result.implicit_bridge_attempted = implicit_bridge_attempted;
  result.implicit_bridge_accepted = implicit_bridge_accepted;
  result.implicit_bridge_cfl = implicit_bridge_cfl;
  result.implicit_bridge_line_scale = implicit_bridge_line_scale;
  result.implicit_bridge_initial_residual = implicit_bridge_initial_residual;
  result.implicit_bridge_final_residual = implicit_bridge_final_residual;
  result.implicit_bridge_relative_growth = implicit_bridge_relative_growth;
  result.implicit_bridge_linear_sweeps = implicit_bridge_linear_sweeps;
  result.nonmonotone_reference_residual = nonmonotone_reference_residual;
  result.nonmonotone_relative_increase = nonmonotone_relative_increase;
  result.fallback_mode = steady_fallback_mode_;
  result.reconstruction_blend = active_reconstruction_blend;
  result.inner.initial_nonlinear_residual =
      std::max(0.0, initial_nonlinear_norm);
  result.inner.final_nonlinear_residual =
      accepted && cached_trial ? cached_trial_norm : last_nonlinear_norm;
  result.inner.nonlinear_ratio =
      initial_nonlinear_norm <= 0.0
          ? 0.0
          : result.inner.final_nonlinear_residual / initial_nonlinear_norm;
  if (accepted) {
    residual_.finalize_cached_result(current);
    result.residual = current.norms;
    result.forces = residual_.forces(current);
    if (physical_time) {
      result.inner.final_nonlinear_residual = last_nonlinear_norm;
      result.inner.nonlinear_ratio = last_nonlinear_ratio;
      result.inner.converged = target_met;
    }
  } else {
    solution.U = rollback_u_workspace_;
    solution.physical_step = entry_step;
    solution.time = entry_time;
    if (physical_time) {
      result.inner.final_nonlinear_residual = last_nonlinear_norm;
      result.inner.nonlinear_ratio = last_nonlinear_ratio;
      result.inner.converged = false;
    } else {
      // A rejected steady attempt still reports the unchanged entry state's
      // true residual and forces; zero/default diagnostics hide stagnation and
      // make retry evidence unusable.
      residual_.finalize_cached_result(current);
      result.residual = current.norms;
      result.forces = residual_.forces(current);
    }
  }
  return result;
}

StepResult FlowSolver::steady_step(RestartableSolution& solution) {
  if (config_.run_control.type != RunType::steady) {
    throw std::logic_error("steady_step called for a transient case");
  }
  if (steady_fallback_consecutive_accepted_steps_ >=
          steady_fallback_maximum_bridge_steps) {
    steady_fallback_disabled_ = true;
    steady_fallback_mode_ = false;
  }
  // Version-11 checkpoints written by the former policy marked fallback
  // disabled solely on entering full order. Re-enable only that distinguishable
  // state; growth- and bridge-limit disables remain permanent.
  if (steady_reconstruction_blend_ == 1.0 && steady_fallback_disabled_ &&
      steady_fallback_consecutive_accepted_steps_ <
          steady_fallback_maximum_bridge_steps &&
      steady_fallback_growth_disables_ == 0U) {
    steady_fallback_disabled_ = false;
  }
  if (steady_reconstruction_blend_ == 1.0 &&
      steady_nonmonotone_residual_window_.empty() &&
      steady_previous_residual_ >= 0.0) {
    steady_nonmonotone_push_residual(
        steady_nonmonotone_residual_window_, steady_previous_residual_);
  }
  if (steady_rescue_cooldown_attempts_ > 0U) {
    --steady_rescue_cooldown_attempts_;
  }
  const bool trust_region_retry_cooldown_active =
      steady_trust_region_retry_cooldown_attempts_ > 0U;
  if (steady_trust_region_retry_cooldown_attempts_ > 0U) {
    --steady_trust_region_retry_cooldown_attempts_;
  }
  // Ordinary JFNK/LU-SGS is the primary method on every externally counted
  // steady attempt. Rescue cooldown suppresses only the expensive high-CFL
  // secondary solve; fallback mode never suppresses the primary method.
  constexpr bool attempt_jfnk = true;
  // A failed low-CFL globalization can occur in every fixed spatial-order
  // phase.  The retry candidates use the active reconstruction blend, so
  // restricting this path to full order leaves startup/ramp states with only
  // the endpoint rescue and the capped fallback.  That can reject forever at
  // a first-order residual floor even though an intermediate implicit CFL is
  // strictly decreasing.
  const bool attempt_trust_region_retry =
      !trust_region_retry_cooldown_active;
  const bool attempt_rescue =
      (steady_jfnk_failure_streak_ >= 1 ||
       steady_rescue_stagnation_count_ >= steady_rescue_stagnation_steps ||
       steady_fallback_disabled_) &&
      steady_rescue_cooldown_attempts_ == 0U;
  const bool allow_fallback = steady_implicit_fallback_allowed(
      steady_reconstruction_blend_, steady_fallback_disabled_,
      steady_fallback_consecutive_accepted_steps_);
  // The activation-envelope experiment is retained only in restart migration
  // fields. It must not precede the implicit pseudo-transient bridge in active
  // production flow.
  constexpr bool allow_nonmonotone = false;
  constexpr bool seed_nonmonotone_envelope = false;
  const double bridge_minimum_cfl = std::max(
      steady_fallback_minimum_cfl,
      0.01 * config_.run_control.cfl_initial);
  const bool allow_implicit_bridge = steady_implicit_bridge_eligible(
      steady_implicit_bridge_.disabled, steady_implicit_bridge_.active,
      steady_strict_decrease_stagnation_streak_, steady_best_residual_);
  if (allow_implicit_bridge && !steady_implicit_bridge_.active) {
    steady_implicit_bridge_.active = true;
    steady_implicit_bridge_.entry_best_residual =
        steady_implicit_bridge_epoch_reference(
            steady_best_residual_, steady_previous_residual_);
    steady_implicit_bridge_.accepted_steps_since_best = 0U;
    steady_implicit_bridge_.cfl = steady_implicit_bridge_bounded_initial_cfl(
        bridge_minimum_cfl, config_.run_control.cfl_max);
  }
  const double entry_best_residual = steady_best_residual_;
  StepResult result = implicit_step(
      solution, false, attempt_jfnk, attempt_trust_region_retry,
      attempt_rescue, allow_fallback, allow_nonmonotone,
      seed_nonmonotone_envelope, allow_implicit_bridge);
  if (!result.accepted && steady_best_residual_ < 0.0) {
    // A blend change creates a new fixed operator before it creates an
    // accepted update. Preserve that operator's exact entry evaluation as the
    // bridge cap baseline, otherwise a phase whose first strict step fails can
    // never become bridge-eligible.
    steady_best_residual_ = steady_phase_best_with_entry_evidence(
        steady_best_residual_, result.residual.total_l2);
    if (steady_best_residual_ >= 0.0) {
      steady_previous_residual_ = steady_best_residual_;
      solution.U_best = solution.U;
    }
  }
  steady_lusgs_preconditioner_applications_ +=
      static_cast<std::size_t>(result.lusgs_preconditioner_applications);
  steady_lusgs_preconditioner_sweeps_ +=
      static_cast<std::size_t>(result.lusgs_preconditioner_sweeps);
  if (result.lusgs_preconditioner_applications > 0) {
    steady_lusgs_last_defect_ratio_ = result.lusgs_last_defect_ratio;
  }

  if (result.jfnk_attempted) {
    ++steady_jfnk_attempts_;
    if (result.jfnk_epsilon_reference_residual >= 0.0) {
      steady_jfnk_epsilon_reference_residual_ =
          result.jfnk_epsilon_reference_residual;
      steady_jfnk_epsilon_multiplier_ = result.jfnk_epsilon_multiplier;
      steady_jfnk_last_epsilon_ = result.jfnk_last_epsilon;
      steady_jfnk_last_epsilon_halvings_ =
          result.jfnk_last_epsilon_halvings;
    }
    steady_fallback_steps_since_jfnk_ = 0U;
    if (result.steady_acceptance == SteadyAcceptanceMode::jfnk) {
      ++steady_jfnk_accepted_steps_;
      steady_jfnk_failure_streak_ = 0;
      steady_fallback_mode_ = false;
      steady_fallback_residual_window_.clear();
      steady_fallback_consecutive_accepted_steps_ = 0U;
      steady_rescue_cooldown_attempts_ = 0U;
    } else {
      steady_jfnk_failure_streak_ =
          std::min(2, steady_jfnk_failure_streak_ + 1);
    }
  }
  if (result.rescue_attempted) {
    ++steady_rescue_attempts_;
    steady_rescue_total_gmres_iterations_ +=
        static_cast<std::size_t>(result.rescue_gmres_iterations);
    steady_rescue_last_gmres_iterations_ = result.rescue_gmres_iterations;
    steady_rescue_max_gmres_iterations_ = std::max(
        steady_rescue_max_gmres_iterations_, result.rescue_gmres_iterations);
    steady_rescue_last_gmres_ratio_ = result.rescue_gmres_ratio;
    steady_rescue_last_cfl_ = result.rescue_cfl;
    steady_rescue_last_line_scale_ = result.line_search_scale;
    if (result.rescue_accepted) {
      ++steady_rescue_accepted_steps_;
      steady_jfnk_failure_streak_ = 0;
      steady_fallback_mode_ = false;
      steady_fallback_consecutive_accepted_steps_ = 0U;
      steady_fallback_residual_window_.clear();
      steady_rescue_cooldown_attempts_ = 0U;
    } else {
      steady_rescue_cooldown_attempts_ = steady_rescue_retry_interval;
    }
  }
  if (result.trust_region_retry_attempted) {
    ++steady_trust_region_retry_batches_;
    steady_trust_region_retry_candidates_ +=
        static_cast<std::size_t>(result.trust_region_retry_candidates);
    steady_trust_region_retry_total_gmres_iterations_ +=
        static_cast<std::size_t>(
            result.trust_region_retry_total_gmres_iterations);
    steady_trust_region_retry_last_candidate_count_ =
        static_cast<std::size_t>(result.trust_region_retry_candidates);
    steady_trust_region_retry_last_total_gmres_iterations_ =
        result.trust_region_retry_total_gmres_iterations;
    if (result.trust_region_retry_accepted) {
      ++steady_trust_region_retry_accepted_steps_;
      steady_trust_region_retry_last_accepted_gmres_iterations_ =
          result.trust_region_retry_accepted_gmres_iterations;
      steady_trust_region_retry_last_accepted_cfl_ =
          result.trust_region_retry_accepted_cfl;
      steady_trust_region_retry_last_line_scale_ = result.line_search_scale;
      steady_trust_region_retry_last_initial_residual_ =
          result.trust_region_retry_initial_residual;
      steady_trust_region_retry_last_final_residual_ =
          result.trust_region_retry_final_residual;
      steady_jfnk_failure_streak_ = 0;
      steady_fallback_mode_ = false;
      steady_fallback_consecutive_accepted_steps_ = 0U;
      steady_fallback_residual_window_.clear();
      steady_trust_region_retry_cooldown_attempts_ = 0U;
      steady_rescue_cooldown_attempts_ = 0U;
    } else {
      steady_trust_region_retry_cooldown_attempts_ =
          steady_trust_region_retry_interval;
    }
  }
  if (result.fallback_attempted) {
    ++steady_fallback_attempts_;
    steady_fallback_cfl_halvings_ +=
        static_cast<std::size_t>(result.fallback_cfl_halvings);
    steady_last_fallback_cfl_ = result.fallback_cfl;
    if (!result.accepted) {
      ++steady_fallback_rejected_steps_;
      steady_fallback_cfl_ =
          std::max(steady_fallback_minimum_cfl,
                   0.5 * result.fallback_cfl);
      steady_fallback_steps_since_jfnk_ =
          steady_fallback_jfnk_retry_interval;
    }
  }
  const double cfl_floor = 0.01 * config_.run_control.cfl_initial;
  constexpr double tiny_line_scale = 0.05;
  if (result.accepted) {
    ++nonlinear_steps_;
  }
  const int ramp = config_.run_control.pseudo_cfl_ramp_steps;
  const double fraction = ramp == 0
                              ? 1.0
                              : std::min(1.0, static_cast<double>(nonlinear_steps_) /
                                                 static_cast<double>(ramp));
  const double scheduled = config_.run_control.cfl_initial +
      fraction * (config_.run_control.cfl_max - config_.run_control.cfl_initial);
  const double residual = result.accepted ? result.residual.total_l2 : 0.0;
  const bool strict_best_improvement =
      result.accepted &&
      (entry_best_residual < 0.0 || residual < entry_best_residual);
  const bool meaningful_strict_best_improvement =
      result.accepted &&
      (entry_best_residual < 0.0 ||
       steady_meaningful_best_decrease(entry_best_residual, residual));
  result.meaningful_strict_best_improvement =
      meaningful_strict_best_improvement;
  result.noise_scale_strict_best_improvement =
      strict_best_improvement && !meaningful_strict_best_improvement;
  if (result.implicit_bridge_attempted) {
    ++steady_implicit_bridge_.attempts;
    steady_implicit_bridge_.linear_sweeps +=
        static_cast<std::size_t>(result.implicit_bridge_linear_sweeps);
    if (!result.implicit_bridge_accepted) {
      ++steady_implicit_bridge_.rejected_steps;
    }
    update_steady_implicit_bridge(
        steady_implicit_bridge_, result.implicit_bridge_accepted,
        meaningful_strict_best_improvement,
        result.implicit_bridge_initial_residual,
        result.implicit_bridge_final_residual, bridge_minimum_cfl,
        config_.run_control.cfl_max);
  } else if (steady_implicit_bridge_.active &&
             meaningful_strict_best_improvement) {
    // An ordinary JFNK/retry/rescue improvement keeps the active bridge's
    // watchdog alive, but it is not an accepted bridge update and therefore
    // must not inflate bridge-only acceptance diagnostics.
    note_steady_implicit_bridge_external_best(steady_implicit_bridge_);
  }
  if (result.reconstruction_blend == 1.0) {
    if (result.nonmonotone_envelope_accepted) {
      ++steady_nonmonotone_envelope_accepted_steps_;
      steady_nonmonotone_envelope_max_relative_increase_ = std::max(
          steady_nonmonotone_envelope_max_relative_increase_,
          result.nonmonotone_envelope_relative_increase);
    }
    if (result.nonmonotone_descent_bypass_attempted) {
      ++steady_nonmonotone_bypass_attempts_;
      steady_nonmonotone_bypass_trial_evaluations_ +=
          static_cast<std::size_t>(result.nonmonotone_trial_evaluations);
      steady_nonmonotone_bypass_last_actual_trial_residual_ =
          result.nonmonotone_best_trial_residual;
      steady_nonmonotone_bypass_last_gmres_ratio_ =
          result.nonmonotone_bypass_gmres_ratio;
      if (result.nonmonotone_descent_bypass_accepted) {
        ++steady_nonmonotone_bypass_accepted_steps_;
      }
    }
    if (result.accepted) {
      steady_nonmonotone_push_residual(
          steady_nonmonotone_residual_window_, residual);
      const bool bridge_was_active = steady_nonmonotone_bridge_active_;
      if (result.nonmonotone_bridge_accepted) {
        steady_nonmonotone_bridge_active_ = true;
        ++steady_nonmonotone_accepted_steps_;
        steady_nonmonotone_max_relative_increase_ = std::max(
            steady_nonmonotone_max_relative_increase_,
            result.nonmonotone_relative_increase);
      }
      if (meaningful_strict_best_improvement &&
          steady_nonmonotone_envelope_reference_ >= 0.0) {
        steady_nonmonotone_envelope_reference_ =
            steady_nonmonotone_update_envelope_reference(
                steady_nonmonotone_envelope_reference_, residual, true);
      }
      if (steady_nonmonotone_bridge_active_) {
        if (meaningful_strict_best_improvement) {
          if (bridge_was_active || result.nonmonotone_bridge_accepted) {
            ++steady_nonmonotone_strict_best_improvements_;
          }
        }
        SteadyNonmonotoneWatchdogState watchdog{
            steady_nonmonotone_bridge_active_,
            steady_nonmonotone_bridge_disabled_,
            steady_nonmonotone_steps_since_strict_best_,
            steady_nonmonotone_watchdog_resets_};
        update_steady_nonmonotone_watchdog(
            watchdog, true, meaningful_strict_best_improvement);
        steady_nonmonotone_bridge_active_ = watchdog.active;
        steady_nonmonotone_bridge_disabled_ = watchdog.disabled;
        steady_nonmonotone_steps_since_strict_best_ =
            watchdog.accepted_steps_since_strict_best;
        steady_nonmonotone_watchdog_resets_ = watchdog.resets;
        if (steady_nonmonotone_bridge_disabled_) {
          steady_nonmonotone_envelope_reference_ = -1.0;
        }
      }
    }
  }
  // Strict globalization can reach a fixed-operator residual floor in any
  // spatial-order phase. Track that stagnation throughout continuation so the
  // bounded implicit bridge can produce genuine accepted updates; blend
  // changes reset this streak before the next operator is evaluated.
  steady_strict_decrease_stagnation_streak_ =
      steady_nonmonotone_stagnation_after_attempt(
          steady_strict_decrease_stagnation_streak_, true,
          meaningful_strict_best_improvement);
  const bool residual_increased = result.accepted &&
      (result.nonmonotone_residual_increase ||
       (steady_previous_residual_ >= 0.0 && residual > steady_previous_residual_));

  if (result.accepted && result.steady_acceptance ==
                              SteadyAcceptanceMode::pseudo_time_fallback) {
    ++steady_fallback_accepted_steps_;
    ++steady_fallback_consecutive_accepted_steps_;
    steady_fallback_cfl_ = result.fallback_cfl;
    steady_fallback_residual_window_.push_back(residual);
    if (steady_fallback_residual_window_.size() >
        steady_fallback_window_capacity) {
      steady_fallback_residual_window_.erase(
          steady_fallback_residual_window_.begin());
    }
    if (steady_jfnk_failure_streak_ >= 2 &&
        !steady_fallback_disabled_) {
      steady_fallback_mode_ = true;
    }
    if (steady_fallback_mode_) {
      steady_fallback_steps_since_jfnk_ = std::min(
          steady_fallback_jfnk_retry_interval,
          steady_fallback_steps_since_jfnk_ + 1U);
    }

    // Adapt only after a full long window. Positivity/nonfinite failures have
    // already contracted CFL inside implicit_step. A bounded recent half may
    // cautiously regrow it; material long-window growth contracts it.
    if (steady_fallback_residual_window_.size() ==
            steady_fallback_window_capacity &&
        steady_fallback_accepted_steps_ % 16U == 0U) {
      const auto middle = steady_fallback_residual_window_.begin() + 16;
      const double older_maximum =
          *std::max_element(steady_fallback_residual_window_.begin(), middle);
      const double recent_maximum =
          *std::max_element(middle, steady_fallback_residual_window_.end());
      const double older_mean = std::accumulate(
          steady_fallback_residual_window_.begin(), middle, 0.0) / 16.0;
      const double recent_mean = std::accumulate(
          middle, steady_fallback_residual_window_.end(), 0.0) / 16.0;
      if (recent_mean > 1.005 * older_mean) {
        // A fallback trajectory with sustained growth is evidence against
        // continuing that trajectory. Disable it permanently for this run and
        // return control to JFNK/high-CFL rescue.
        steady_fallback_disabled_ = true;
        steady_fallback_mode_ = false;
        ++steady_fallback_growth_disables_;
      } else if (recent_maximum <= 1.5 * older_maximum) {
        steady_fallback_cfl_ = std::min(0.1, 1.25 * steady_fallback_cfl_);
      } else {
        steady_fallback_cfl_ =
            std::max(steady_fallback_minimum_cfl,
                     0.5 * steady_fallback_cfl_);
      }
    }
    if (steady_fallback_consecutive_accepted_steps_ >=
        steady_fallback_maximum_bridge_steps) {
      steady_fallback_disabled_ = true;
      steady_fallback_mode_ = false;
    }
  } else if (result.fallback_attempted &&
              steady_jfnk_failure_streak_ >= 2 &&
              !steady_fallback_disabled_) {
    // A rejected fallback does not establish a fallback trajectory. Entering
    // mode without an accepted residual window emits continuation states that
    // cannot be resumed and gives the trusted-scale bound no evidence.
    steady_fallback_mode_ = steady_fallback_mode_has_history(
        true, steady_fallback_disabled_,
        steady_fallback_residual_window_.size());
  }

  if (result.steady_acceptance ==
          SteadyAcceptanceMode::pseudo_time_fallback ||
      (steady_fallback_mode_ && !steady_fallback_disabled_)) {
    cfl_ = std::clamp(2.0 * steady_fallback_cfl_, cfl_floor, scheduled);
    steady_probe_active_ = false;
    steady_recovery_probe_cfl_ = 0.0;
    steady_recovery_restore_pending_ = false;
    steady_rejected_attempts_ = result.accepted
                                    ? 0
                                    : steady_rejected_attempts_ + 1;
    steady_trend_reference_residual_ = -1.0;
    steady_trend_samples_ = 0;
  } else {
    SteadyCflAdaptationState adaptation{
        cfl_, steady_trend_reference_residual_, steady_trend_samples_,
        false, steady_rejected_attempts_, 0.0, false};
    update_steady_cfl_adaptation(
        adaptation, result.accepted, residual_increased,
        result.accepted && result.line_search_scale < tiny_line_scale,
        residual, cfl_floor, scheduled);
    cfl_ = adaptation.cfl;
    steady_trend_reference_residual_ = adaptation.trend_reference_residual;
    steady_trend_samples_ = adaptation.trend_samples;
    steady_probe_active_ = false;
    steady_rejected_attempts_ = adaptation.rejected_attempts;
    steady_recovery_probe_cfl_ = 0.0;
    steady_recovery_restore_pending_ = false;
  }
  if (result.accepted) {
    steady_previous_residual_ = residual;
    if (steady_best_residual_ < 0.0 || residual < steady_best_residual_) {
      steady_best_residual_ = residual;
      solution.U_best = solution.U;
    }

    const SteadySpatialOrderSchedule order_schedule =
        steady_spatial_order_schedule(
            config_.run_control.pseudo_cfl_ramp_steps);
    bool blend_advanced = false;
    bool transition_started = false;
    if (result.reconstruction_blend == 1.0) {
      if (steady_full_order_initial_residual_ < 0.0) {
        steady_full_order_initial_residual_ =
            result.inner.initial_nonlinear_residual;
        steady_full_order_best_residual_ =
            std::min(steady_full_order_initial_residual_, residual);
      } else {
        steady_full_order_best_residual_ =
            std::min(steady_full_order_best_residual_, residual);
      }
      ++steady_full_order_accepted_steps_;
    }

    if (steady_first_order_accepted_steps_ <
        order_schedule.first_order_steps) {
      ++steady_first_order_accepted_steps_;
      if (steady_first_order_accepted_steps_ ==
          order_schedule.first_order_steps) {
        steady_reconstruction_blend_ = smooth_reconstruction_blend(
            1U, order_schedule.ramp_steps + 1U);
        blend_advanced = true;
        transition_started = true;
      }
    } else if (steady_order_ramp_accepted_steps_ <
               order_schedule.ramp_steps) {
      ++steady_order_ramp_accepted_steps_;
      const double next_blend = steady_order_ramp_accepted_steps_ <
                                        order_schedule.ramp_steps
                                    ? smooth_reconstruction_blend(
                                          steady_order_ramp_accepted_steps_ + 1U,
                                          order_schedule.ramp_steps + 1U)
                                    : 1.0;
      blend_advanced = next_blend != steady_reconstruction_blend_;
      steady_reconstruction_blend_ = next_blend;
    }

    steady_target_met_ = steady_convergence_gate(
        result.reconstruction_blend, steady_full_order_accepted_steps_,
        config_.run_control.pseudo_cfl_ramp_steps,
        steady_initial_residual_scale_, residual,
        *config_.run_control.residual_reduction_target);
    result.target_met = steady_target_met_;

    if (blend_advanced) {
      // Residual magnitudes from different spatial operators are not compared
      // by globalization, CFL trends, or the fallback trusted-scale bound.
      steady_previous_residual_ = -1.0;
      steady_best_residual_ = -1.0;
      steady_trend_reference_residual_ = -1.0;
      steady_trend_samples_ = 0;
      steady_rescue_reference_residual_ = -1.0;
      steady_rescue_stagnation_count_ = 0U;
      steady_nonmonotone_residual_window_.clear();
      steady_strict_decrease_stagnation_streak_ = 0U;
      steady_nonmonotone_bridge_active_ = false;
      steady_nonmonotone_bridge_disabled_ = false;
      steady_nonmonotone_steps_since_strict_best_ = 0U;
      steady_nonmonotone_envelope_reference_ = -1.0;
      steady_implicit_bridge_ = SteadyImplicitBridgeState{};
      steady_implicit_bridge_.cfl =
          steady_implicit_bridge_bounded_initial_cfl(
              bridge_minimum_cfl, config_.run_control.cfl_max);
      // Fallback residuals and mode are fixed-operator evidence. Every ramp
      // increment changes that operator, not only the first transition.
      steady_fallback_mode_ = false;
      steady_fallback_residual_window_.clear();
      steady_fallback_consecutive_accepted_steps_ = 0U;
      steady_fallback_steps_since_jfnk_ = 0U;
    }
    if (transition_started) {
      // The first change in spatial operator restarts conservative CFL growth
      // and gives the JFNK path a fresh attempt before fallback is re-entered.
      cfl_ = result.rescue_accepted ? result.rescue_cfl
                                    : config_.run_control.cfl_initial;
      steady_fallback_mode_ = false;
      steady_fallback_cfl_ = std::min(0.1, steady_fallback_cfl_);
      steady_fallback_residual_window_.clear();
      steady_fallback_steps_since_jfnk_ = 0U;
      steady_jfnk_failure_streak_ = 0;
      steady_rejected_attempts_ = 0;
    } else if (result.rescue_accepted) {
      cfl_ = result.rescue_cfl;
    } else if (result.trust_region_retry_accepted) {
      cfl_ = result.trust_region_retry_accepted_cfl;
      result.cfl = result.trust_region_retry_accepted_cfl;
    }

    if (result.rescue_accepted || result.trust_region_retry_accepted) {
      steady_rescue_reference_residual_ = residual;
      steady_rescue_stagnation_count_ = 0U;
    } else if (!blend_advanced) {
      if (steady_rescue_reference_residual_ < 0.0) {
        steady_rescue_reference_residual_ = residual;
        steady_rescue_stagnation_count_ = 0U;
      } else if (residual <= 0.99 * steady_rescue_reference_residual_) {
        steady_rescue_reference_residual_ = residual;
        steady_rescue_stagnation_count_ = 0U;
      } else {
        ++steady_rescue_stagnation_count_;
      }
    }
  }
  result.first_order_accepted_steps = steady_first_order_accepted_steps_;
  result.order_ramp_accepted_steps = steady_order_ramp_accepted_steps_;
  result.full_order_accepted_steps = steady_full_order_accepted_steps_;
  result.steady_initial_residual_baseline = steady_initial_residual_scale_;
  result.full_order_residual_baseline =
      steady_full_order_initial_residual_;
  result.fallback_mode = steady_fallback_mode_;
  return result;
}

StepResult FlowSolver::transient_step(RestartableSolution& solution) {
  if (config_.run_control.type != RunType::transient) {
    throw std::logic_error("transient_step called for a steady case");
  }
  predictor_rollback_u_workspace_ = solution.U;
  const std::size_t entry_step = solution.physical_step;
  const double entry_time = solution.time;
  double predictor_scale = 0.0;
  if (solution.physical_step > 0U) {
    solution.U = solution.U_n;
    predictor_scale = 1.0;
    bool accepted_predictor = false;
    while (predictor_scale >= 1.0 / 1048576.0) {
      int local_admissible = 1;
      for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
        Conservative candidate{};
        for (std::size_t k = 0; k < 4U; ++k) {
          const std::size_t index = cell * 4U + k;
          candidate[k] = solution.U_n[index] + predictor_scale *
              (solution.U_n[index] - solution.U_nm1[index]);
        }
        if (!residual_.gas().admissible(candidate)) {
          local_admissible = 0;
          break;
        }
      }
      int global_admissible = 0;
      MPI_Allreduce(&local_admissible, &global_admissible, 1, MPI_INT, MPI_MIN,
                    communicator_);
      if (global_admissible != 0) {
        for (std::size_t cell = 0; cell < mesh_.owned_cell_count; ++cell) {
          for (std::size_t k = 0; k < 4U; ++k) {
            const std::size_t index = cell * 4U + k;
            solution.U[index] = solution.U_n[index] + predictor_scale *
                (solution.U_n[index] - solution.U_nm1[index]);
          }
        }
        accepted_predictor = true;
        break;
      }
      predictor_scale *= 0.5;
    }
    if (!accepted_predictor) predictor_scale = 0.0;
  }

  StepResult result;
  try {
    result = implicit_step(solution, true);
  } catch (...) {
    solution.U = predictor_rollback_u_workspace_;
    solution.physical_step = entry_step;
    solution.time = entry_time;
    throw;
  }
  result.predictor_scale = predictor_scale;
  ++transient_samples_;
  transient_iteration_sum_ +=
      static_cast<double>(result.inner.nonlinear_iterations);
  if (transient_samples_ == 1U) {
    transient_stats_.observed_min = result.inner.nonlinear_iterations;
    transient_stats_.observed_max = result.inner.nonlinear_iterations;
  } else {
    transient_stats_.observed_min =
        std::min(transient_stats_.observed_min, result.inner.nonlinear_iterations);
    transient_stats_.observed_max =
        std::max(transient_stats_.observed_max, result.inner.nonlinear_iterations);
  }
  transient_stats_.observed_mean =
      transient_iteration_sum_ / static_cast<double>(transient_samples_);
  transient_stats_.last_ratio = result.inner.nonlinear_ratio;
  if (!result.target_met) {
    ++transient_stats_.target_misses;
  }
  transient_stats_.target_met_fraction =
      static_cast<double>(transient_samples_ - transient_stats_.target_misses) /
      static_cast<double>(transient_samples_);
  if (!result.accepted) {
    solution.U = predictor_rollback_u_workspace_;
    solution.physical_step = entry_step;
    solution.time = entry_time;
    return result;
  }

  // Histories move only here, after the complete nonlinear inner solve.
  solution.U_nm1 = solution.U_n;
  solution.U_n = solution.U;
  ++solution.physical_step;
  solution.time += *config_.run_control.time_step;
  return result;
}

std::vector<StepResult> FlowSolver::run_steady(RestartableSolution& solution) {
  if (!config_.run_control.max_steps.has_value() ||
      !config_.run_control.residual_reduction_target.has_value()) {
    throw std::logic_error("steady controls are incomplete");
  }
  std::vector<StepResult> rows;
  rows.reserve(static_cast<std::size_t>(*config_.run_control.max_steps));
  for (int step = 0; step < *config_.run_control.max_steps; ++step) {
    rows.push_back(steady_step(solution));
    StepResult& current = rows.back();
    if (current.target_met) break;
  }
  return rows;
}

std::vector<StepResult> FlowSolver::run_transient(RestartableSolution& solution) {
  std::vector<StepResult> rows;
  const double final_time = *config_.run_control.final_time;
  const double dt = *config_.run_control.time_step;
  while (solution.time + 0.5 * dt < final_time) {
    StepResult row = transient_step(solution);
    rows.push_back(row);
    if (!row.accepted) break;
  }
  return rows;
}

}  // namespace cfd
