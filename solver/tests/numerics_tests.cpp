#include "cfd/boundary.hpp"
#include "cfd/compensated_sum.hpp"
#include "cfd/config.hpp"
#include "cfd/flux.hpp"
#include "cfd/mesh.hpp"
#include "cfd/partition.hpp"
#include "cfd/reconstruction.hpp"
#include "cfd/residual.hpp"
#include "cfd/solver.hpp"

#include <mpi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error("test assertion failed: " + message);
}

bool close(double left, double right, double tolerance = 1.0e-12) {
  return std::abs(left - right) <=
         tolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

void test_compensated_accumulation() {
  double naive = 0.0;
  cfd::CompensatedSum compensated;
  for (double value : {1.0e16, 1.0, -1.0e16}) {
    naive += value;
    compensated.add(value);
  }
  require(naive == 0.0 && compensated.value() == 1.0,
          "Neumaier accumulation retains a known tiny cancellation remainder");

  cfd::CompensatedSum left;
  cfd::CompensatedSum right;
  for (double contribution : {1.0e16, -3.0, -1.0e16, 2.0, 0.5}) {
    left.add(contribution);
    right.add(-contribution);
  }
  require(left.value() == -right.value() &&
              left.value() + right.value() == 0.0,
          "opposite signed face contributions remain exactly conservative");
}

void test_frozen_rusanov_jacobian() {
  const cfd::CaloricallyPerfectGas gas(1.4, 287.0, 0.72);
  const cfd::Conservative left =
      gas.conservative(gas.complete(1.17, 43.0, -7.0, 93000.0));
  const cfd::Conservative right =
      gas.conservative(gas.complete(0.91, -12.0, 19.0, 71000.0));
  const cfd::Vec2 normal{0.6, 0.8};
  constexpr double length = 1.37;
  constexpr double dissipation = 0.83;
  const cfd::RusanovFaceJacobian blocks =
      cfd::frozen_rusanov_face_jacobian(
          left, right, normal, length, gas, dissipation);
  auto frozen_flux = [&](const cfd::Conservative& ul,
                         const cfd::Conservative& ur) {
    const cfd::Primitive wl = gas.primitive(ul);
    const cfd::Primitive wr = gas.primitive(ur);
    const cfd::Conservative fl = cfd::euler_normal_flux(ul, wl, normal);
    const cfd::Conservative fr = cfd::euler_normal_flux(ur, wr, normal);
    cfd::Conservative value{};
    for (std::size_t row = 0; row < 4U; ++row) {
      value[row] = length *
          (0.5 * (fl[row] + fr[row]) -
           0.5 * dissipation * blocks.frozen_wave_speed *
               (ur[row] - ul[row]));
    }
    return value;
  };
  for (std::size_t column = 0; column < 4U; ++column) {
    const double left_epsilon =
        2.0e-6 * std::max(1.0, std::abs(left[column]));
    cfd::Conservative plus = left;
    cfd::Conservative minus = left;
    plus[column] += left_epsilon;
    minus[column] -= left_epsilon;
    const cfd::Conservative plus_flux = frozen_flux(plus, right);
    const cfd::Conservative minus_flux = frozen_flux(minus, right);
    for (std::size_t row = 0; row < 4U; ++row) {
      const double derivative =
          (plus_flux[row] - minus_flux[row]) / (2.0 * left_epsilon);
      require(close(derivative, blocks.left_left[row * 4U + column], 2.0e-6) &&
                  close(-derivative, blocks.right_left[row * 4U + column], 2.0e-6),
              "frozen Rusanov left conservative block matches centered FD");
    }

    const double right_epsilon =
        2.0e-6 * std::max(1.0, std::abs(right[column]));
    plus = right;
    minus = right;
    plus[column] += right_epsilon;
    minus[column] -= right_epsilon;
    const cfd::Conservative plus_right_flux = frozen_flux(left, plus);
    const cfd::Conservative minus_right_flux = frozen_flux(left, minus);
    for (std::size_t row = 0; row < 4U; ++row) {
      const double derivative =
          (plus_right_flux[row] - minus_right_flux[row]) /
          (2.0 * right_epsilon);
      require(close(derivative, blocks.left_right[row * 4U + column], 2.0e-6) &&
                  close(-derivative, blocks.right_right[row * 4U + column], 2.0e-6),
              "frozen Rusanov right conservative block matches centered FD");
    }
  }

  const cfd::RusanovFaceJacobian reversed =
      cfd::frozen_rusanov_face_jacobian(
          right, left, -1.0 * normal, length, gas, dissipation);
  for (std::size_t entry = 0; entry < 16U; ++entry) {
    require(close(reversed.left_left[entry], blocks.right_right[entry]) &&
                close(reversed.left_right[entry], blocks.right_left[entry]) &&
                close(reversed.right_left[entry], blocks.left_right[entry]) &&
                close(reversed.right_right[entry], blocks.left_left[entry]),
            "Rusanov face blocks preserve left/right normal reversal");
  }
}

cfd::CaseConfig base_config(cfd::RunType type = cfd::RunType::steady) {
  cfd::CaseConfig config;
  config.physics.mode = cfd::PhysicsMode::inviscid;
  config.gas = cfd::GasConfig{"calorically_perfect", 1.4, 1.0, 0.72};
  config.freestream =
      cfd::FreestreamConfig{0.2, 0.0, 1.0, 1.0, 1.0 / (1.4 * 0.2 * 0.2)};
  config.reference.length = 1.0;
  config.reference.area = 1.0;
  config.reference.reynolds_length = 1.0;
  config.boundary_conditions["FAR"] = cfd::BoundaryCondition::farfield;
  config.numerics_required.spatial_order = 2;
  config.run_control.type = type;
  config.run_control.min_inner_iterations = type == cfd::RunType::transient ? 5 : 3;
  config.run_control.max_inner_iterations = type == cfd::RunType::transient ? 5 : 20;
  config.run_control.inner_residual_reduction_target =
      type == cfd::RunType::transient ? 1.0e-3 : 1.0e-2;
  config.run_control.cfl_initial = 1.0;
  config.run_control.cfl_max = type == cfd::RunType::transient ? 1.0 : 10.0;
  config.run_control.pseudo_cfl_ramp_steps = type == cfd::RunType::transient ? 0 : 10;
  config.run_control.max_steps = 10;
  config.run_control.residual_reduction_target = 3.0;
  if (type == cfd::RunType::transient) {
    config.run_control.time_step = 0.01;
    config.run_control.final_time = 0.02;
    config.numerics_required.transient_order = 2;
  }
  return config;
}

cfd::DistributedMesh one_cell_mesh(int rank, int size) {
  cfd::DistributedMesh mesh;
  mesh.rank = rank;
  mesh.size = size;
  mesh.global_cell_count = static_cast<std::size_t>(size);
  mesh.global_face_count = static_cast<std::size_t>(4 * size);
  mesh.global_vertex_count = static_cast<std::size_t>(4 * size);
  mesh.owned_cell_count = 1;
  cfd::LocalCell cell;
  cell.global_id = rank;
  cell.owner = rank;
  cell.owned = true;
  cell.center = {0.0, 0.0};
  cell.area = 1.0;
  cell.faces = {0, 1, 2, 3};
  mesh.cells.push_back(cell);
  const std::array<cfd::Vec2, 4> centers{
      cfd::Vec2{0.5, 0.0}, cfd::Vec2{0.0, 0.5},
      cfd::Vec2{-0.5, 0.0}, cfd::Vec2{0.0, -0.5}};
  const std::array<cfd::Vec2, 4> normals{
      cfd::Vec2{1.0, 0.0}, cfd::Vec2{0.0, 1.0},
      cfd::Vec2{-1.0, 0.0}, cfd::Vec2{0.0, -1.0}};
  for (std::size_t i = 0; i < 4U; ++i) {
    cfd::LocalFace face;
    face.global_id = static_cast<cfd::GlobalId>(4 * rank + static_cast<int>(i));
    face.left_cell = 0;
    face.center = centers[i];
    face.normal = normals[i];
    face.length = 1.0;
    face.boundary = "FAR";
    mesh.faces.push_back(face);
  }
  return mesh;
}

cfd::DistributedMesh cross_mesh(int rank, int size) {
  cfd::DistributedMesh mesh;
  mesh.rank = rank;
  mesh.size = size;
  mesh.global_cell_count = static_cast<std::size_t>(5 * size);
  mesh.owned_cell_count = 5;
  const std::array<cfd::Vec2, 5> centers{
      cfd::Vec2{0.0, 0.0}, cfd::Vec2{1.0, 0.0}, cfd::Vec2{-1.0, 0.0},
      cfd::Vec2{0.0, 1.0}, cfd::Vec2{0.0, -1.0}};
  for (std::size_t i = 0; i < centers.size(); ++i) {
    cfd::LocalCell cell;
    cell.global_id = static_cast<cfd::GlobalId>(5 * rank + static_cast<int>(i));
    cell.owner = rank;
    cell.owned = true;
    cell.center = centers[i];
    cell.area = 1.0;
    mesh.cells.push_back(cell);
  }
  for (std::size_t neighbor = 1; neighbor < centers.size(); ++neighbor) {
    cfd::LocalFace face;
    face.global_id = static_cast<cfd::GlobalId>(4 * rank + static_cast<int>(neighbor - 1U));
    face.left_cell = 0;
    face.right_cell = static_cast<cfd::LocalIndex>(neighbor);
    face.center = 0.5 * centers[neighbor];
    face.normal = centers[neighbor];
    face.length = 1.0;
    const cfd::LocalIndex face_index = static_cast<cfd::LocalIndex>(mesh.faces.size());
    mesh.faces.push_back(face);
    mesh.cells[0].faces.push_back(face_index);
    mesh.cells[neighbor].faces.push_back(face_index);
  }
  mesh.global_face_count = static_cast<std::size_t>(4 * size);
  return mesh;
}

cfd::DistributedMesh flat_wall_mesh(int rank, int size) {
  cfd::DistributedMesh mesh;
  mesh.rank = rank;
  mesh.size = size;
  mesh.global_cell_count = static_cast<std::size_t>(4 * size);
  mesh.global_face_count = static_cast<std::size_t>(4 * size);
  mesh.owned_cell_count = 4;
  const std::array<cfd::Vec2, 4> centers{
      cfd::Vec2{0.0, 0.5}, cfd::Vec2{-1.0, 0.5},
      cfd::Vec2{1.0, 0.5}, cfd::Vec2{0.0, 1.5}};
  for (std::size_t cell = 0; cell < centers.size(); ++cell) {
    cfd::LocalCell local;
    local.global_id = static_cast<cfd::GlobalId>(4 * rank + static_cast<int>(cell));
    local.owner = rank;
    local.owned = true;
    local.center = centers[cell];
    local.area = 1.0;
    mesh.cells.push_back(local);
  }
  for (std::size_t neighbor = 1; neighbor < centers.size(); ++neighbor) {
    cfd::LocalFace face;
    face.global_id = static_cast<cfd::GlobalId>(4 * rank +
                                               static_cast<int>(neighbor - 1U));
    face.left_cell = 0;
    face.right_cell = static_cast<cfd::LocalIndex>(neighbor);
    face.center = 0.5 * (centers[0] + centers[neighbor]);
    face.normal = centers[neighbor] - centers[0];
    face.length = 1.0;
    const cfd::LocalIndex index =
        static_cast<cfd::LocalIndex>(mesh.faces.size());
    mesh.faces.push_back(face);
    mesh.cells[0].faces.push_back(index);
    mesh.cells[neighbor].faces.push_back(index);
  }
  cfd::LocalFace wall;
  wall.global_id = static_cast<cfd::GlobalId>(4 * rank + 3);
  wall.left_cell = 0;
  wall.center = {0.0, 0.0};
  wall.normal = {0.0, -1.0};
  wall.length = 1.0;
  wall.boundary = "WALL";
  mesh.cells[0].faces.push_back(
      static_cast<cfd::LocalIndex>(mesh.faces.size()));
  mesh.faces.push_back(wall);
  return mesh;
}

void test_restarted_gmres(int rank, int size) {
  const cfd::DistributedMesh mesh = one_cell_mesh(rank, size);
  std::vector<double> rhs(mesh.cells.size() * 4U, 0.0);
  for (std::size_t component = 0; component < 4U; ++component) {
    rhs[component] = (1.0 + 0.25 * static_cast<double>(rank)) *
                     static_cast<double>(component + 1U);
  }
  const cfd::DistributedLinearAction apply =
      [&](const std::vector<double>& input, std::vector<double>& output) {
        output.assign(input.size(), 0.0);
        for (std::size_t component = 0; component < 4U; ++component) {
          output[component] = static_cast<double>(component + 1U) * input[component];
        }
      };
  const cfd::DistributedLinearAction identity =
      [](const std::vector<double>& input, std::vector<double>& output) {
        output = input;
      };
  cfd::MatrixFreeGmresOptions options;
  options.restart = 6;
  options.minimum_iterations = 3;
  options.maximum_iterations = 12;
  options.relative_tolerance = 1.0e-11;
  std::vector<double> solution;
  const cfd::MatrixFreeGmresResult result = cfd::restarted_gmres(
      mesh, rhs, apply, identity, options, MPI_COMM_WORLD, solution);
  require(result.converged && result.iterations >= 3 && result.iterations <= 6 &&
              result.residual_ratio < 1.0e-11,
          "restarted GMRES reaches its global reduction target");
  for (std::size_t component = 0; component < 4U; ++component) {
    require(close(solution[component], rhs[component] /
                                         static_cast<double>(component + 1U),
                  2.0e-11),
            "distributed GMRES solution matches the diagonal oracle");
  }

  const cfd::DistributedLinearAction diagonal_inverse =
      [](const std::vector<double>& input, std::vector<double>& output) {
        output.assign(input.size(), 0.0);
        for (std::size_t component = 0; component < 4U; ++component) {
          output[component] = input[component] /
                              static_cast<double>(component + 1U);
        }
      };
  options.minimum_iterations = 1;
  std::vector<double> preconditioned_solution;
  const cfd::MatrixFreeGmresResult preconditioned = cfd::restarted_gmres(
      mesh, rhs, apply, diagonal_inverse, options, MPI_COMM_WORLD,
      preconditioned_solution);
  require(preconditioned.converged && preconditioned.iterations == 1 &&
              preconditioned.residual_ratio < 1.0e-12,
          "nontrivial right preconditioner acts inside the Krylov operator");
  for (std::size_t component = 0; component < 4U; ++component) {
    require(close(preconditioned_solution[component],
                  rhs[component] / static_cast<double>(component + 1U),
                  2.0e-11),
            "right-preconditioned GMRES returns the physical correction");
  }
}

std::vector<double> state_from_primitive(const cfd::DistributedMesh& mesh,
                                         const cfd::CaloricallyPerfectGas& gas,
                                         const std::vector<cfd::Primitive>& primitive) {
  std::vector<double> result(mesh.cells.size() * 4U);
  for (std::size_t cell = 0; cell < mesh.cells.size(); ++cell) {
    const cfd::Conservative U = gas.conservative(primitive[cell]);
    for (std::size_t k = 0; k < 4U; ++k) result[cell * 4U + k] = U[k];
  }
  return result;
}

void test_gas_and_inviscid_flux() {
  const cfd::CaloricallyPerfectGas gas(1.4, 287.0, 0.71);
  const cfd::Primitive original = gas.complete(1.23, 42.0, -9.0, 101325.0);
  const cfd::Conservative U = gas.conservative(original);
  const cfd::Primitive round_trip = gas.primitive(U);
  require(close(round_trip.rho, original.rho) && close(round_trip.u, original.u) &&
              close(round_trip.v, original.v) && close(round_trip.p, original.p),
          "strict primitive/conservative round trip");
  cfd::Conservative bad = U;
  bad[3] = 0.0;
  require(!gas.admissible(bad), "negative internal energy is inadmissible");
  cfd::FreestreamConfig inconsistent{0.5, 0.0, 1.0, 1.0, 1.0};
  bool rejected_mach = false;
  try {
    (void)gas.freestream(inconsistent);
  } catch (const std::invalid_argument&) {
    rejected_mach = true;
  }
  require(rejected_mach, "inconsistent supplied Mach is rejected");
  cfd::FreestreamConfig rounded{0.0, 0.0, 1.0, 1.0, 100000.0};
  rounded.mach = rounded.velocity_magnitude /
                 std::sqrt(1.4 * rounded.pressure / rounded.rho);
  rounded.mach *= 1.0 + 0.5 * cfd::freestream_mach_relative_tolerance;
  require(gas.admissible(gas.freestream(rounded)),
          "Mach validation admits documented relative input rounding");
  rounded.mach /= 1.0 + 0.5 * cfd::freestream_mach_relative_tolerance;
  rounded.mach *= 1.0 + 2.0 * cfd::freestream_mach_relative_tolerance;
  rejected_mach = false;
  try {
    (void)gas.freestream(rounded);
  } catch (const std::invalid_argument&) {
    rejected_mach = true;
  }
  require(rejected_mach, "Mach validation rejects mismatch beyond documented tolerance");

  const cfd::Primitive reconstructed =
      gas.complete(0.31, -12.0, 6.0, 42000.0);
  const cfd::Primitive first_order = cfd::blend_reconstructed_primitive(
      original, reconstructed, 0.0, gas);
  const cfd::Primitive second_order = cfd::blend_reconstructed_primitive(
      original, reconstructed, 1.0, gas);
  const cfd::Primitive quarter = cfd::blend_reconstructed_primitive(
      original, reconstructed, 0.25, gas);
  require(first_order.rho == original.rho && first_order.u == original.u &&
              first_order.v == original.v && first_order.p == original.p &&
              second_order.rho == reconstructed.rho &&
              second_order.u == reconstructed.u &&
              second_order.v == reconstructed.v &&
              second_order.p == reconstructed.p,
          "reconstruction blend preserves exact first- and second-order endpoints");
  require(close(quarter.rho, 0.75 * original.rho + 0.25 * reconstructed.rho) &&
              close(quarter.u, 0.75 * original.u + 0.25 * reconstructed.u) &&
              close(quarter.v, 0.75 * original.v + 0.25 * reconstructed.v) &&
              close(quarter.p, 0.75 * original.p + 0.25 * reconstructed.p) &&
              gas.admissible(quarter),
          "intermediate reconstruction blend is algebraically linear and positive");
  bool rejected_blend = false;
  try {
    (void)cfd::blend_reconstructed_primitive(
        original, reconstructed, 1.01, gas);
  } catch (const std::invalid_argument&) {
    rejected_blend = true;
  }
  require(rejected_blend, "reconstruction blend rejects values outside [0,1]");

  const cfd::Vec2 normal{0.6, 0.8};
  const cfd::NumericalFlux consistent = cfd::rusanov_flux(U, U, normal, gas, 1.7);
  const cfd::Conservative physical = cfd::euler_normal_flux(U, original, normal);
  for (std::size_t k = 0; k < 4U; ++k) {
    require(close(consistent.value[k], physical[k]), "Rusanov exact consistency");
  }
  const cfd::Conservative right = gas.conservative(gas.complete(0.9, -3.0, 7.0, 90000.0));
  const cfd::NumericalFlux lr = cfd::rusanov_flux(U, right, normal, gas, 0.8);
  const cfd::NumericalFlux rl = cfd::rusanov_flux(right, U, -1.0 * normal, gas, 0.8);
  for (std::size_t k = 0; k < 4U; ++k) {
    require(close(lr.value[k], -rl.value[k]), "left/right-normal antisymmetry");
  }
}

void test_boundary_exterior_states() {
  const cfd::CaloricallyPerfectGas gas(1.4, 1.0, 0.72);
  const cfd::Vec2 normal{0.6, 0.8};
  const cfd::Vec2 tangent{-0.8, 0.6};
  const cfd::Primitive interior = gas.complete(1.2, 2.0, -0.5, 3.0);
  const cfd::Primitive freestream = gas.complete(0.9, -0.2, 0.4, 2.0);
  const cfd::Primitive slip = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::slip_wall, interior, freestream, normal, gas);
  const double interior_normal = interior.u * normal.x + interior.v * normal.y;
  const double slip_normal = slip.u * normal.x + slip.v * normal.y;
  require(close(slip.rho, interior.rho) && close(slip.p, interior.p) &&
              close(slip_normal, -interior_normal) &&
              close(slip.u * tangent.x + slip.v * tangent.y,
                    interior.u * tangent.x + interior.v * tangent.y),
          "slip exterior reflects only normal velocity");
  const cfd::Primitive no_slip = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::no_slip_adiabatic_wall, interior,
      freestream, normal, gas);
  require(close(no_slip.rho, interior.rho) && close(no_slip.p, interior.p) &&
              close(no_slip.T, interior.T) && close(no_slip.u, -interior.u) &&
              close(no_slip.v, -interior.v),
          "no-slip adiabatic exterior has zero midpoint velocity and symmetric T");

  const cfd::Vec2 x_normal{1.0, 0.0};
  const cfd::Primitive unit_free = gas.complete(1.0, 0.1, 0.4, 1.0 / 1.4);
  const cfd::Primitive supersonic_in = gas.complete(1.0, -2.0, 0.3, 1.0 / 1.4);
  const cfd::Primitive inflow = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::farfield, supersonic_in, unit_free,
      x_normal, gas);
  require(close(inflow.rho, unit_free.rho) && close(inflow.u, unit_free.u) &&
              close(inflow.v, unit_free.v) && close(inflow.p, unit_free.p),
          "supersonic farfield inflow takes the complete freestream state");
  const cfd::Primitive supersonic_out = gas.complete(1.1, 2.0, -0.2, 1.1 / 1.4);
  const cfd::Primitive outflow = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::farfield, supersonic_out, unit_free,
      x_normal, gas);
  require(close(outflow.rho, supersonic_out.rho) &&
              close(outflow.u, supersonic_out.u) &&
              close(outflow.v, supersonic_out.v) &&
              close(outflow.p, supersonic_out.p),
          "supersonic farfield outflow takes the complete interior state");

  const cfd::Primitive subsonic_interior =
      gas.complete(1.2, 0.2, -0.3, 1.2 / 1.4);
  const cfd::Primitive subsonic = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::farfield, subsonic_interior, unit_free,
      x_normal, gas);
  const double gamma_minus_one = gas.gamma() - 1.0;
  require(close(subsonic.u + 2.0 * subsonic.a / gamma_minus_one,
                subsonic_interior.u +
                    2.0 * subsonic_interior.a / gamma_minus_one) &&
              close(subsonic.u - 2.0 * subsonic.a / gamma_minus_one,
                    unit_free.u - 2.0 * unit_free.a / gamma_minus_one) &&
              close(subsonic.v, subsonic_interior.v) &&
              close(subsonic.p / std::pow(subsonic.rho, gas.gamma()),
                    subsonic_interior.p /
                        std::pow(subsonic_interior.rho, gas.gamma())),
          "subsonic outflow combines R+/R- and exports entropy/tangential velocity");
  const cfd::Primitive incoming_free = gas.complete(0.95, -0.4, 0.7, 0.95 / 1.4);
  const cfd::Primitive incoming_interior = gas.complete(1.1, -0.2, -0.1, 1.1 / 1.4);
  const cfd::Primitive subsonic_inflow = cfd::boundary_exterior_state(
      cfd::BoundaryCondition::farfield, incoming_interior, incoming_free,
      x_normal, gas);
  require(subsonic_inflow.u < 0.0 && close(subsonic_inflow.v, incoming_free.v) &&
              close(subsonic_inflow.p /
                        std::pow(subsonic_inflow.rho, gas.gamma()),
                    incoming_free.p /
                        std::pow(incoming_free.rho, gas.gamma())),
          "subsonic inflow imports freestream entropy and tangential velocity");
}

