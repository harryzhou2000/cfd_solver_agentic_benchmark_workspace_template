#include "cfd/Physics.hpp"
#include "cfd/Reconstruction.hpp"
#include "cfd/SpatialOperator.hpp"

#include <mpi.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr double kTolerance = 1.0e-11;

bool near(double actual, double expected, double tolerance = kTolerance) {
  return std::abs(actual - expected) <=
         tolerance * std::max({1.0, std::abs(actual), std::abs(expected)});
}

void assert_finite(const cfd::Conserved& value) {
  for (double component : value) assert(std::isfinite(component));
}

cfd::CaseConfig make_config() {
  cfd::CaseConfig config;
  config.physics.mode = "inviscid";
  config.gas.gamma = 1.4;
  config.gas.gas_constant = 287.0;
  config.gas.prandtl = 0.72;
  config.freestream.rho = 1.2;
  config.freestream.velocity_magnitude = 80.0;
  config.freestream.aoa_degrees = 0.0;
  config.freestream.pressure = 101325.0;
  config.reference.length = 1.0;
  config.reference.area = 1.0;
  config.reference.moment_center = {0.0, 0.0};
  return config;
}

cfd::LocalMesh one_cell_square(const std::string& boundary_tag) {
  cfd::LocalMesh mesh;
  mesh.global_cells = 1;
  mesh.global_faces = 4;
  mesh.owned_count = 1;
  mesh.cells.push_back({0, 0, {0.5, 0.5}, 1.0, {}});
  mesh.faces = {
      {0, -1, {1.0, 0.0}, {1.0, 1.0}, {1.0, 0.5}, {1.0, 0.0}, 1.0, boundary_tag},
      {0, -1, {1.0, 1.0}, {0.0, 1.0}, {0.5, 1.0}, {0.0, 1.0}, 1.0, boundary_tag},
      {0, -1, {0.0, 1.0}, {0.0, 0.0}, {0.0, 0.5}, {-1.0, 0.0}, 1.0, boundary_tag},
      {0, -1, {0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, {0.0, -1.0}, 1.0, boundary_tag},
  };
  return mesh;
}

void test_farfield_uniform_conservation() {
  cfd::CaseConfig config = make_config();
  config.boundary_conditions["outer"] = "farfield";
  const cfd::LocalMesh mesh = one_cell_square("outer");
  const cfd::GasProperties gas = cfd::gas_properties(config);
  const cfd::Conserved uniform = cfd::freestream_conserved(config);
  std::vector<cfd::Conserved> state{uniform};
  std::vector<cfd::ReconstructionData> reconstruction;

  cfd::SpatialOperator spatial(mesh, config, MPI_COMM_WORLD);
  const cfd::SpatialEvaluation evaluation = spatial.evaluate(state, reconstruction);

  assert(evaluation.residual.size() == 1);
  assert(evaluation.spectral_radius.size() == 1);
  // The energy flux is O(1e7) for this dimensional fixture, so cancellation
  // leaves absolute roundoff near 1e-7 on some MPI/libm builds.
  for (double component : evaluation.residual.front()) assert(std::abs(component) < 1.0e-6);
  assert(std::isfinite(evaluation.spectral_radius.front()));
  assert(evaluation.spectral_radius.front() > 0.0);
  assert(near(evaluation.forces.pressure_drag, 0.0));
  assert(near(evaluation.forces.pressure_lift, 0.0));

  // A residual-derived update is finite and remains admissible.  This mirrors
  // the scalar spectral-radius update used by Solver::update_owned.
  const cfd::Primitive perturbed = cfd::complete_primitive(
      {0.9, 125.0, -20.0, 80000.0}, gas);
  state.front() = cfd::conserved_from_primitive(perturbed, gas);
  const cfd::SpatialEvaluation perturbed_evaluation = spatial.evaluate(state, reconstruction);
  assert_finite(perturbed_evaluation.residual.front());
  const double diagonal = perturbed_evaluation.spectral_radius.front() * 3.0;
  assert(std::isfinite(diagonal) && diagonal > 0.0);
  cfd::Conserved increment{};
  for (int variable = 0; variable < 4; ++variable) {
    increment[variable] = -0.2 * perturbed_evaluation.residual.front()[variable] / diagonal;
  }
  const cfd::Conserved updated = cfd::positivity_safe_update(state.front(), increment, gas);
  assert_finite(updated);
  assert(cfd::is_physical(updated, gas));
}

void test_wall_boundary_semantics() {
  cfd::CaseConfig config = make_config();
  const cfd::GasProperties gas = cfd::gas_properties(config);
  const cfd::Primitive interior =
      cfd::complete_primitive({1.1, 3.0, 4.0, 90000.0}, gas);

  {
    config.boundary_conditions = {{"wall", "slip_wall"}};
    const cfd::LocalMesh mesh = one_cell_square("wall");
    cfd::Reconstructor reconstructor(mesh, config, MPI_COMM_WORLD);
    const cfd::Primitive wall = reconstructor.boundary_value(mesh.faces.front(), interior);
    assert(near(wall.u, 0.0));
    assert(near(wall.v, interior.v));

    const std::vector<cfd::Conserved> state{cfd::conserved_from_primitive(interior, gas)};
    const std::vector<cfd::ReconstructionData> data(1);
    const cfd::FaceStates face = reconstructor.face_states(mesh.faces.front(), state, data);
    assert(near(face.right.u, -interior.u));
    assert(near(face.right.v, interior.v));
    assert(near(face.right.rho, interior.rho));
    assert(near(face.right.p, interior.p));
  }

  {
    config.boundary_conditions = {{"wall", "no_slip_adiabatic_wall"}};
    const cfd::LocalMesh mesh = one_cell_square("wall");
    cfd::Reconstructor reconstructor(mesh, config, MPI_COMM_WORLD);
    const cfd::Primitive wall = reconstructor.boundary_value(mesh.faces.front(), interior);
    assert(near(wall.u, 0.0));
    assert(near(wall.v, 0.0));

    const std::vector<cfd::Conserved> state{cfd::conserved_from_primitive(interior, gas)};
    const std::vector<cfd::ReconstructionData> data(1);
    const cfd::FaceStates face = reconstructor.face_states(mesh.faces.front(), state, data);
    assert(near(face.right.u, -interior.u));
    assert(near(face.right.v, -interior.v));
    assert(near(face.right.rho, interior.rho));
    assert(near(face.right.p, interior.p));
  }
}

void test_viscous_force_excludes_normal_velocity() {
  // The vertical wall has tangent (0,1). Normal velocity contributes no skin
  // friction; tangential velocity gives mu*u_t/d times area along the tangent.
  const cfd::Vec2 normal_only =
      cfd::tangential_wall_shear_force({3.0, 0.0}, {1.0, 0.0}, 2.0, 0.5, 2.0);
  assert(near(normal_only.x, 0.0));
  assert(near(normal_only.y, 0.0));
  const cfd::Vec2 shear =
      cfd::tangential_wall_shear_force({3.0, 4.0}, {1.0, 0.0}, 2.0, 0.5, 2.0);
  assert(near(shear.x, 0.0));
  assert(near(shear.y, 2.0));
}

}  // namespace

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  test_farfield_uniform_conservation();
  test_wall_boundary_semantics();
  test_viscous_force_excludes_normal_velocity();
  MPI_Finalize();
  std::cout << "Spatial operator tests passed\n";
}