void test_boundary_reconstruction(int rank, int size) {
  const cfd::DistributedMesh mesh = flat_wall_mesh(rank, size);
  const cfd::CaloricallyPerfectGas gas(1.4, 1.0, 0.72);
  const cfd::Primitive freestream = gas.complete(1.0, 0.0, 0.0, 2.0);
  std::map<std::string, cfd::BoundaryCondition> boundaries;
  boundaries["WALL"] = cfd::BoundaryCondition::slip_wall;
  std::vector<cfd::Primitive> values;
  for (const cfd::LocalCell& cell : mesh.cells) {
    values.push_back(gas.complete(1.0, 2.0 + 0.3 * cell.center.x,
                                  0.0, 2.0));
  }
  std::vector<double> state = state_from_primitive(mesh, gas, values);
  cfd::PrimitiveReconstruction slip_reconstruction(
      mesh, gas, boundaries, freestream, MPI_COMM_WORLD);
  const cfd::ReconstructionData& slip = slip_reconstruction.compute(state);
  require(close(slip.gradient[0][1].x, 0.3) &&
              close(slip.gradient[0][1].y, 0.0) &&
              slip.limiter[0][1] > 0.999999,
          "flat slip wall preserves smooth tangential gradient and limiter");
  const cfd::Primitive slip_face =
      slip_reconstruction.face_value(0, {0.0, 0.0});
  require(close(slip_face.u, 2.0) && close(slip_face.v, 0.0),
          "flat slip-wall reconstruction reaches its symmetric face value");

  boundaries["WALL"] = cfd::BoundaryCondition::no_slip_adiabatic_wall;
  values.clear();
  for (const cfd::LocalCell& cell : mesh.cells) {
    values.push_back(gas.complete(1.0, 2.0 * cell.center.y,
                                  -cell.center.y, 2.0));
  }
  state = state_from_primitive(mesh, gas, values);
  cfd::PrimitiveReconstruction no_slip_reconstruction(
      mesh, gas, boundaries, freestream, MPI_COMM_WORLD);
  const cfd::ReconstructionData& no_slip = no_slip_reconstruction.compute(state);
  require(close(no_slip.gradient[0][1].y, 2.0) &&
              close(no_slip.gradient[0][2].y, -1.0) &&
              no_slip.limiter[0][1] > 0.999999 &&
              no_slip.limiter[0][2] > 0.999999,
          "flat no-slip ghost recovers manufactured normal velocity gradients");
  const cfd::Primitive no_slip_face =
      no_slip_reconstruction.face_value(0, {0.0, 0.0});
  require(std::abs(no_slip_face.u) < 1.0e-12 &&
              std::abs(no_slip_face.v) < 1.0e-12,
          "no-slip reconstructed midpoint velocity is zero");
}

void test_venkatakrishnan_limiter(int rank, int size) {
  const double smooth_positive =
      cfd::venkatakrishnan_face_limiter(0.0, 1.0e-6, 1.0e-6);
  const double smooth_negative =
      cfd::venkatakrishnan_face_limiter(0.0, -1.0e-6, 1.0e-6);
  require(smooth_positive > 0.999 && close(smooth_positive, smooth_negative) &&
              cfd::venkatakrishnan_face_limiter(1.0, 0.0, 1.0e-6) == 1.0 &&
              cfd::venkatakrishnan_face_limiter(0.0, 1.0, 0.0) == 0.0,
          "Venkatakrishnan limiter is smooth and sign-symmetric at extrema");
  for (double allowed : {0.0, 0.1, 1.0, 10.0}) {
    for (double increment : {-2.0, -0.1, 0.0, 0.1, 2.0}) {
      const double phi =
          cfd::venkatakrishnan_face_limiter(allowed, increment, 1.0e-8);
      require(std::isfinite(phi) && phi >= 0.0 && phi <= 1.0,
              "Venkatakrishnan multiplier is bounded");
    }
  }

  const double below_transition =
      cfd::shock_limited_face_limiter(0.8, 0.4, 1.5);
  const double transition_midpoint =
      cfd::shock_limited_face_limiter(0.8, 0.4, 1.625);
  const double strong_shock =
      cfd::shock_limited_face_limiter(0.8, 0.4, 1.75);
  const double just_below_strong =
      cfd::shock_limited_face_limiter(0.8, 0.4, 1.75 - 1.0e-8);
  require(close(below_transition, 0.8) && close(transition_midpoint, 0.6) &&
              close(strong_shock, 0.4) &&
              std::abs(just_below_strong - strong_shock) < 1.0e-12,
          "shock limiter transition is smooth and reaches the strict bound");
  for (double jump_ratio : {1.0, 1.5, 1.6, 1.7, 1.75, 3.0}) {
    const double limited =
        cfd::shock_limited_face_limiter(0.8, 0.4, jump_ratio);
    require(limited <= 0.8 && limited >= 0.4,
            "shock transition never weakens the smooth limiter");
  }

  const cfd::CaloricallyPerfectGas gas(1.4, 1.0, 0.72);
  const cfd::Primitive freestream = gas.complete(1.0, 0.0, 0.0, 2.0);
  const std::map<std::string, cfd::BoundaryCondition> boundaries;
  {
    const cfd::DistributedMesh mesh = cross_mesh(rank, size);
    std::vector<cfd::Primitive> values(
        mesh.cells.size(), gas.complete(1.0, 0.0, 0.0, 2.0));
    values[0] = gas.complete(1.0, 1.0, 0.0, 2.0);
    values[1] = gas.complete(1.0, 1.0 - 1.0e-6, 0.0, 2.0);
    values[2] = gas.complete(1.0, 1.0 - 2.0e-6, 0.0, 2.0);
    values[3] = gas.complete(1.0, 1.0 - 2.0e-6, 0.0, 2.0);
    values[4] = gas.complete(1.0, 1.0 - 2.0e-6, 0.0, 2.0);
    std::vector<double> state = state_from_primitive(mesh, gas, values);
    cfd::PrimitiveReconstruction reconstruction(
        mesh, gas, boundaries, freestream, MPI_COMM_WORLD, 100.0);
    const cfd::ReconstructionData& data = reconstruction.compute(state);
    const cfd::Primitive face = reconstruction.face_value(0, {0.5, 0.0});
    require(data.limiter[0][1] > 0.99 && face.u >= values[1].u &&
                face.u <= values[0].u + 1.0e-6,
            "smooth local extremum is regularized without an active-set cutoff");
  }

  {
    const cfd::DistributedMesh mesh = cross_mesh(rank, size);
    std::vector<cfd::Primitive> values(
        mesh.cells.size(), gas.complete(1.0, 0.0, 0.0, 2.0));
    values[1] = gas.complete(1.0, 1.0, 0.0, 2.0);
    values[2] = gas.complete(1.0, -0.5, 0.0, 2.0);
    std::vector<double> state = state_from_primitive(mesh, gas, values);
    cfd::PrimitiveReconstruction reconstruction(
        mesh, gas, boundaries, freestream, MPI_COMM_WORLD, 100.0);
    reconstruction.compute(state);
    const cfd::Primitive positive_face =
        reconstruction.face_value(0, {0.5, 0.0});
    const cfd::Primitive negative_face =
        reconstruction.face_value(0, {-0.5, 0.0});
    require(positive_face.u >= -0.5 && positive_face.u <= 1.0 &&
                negative_face.u >= -0.5 && negative_face.u <= 1.0,
            "smooth limiter keeps non-extremal faces within local velocity bounds");
  }

  double previous_error = std::numeric_limits<double>::infinity();
  for (double spacing : {1.0, 0.5, 0.25}) {
    cfd::DistributedMesh mesh = cross_mesh(rank, size);
    for (cfd::LocalCell& cell : mesh.cells) {
      cell.center = spacing * cell.center;
      cell.area *= spacing * spacing;
    }
    for (cfd::LocalFace& face : mesh.faces) {
      face.center = spacing * face.center;
      face.length *= spacing;
    }
    std::vector<cfd::Primitive> values;
    for (const cfd::LocalCell& cell : mesh.cells) {
      values.push_back(gas.complete(2.0 + 0.1 * cell.center.x -
                                             0.05 * cell.center.y,
                                    3.0 + 0.4 * cell.center.x +
                                             0.2 * cell.center.y,
                                    -1.0 + 0.3 * cell.center.x -
                                               0.6 * cell.center.y,
                                    10.0 + 0.7 * cell.center.x +
                                               0.9 * cell.center.y));
    }
    std::vector<double> state = state_from_primitive(mesh, gas, values);
    cfd::PrimitiveReconstruction reconstruction(
        mesh, gas, boundaries, freestream, MPI_COMM_WORLD, 1.0);
    const cfd::ReconstructionData& data = reconstruction.compute(state);
    double error = 0.0;
    for (const cfd::LocalFace& face : mesh.faces) {
      if (face.left_cell != 0) continue;
      const cfd::Primitive value = reconstruction.face_value(0, face.center);
      error = std::max(error, std::abs(
          value.p - (10.0 + 0.7 * face.center.x + 0.9 * face.center.y)));
    }
    require(error < 2.0e-12 && data.limiter[0][3] > 1.0 - 1.0e-12 &&
                error <= previous_error + 2.0e-12,
            "linear field remains second-order exact under mesh refinement");
    previous_error = error;
  }
}

void test_reconstruction(int rank, int size) {
  const cfd::DistributedMesh mesh = cross_mesh(rank, size);
  const cfd::CaloricallyPerfectGas gas(1.4, 1.0, 0.72);
  std::vector<cfd::Primitive> values;
  values.assign(mesh.cells.size(), gas.complete(1.0, 2.0, -1.0, 10.0));
  std::vector<double> U = state_from_primitive(mesh, gas, values);
  const cfd::Primitive freestream = gas.complete(1.0, 2.0, -1.0, 10.0);
  std::map<std::string, cfd::BoundaryCondition> boundaries;
  cfd::PrimitiveReconstruction reconstruction(
      mesh, gas, boundaries, freestream, MPI_COMM_WORLD);
  const cfd::ReconstructionData& constant_data = reconstruction.compute(U);
  for (const cfd::Vec2& gradient : constant_data.gradient[0]) {
    require(gradient.x == 0.0 && gradient.y == 0.0,
            "constant primitive field has exact zero gradient");
  }
  values.clear();
  for (const cfd::LocalCell& cell : mesh.cells) {
    const double x = cell.center.x;
    const double y = cell.center.y;
    values.push_back(gas.complete(2.0 + 0.1 * x - 0.05 * y,
                                  3.0 + 0.4 * x + 0.2 * y,
                                  -1.0 + 0.3 * x - 0.6 * y,
                                  10.0 + 0.7 * x + 0.9 * y));
  }
  U = state_from_primitive(mesh, gas, values);
  const cfd::ReconstructionData& data = reconstruction.compute(U);
  const cfd::PrimitiveGradient& gradient = data.gradient[0];
  require(close(gradient[0].x, 0.1) && close(gradient[0].y, -0.05),
          "linear density gradient recovery");
  require(close(gradient[1].x, 0.4) && close(gradient[1].y, 0.2),
          "linear velocity gradient recovery");
  require(close(gradient[3].x, 0.7) && close(gradient[3].y, 0.9),
          "linear pressure gradient recovery");

  values[0] = gas.complete(2.0, 3.0, -1.0, 10.0);
  values[1] = gas.complete(1.0, 2.0, -1.0, 9.0);
  values[2] = gas.complete(0.2, 1.0, -1.0, 8.0);
  U = state_from_primitive(mesh, gas, values);
  reconstruction.compute(U);
  require(reconstruction.data().diagnostics.shock_fallback_cells > 0U &&
              reconstruction.data().limiter[0][0] <=
                  reconstruction.data().barth_limiter[0][0] &&
              reconstruction.data().limiter[0][0] < 1.0,
          "strong discontinuity activates the stricter Barth-Jespersen fallback");

  auto& mutable_data = const_cast<cfd::ReconstructionData&>(reconstruction.data());
  mutable_data.gradient[0][0] = {-100.0, 0.0};
  mutable_data.gradient[0][3] = {-1000.0, 0.0};
  mutable_data.limiter[0] = {1.0, 1.0, 1.0, 1.0};
  mutable_data.barth_limiter[0] = {1.0, 1.0, 1.0, 1.0};
  mutable_data.minimum[0] = {-1000.0, -1000.0, -1000.0, -10000.0};
  mutable_data.maximum[0] = {1000.0, 1000.0, 1000.0, 10000.0};
  mutable_data.shock_fallback[0] = false;
  const cfd::Primitive positive = reconstruction.face_value(0, {0.5, 0.0});
  require(positive.rho > 0.0 && positive.p > 0.0,
          "face positivity scaling retains admissibility");
  require(reconstruction.data().diagnostics.positivity_scaled > 0U,
           "face positivity scaling is diagnosed");
  require(reconstruction.data().diagnostics.positivity_barth_fallbacks > 0U,
          "inadmissible smooth face retries the stricter Barth limiter");
  mutable_data.gradient[0][1] = {std::numeric_limits<double>::infinity(), 0.0};
  const cfd::Primitive fallback = reconstruction.face_value(0, {0.5, 0.0});
  require(close(fallback.rho, mutable_data.primitive[0].rho) &&
              reconstruction.data().diagnostics.first_order_fallbacks > 0U,
          "inadmissible reconstruction falls back to first order and is diagnosed");
}

void test_viscous_flux() {
  const cfd::CaloricallyPerfectGas gas(1.4, 1.0, 0.7);
  const cfd::Primitive state = gas.complete(1.0, 2.0, -3.0, 5.0);
  const cfd::VelocityTemperatureGradients gradient{
      cfd::Vec2{2.0, 3.0}, cfd::Vec2{4.0, -1.0}, cfd::Vec2{0.5, -0.25}};
  const cfd::ViscousFaceFlux flux =
      cfd::viscous_flux(state, gradient, {1.0, 0.0}, 0.2, gas);
  const double divergence = 1.0;
  require(close(flux.traction.x, 2.0 * 0.2 * 2.0 - (2.0 / 3.0) * 0.2 * divergence),
          "Newtonian normal stress with Stokes hypothesis");
  require(close(flux.traction.y, 0.2 * (3.0 + 4.0)),
          "Newtonian shear stress");
  const cfd::ViscousFaceFlux wall = cfd::no_slip_adiabatic_wall_flux(
      state, gradient, {1.0, 0.0}, 0.5, 0.2, gas);
  require(wall.value[3] == 0.0, "stationary adiabatic wall has exact zero energy flux");
  require(close(cfd::dot(wall.tangential_traction, {1.0, 0.0}), 0.0),
          "wall skin friction is tangential");
}

void test_implicit_and_bdf(int rank, int size) {
  const cfd::Conservative Ustar{1.3, -2.0, 4.2, 10.0};
  const cfd::Conservative Un{1.1, -1.5, 3.7, 9.0};
  const cfd::Conservative Unm1{0.9, -1.0, 3.1, 8.4};
  constexpr double volume = 2.5;
  constexpr double dt = 0.04;
  const cfd::Conservative bdf1 =
      cfd::bdf_physical_residual(Ustar, Un, Unm1, volume, dt, 1);
  const cfd::Conservative bdf2 =
      cfd::bdf_physical_residual(Ustar, Un, Unm1, volume, dt, 2);
  for (std::size_t k = 0; k < 4U; ++k) {
    require(close(bdf1[k], volume * (Ustar[k] - Un[k]) / dt),
            "BDF1 physical residual coefficient oracle");
    require(close(bdf2[k], volume *
                               (3.0 * Ustar[k] - 4.0 * Un[k] + Unm1[k]) /
                               (2.0 * dt)),
            "BDF2 physical residual coefficient oracle");
  }
  require(close(cfd::steady_residual_growth_fraction(0.01), 1.0e-5) &&
              close(cfd::steady_residual_growth_fraction(1.0), 1.0e-3) &&
              close(cfd::steady_residual_growth_fraction(100.0), 1.0e-3),
          "steady nonmonotone allowance is CFL-scaled and tightly bounded");
  require(cfd::steady_residual_within_envelope(1.0, 1.0 + 5.0e-6, 1.0, 0.01),
          "infinitesimal limiter-nonsmooth residual increase is accepted");
  require(!cfd::steady_residual_within_envelope(1.0, 1.0 + 2.0e-5, 1.0, 0.01) &&
              !cfd::steady_residual_within_envelope(1.0, 1.2, 1.0, 1.0),
          "out-of-band and original runaway residual growth are rejected");
  require(!cfd::steady_residual_within_envelope(1.0095, 1.0101, 1.0, 1.0),
          "cumulative repeated growth cannot escape the global-best envelope");
  cfd::SteadyCflAdaptationState plateau_probe{
      0.01, 1.0, 3, false, 0, 0.0, false};
  cfd::update_steady_cfl_adaptation(
      plateau_probe, true, false, false, 1.00001, 0.01, 2.0);
  require(close(plateau_probe.cfl, 0.01) && !plateau_probe.probe_active &&
               plateau_probe.trend_samples == 0,
           "near-stagnation does not schedule recovery probes");
  cfd::SteadyCflAdaptationState oscillatory_plateau{
      0.1, 1.0, 3, false, 0, 0.0, false};
  cfd::update_steady_cfl_adaptation(
      oscillatory_plateau, true, true, false, 1.0005, 0.01, 2.0);
  require(close(oscillatory_plateau.cfl, 0.09) &&
               !oscillatory_plateau.probe_active,
           "normal-mode positive drift contracts without a recovery probe");
  cfd::update_steady_cfl_adaptation(
      plateau_probe, false, false, false, 0.0, 0.01, 2.0);
  require(close(plateau_probe.cfl, 0.01) && !plateau_probe.probe_active &&
              plateau_probe.trend_reference_residual < 0.0,
          "failed plateau probe halves CFL and resets probe trend state");
  cfd::SteadyCflAdaptationState rejected_floor{
      0.01, -1.0, 0, false, 3, 0.0, false};
  cfd::update_steady_cfl_adaptation(
      rejected_floor, false, false, false, 0.0, 0.01, 2.0);
  require(close(rejected_floor.cfl, 0.01) && !rejected_floor.probe_active &&
               rejected_floor.rejected_attempts == 4 &&
               !rejected_floor.recovery_restore_pending,
           "repeated floor rejections never schedule state restoration");
  cfd::SteadyCflAdaptationState larger_recovery{
      0.01, -1.0, 0, false, 3, 0.08, false};
  cfd::update_steady_cfl_adaptation(
      larger_recovery, false, false, false, 0.0, 0.01, 0.1);
  require(close(larger_recovery.cfl, 0.01) &&
              !larger_recovery.probe_active &&
              larger_recovery.recovery_probe_cfl == 0.0,
          "legacy recovery state is cleared rather than probed");
  cfd::SteadyCflAdaptationState small_trend{
      0.1, 1.0, 3, false, 0, 0.0, false};
  cfd::update_steady_cfl_adaptation(
      small_trend, true, false, false, 0.9998, 0.01, 2.0);
  require(close(small_trend.cfl, 0.125) && !small_trend.probe_active,
           "robust small window decrease regrows CFL without the old 0.5 percent gate");

  const std::vector<double> bounded_window{1.0, 1.2, 1.5};
  require(close(cfd::steady_fallback_runaway_bound(
                    bounded_window, 1.0, 1.0, 1.5),
                3.0) &&
              cfd::steady_fallback_within_runaway_bound(
                  bounded_window, 1.0, 1.0, 1.5, 1.8) &&
              cfd::steady_fallback_within_runaway_bound(
                  bounded_window, 1.0, 1.0, 1.8, 0.8),
          "bounded pseudo-time envelope permits a temporary rise followed by decay");
  require(!cfd::steady_fallback_within_runaway_bound(
               bounded_window, 1.0, 1.0, 1.5, 3.01) &&
              !cfd::steady_fallback_within_runaway_bound(
                  {6.0}, 1.0, 0.5, 6.0, 10.01) &&
              !cfd::steady_fallback_within_runaway_bound(
                  bounded_window, 1.0, 1.0, 1.5,
                  std::numeric_limits<double>::infinity()),
            "pseudo-time envelope rejects recent-window, trusted-scale, and nonfinite runaway");

  const std::vector<double> retry_decades =
      cfd::steady_trust_region_retry_cfls(0.01, 100.0);
  require(retry_decades == std::vector<double>({0.1, 1.0, 10.0}) &&
              cfd::steady_trust_region_retry_cfls(0.1, 100.0) ==
                  std::vector<double>({1.0, 10.0}) &&
              cfd::steady_trust_region_retry_cfls(1.0, 1.0).empty(),
          "trust-region retry schedule contains every strict intermediate decade");
  const auto best_retry = cfd::best_strict_residual_decrease(
      1.0, {1.1, 0.7, 1.2, std::numeric_limits<double>::infinity()});
  const auto tied_retry =
      cfd::best_strict_residual_decrease(1.0, {0.8, 0.8, 0.9});
  require(best_retry == std::optional<std::size_t>(1U) &&
              tied_retry == std::optional<std::size_t>(0U) &&
              !cfd::best_strict_residual_decrease(1.0, {1.0, 1.1}).has_value() &&
              !cfd::best_strict_residual_decrease(
                   1.0, {std::numeric_limits<double>::quiet_NaN()})
                   .has_value(),
          "strict globalization selects decrease and rejects equal, growing, and nonfinite trials");
  require(cfd::strict_steady_merit_decrease(1.0, 0.99, 0.01) &&
              cfd::strict_steady_merit_decrease(3.8e-8, 3.79e-8, 1.0e-16) &&
              !cfd::strict_steady_merit_decrease(1.0, 1.0, 0.01) &&
              !cfd::strict_steady_merit_decrease(1.0, 1.01, 0.01) &&
              !cfd::strict_steady_merit_decrease(1.0, 0.99, -0.01) &&
              !cfd::strict_steady_merit_decrease(
                  1.0, std::numeric_limits<double>::quiet_NaN(), 0.01),
           "ordinary steady globalization rejects every non-decrease, including at low residual");

  std::vector<double> nonmonotone_window;
  for (int sample = 0;
       sample <= static_cast<int>(cfd::steady_nonmonotone_window_capacity);
       ++sample) {
    cfd::steady_nonmonotone_push_residual(
        nonmonotone_window, 1.0 - 0.01 * static_cast<double>(sample));
  }
  require(nonmonotone_window.size() ==
                  cfd::steady_nonmonotone_window_capacity &&
              close(nonmonotone_window.front(), 0.99) &&
              close(nonmonotone_window.back(), 0.90) &&
              close(cfd::steady_nonmonotone_reference(
                        nonmonotone_window, 0.89),
                    0.99),
          "nonmonotone window retains the latest ten accepted residuals and uses their maximum");
  require(cfd::steady_nonmonotone_trial_acceptable(
              1.01, 1.005, 1.0, 0.01) &&
              !cfd::steady_nonmonotone_trial_acceptable(
                  1.02, 1.0100001, 1.0, 0.01) &&
              !cfd::steady_nonmonotone_trial_acceptable(
                  1.0, 1.0, 1.0, 0.01),
          "nonmonotone Armijo permits bounded window-relative increase but enforces the 1.01 best cap");
  cfd::GlobalDiagnostics smooth_limiter_diagnostics;
  smooth_limiter_diagnostics.venkatakrishnan_limited_face_components = 17U;
  require(cfd::steady_limiter_nonlinearity_active(
              smooth_limiter_diagnostics) &&
              smooth_limiter_diagnostics.shock_fallback_cells == 0U,
          "smooth Venkat-limited full-order flow is limiter-active without shock fallback");
  cfd::GlobalDiagnostics zero_limiter_diagnostics;
  require(!cfd::steady_limiter_nonlinearity_active(
               zero_limiter_diagnostics) &&
              cfd::steady_nonmonotone_eligible(
                  true, true, 1.0, std::vector<double>{1.0}) &&
              !cfd::steady_nonmonotone_eligible(
                  false, true, 1.0, std::vector<double>{1.0}),
          "full-order stagnation enables nonmonotone globalization even when shock and limiter diagnostics are zero");
  const double plateau_best = 3.4753818030399364e-05;
  const double noise_scale_best = std::nextafter(plateau_best, 0.0);
  const double meaningful_best =
      plateau_best * (1.0 - 2.0 *
                                cfd::steady_meaningful_best_relative_decrease);
  require(noise_scale_best < plateau_best &&
              !cfd::steady_meaningful_best_decrease(
                  plateau_best, noise_scale_best) &&
              cfd::steady_meaningful_best_decrease(
                  plateau_best, meaningful_best),
          "noise-scale pseudo-best is distinct from a meaningful strict best");
  std::size_t stagnation = cfd::steady_nonmonotone_stagnation_attempts - 1U;
  stagnation = cfd::steady_nonmonotone_stagnation_after_attempt(
      stagnation, true,
      cfd::steady_meaningful_best_decrease(plateau_best, noise_scale_best));
  require(stagnation == cfd::steady_nonmonotone_stagnation_attempts &&
              cfd::steady_nonmonotone_stagnation_after_attempt(
                  stagnation, true, true) == 0U,
          "noise-scale accepted decrease sustains activation while a meaningful best resets stagnation");
  require(!cfd::steady_nonmonotone_descent_bypass_allowed(
              false, true, 0.0, 1.0),
          "strict mode never bypasses a non-descent merit model");
  const bool bypass = cfd::steady_nonmonotone_descent_bypass_allowed(
      true, true, 0.0, 1.0);
  const double surrogate_slope =
      cfd::steady_nonmonotone_surrogate_slope(1.0, 0.2);
  const double surrogate_prediction = -surrogate_slope;
  require(bypass && surrogate_slope < 0.0 &&
              !cfd::steady_nonmonotone_trial_acceptable(
                  1.01, 1.02, 1.0, surrogate_prediction),
          "nonmonotone bypass evaluates but rejects a bad actual residual");
  require(cfd::steady_nonmonotone_trial_acceptable(
              1.01, 1.005, 1.0, surrogate_prediction) &&
              !cfd::steady_nonmonotone_trial_acceptable(
                  1.02, 1.0100001, 1.0, surrogate_prediction),
          "nonmonotone bypass accepts only window-Armijo and global-cap compliant actual residuals");
  const double activation_reference =
      cfd::steady_nonmonotone_update_envelope_reference(-1.0, 1.0, true);
  const double unchanged_reference =
      cfd::steady_nonmonotone_update_envelope_reference(
          activation_reference, 1.00005, false);
  const double bounded_reference =
      cfd::steady_nonmonotone_bounded_reference(
          {1.0, 1.00005}, 1.00005, 1.0, activation_reference);
  require(close(activation_reference, 1.0001) &&
              unchanged_reference == activation_reference &&
              bounded_reference == activation_reference &&
              cfd::steady_nonmonotone_trial_acceptable(
                  bounded_reference, 1.00005, 1.0, 0.1),
          "flat-window activation seeds one deterministic envelope and accepts a tiny actual increase");
  require(!cfd::steady_nonmonotone_trial_acceptable(
              bounded_reference, 1.0001001, 1.0, 0.1) &&
              cfd::steady_nonmonotone_update_envelope_reference(
                  unchanged_reference, 1.00009, false) ==
                  activation_reference,
          "activation envelope rejects excess growth and cannot inflate cumulatively");
  require(close(cfd::steady_nonmonotone_update_envelope_reference(
                    activation_reference, 0.99, true),
                0.99 * 1.0001),
          "meaningful strict reset deterministically reseeds the activation envelope");
  require(std::string(cfd::steady_implicit_bridge_solver_name).find(
              "full_block_rusanov_lu_sgs") != std::string::npos &&
              std::string(cfd::steady_implicit_bridge_solver_name).find(
                  "explicit") == std::string::npos,
          "pseudo-transient bridge identifies the existing full-block implicit LU-SGS operator");
  require(close(cfd::steady_implicit_bridge_bounded_initial_cfl(1.0e-6,
                                                                 100.0),
                0.1) &&
              close(cfd::steady_implicit_bridge_bounded_initial_cfl(0.5, 1.0),
                    0.5) &&
              cfd::steady_implicit_bridge_residual_within_cap(1.0, 1.25) &&
              !cfd::steady_implicit_bridge_residual_within_cap(1.0, 1.250001),
          "implicit bridge starts conservatively and enforces its fixed entry-best cap");
  require(cfd::steady_implicit_bridge_eligible(
              false, false, cfd::steady_nonmonotone_stagnation_attempts, 1.0) &&
              cfd::steady_implicit_bridge_eligible(false, true, 0U, 1.0) &&
              !cfd::steady_implicit_bridge_eligible(
                  true, true, cfd::steady_nonmonotone_stagnation_attempts, 1.0) &&
              !cfd::steady_implicit_bridge_eligible(
                  false, false, cfd::steady_nonmonotone_stagnation_attempts,
                  -1.0),
          "implicit bridge eligibility is phase-independent but still requires persisted stagnation, a valid best, and an enabled bridge");
  require(close(cfd::steady_phase_best_with_entry_evidence(-1.0, 0.75),
                0.75) &&
              close(cfd::steady_phase_best_with_entry_evidence(0.5, 0.75),
                    0.5) &&
              cfd::steady_phase_best_with_entry_evidence(
                  -1.0, std::numeric_limits<double>::infinity()) == -1.0,
          "a rejected first step seeds only missing fixed-phase best evidence from its exact finite entry residual");
  require(close(cfd::steady_implicit_bridge_epoch_reference(0.75, 1.0),
                1.0) &&
              close(cfd::steady_implicit_bridge_epoch_reference(0.75, -1.0),
                    0.75) &&
              cfd::steady_implicit_bridge_epoch_reference(-1.0, 1.0) == -1.0,
          "implicit bridge cycles renew from accepted trajectory evidence without losing the global best");
  const cfd::CaloricallyPerfectGas bridge_gas(1.4, 1.0, 0.72);
  const cfd::Conservative bridge_base = bridge_gas.conservative(
      bridge_gas.complete(1.0, 0.0, 0.0, 1.0));
  std::vector<double> bridge_state(bridge_base.begin(), bridge_base.end());
  std::vector<double> bridge_direction(4U, 0.0);
  bridge_direction[0] = -2.0;
  const double positivity_scale = cfd::steady_largest_positivity_safe_scale(
      bridge_state, bridge_direction, 1U, bridge_gas);
  cfd::Conservative bridge_trial = bridge_base;
  for (std::size_t component = 0; component < 4U; ++component) {
    bridge_trial[component] += positivity_scale * bridge_direction[component];
  }
  require(positivity_scale == 0.25 && bridge_gas.admissible(bridge_trial),
          "implicit bridge selects the largest positivity-safe geometric scale");
  cfd::SteadyImplicitBridgeState bridge_control;
  bridge_control.active = true;
  bridge_control.cfl = 0.1;
  bridge_control.entry_best_residual = 1.0;
  cfd::update_steady_implicit_bridge(bridge_control, true, false, 1.0, 1.06,
                                     1.0e-6, 10.0);
  require(close(bridge_control.cfl, 0.05) &&
              bridge_control.accepted_steps == 1U &&
              close(bridge_control.maximum_relative_growth, 0.06),
          "implicit bridge contracts CFL on more than five percent residual growth");
  cfd::SteadyImplicitBridgeState small_growth_bridge;
  small_growth_bridge.active = true;
  small_growth_bridge.cfl = 0.1;
  small_growth_bridge.entry_best_residual = 1.0;
  cfd::update_steady_implicit_bridge(small_growth_bridge, true, false, 1.0,
                                     1.049, 1.0e-6, 10.0);
  require(close(small_growth_bridge.cfl, 0.1),
          "sub-five-percent residual growth does not lower bridge CFL below 0.1");
  small_growth_bridge.cfl = 1.0e-6;
  small_growth_bridge.accepted_steps_since_best = 12U;
  cfd::update_steady_implicit_bridge(small_growth_bridge, false, false, 1.049,
                                     1.05, 1.0e-6, 10.0);
  require(!small_growth_bridge.active &&
              small_growth_bridge.accepted_steps_since_best == 0U &&
              small_growth_bridge.watchdog_stops == 1U,
          "a mildly growing bridge cycle renews after its cap rejects at the CFL floor");
  cfd::update_steady_implicit_bridge(bridge_control, true, true, 1.06, 0.99,
                                     1.0e-6, 10.0);
  require(close(bridge_control.cfl, 0.06) &&
              bridge_control.meaningful_best_improvements == 1U &&
              bridge_control.accepted_steps_since_best == 0U &&
              close(bridge_control.entry_best_residual, 1.0),
          "implicit bridge mildly grows CFL on decrease while preserving its activation cap");
  bridge_control.accepted_steps_since_best = 7U;
  const std::size_t bridge_accepts_before_external_best =
      bridge_control.accepted_steps;
  const std::size_t bridge_improvements_before_external_best =
      bridge_control.meaningful_best_improvements;
  cfd::note_steady_implicit_bridge_external_best(bridge_control);
  require(bridge_control.accepted_steps_since_best == 0U &&
              bridge_control.accepted_steps == bridge_accepts_before_external_best &&
              bridge_control.meaningful_best_improvements ==
                  bridge_improvements_before_external_best,
          "ordinary implicit best resets the active bridge watchdog without inflating bridge-only counters");
  cfd::SteadyImplicitBridgeState bridge_watchdog;
  bridge_watchdog.active = true;
  bridge_watchdog.cfl = 0.1;
  bridge_watchdog.entry_best_residual = 1.0;
  bridge_watchdog.accepted_steps =
      cfd::steady_implicit_bridge_watchdog_steps - 1U;
  bridge_watchdog.accepted_steps_since_best =
      cfd::steady_implicit_bridge_watchdog_steps - 1U;
  bridge_watchdog.residual_minimum = 1.0;
  bridge_watchdog.residual_maximum = 1.0;
  cfd::update_steady_implicit_bridge(bridge_watchdog, true, false, 1.0, 1.0,
                                     1.0e-6, 10.0);
  require(!bridge_watchdog.active && !bridge_watchdog.disabled &&
              bridge_watchdog.accepted_steps_since_best == 0U &&
              bridge_watchdog.watchdog_stops == 1U,
          "implicit bridge stops a cycle at five hundred accepted steps without a meaningful best");
  bridge_watchdog.active = true;
  bridge_watchdog.entry_best_residual = 0.9;
  cfd::update_steady_implicit_bridge(bridge_watchdog, true, true, 1.0, 0.9,
                                     1.0e-6, 10.0);
  require(bridge_watchdog.active && !bridge_watchdog.disabled &&
              bridge_watchdog.accepted_steps ==
                  cfd::steady_implicit_bridge_watchdog_steps + 1U &&
              bridge_watchdog.accepted_steps_since_best == 0U &&
              bridge_watchdog.watchdog_stops == 1U,
          "implicit bridge may re-enter after a watchdog stop while retaining cumulative statistics");
  cfd::SteadyImplicitBridgeState bridge_step_cap;
  bridge_step_cap.active = true;
  bridge_step_cap.cfl = 0.1;
  bridge_step_cap.entry_best_residual = 1.0;
  bridge_step_cap.accepted_steps =
      cfd::steady_implicit_bridge_maximum_steps - 1U;
  bridge_step_cap.residual_minimum = 1.0;
  bridge_step_cap.residual_maximum = 1.0;
  cfd::update_steady_implicit_bridge(bridge_step_cap, true, true, 1.0, 0.9,
                                     1.0e-6, 10.0);
  require(bridge_step_cap.active && !bridge_step_cap.disabled &&
              bridge_step_cap.accepted_steps ==
                  cfd::steady_implicit_bridge_maximum_steps &&
              bridge_step_cap.accepted_steps_since_best == 0U,
          "productive implicit bridge work may reach the cumulative step cap");
  cfd::update_steady_implicit_bridge(bridge_step_cap, true, false, 0.9, 0.9,
                                     1.0e-6, 10.0);
  require(bridge_step_cap.active && !bridge_step_cap.disabled &&
              bridge_step_cap.accepted_steps ==
                  cfd::steady_implicit_bridge_maximum_steps + 1U &&
              bridge_step_cap.accepted_steps_since_best == 1U,
          "implicit bridge cap applies per interval without a meaningful best");
  require(!cfd::steady_nonmonotone_watchdog_expired(
               cfd::steady_nonmonotone_watchdog_steps - 1U) &&
              cfd::steady_nonmonotone_watchdog_expired(
                  cfd::steady_nonmonotone_watchdog_steps),
          "nonmonotone watchdog expires exactly at twenty accepted steps without a strict best");
  cfd::SteadyNonmonotoneWatchdogState watchdog{
      true, false, cfd::steady_nonmonotone_watchdog_steps - 1U, 2U};
  cfd::update_steady_nonmonotone_watchdog(watchdog, true, false);
  require(!watchdog.active && watchdog.disabled &&
              watchdog.accepted_steps_since_strict_best == 0U &&
              watchdog.resets == 3U,
          "expired nonmonotone watchdog disables and resets the bridge");
  watchdog = {true, false, 7U, 3U};
  cfd::update_steady_nonmonotone_watchdog(
      watchdog, true,
      cfd::steady_meaningful_best_decrease(plateau_best, noise_scale_best));
  require(watchdog.active && !watchdog.disabled &&
              watchdog.accepted_steps_since_strict_best == 8U &&
              watchdog.resets == 3U,
          "noise-scale pseudo-best does not reset the nonmonotone watchdog");
  cfd::update_steady_nonmonotone_watchdog(watchdog, true, true);
  require(watchdog.active && !watchdog.disabled &&
              watchdog.accepted_steps_since_strict_best == 0U &&
              watchdog.resets == 3U,
          "strict-best improvement resets the watchdog without disabling the bridge");

  const std::array<double, 5> epsilon_multipliers{
      cfd::steady_jfnk_epsilon_multiplier(1.0, 1.0),
      cfd::steady_jfnk_epsilon_multiplier(0.25, 1.0),
      cfd::steady_jfnk_epsilon_multiplier(1.0e-8, 1.0),
      cfd::steady_jfnk_epsilon_multiplier(2.0, 1.0),
      cfd::steady_jfnk_epsilon_multiplier(
          std::numeric_limits<double>::quiet_NaN(), 1.0)};
  const std::array<double, 5> expected_epsilon_multipliers{
      1.0, 0.5, cfd::steady_jfnk_minimum_epsilon_multiplier, 1.0, 1.0};
  for (std::size_t index = 0; index < epsilon_multipliers.size(); ++index) {
    double global_minimum = 0.0;
    double global_maximum = 0.0;
    MPI_Allreduce(&epsilon_multipliers[index], &global_minimum, 1, MPI_DOUBLE,
                  MPI_MIN, MPI_COMM_WORLD);
    MPI_Allreduce(&epsilon_multipliers[index], &global_maximum, 1, MPI_DOUBLE,
                  MPI_MAX, MPI_COMM_WORLD);
    require(global_minimum == global_maximum &&
                global_minimum == expected_epsilon_multipliers[index] &&
                global_minimum >=
                    cfd::steady_jfnk_minimum_epsilon_multiplier &&
                global_minimum <= 1.0,
            "adaptive JFNK epsilon multiplier is bounded and rank deterministic");
  }

  const cfd::SteadySpatialOrderSchedule production_schedule =
      cfd::steady_spatial_order_schedule(2000);
  const cfd::SteadySpatialOrderSchedule short_schedule =
      cfd::steady_spatial_order_schedule(10);
  require(production_schedule.first_order_steps == 500U &&
              production_schedule.ramp_steps == 500U &&
              short_schedule.first_order_steps == 2U &&
              short_schedule.ramp_steps == 2U &&
              cfd::steady_spatial_order_schedule(0).first_order_steps == 1U,
          "spatial-order schedule is generically derived and bounded");
  require(cfd::steady_full_order_minimum_steps(0) == 50U &&
              cfd::steady_full_order_minimum_steps(10) == 50U &&
              cfd::steady_full_order_minimum_steps(2000) == 200U &&
              cfd::steady_full_order_minimum_steps(3000) == 250U,
          "full-order hold is deterministically derived and bounded");
  require(!cfd::steady_convergence_gate(0.999, 1000U, 2000, 10.0,
                                        1.0e-4, 4.0) &&
              !cfd::steady_convergence_gate(1.0, 199U, 2000, 10.0,
                                             1.0e-4, 4.0) &&
              !cfd::steady_convergence_gate(1.0, 200U, 2000, 10.0,
                                             1.0000001e-3, 4.0) &&
              cfd::steady_convergence_gate(1.0, 200U, 2000, 10.0,
                                            1.0e-3, 4.0) &&
              !cfd::steady_convergence_gate(1.0, 200U, 2000, 10.0,
                                              4.0, 0.5),
           "gate requires full order, exact accepted hold, and 10^-target without weakening");
  const double diagnostic_best_residual = 9.0e-4;
  require(diagnostic_best_residual <= 1.0e-3 &&
              !cfd::steady_convergence_gate(1.0, 200U, 2000, 10.0,
                                             1.1e-3, 4.0),
          "final gate uses the current residual rather than a historical strict best");
  require(cfd::smooth_reconstruction_blend(0, 2) == 0.0 &&
              close(cfd::smooth_reconstruction_blend(1, 2), 0.5) &&
              cfd::smooth_reconstruction_blend(2, 2) == 1.0,
          "spatial-order ramp uses smooth exact endpoints");
  require(cfd::steady_implicit_fallback_allowed(0.0, false, 0U) &&
              cfd::steady_implicit_fallback_allowed(1.0, false, 0U) &&
              !cfd::steady_implicit_fallback_allowed(1.0, true, 0U) &&
              !cfd::steady_implicit_fallback_allowed(
                  1.0, false, cfd::steady_fallback_maximum_bridge_steps),
          "bounded implicit fallback remains available at full spatial order");
  require(!cfd::steady_fallback_mode_has_history(true, false, 0U) &&
              cfd::steady_fallback_mode_has_history(true, false, 1U) &&
              !cfd::steady_fallback_mode_has_history(true, true, 1U) &&
              !cfd::steady_fallback_mode_has_history(false, false, 1U),
          "fallback mode requires accepted fixed-operator residual history");

  const cfd::DistributedMesh mesh = one_cell_mesh(rank, size);
  cfd::CaseConfig steady = base_config();
  cfd::FlowSolver steady_solver(mesh, steady, MPI_COMM_WORLD);
  cfd::RestartableSolution solution = steady_solver.uniform_initial_solution();
  const cfd::CaloricallyPerfectGas gas(steady.gas);
  const cfd::Conservative perturb = gas.conservative(gas.complete(
      1.1 + 0.01 * static_cast<double>(rank), 0.8, 0.1, 21.0));
  for (std::size_t k = 0; k < 4U; ++k) solution.U[k] = perturb[k];
  std::vector<double> initial_state = solution.U;
  cfd::ResidualOperator initial_residual_operator(mesh, steady, MPI_COMM_WORLD);
  cfd::ResidualEvaluationOptions initial_options;
  initial_options.reconstruction_blend = 0.0;
  const cfd::ResidualResult expected_global_initial =
      initial_residual_operator.evaluate(initial_state,
                                         steady.run_control.cfl_initial,
                                         initial_options);
  const cfd::StepResult step = steady_solver.steady_step(solution);
  require(step.accepted, "steady implicit update accepted");
  require(step.inner.nonlinear_iterations == 1 &&
              step.inner.linear_sweeps >= 3 && step.inner.linear_sweeps <= 40 &&
              step.inner.linear_solver == cfd::steady_linear_solver_name &&
              step.lusgs_preconditioner_applications >= 3 &&
              step.lusgs_preconditioner_sweeps >=
                  3 * step.lusgs_preconditioner_applications &&
              std::isfinite(step.lusgs_last_defect_ratio),
          "steady solve reports bounded matrix-free GMRES iterations");
  require(step.inner.final_defect < step.inner.initial_defect,
           "matrix-free GMRES reduces the true linear defect");
  require(step.reconstruction_blend == 0.0 && !step.target_met &&
              step.first_order_accepted_steps == 1U &&
              step.order_ramp_accepted_steps == 0U &&
              step.full_order_accepted_steps == 0U,
          "steady continuation starts first order and cannot complete there");
  const double original_initial_residual =
      steady_solver.continuation_state().steady_initial_residual_scale;
  require(close(original_initial_residual,
                expected_global_initial.norms.total_l2, 2.0e-12) &&
              step.steady_initial_residual_baseline ==
                  original_initial_residual,
          "steady convergence baseline is the original global MPI residual norm");
  require(close(steady_solver.continuation_state().cfl,
                steady.run_control.cfl_initial),
          "scheduled steady CFL ramp does not increase before sustained descent");
  double previous_steady_residual = step.residual.total_l2;
  double previous_blend = step.reconstruction_blend;
  std::vector<double> observed_blends{step.reconstruction_blend};
  for (int iteration = 0; iteration < 4; ++iteration) {
    const cfd::StepResult next = steady_solver.steady_step(solution);
    require(next.accepted && !next.target_met,
            "startup and ramp updates are accepted without premature completion");
    if (next.reconstruction_blend == previous_blend) {
      require(next.residual.total_l2 <=
                  previous_steady_residual * (1.0 + 2.0e-12),
              "accepted residual is non-increasing within one spatial operator");
    }
    previous_steady_residual = next.residual.total_l2;
    previous_blend = next.reconstruction_blend;
    observed_blends.push_back(next.reconstruction_blend);
  }
  const double first_ramp_blend = cfd::smooth_reconstruction_blend(1, 3);
  const double second_ramp_blend = cfd::smooth_reconstruction_blend(2, 3);
  require(observed_blends ==
              std::vector<double>({0.0, 0.0, first_ramp_blend,
                                   second_ramp_blend, 1.0}) &&
              steady_solver.continuation_state().steady_first_order_accepted_steps == 2U &&
              steady_solver.continuation_state().steady_order_ramp_accepted_steps == 2U &&
              steady_solver.continuation_state().steady_full_order_accepted_steps == 1U,
          "accepted-step state machine completes exact startup and ramp counts before full order");
  require(steady_solver.continuation_state().steady_initial_residual_scale ==
              original_initial_residual,
          "spatial continuation does not reset the original-run convergence baseline");
  require(steady_solver.continuation_state().cfl <=
              steady.run_control.cfl_initial * 1.25 + 1.0e-14,
          "scheduled envelope is limited by windowed adaptive CFL growth");

  cfd::CaseConfig production_continuation = steady;
  production_continuation.run_control.pseudo_cfl_ramp_steps = 2000;
  const cfd::SteadySpatialOrderSchedule exact_schedule =
      cfd::steady_spatial_order_schedule(2000);
  require(exact_schedule.first_order_steps == 500U &&
              exact_schedule.ramp_steps == 500U,
          "production continuation derives exact 500/500 phase counts");

  cfd::FlowSolver startup_boundary_solver(
      mesh, production_continuation, MPI_COMM_WORLD);
  cfd::RestartableSolution startup_boundary_solution =
      startup_boundary_solver.uniform_initial_solution();
  for (std::size_t k = 0; k < 4U; ++k) {
    startup_boundary_solution.U[k] = perturb[k];
  }
  cfd::FlowSolverContinuation startup_boundary;
  startup_boundary.cfl = 1.0;
  startup_boundary.nonlinear_steps = 499;
  startup_boundary.steady_first_order_accepted_steps = 499;
  startup_boundary_solver.restore_continuation_state(startup_boundary);
  const cfd::StepResult startup_500 =
      startup_boundary_solver.steady_step(startup_boundary_solution);
  const cfd::FlowSolverContinuation after_startup_500 =
      startup_boundary_solver.continuation_state();
  require(startup_500.accepted && startup_500.reconstruction_blend == 0.0 &&
              after_startup_500.steady_first_order_accepted_steps == 500U &&
              after_startup_500.steady_order_ramp_accepted_steps == 0U &&
              close(after_startup_500.steady_reconstruction_blend,
                    cfd::smooth_reconstruction_blend(1, 501)),
          "blend remains first order through the 500th accepted startup update");
  cfd::FlowSolver continuation_bridge_restore_solver(
      mesh, production_continuation, MPI_COMM_WORLD);
  cfd::FlowSolverContinuation continuation_bridge_state = after_startup_500;
  continuation_bridge_state.steady_previous_residual = 1.0;
  continuation_bridge_state.steady_best_residual = 1.0;
  continuation_bridge_state.steady_implicit_bridge.active = true;
  continuation_bridge_state.steady_implicit_bridge.cfl = 0.1;
  continuation_bridge_state.steady_implicit_bridge.entry_best_residual = 1.0;
  continuation_bridge_restore_solver.restore_continuation_state(
      continuation_bridge_state);
  require(continuation_bridge_restore_solver.continuation_state()
                  .steady_implicit_bridge.active &&
              continuation_bridge_restore_solver.continuation_state()
                      .steady_reconstruction_blend < 1.0,
          "restart validation accepts an active bounded implicit bridge in a continuation phase");

  cfd::FlowSolver ramp_boundary_solver(
      mesh, production_continuation, MPI_COMM_WORLD);
  cfd::RestartableSolution ramp_boundary_solution =
      ramp_boundary_solver.uniform_initial_solution();
  for (std::size_t k = 0; k < 4U; ++k) {
    ramp_boundary_solution.U[k] = perturb[k];
  }
  cfd::FlowSolverContinuation ramp_boundary;
  ramp_boundary.cfl = 1.0;
  ramp_boundary.nonlinear_steps = 999;
  ramp_boundary.steady_first_order_accepted_steps = 500;
  ramp_boundary.steady_order_ramp_accepted_steps = 499;
  ramp_boundary.steady_reconstruction_blend =
      cfd::smooth_reconstruction_blend(500, 501);
  ramp_boundary_solver.restore_continuation_state(ramp_boundary);
  const cfd::StepResult ramp_500 =
      ramp_boundary_solver.steady_step(ramp_boundary_solution);
  const cfd::FlowSolverContinuation after_ramp_500 =
      ramp_boundary_solver.continuation_state();
  require(ramp_500.accepted && ramp_500.reconstruction_blend < 1.0 &&
              after_ramp_500.steady_first_order_accepted_steps == 500U &&
              after_ramp_500.steady_order_ramp_accepted_steps == 500U &&
              after_ramp_500.steady_full_order_accepted_steps == 0U &&
              after_ramp_500.steady_reconstruction_blend == 1.0,
          "blend reaches one only after the 500th accepted ramp update");
  const cfd::StepResult first_full_order =
      ramp_boundary_solver.steady_step(ramp_boundary_solution);
  require(first_full_order.accepted &&
              first_full_order.reconstruction_blend == 1.0 &&
              ramp_boundary_solver.continuation_state()
                      .steady_full_order_accepted_steps == 1U,
          "full-order counting begins on the update after the complete ramp");
  cfd::FlowSolverContinuation invalid_promoted =
      ramp_boundary_solver.continuation_state();
  invalid_promoted.steady_order_rescue_promoted = true;
  bool rejected_early_promotion = false;
  try {
    cfd::FlowSolver invalid_promoted_solver(
        mesh, production_continuation, MPI_COMM_WORLD);
    invalid_promoted_solver.restore_continuation_state(invalid_promoted);
  } catch (const std::invalid_argument&) {
    rejected_early_promotion = true;
  }
  require(rejected_early_promotion,
          "restart validation rejects rescue-promoted spatial-order state");

  cfd::FlowSolver completion_solver(mesh, steady, MPI_COMM_WORLD);
  cfd::RestartableSolution completion_solution = solution;
  cfd::FlowSolverContinuation almost_sustained;
  almost_sustained.cfl = 1.0;
  const std::size_t required_hold =
      cfd::steady_full_order_minimum_steps(
          steady.run_control.pseudo_cfl_ramp_steps);
  almost_sustained.nonlinear_steps = 4U + required_hold - 1U;
  almost_sustained.steady_reconstruction_blend = 1.0;
  almost_sustained.steady_rejected_attempts = 17;
  almost_sustained.steady_first_order_accepted_steps = 2;
  almost_sustained.steady_order_ramp_accepted_steps = 2;
  almost_sustained.steady_full_order_accepted_steps =
      required_hold - 1U;
  almost_sustained.steady_initial_residual_scale = 1.0e6;
  almost_sustained.steady_full_order_initial_residual =
      previous_steady_residual;
  almost_sustained.steady_full_order_best_residual =
      previous_steady_residual;
  completion_solver.restore_continuation_state(almost_sustained);
  const cfd::StepResult sustained =
      completion_solver.steady_step(completion_solution);
  require(sustained.accepted && sustained.reconstruction_blend == 1.0 &&
              sustained.full_order_accepted_steps ==
                  required_hold &&
              sustained.target_met,
          "steady target becomes eligible at the exact accepted full-order hold, independent of rejected attempts");
  require(completion_solver.continuation_state()
                  .steady_initial_residual_scale == 1.0e6 &&
              completion_solver.continuation_state()
                      .steady_full_order_initial_residual ==
                  previous_steady_residual,
          "original and diagnostic full-order residual scales remain distinct");
  const cfd::FlowSolverContinuation converged_checkpoint =
      completion_solver.continuation_state();
  cfd::FlowSolver resumed_completion_solver(mesh, steady, MPI_COMM_WORLD);
  resumed_completion_solver.restore_continuation_state(converged_checkpoint);
  const cfd::FlowSolverContinuation resumed_checkpoint =
      resumed_completion_solver.continuation_state();
  require(resumed_completion_solver.steady_target_met() &&
              resumed_checkpoint.steady_initial_residual_scale ==
                  converged_checkpoint.steady_initial_residual_scale &&
              resumed_checkpoint.steady_full_order_initial_residual ==
                  converged_checkpoint.steady_full_order_initial_residual &&
              resumed_checkpoint.steady_full_order_accepted_steps ==
                  required_hold,
          "corrected convergence gate and both baselines are restart invariant");

  const cfd::DistributedMesh recovery_mesh = cross_mesh(rank, size);
  cfd::FlowSolver recovery_solver(recovery_mesh, steady, MPI_COMM_WORLD);
  cfd::RestartableSolution recovery_solution = recovery_solver.uniform_initial_solution();
  cfd::FlowSolverContinuation full_order_recovery;
  full_order_recovery.cfl = 1.0;
  full_order_recovery.nonlinear_steps = 4;
  full_order_recovery.steady_reconstruction_blend = 1.0;
  full_order_recovery.steady_first_order_accepted_steps = 2;
  full_order_recovery.steady_order_ramp_accepted_steps = 2;
  recovery_solver.restore_continuation_state(full_order_recovery);
  constexpr int recovery_seed = 448;
  for (std::size_t cell = 0; cell < recovery_mesh.cells.size(); ++cell) {
    const double phase = static_cast<double>(recovery_seed * (17 + 13 * cell));
    const double a = 0.5 + 0.5 * std::sin(phase * 0.731);
    const double b = 0.5 + 0.5 * std::sin(phase * 1.117 + 0.3);
    const double d = 0.5 + 0.5 * std::sin(phase * 1.913 + 0.7);
    const cfd::Conservative candidate = gas.conservative(gas.complete(
        std::exp(-3.0 + 6.0 * a), -30.0 + 60.0 * b,
        -10.0 + 20.0 * d, std::exp(-3.0 + 9.0 * d)));
    for (std::size_t k = 0; k < 4U; ++k) {
      recovery_solution.U[cell * 4U + k] = candidate[k];
    }
  }
  // A low-CFL pseudo-time regularization can rotate a Newton correction into
  // a non-descent direction, while a high-CFL system approaches Newton and
  // nearly eliminates this linear residual in one step.
  const auto regularized_direction = [](double cfl) {
    const double diagonal = 1.0 + 1.0 / cfl;
    const double d1 = 1.0 / diagonal;
    const double d0 = (-1.0 - 10.0 * d1) / diagonal;
    return std::array<double, 2>{d0, d1};
  };
  const auto merit_after = [&](double cfl) {
    const auto direction = regularized_direction(cfl);
    const double r0 = 1.0 + direction[0] + 10.0 * direction[1];
    const double r1 = -1.0 + direction[1];
    return std::hypot(r0, r1);
  };
  require(merit_after(0.01) > std::sqrt(2.0) &&
              merit_after(1.0e6) < 1.0e-4,
          "high-CFL Newton rescue direction converges where low-CFL direction is not descent");
  constexpr int fallback_seed = 13;
  cfd::CaseConfig fallback_config = steady;
  fallback_config.run_control.pseudo_cfl_ramp_steps = 256;
  cfd::FlowSolver fallback_solver(recovery_mesh, fallback_config,
                                  MPI_COMM_WORLD);
  cfd::RestartableSolution fallback_solution =
      fallback_solver.uniform_initial_solution();
  for (std::size_t cell = 0; cell < recovery_mesh.cells.size(); ++cell) {
    const double phase = static_cast<double>(fallback_seed * (17 + 13 * cell));
    const double a = 0.5 + 0.5 * std::sin(phase * 0.731);
    const double b = 0.5 + 0.5 * std::sin(phase * 1.117 + 0.3);
    const double d = 0.5 + 0.5 * std::sin(phase * 1.913 + 0.7);
    const cfd::Conservative candidate = gas.conservative(gas.complete(
        0.9 + 0.2 * a, -30.0 + 60.0 * b,
        -10.0 + 20.0 * d, 15.0 + 10.0 * a));
    for (std::size_t k = 0; k < 4U; ++k) {
      fallback_solution.U[cell * 4U + k] = candidate[k];
    }
  }
  cfd::ResidualOperator fallback_residual(recovery_mesh, fallback_config,
                                          MPI_COMM_WORLD);
  const cfd::ResidualResult nonsmooth_initial =
      fallback_residual.evaluate(fallback_solution.U, 1.0, MPI_COMM_WORLD);
  require(nonsmooth_initial.diagnostics.shock_fallback_cells == 0U &&
              nonsmooth_initial.diagnostics
                      .venkatakrishnan_limited_face_components > 0U,
          "fallback fixture is Venkatakrishnan-nonsmooth without shock fallback");
  cfd::FlowSolverContinuation forced_fallback;
  forced_fallback.cfl = 0.2;
  forced_fallback.nonlinear_steps = 31;
  forced_fallback.steady_previous_residual = nonsmooth_initial.norms.total_l2;
  forced_fallback.steady_best_residual =
      0.5 * nonsmooth_initial.norms.total_l2;
  forced_fallback.steady_fallback_accepted_steps = 31;
  forced_fallback.steady_fallback_attempts = 31;
  forced_fallback.steady_last_fallback_cfl = 0.1;
  forced_fallback.steady_fallback_mode = true;
  forced_fallback.steady_fallback_cfl = 0.1;
  forced_fallback.steady_fallback_steps_since_jfnk = 0;
  forced_fallback.steady_jfnk_failure_streak = 2;
  forced_fallback.steady_jfnk_attempts = 2;
  forced_fallback.steady_initial_residual_scale =
      nonsmooth_initial.norms.total_l2;
  forced_fallback.steady_first_order_accepted_steps = 31;
  forced_fallback.steady_fallback_consecutive_accepted_steps = 31;
  for (int sample = 0; sample < 31; ++sample) {
    const double factor = sample < 15 ? 0.5 : 1.0;
    forced_fallback.steady_fallback_residual_window.push_back(
        factor * nonsmooth_initial.norms.total_l2);
  }
  fallback_solver.restore_continuation_state(forced_fallback);
  const cfd::RestartableSolution fallback_entry_solution = fallback_solution;
  const cfd::StepResult fallback_step =
      fallback_solver.steady_step(fallback_solution);
  require(fallback_step.accepted && fallback_step.jfnk_attempted &&
              fallback_step.lusgs_preconditioner_applications >= 1,
          "ordinary JFNK/LU-SGS runs even while fallback mode is active");
  if (fallback_step.steady_acceptance ==
      cfd::SteadyAcceptanceMode::pseudo_time_fallback) {
    require(fallback_step.fallback_attempted &&
                fallback_step.fallback_cfl > 0.0 &&
                fallback_step.fallback_cfl <= 0.1 &&
                fallback_step.fallback_sweeps >= 3 &&
                fallback_step.lusgs_last_defect_ratio < 1.0,
            "startup fallback remains a bounded secondary LU-SGS path");
  } else {
    require((fallback_step.steady_acceptance ==
                 cfd::SteadyAcceptanceMode::jfnk ||
             fallback_step.steady_acceptance ==
                 cfd::SteadyAcceptanceMode::implicit_trust_region_retry ||
             fallback_step.steady_acceptance ==
                 cfd::SteadyAcceptanceMode::newton_rescue) &&
                 !fallback_step.fallback_attempted,
            "an accepted implicit update bypasses lower-priority fallback");
  }
  const cfd::FlowSolverContinuation fallback_progress =
      fallback_solver.continuation_state();
  require(!fallback_progress.steady_probe_active &&
              !fallback_progress.steady_recovery_restore_pending &&
              fallback_progress.steady_recovery_probe_cfl == 0.0 &&
              fallback_progress.steady_rejected_attempts == 0,
          "accepted fallback disables trusted-best restore/probe cycling");
  const cfd::FlowSolverContinuation fallback_statistics =
      fallback_solver.continuation_state();
  if (fallback_step.steady_acceptance ==
      cfd::SteadyAcceptanceMode::pseudo_time_fallback) {
    require(fallback_statistics.steady_fallback_disabled &&
                !fallback_statistics.steady_fallback_mode &&
                fallback_statistics.steady_fallback_growth_disables == 1U &&
                fallback_statistics.steady_fallback_accepted_steps == 32U,
            "sustained fallback residual growth disables the secondary path");
  } else {
    require(!fallback_statistics.steady_fallback_mode &&
                fallback_statistics.steady_fallback_accepted_steps == 31U,
            "primary acceptance exits fallback mode without a fallback update");
  }

  cfd::FlowSolver full_order_solver(recovery_mesh, steady, MPI_COMM_WORLD);
  cfd::FlowSolverContinuation full_order_state;
  full_order_state.cfl = 0.2;
  full_order_state.nonlinear_steps = 5;
  full_order_state.steady_reconstruction_blend = 1.0;
  full_order_state.steady_first_order_accepted_steps = 2;
  full_order_state.steady_order_ramp_accepted_steps = 2;
  full_order_state.steady_full_order_accepted_steps = 1;
  full_order_state.steady_full_order_initial_residual =
      nonsmooth_initial.norms.total_l2;
  full_order_state.steady_full_order_best_residual =
      nonsmooth_initial.norms.total_l2;
  full_order_state.steady_jfnk_failure_streak = 1;
  full_order_state.steady_jfnk_attempts = 1;
  full_order_state.steady_fallback_disabled = true;
  full_order_solver.restore_continuation_state(full_order_state);
  cfd::RestartableSolution full_order_solution = fallback_entry_solution;
  const std::vector<double> full_order_entry = full_order_solution.U;
  const cfd::StepResult full_order_attempt =
      full_order_solver.steady_step(full_order_solution);
  require(full_order_attempt.jfnk_attempted &&
              full_order_attempt.lusgs_preconditioner_applications >= 1 &&
              full_order_attempt.trust_region_retry_attempted &&
              full_order_attempt.trust_region_retry_candidates == 1 &&
              full_order_attempt.trust_region_retry_accepted &&
              !full_order_attempt.rescue_attempted &&
              !full_order_attempt.fallback_attempted &&
              full_order_attempt.steady_acceptance ==
                  cfd::SteadyAcceptanceMode::implicit_trust_region_retry,
          "failed full-order globalization accepts an intermediate CFL before endpoint rescue");
  require(full_order_attempt.trust_region_retry_total_gmres_iterations <= 50 &&
              full_order_attempt.trust_region_retry_initial_residual >= 0.0 &&
              (!full_order_attempt.trust_region_retry_accepted ||
               (full_order_attempt.steady_acceptance ==
                    cfd::SteadyAcceptanceMode::implicit_trust_region_retry &&
                full_order_attempt.trust_region_retry_accepted_cfl == 1.0 &&
                full_order_attempt.trust_region_retry_final_residual <
                    full_order_attempt.trust_region_retry_initial_residual)),
          "intermediate retry is bounded and accepts only strict actual decrease");
  if (!full_order_attempt.accepted) {
    require(full_order_solution.U == full_order_entry &&
                full_order_attempt.residual.total_l2 > 0.0 &&
                std::isfinite(full_order_attempt.forces.cd),
            "failed full-order attempt preserves state and reports true entry diagnostics");
  }
  if (full_order_attempt.trust_region_retry_accepted) {
    const cfd::FlowSolverContinuation accepted_retry_state =
        full_order_solver.continuation_state();
    require(full_order_attempt.cfl ==
                    full_order_attempt.trust_region_retry_accepted_cfl &&
                accepted_retry_state.steady_trust_region_retry_accepted_steps ==
                    1U &&
                accepted_retry_state
                        .steady_trust_region_retry_last_accepted_cfl ==
                    full_order_attempt.trust_region_retry_accepted_cfl &&
                accepted_retry_state
                        .steady_trust_region_retry_last_line_scale ==
                    full_order_attempt.line_search_scale &&
                accepted_retry_state
                        .steady_trust_region_retry_last_final_residual <
                    accepted_retry_state
                        .steady_trust_region_retry_last_initial_residual,
            "accepted retry persists its actual CFL, line scale, and strict residual decrease");
  }

  cfd::FlowSolver first_order_retry_solver(recovery_mesh, steady,
                                            MPI_COMM_WORLD);
  cfd::FlowSolverContinuation first_order_retry_state = full_order_state;
  first_order_retry_state.steady_reconstruction_blend = 0.0;
  first_order_retry_state.steady_first_order_accepted_steps = 1;
  first_order_retry_state.steady_order_ramp_accepted_steps = 0;
  first_order_retry_state.steady_full_order_accepted_steps = 0;
  first_order_retry_state.steady_full_order_initial_residual = -1.0;
  first_order_retry_state.steady_full_order_best_residual = -1.0;
  first_order_retry_state.steady_fallback_disabled = false;
  first_order_retry_solver.restore_continuation_state(first_order_retry_state);
  cfd::RestartableSolution first_order_retry_solution = fallback_entry_solution;
  const cfd::StepResult first_order_retry_attempt =
      first_order_retry_solver.steady_step(first_order_retry_solution);
  if (!first_order_retry_attempt.accepted ||
      first_order_retry_attempt.steady_acceptance !=
          cfd::SteadyAcceptanceMode::jfnk) {
    require(first_order_retry_attempt.trust_region_retry_attempted &&
                first_order_retry_attempt.trust_region_retry_candidates > 0 &&
                (!first_order_retry_attempt.trust_region_retry_accepted ||
                 first_order_retry_attempt.trust_region_retry_final_residual <
                     first_order_retry_attempt.trust_region_retry_initial_residual),
            "failed first-order globalization tries bounded intermediate implicit CFLs with strict decrease");
  }
  const cfd::FlowSolverContinuation first_order_retry_progress =
      first_order_retry_solver.continuation_state();
  require(first_order_retry_progress.steady_order_ramp_accepted_steps == 0U &&
              first_order_retry_progress.steady_full_order_accepted_steps == 0U &&
              first_order_retry_progress.steady_first_order_accepted_steps ==
                  1U + (first_order_retry_attempt.accepted ? 1U : 0U),
          "first-order retry advances only the accepted startup counter");
  if (full_order_attempt.rescue_attempted) {
    require(full_order_attempt.rescue_cfl <= steady.run_control.cfl_max &&
                full_order_attempt.rescue_gmres_iterations <= 50 &&
                full_order_attempt.rescue_gmres_ratio >= 0.0 &&
                (!full_order_attempt.rescue_accepted ||
                 full_order_attempt.rescue_final_residual <
                     full_order_attempt.rescue_initial_residual),
            "rescue is bounded and accepts only actual full-order residual decrease");
  }
  if (!full_order_attempt.accepted) {
    const cfd::FlowSolverContinuation before_cooldown =
        full_order_solver.continuation_state();
    const cfd::StepResult cooldown_attempt =
        full_order_solver.steady_step(full_order_solution);
    require(cooldown_attempt.jfnk_attempted &&
                cooldown_attempt.lusgs_preconditioner_applications >= 1 &&
                !cooldown_attempt.trust_region_retry_attempted &&
                !cooldown_attempt.rescue_attempted &&
                !cooldown_attempt.fallback_attempted,
            "rescue cooldown suppresses rescue only, never ordinary JFNK/LU-SGS");
    if (!cooldown_attempt.accepted) {
      const cfd::FlowSolverContinuation after_cooldown =
          full_order_solver.continuation_state();
      require(full_order_solution.U == full_order_entry &&
                  after_cooldown.steady_first_order_accepted_steps ==
                      before_cooldown.steady_first_order_accepted_steps &&
                  after_cooldown.steady_order_ramp_accepted_steps ==
                      before_cooldown.steady_order_ramp_accepted_steps &&
                  after_cooldown.steady_full_order_accepted_steps ==
                      before_cooldown.steady_full_order_accepted_steps &&
                  after_cooldown.steady_reconstruction_blend ==
                      before_cooldown.steady_reconstruction_blend,
              "rejected cooldown attempt preserves all accepted-step phase counters");
    }
  }
  for (std::size_t cell = 0; cell < recovery_mesh.owned_cell_count; ++cell) {
    cfd::Conservative final_state{};
    for (std::size_t k = 0; k < 4U; ++k) {
      final_state[k] = fallback_solution.U[cell * 4U + k];
    }
    require(gas.admissible(final_state),
            "bounded hybrid fixture preserves positive finite states");
  }
  for (std::size_t k = 0; k < 4U; ++k) {
    double local_checksum = 0.0;
    for (std::size_t cell = 0; cell < recovery_mesh.owned_cell_count; ++cell) {
      local_checksum += fallback_solution.U[cell * 4U + k];
    }
    double minimum_checksum = 0.0;
    double maximum_checksum = 0.0;
    MPI_Allreduce(&local_checksum, &minimum_checksum, 1, MPI_DOUBLE, MPI_MIN,
                  MPI_COMM_WORLD);
    MPI_Allreduce(&local_checksum, &maximum_checksum, 1, MPI_DOUBLE, MPI_MAX,
                  MPI_COMM_WORLD);
    require(close(minimum_checksum, maximum_checksum, 5.0e-13),
            "bounded fallback replicated-rank checksums agree");
  }
  int recovery_attempts = 0;
  int recovery_rejections = 0;
  cfd::StepResult recovery;
  do {
    recovery = recovery_solver.steady_step(recovery_solution);
    ++recovery_attempts;
    if (!recovery.accepted) {
      ++recovery_rejections;
      require(recovery_solver.continuation_state().nonlinear_steps == 4U,
              "rejected attempts do not advance accepted-step CFL ramp progress");
    }
  } while (!recovery.accepted && recovery_attempts < 6);
  require(recovery_rejections == 0 && recovery.accepted &&
              recovery_attempts == 1 && recovery.line_search_scale > 0.0,
          "full-block-preconditioned Newton accepts the smooth fixture without rejection");
  require(recovery_attempts == recovery_rejections + 1 &&
                recovery_solver.continuation_state().nonlinear_steps == 5U,
           "attempt budget counts rejections while ramp progress counts acceptance");

  cfd::FlowSolver failed_recovery_solver(recovery_mesh, steady, MPI_COMM_WORLD);
  cfd::RestartableSolution failed_recovery =
      failed_recovery_solver.uniform_initial_solution();
  const std::vector<double> trusted_best = failed_recovery.U_best;
  failed_recovery.U = recovery_solution.U;
  const std::vector<double> failed_entry_state = failed_recovery.U;
  require(failed_entry_state != trusted_best,
          "failed fallback fixture separates current and diagnostic-best states");
  cfd::FlowSolverContinuation forced_recovery;
  forced_recovery.cfl = 0.2;
  forced_recovery.steady_previous_residual = 1.0e-300;
  forced_recovery.steady_best_residual = 1.0e-300;
  forced_recovery.steady_initial_residual_scale = 1.0e-300;
  forced_recovery.steady_fallback_mode = true;
  forced_recovery.steady_fallback_cfl = 0.1;
  forced_recovery.steady_fallback_residual_window = {1.0e-300};
  forced_recovery.steady_jfnk_failure_streak = 2;
  failed_recovery_solver.restore_continuation_state(forced_recovery);
  const cfd::StepResult failed_probe =
      failed_recovery_solver.steady_step(failed_recovery);
  require(failed_probe.jfnk_attempted && failed_recovery.U != trusted_best,
          "fallback mode runs ordinary JFNK and never restores diagnostic U_best");
  if (!failed_probe.accepted) {
    require(failed_recovery.U == failed_entry_state &&
                failed_recovery_solver.continuation_state().nonlinear_steps == 0U,
            "a rejected primary/secondary attempt preserves accepted state");
  }

  cfd::CaseConfig transient = base_config(cfd::RunType::transient);
  transient.run_control.max_inner_iterations = 50;
  cfd::FlowSolver transient_solver(mesh, transient, MPI_COMM_WORLD);
  cfd::RestartableSolution history = transient_solver.uniform_initial_solution();
  const cfd::Conservative current = gas.conservative(gas.complete(1.08, 0.85, 0.04, 18.4));
  const cfd::Conservative previous = gas.conservative(gas.complete(1.02, 0.93, -0.03, 17.9));
  const cfd::Conservative older = gas.conservative(gas.complete(0.97, 1.05, 0.02, 18.2));
  for (std::size_t k = 0; k < 4U; ++k) {
    history.U[k] = current[k];
    history.U_n[k] = previous[k];
    history.U_nm1[k] = older[k];
  }
  history.physical_step = 1;
  const std::vector<double> old_un = history.U_n;
  const cfd::StepResult physical = transient_solver.transient_step(history);
  require(physical.accepted,
          "physical-time inner solve accepted: nonlinear=" +
              std::to_string(physical.inner.nonlinear_iterations) +
              " ratio=" + std::to_string(physical.inner.nonlinear_ratio) +
              " linear=" + std::to_string(physical.inner.linear_sweeps) +
              " linear_ratio=" + std::to_string(physical.inner.defect_ratio) +
              " line_scale=" + std::to_string(physical.line_search_scale));
  require(physical.predictor_scale > 0.0 && physical.predictor_scale <= 1.0,
          "positive BDF2 extrapolated predictor is used");
  require(physical.target_met && physical.inner.nonlinear_iterations >= 5 &&
              physical.inner.nonlinear_iterations <=
                  transient.run_control.max_inner_iterations,
          "physical step accepts only after minimum iterations and target");
  require(physical.inner.linear_sweeps >= 3 && physical.inner.linear_sweeps <= 20 &&
              physical.inner.total_linear_sweeps >= physical.inner.linear_sweeps &&
              physical.inner.linear_solver == cfd::transient_linear_solver_name &&
              physical.inner.nonlinear_ratio <=
                  transient.run_control.inner_residual_reduction_target,
          "transient JFNK reports bounded linear iterations separately from nonlinear iterations");
  require(physical.inner.nonlinear_iterations <= 20,
          "block relaxation and predictor materially reduce nonlinear iterations");
  require(gas.admissible(cfd::Conservative{history.U[0], history.U[1],
                                           history.U[2], history.U[3]}),
          "accepted predictor path remains positive");
  require(history.physical_step == 2U && close(history.time, 0.01),
          "accepted physical step advances step and time once");
  require(history.U_nm1 == old_un && history.U_n == history.U,
          "BDF histories update only after accepted inner solve");

  cfd::CaseConfig forced = base_config(cfd::RunType::transient);
  forced.run_control.max_inner_iterations = 5;
  forced.run_control.inner_residual_reduction_target = 1.0e-30;
  cfd::FlowSolver forced_solver(mesh, forced, MPI_COMM_WORLD);
  cfd::RestartableSolution rollback = forced_solver.uniform_initial_solution();
  const cfd::Conservative low_history =
      gas.conservative(gas.complete(0.10, 1.0, 0.0, 1.8));
  const cfd::Conservative high_history =
      gas.conservative(gas.complete(0.25, 1.0, 0.0, 4.5));
  for (std::size_t k = 0; k < 4U; ++k) {
    rollback.U[k] = current[k];
    rollback.U_n[k] = low_history[k];
    rollback.U_nm1[k] = high_history[k];
  }
  rollback.physical_step = 7;
  rollback.time = 1.25;
  const cfd::RestartableSolution entry = rollback;
  const cfd::StepResult rejected = forced_solver.transient_step(rollback);
  require(!rejected.accepted && !rejected.target_met,
          "target miss rejects the complete physical step");
  require(rejected.predictor_scale > 0.0 && rejected.predictor_scale < 1.0,
          "inadmissible full BDF2 extrapolation is positively blended toward Un");
  require(rejected.inner.nonlinear_iterations == 5 &&
              rejected.inner.linear_sweeps >= 3 && rejected.inner.linear_sweeps <= 20 &&
              rejected.inner.nonlinear_ratio >
                  forced.run_control.inner_residual_reduction_target,
          "forced miss honors configured nonlinear and bounded linear iteration limits");
  require(rollback.U == entry.U && rollback.U_n == entry.U_n &&
              rollback.U_nm1 == entry.U_nm1 &&
              rollback.physical_step == entry.physical_step && rollback.time == entry.time,
          "forced nonconvergence restores U, histories, step, and time exactly");
  require(forced_solver.transient_stats().target_misses == 1U,
          "rejected nonlinear target miss is tracked");
}

void test_force_sign_and_split(int rank, int size) {
  cfd::DistributedMesh mesh = one_cell_mesh(rank, size);
  cfd::CaseConfig config = base_config();
  config.physics.mode = cfd::PhysicsMode::laminar;
  config.physics.reynolds = 10.0;
  config.physics.viscosity_model = "constant";
  config.boundary_conditions["WALL"] = cfd::BoundaryCondition::no_slip_adiabatic_wall;
  if (rank == 0) mesh.faces[0].boundary = "WALL";
  cfd::ResidualOperator residual(mesh, config, MPI_COMM_WORLD);
  std::vector<double> U(mesh.cells.size() * 4U);
  const cfd::CaloricallyPerfectGas gas(config.gas);
  const cfd::Primitive center = gas.complete(1.0, 2.0, 1.0, 5.0);
  const cfd::Conservative state = rank == 0 ? gas.conservative(center) : residual.freestream();
  for (std::size_t k = 0; k < 4U; ++k) U[k] = state[k];
  const cfd::ResidualResult row = residual.evaluate(U, 1.0, true);
  cfd::PrimitiveReconstruction independent_reconstruction(
      mesh, gas, config.boundary_conditions,
      gas.primitive(residual.freestream()), MPI_COMM_WORLD);
  const cfd::ReconstructionData& independent =
      independent_reconstruction.compute(U);
  std::array<double, 2> expected_traction{};
  if (rank == 0) {
    const cfd::VelocityTemperatureGradients gradient =
        cfd::velocity_temperature_gradients(center, independent.gradient[0], gas);
    const cfd::ViscousFaceFlux expected = cfd::no_slip_adiabatic_wall_flux(
        center, gradient, {1.0, 0.0}, 0.5, residual.viscosity(), gas);
    expected_traction = {expected.traction.x, expected.traction.y};
  }
  MPI_Bcast(expected_traction.data(), 2, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  if (rank == 0) {
    const auto wall = std::find_if(row.surface.begin(), row.surface.end(),
                                   [](const cfd::SurfaceBoundaryState& face) {
      return face.condition == cfd::BoundaryCondition::no_slip_adiabatic_wall;
    });
    require(wall != row.surface.end(), "real no-slip wall surface row is collected");
    require(close(wall->viscous_body_force.x, -expected_traction[0]) &&
                close(wall->viscous_body_force.y, -expected_traction[1]),
            "wall body force has opposite sign and full viscous traction");
    require(close(wall->tangential_shear_force.x, 0.0) &&
                close(wall->tangential_shear_force.y, -expected_traction[1]),
            "surface Cf field retains tangential shear only");
    require(std::abs(wall->viscous_body_force.x) > 0.0,
            "normal viscous stress is retained in force coefficients");
  }
  const cfd::ForceCoefficients force = residual.forces(row);
  require(close(force.cd_pressure, 10.0) && close(force.cl_pressure, 0.0),
          "outward-fluid pressure force sign and scaling");
  require(close(force.cd_viscous, -2.0 * expected_traction[0]) &&
              close(force.cl_viscous, -2.0 * expected_traction[1]),
          "force coefficients use complete signed viscous traction");
  require(close(force.cd, force.cd_pressure + force.cd_viscous) &&
              close(force.cl, force.cl_pressure + force.cl_viscous),
          "total force is exact pressure/viscous sum");
  cfd::ResidualEvaluationOptions first_order_options;
  first_order_options.reconstruction_blend = 0.0;
  const cfd::ForceCoefficients first_order_force =
      residual.forces(residual.evaluate(U, 1.0, first_order_options));
  require(close(first_order_force.cd_viscous, force.cd_viscous) &&
              close(first_order_force.cl_viscous, force.cl_viscous) &&
              std::abs(first_order_force.cd_viscous) > 0.0,
          "first-order inviscid startup keeps physical viscous gradients active");
  cfd::ResidualEvaluationOptions fast;
  fast.compute_global_norms = false;
  fast.compute_global_diagnostics = false;
  fast.collect_surface = false;
  const cfd::ResidualResult trial = residual.evaluate(U, 1.0, fast);
  require(trial.surface.empty() && trial.norms.total_l2 == 0.0,
          "trial evaluation omits surface rows and global norm reductions");
}

void test_collective_preflight(int rank, int size) {
  if (size == 1) return;
  const cfd::DistributedMesh mesh = one_cell_mesh(rank, size);
  cfd::CaseConfig config = base_config();
  cfd::ResidualOperator residual(mesh, config, MPI_COMM_WORLD);
  std::vector<double> U(mesh.cells.size() * 4U);
  const cfd::Conservative freestream = residual.freestream();
  for (std::size_t k = 0; k < 4U; ++k) U[k] = freestream[k];
  if (rank == 0) U.pop_back();
  bool rejected = false;
  try {
    (void)residual.evaluate(U, 1.0, true);
  } catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("collective residual preflight") !=
               std::string::npos;
  }
  int local = rejected ? 1 : 0;
  int global = 0;
  MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  require(global == size, "one-rank dimension failure is reported collectively");

  U.assign(mesh.cells.size() * 4U, 0.0);
  for (std::size_t k = 0; k < 4U; ++k) U[k] = freestream[k];
  if (rank == size - 1) U[0] = -1.0;
  rejected = false;
  try {
    (void)residual.evaluate(U, 1.0, true);
  } catch (const std::invalid_argument& error) {
    rejected = std::string(error.what()).find("inadmissible owned conservative state") !=
               std::string::npos;
  }
  local = rejected ? 1 : 0;
  MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  require(global == size, "one-rank inadmissible state is reported collectively");
}

void test_case_freestream_consistency(const std::filesystem::path& cases) {
  const std::array<const char*, 8> names{
      "naca0012_m015_inviscid.json", "naca0012_m080_inviscid.json",
      "naca0012_m200_inviscid.json", "naca0012_m015_laminar_re5000.json",
      "naca0012_m080_laminar_re5000.json", "naca0012_m200_laminar_re5000.json",
      "cylinder_m010_laminar_re20.json", "cylinder_m010_laminar_re200.json"};
  for (const char* name : names) {
    const cfd::CaseConfig config = cfd::parse_case_file(cases / name);
    const cfd::CaloricallyPerfectGas gas(config.gas);
    require(gas.admissible(gas.freestream(config.freestream)),
            std::string("consistent benchmark freestream ") + name);
  }
}

void test_mesh_freestream(const std::filesystem::path& case_path,
                          MPI_Comm communicator, int rank) {
  cfd::CaseConfig config = cfd::parse_case_file(case_path);
  std::unique_ptr<cfd::Mesh> root_mesh;
  if (rank == 0) root_mesh = std::make_unique<cfd::Mesh>(cfd::read_cgns_mesh(config.mesh.file));
  cfd::DistributedMesh mesh =
      cfd::partition_and_distribute(std::move(root_mesh), communicator, 0);
  for (auto& boundary : config.boundary_conditions) {
    boundary.second = cfd::BoundaryCondition::farfield;
  }
  cfd::ResidualOperator residual(mesh, config, communicator);
  std::vector<double> U(mesh.cells.size() * 4U);
  const cfd::Conservative freestream = residual.freestream();
  for (std::size_t cell = 0; cell < mesh.cells.size(); ++cell) {
    for (std::size_t k = 0; k < 4U; ++k) U[cell * 4U + k] = freestream[k];
  }
  const cfd::ResidualResult row = residual.evaluate(U, 1.0, true);
  cfd::ResidualEvaluationOptions uniform_accumulation_options;
  uniform_accumulation_options.reconstruction_blend = 0.0;
  const cfd::ResidualResult compensated_uniform =
      residual.evaluate(U, 1.0, uniform_accumulation_options);
  std::vector<double> naive(mesh.owned_cell_count * 4U, 0.0);
  std::vector<long double> extended_reference(
      mesh.owned_cell_count * 4U, 0.0L);
  std::array<cfd::CompensatedSum, 4> local_boundary_flux{};
  const cfd::Primitive freestream_primitive =
      residual.gas().primitive(freestream);
  for (const cfd::LocalFace& face : mesh.faces) {
    cfd::Conservative exterior = freestream;
    if (face.right_cell < 0) {
      exterior = residual.gas().conservative(cfd::boundary_exterior_state(
          cfd::BoundaryCondition::farfield, freestream_primitive,
          freestream_primitive, face.normal, residual.gas()));
    }
    const cfd::NumericalFlux flux = cfd::rusanov_flux(
        freestream, exterior, face.normal, residual.gas(),
        config.run_control.rusanov_dissipation_scale.value_or(1.0));
    const std::size_t left = static_cast<std::size_t>(face.left_cell);
    if (left < mesh.owned_cell_count) {
      for (std::size_t component = 0; component < 4U; ++component) {
        const double contribution = face.length * flux.value[component];
        naive[left * 4U + component] += contribution;
        extended_reference[left * 4U + component] +=
            static_cast<long double>(contribution);
      }
    }
    if (face.right_cell >= 0) {
      const std::size_t right = static_cast<std::size_t>(face.right_cell);
      if (right < mesh.owned_cell_count) {
        for (std::size_t component = 0; component < 4U; ++component) {
          const double contribution = -face.length * flux.value[component];
          naive[right * 4U + component] += contribution;
          extended_reference[right * 4U + component] +=
              static_cast<long double>(contribution);
        }
      }
    } else {
      for (std::size_t component = 0; component < 4U; ++component) {
        local_boundary_flux[component].add(
            face.length * flux.value[component]);
      }
    }
  }
  long double local_naive_squared = 0.0L;
  long double local_compensated_squared = 0.0L;
  std::array<cfd::CompensatedSum, 4> local_residual_sum{};
  for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
    for (std::size_t component = 0; component < 4U; ++component) {
      const std::size_t index = cell * 4U + component;
      const long double naive_error =
          static_cast<long double>(naive[index]) - extended_reference[index];
      const long double compensated_error =
          static_cast<long double>(compensated_uniform.value[index]) -
          extended_reference[index];
      local_naive_squared += naive_error * naive_error;
      local_compensated_squared += compensated_error * compensated_error;
      local_residual_sum[component].add(compensated_uniform.value[index]);
    }
  }
  std::array<long double, 2> local_errors{
      local_naive_squared, local_compensated_squared};
  std::array<long double, 2> global_errors{};
  MPI_Allreduce(local_errors.data(), global_errors.data(), 2,
                MPI_LONG_DOUBLE, MPI_SUM, communicator);
  require(global_errors[1] <= global_errors[0],
          "compensated uniform residual is no worse than naive face accumulation");

  std::array<double, 8> local_conservation{};
  for (std::size_t component = 0; component < 4U; ++component) {
    local_conservation[component] = local_residual_sum[component].value();
    local_conservation[4U + component] =
        local_boundary_flux[component].value();
  }
  std::array<double, 8> global_conservation{};
  MPI_Allreduce(local_conservation.data(), global_conservation.data(), 8,
                MPI_DOUBLE, MPI_SUM, communicator);
  for (std::size_t component = 0; component < 4U; ++component) {
    require(close(global_conservation[component],
                  global_conservation[4U + component], 2.0e-12),
            "compensated cell residual preserves boundary-flux conservation");
  }
  require(row.norms.total_l2 < 2.0e-10,
          "uniform all-farfield residual is near roundoff on " + case_path.filename().string());
  require(row.diagnostics.first_order_fallbacks == 0U,
          "uniform freestream needs no reconstruction fallback");
  if (config.physics.mode == cfd::PhysicsMode::inviscid) {
    const cfd::ForceCoefficients force = residual.forces(row);
    require(force.cl_viscous == 0.0 && force.cd_viscous == 0.0 &&
                force.cmz_viscous == 0.0,
            "inviscid viscous force components are exactly zero");
  }

  if (case_path.filename() == "naca0012_m015_inviscid.json") {
    const cfd::CaloricallyPerfectGas gas(config.gas);
    for (std::size_t cell = 0; cell < mesh.cells.size(); ++cell) {
      const cfd::Vec2 center = mesh.cells[cell].center;
      const cfd::Primitive manufactured = gas.complete(
          1.0 + 0.02 * std::sin(0.07 * center.x) + 0.01 * std::cos(0.11 * center.y),
          1.0 + 0.03 * std::cos(0.05 * center.x - 0.09 * center.y),
          0.02 * std::sin(0.08 * center.x + 0.04 * center.y),
          config.freestream.pressure *
              (1.0 + 0.01 * std::cos(0.03 * center.x - 0.06 * center.y)));
      const cfd::Conservative state = gas.conservative(manufactured);
      if (cell < mesh.owned_cell_count) {
        for (std::size_t k = 0; k < 4U; ++k) U[cell * 4U + k] = state[k];
      } else {
        // Deliberately stale ghosts prove that the state halo is active.
        for (std::size_t k = 0; k < 4U; ++k) U[cell * 4U + k] = freestream[k];
      }
    }
    const cfd::ResidualResult manufactured = residual.evaluate(U, 1.0, true);
    cfd::ResidualEvaluationOptions first_order_options;
    first_order_options.reconstruction_blend = 0.0;
    const cfd::ResidualResult manufactured_first =
        residual.evaluate(U, 1.0, first_order_options);
    cfd::FrozenRusanovLUSGSOperator frozen_lusgs(
        mesh, config, U, manufactured_first, 1.0, communicator);
    std::vector<double> frozen_direction(mesh.cells.size() * 4U, 0.0);
    std::vector<double> frozen_rhs(mesh.cells.size() * 4U, 0.0);
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double gid = static_cast<double>(mesh.cells[cell].global_id + 1);
      for (std::size_t k = 0; k < 4U; ++k) {
        frozen_direction[cell * 4U + k] =
            std::sin(0.017 * gid * static_cast<double>(k + 1U));
        frozen_rhs[cell * 4U + k] =
            -manufactured_first.value[cell * 4U + k];
      }
    }
    std::vector<double> frozen_product;
    frozen_lusgs.apply(frozen_direction, frozen_product);
    std::array<double, 4> local_frozen_checksum{};
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double weight = 1.0 +
          0.001 * static_cast<double>(mesh.cells[cell].global_id % 101);
      for (std::size_t k = 0; k < 4U; ++k) {
        local_frozen_checksum[k] +=
            weight * frozen_product[cell * 4U + k];
      }
    }
    std::array<double, 4> global_frozen_checksum{};
    MPI_Allreduce(local_frozen_checksum.data(), global_frozen_checksum.data(),
                  4, MPI_DOUBLE, MPI_SUM, communicator);
    const std::array<double, 4> expected_frozen_checksum{
        -3574.9599661997554, -16193.943606264444,
        -3814.5019642983075, -321.60503717630985};
    for (std::size_t k = 0; k < 4U; ++k) {
      require(close(global_frozen_checksum[k], expected_frozen_checksum[k],
                    2.0e-11),
              "frozen full-block operator checksum is consistent on np1/2/8");
    }
    std::vector<double> lusgs_correction;
    const cfd::InnerSolveStats lusgs_stats = frozen_lusgs.solve(
        frozen_rhs, 3, 8, 0.1, lusgs_correction);
    require(lusgs_stats.linear_sweeps >= 3 &&
                lusgs_stats.linear_sweeps <= 8 &&
                std::isfinite(lusgs_stats.defect_ratio) &&
                lusgs_stats.defect_ratio < 1.0,
            "full-block LU-SGS reduces the MPI-global true frozen defect");
    cfd::ResidualEvaluationOptions half_order_options;
    half_order_options.reconstruction_blend = 0.5;
    const cfd::ResidualResult manufactured_half =
        residual.evaluate(U, 1.0, half_order_options);
    std::array<std::array<double, 4>, 3> local_blend_checksums{};
    const std::array<const cfd::ResidualResult*, 3> blend_rows{
        &manufactured_first, &manufactured_half, &manufactured};
    for (std::size_t blend = 0; blend < blend_rows.size(); ++blend) {
      for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
        const double weight = 1.0 +
            0.001 * static_cast<double>(mesh.cells[cell].global_id % 101);
        for (std::size_t k = 0; k < 4U; ++k) {
          local_blend_checksums[blend][k] +=
              weight * blend_rows[blend]->value[cell * 4U + k];
        }
      }
    }
    std::array<std::array<double, 4>, 3> global_blend_checksums{};
    MPI_Allreduce(local_blend_checksums.data(), global_blend_checksums.data(),
                  12, MPI_DOUBLE, MPI_SUM, communicator);
    const std::array<std::array<double, 4>, 3> expected_blend_checksums{{
        {{19.310129019134759, -73.948257740104268,
          4.7874168324943307, 1516.7542202742741}},
        {{15.996554742446705, -57.012208896270465,
          5.4466850428833977, 1300.113157468734}},
        {{12.66269148360249, -40.298195342337984,
          6.1718828143651638, 1081.1627352159785}}}};
    for (std::size_t blend = 0; blend < global_blend_checksums.size(); ++blend) {
      for (std::size_t k = 0; k < 4U; ++k) {
        require(close(global_blend_checksums[blend][k],
                      expected_blend_checksums[blend][k], 2.0e-11),
                "first/ramped/full residual checksum is partition independent");
      }
    }

    // Compare the production positivity-safe Jv against an
    // independently scaled centered finite difference of the same active
    // second-order residual on the real nonuniform distributed mesh.
    std::vector<double> direction(mesh.cells.size() * 4U, 0.0);
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double gid = static_cast<double>(mesh.cells[cell].global_id + 1);
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        direction[index] = std::sin(0.013 * gid * static_cast<double>(k + 1U)) /
                           std::max(1.0, std::abs(U[index]));
      }
    }
    cfd::SteadyMatrixFreeOperator matrix_free(
        mesh, residual, U, manufactured, 1.0, communicator);
    std::vector<double> production_jv;
    matrix_free.apply(direction, production_jv);
    const double centered_epsilon = 1.25 * matrix_free.last_epsilon();
    std::vector<double> plus = U;
    std::vector<double> minus = U;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        plus[index] += centered_epsilon * direction[index];
        minus[index] -= centered_epsilon * direction[index];
      }
    }
    cfd::ResidualEvaluationOptions jv_options;
    jv_options.compute_global_norms = false;
    jv_options.compute_global_diagnostics = false;
    jv_options.collect_surface = false;
    const cfd::ResidualResult plus_residual =
        residual.evaluate(plus, 1.0, jv_options);
    const cfd::ResidualResult minus_residual =
        residual.evaluate(minus, 1.0, jv_options);
    std::vector<double> centered(mesh.cells.size() * 4U, 0.0);
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        centered[index] =
            (plus_residual.value[index] - minus_residual.value[index]) /
                (2.0 * centered_epsilon) +
            manufactured.spectral_radius[cell] * direction[index];
      }
    }
    double local_jv_error = 0.0;
    double local_jv_reference = 0.0;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        const double difference = production_jv[index] - centered[index];
        local_jv_error += difference * difference;
        local_jv_reference += centered[index] * centered[index];
      }
    }
    std::array<double, 2> local_jv_comparison{
        local_jv_error, local_jv_reference};
    std::array<double, 2> global_jv_comparison{};
    MPI_Allreduce(local_jv_comparison.data(), global_jv_comparison.data(), 2,
                  MPI_DOUBLE, MPI_SUM, communicator);
    const double relative_jv_error =
        std::sqrt(global_jv_comparison[0] /
                  std::max(global_jv_comparison[1], 1.0e-300));
    require(matrix_free.last_epsilon() > 0.0 && relative_jv_error < 1.0e-2,
            "matrix-free second-order Jv matches independent centered FD: relative=" +
                std::to_string(relative_jv_error) +
                " epsilon=" + std::to_string(matrix_free.last_epsilon()));

    // Verify the production BDF2 matrix-free action against an independent
    // finite difference of the complete frozen-history physical residual and
    // prove that both histories remain immutable throughout every Jv.
    std::vector<double> history_n = U;
    std::vector<double> history_nm1 = U;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        history_n[index] += 1.0e-4 * direction[index];
        history_nm1[index] -= 2.0e-4 * direction[index];
      }
    }
    const std::vector<double> frozen_history_n = history_n;
    const std::vector<double> frozen_history_nm1 = history_nm1;
    constexpr double transient_dt = 0.01;
    cfd::TransientMatrixFreeOperator transient_matrix_free(
        mesh, residual, U, manufactured, history_n, history_nm1,
        transient_dt, 2, 1.0, communicator);
    std::vector<double> transient_jv;
    transient_matrix_free.apply(direction, transient_jv);
    const double transient_epsilon = 1.25 * transient_matrix_free.last_epsilon();
    plus = U;
    minus = U;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        plus[index] += transient_epsilon * direction[index];
        minus[index] -= transient_epsilon * direction[index];
      }
    }
    const cfd::ResidualResult transient_plus_residual =
        residual.evaluate(plus, 1.0, jv_options);
    const cfd::ResidualResult transient_minus_residual =
        residual.evaluate(minus, 1.0, jv_options);
    double local_transient_error = 0.0;
    double local_transient_reference = 0.0;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      cfd::Conservative plus_state{};
      cfd::Conservative minus_state{};
      cfd::Conservative history_state_n{};
      cfd::Conservative history_state_nm1{};
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        plus_state[k] = plus[index];
        minus_state[k] = minus[index];
        history_state_n[k] = history_n[index];
        history_state_nm1[k] = history_nm1[index];
      }
      const cfd::Conservative plus_physical = cfd::bdf_physical_residual(
          plus_state, history_state_n, history_state_nm1,
          mesh.cells[cell].area, transient_dt, 2);
      const cfd::Conservative minus_physical = cfd::bdf_physical_residual(
          minus_state, history_state_n, history_state_nm1,
          mesh.cells[cell].area, transient_dt, 2);
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        const double independent =
            (transient_plus_residual.value[index] + plus_physical[k] -
             transient_minus_residual.value[index] - minus_physical[k]) /
            (2.0 * transient_epsilon);
        const double difference = transient_jv[index] - independent;
        local_transient_error += difference * difference;
        local_transient_reference += independent * independent;
      }
    }
    std::array<double, 2> local_transient_comparison{
        local_transient_error, local_transient_reference};
    std::array<double, 2> global_transient_comparison{};
    MPI_Allreduce(local_transient_comparison.data(),
                  global_transient_comparison.data(), 2, MPI_DOUBLE, MPI_SUM,
                  communicator);
    const double relative_transient_error = std::sqrt(
        global_transient_comparison[0] /
        std::max(global_transient_comparison[1], 1.0e-300));
    require(relative_transient_error < 1.0e-2 &&
                history_n == frozen_history_n &&
                history_nm1 == frozen_history_nm1,
            "BDF2 matrix-free physical Jacobian matches finite difference and freezes histories");

    // The full-block preconditioner must carry the same alpha_0 V/dt physical
    // diagonal as the matrix-free dual-time operator.
    cfd::FrozenRusanovLUSGSOperator spatial_preconditioner(
        mesh, config, U, manufactured_first, 1.0, communicator, 0.0, 0.0);
    cfd::FrozenRusanovLUSGSOperator bdf2_preconditioner(
        mesh, config, U, manufactured_first, 1.0, communicator,
        transient_matrix_free.physical_diagonal(), 0.0);
    std::vector<double> spatial_preconditioned_product;
    std::vector<double> bdf2_preconditioned_product;
    spatial_preconditioner.apply(direction, spatial_preconditioned_product);
    bdf2_preconditioner.apply(direction, bdf2_preconditioned_product);
    double local_preconditioner_error = 0.0;
    double local_preconditioner_reference = 0.0;
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      for (std::size_t k = 0; k < 4U; ++k) {
        const std::size_t index = cell * 4U + k;
        const double expected = transient_matrix_free.physical_diagonal() *
                                mesh.cells[cell].area * direction[index];
        const double observed = bdf2_preconditioned_product[index] -
                                spatial_preconditioned_product[index];
        const double difference = observed - expected;
        local_preconditioner_error += difference * difference;
        local_preconditioner_reference += expected * expected;
      }
    }
    std::array<double, 2> local_preconditioner_comparison{
        local_preconditioner_error, local_preconditioner_reference};
    std::array<double, 2> global_preconditioner_comparison{};
    MPI_Allreduce(local_preconditioner_comparison.data(),
                  global_preconditioner_comparison.data(), 2, MPI_DOUBLE,
                  MPI_SUM, communicator);
    require(std::sqrt(global_preconditioner_comparison[0] /
                      std::max(global_preconditioner_comparison[1], 1.0e-300)) <
                1.0e-11,
            "full-block Rusanov LU-SGS includes the exact BDF2 physical diagonal");

    std::vector<double> jv_four;
    std::vector<double> jv_two;
    std::vector<double> jv_half;
    std::vector<double> jv_quarter;
    matrix_free.apply(direction, jv_four, 4.0);
    matrix_free.apply(direction, jv_two, 2.0);
    matrix_free.apply(direction, jv_half, 0.5);
    matrix_free.apply(direction, jv_quarter, 0.25);
    auto global_difference = [&](const std::vector<double>& left,
                                 const std::vector<double>& right) {
      double local = 0.0;
      for (std::size_t index = 0; index < mesh.owned_cell_count * 4U; ++index) {
        const double difference = left[index] - right[index];
        local += difference * difference;
      }
      double global = 0.0;
      MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, communicator);
      return std::sqrt(global);
    };
    const double coarse_error = global_difference(jv_four, jv_quarter);
    const double medium_error = global_difference(jv_two, jv_quarter);
    const double nominal_error = global_difference(production_jv, jv_quarter);
    const double fine_error = global_difference(jv_half, jv_quarter);
    require(medium_error < coarse_error && nominal_error < medium_error &&
                fine_error < nominal_error,
            "centered component-scaled Jv converges as epsilon is refined");
    const double low_residual_multiplier =
        cfd::steady_jfnk_epsilon_multiplier(1.0e-4, 1.0);
    std::vector<double> low_residual_jv;
    std::vector<double> low_residual_reference_jv;
    matrix_free.apply(direction, low_residual_jv, low_residual_multiplier);
    const double low_residual_epsilon = matrix_free.last_epsilon();
    matrix_free.apply(direction, low_residual_reference_jv,
                      0.1 * low_residual_multiplier);
    const double nominal_low_residual_error =
        global_difference(production_jv, low_residual_reference_jv);
    const double adaptive_low_residual_error =
        global_difference(low_residual_jv, low_residual_reference_jv);
    require(low_residual_multiplier == 1.0e-2 &&
                low_residual_epsilon > 0.0 &&
                adaptive_low_residual_error < nominal_low_residual_error,
            "residual-adaptive epsilon improves centered Jv on a smooth low-residual perturbation");
    std::array<double, 4> local_residual_checksum{};
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double weight = 1.0 +
          0.001 * static_cast<double>(mesh.cells[cell].global_id % 101);
      for (std::size_t k = 0; k < 4U; ++k) {
        local_residual_checksum[k] += weight * manufactured.value[cell * 4U + k];
      }
    }
    std::array<double, 4> global_residual_checksum{};
    MPI_Allreduce(local_residual_checksum.data(), global_residual_checksum.data(), 4,
                  MPI_DOUBLE, MPI_SUM, communicator);

    cfd::FlowSolver solver(mesh, config, communicator);
    cfd::RestartableSolution solution = solver.uniform_initial_solution();
    solution.U = U;
    solution.U_n = U;
    solution.U_nm1 = U;
    const cfd::StepResult step = solver.steady_step(solution);
    require(step.accepted && step.inner.nonlinear_iterations == 1 &&
                step.inner.linear_sweeps >= config.run_control.min_inner_iterations,
            "distributed manufactured state performs coupled correction sweeps");
    std::array<double, 4> local_state_checksum{};
    for (std::size_t cell = 0; cell < mesh.owned_cell_count; ++cell) {
      const double weight = 1.0 +
          0.001 * static_cast<double>(mesh.cells[cell].global_id % 101);
      for (std::size_t k = 0; k < 4U; ++k) {
        local_state_checksum[k] += weight * solution.U[cell * 4U + k];
      }
    }
    std::array<double, 4> global_state_checksum{};
    MPI_Allreduce(local_state_checksum.data(), global_state_checksum.data(), 4,
                  MPI_DOUBLE, MPI_SUM, communicator);
    constexpr double expected_l2 = 5.6278855038771116;
    const std::array<double, 4> expected_residual{
        12.66269148360249, -40.298195342337984,
        6.1718828143651638, 1081.1627352159785};
    require(close(manufactured.norms.total_l2, expected_l2, 2.0e-11),
            "manufactured residual norm matches serial baseline");
    for (std::size_t k = 0; k < 4U; ++k) {
      require(close(global_residual_checksum[k], expected_residual[k], 2.0e-11),
              "manufactured residual checksum is partition independent");
      require(std::isfinite(global_state_checksum[k]),
              "matrix-free implicit correction checksum is finite");
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  int size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  try {
    test_compensated_accumulation();
    test_frozen_rusanov_jacobian();
    test_gas_and_inviscid_flux();
    test_boundary_exterior_states();
    test_boundary_reconstruction(rank, size);
    test_venkatakrishnan_limiter(rank, size);
    test_reconstruction(rank, size);
    test_viscous_flux();
    test_restarted_gmres(rank, size);
    test_implicit_and_bdf(rank, size);
    test_force_sign_and_split(rank, size);
    test_collective_preflight(rank, size);
    const std::filesystem::path cases =
        std::filesystem::path(BENCHMARK_ROOT) / "inputs" / "cases";
    test_case_freestream_consistency(cases);
    test_mesh_freestream(cases / "naca0012_m015_inviscid.json", MPI_COMM_WORLD, rank);
    test_mesh_freestream(cases / "cylinder_m010_laminar_re200.json", MPI_COMM_WORLD, rank);
    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == 0) std::cout << "numerics tests passed\n";
    MPI_Finalize();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rank " << rank << ": " << error.what() << '\n';
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 1;
  }
}
