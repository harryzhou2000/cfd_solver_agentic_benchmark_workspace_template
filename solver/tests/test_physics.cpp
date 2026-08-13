#include "numerics.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace aerofv;

namespace {
bool near(double a, double b, double tolerance = 1.0e-11) {
  return std::abs(a - b) <= tolerance;
}

void assert_finite_flux(const Conservative &flux) {
  for (const double value : flux) {
    assert(std::isfinite(value));
  }
}
} // namespace

int main() {
  const GasModel gas{};
  const Primitive primitive{1.2, 3.0, -0.5, 2.5};
  const Conservative conservative = primitive_to_conservative(primitive, gas);
  const Primitive restored = conservative_to_primitive(conservative, gas);
  assert(near(restored.rho, primitive.rho));
  assert(near(restored.u, primitive.u));
  assert(near(restored.v, primitive.v));
  assert(near(restored.p, primitive.p));

  const Conservative euler = euler_normal_flux(conservative, primitive, {1.0, 0.0});
  assert(near(euler[0], primitive.rho * primitive.u));
  assert(near(euler[1], primitive.rho * primitive.u * primitive.u + primitive.p));
  assert(near(euler[2], primitive.rho * primitive.u * primitive.v));

  const Conservative hllc_same = hllc_flux(conservative, conservative, {1.0, 0.0}, gas);
  const Conservative rusanov_same =
      rusanov_flux(conservative, conservative, {1.0, 0.0}, gas);
  for (std::size_t i = 0; i < 4; ++i) {
    assert(near(hllc_same[i], euler[i]));
    assert(near(rusanov_same[i], euler[i]));
  }
  const Conservative bad{0.0, 1.0, 2.0, -1.0};
  assert_finite_flux(hllc_flux(bad, conservative, {1.0, 0.0}, gas));
  assert_finite_flux(rusanov_flux(conservative, bad, {1.0, 0.0}, gas));

  const Primitive interior{1.0, 2.0, 3.0, 1.0};
  const Primitive freestream{1.0, 5.0, 0.0, 1.0};
  const Primitive slip = boundary_ghost_primitive(
      interior, freestream, {1.0, 0.0}, BoundaryType::slip_wall, gas);
  assert(near(slip.u, -2.0));
  assert(near(slip.v, 3.0));
  const Primitive no_slip = boundary_surface_primitive(
      interior, freestream, {1.0, 0.0}, BoundaryType::no_slip_adiabatic_wall, gas);
  assert(near(no_slip.u, 0.0));
  assert(near(no_slip.v, 0.0));
  const Conservative wall_flux =
      impermeable_wall_flux(interior, {3.0, 4.0}, gas);
  assert(near(wall_flux[0], 0.0));
  assert(near(wall_flux[1], 0.6 * interior.p));
  assert(near(wall_flux[2], 0.8 * interior.p));
  assert(near(wall_flux[3], 0.0));
  assert(near(-0.8 * wall_flux[1] + 0.6 * wall_flux[2], 0.0));
  const Primitive farfield = boundary_ghost_primitive(
      Primitive{1.0, 0.2, 0.1, 1.0}, Primitive{1.0, 0.15, 0.0, 1.0},
      {1.0, 0.0}, BoundaryType::farfield, gas);
  assert(physically_valid(farfield, gas));
  assert(std::abs(farfield.u - 0.2) < 0.1);

  PrimitiveGradient gradient{};
  gradient[1] = {3.0, 1.0};
  gradient[2] = {3.0, 4.0};
  gradient[3] = {1.0, 0.0};
  const Conservative viscous =
      viscous_normal_flux(Primitive{1.0, 1.0, 0.0, 1.0}, gradient, {1.0, 0.0},
                          0.5, gas);
  assert(near(viscous[0], 0.0));
  assert(near(viscous[1], 2.0 / 3.0));
  assert(near(viscous[2], 2.0));
  assert(std::isfinite(viscous[3]));
  assert(temperature_gradient(Primitive{1.0, 0.0, 0.0, 1.0}, gradient, gas).x ==
         1.0);

  // Both reconstructed traces below are at the same face and therefore agree
  // exactly for this linear field.  The viscous normal correction must use the
  // cell-centred endpoints instead; using the coincident face values would
  // incorrectly erase all normal derivative and wall shear.
  const Primitive left_cell{0.9, -0.5, -0.5, 0.8};
  const Primitive right_cell{1.1, 0.5, 0.5, 1.2};
  PrimitiveGradient linear_gradient{};
  linear_gradient[0] = {0.2, 0.3};
  linear_gradient[1] = {1.0, 0.4};
  linear_gradient[2] = {1.0, -0.2};
  linear_gradient[3] = {0.4, 0.1};
  const Primitive left_face = reconstruct_primitive(
      left_cell, linear_gradient, {0.5, 0.0});
  const Primitive right_face = reconstruct_primitive(
      right_cell, linear_gradient, {-0.5, 0.0});
  assert(near(left_face.rho, right_face.rho));
  assert(near(left_face.u, right_face.u));
  assert(near(left_face.v, right_face.v));
  assert(near(left_face.p, right_face.p));
  const PrimitiveGradient corrected = corrected_primitive_face_gradient(
      linear_gradient, linear_gradient, left_cell, right_cell, {1.0, 0.0},
      {1.0, 0.0});
  assert(near(corrected[0].x, 0.2));
  assert(near(corrected[1].x, 1.0));
  assert(near(corrected[2].x, 1.0));
  assert(near(corrected[3].x, 0.4));
  assert(near(corrected[0].y, 0.3));
  assert(near(corrected[1].y, 0.4));
  assert(near(corrected[2].y, -0.2));
  assert(near(corrected[3].y, 0.1));

  // A cell-centred reflected no-slip ghost must retain the tangential wall
  // shear of the same linear field, independent of face reconstruction.
  const Primitive wall_cell{1.0, 0.0, -0.5, 1.0};
  const Primitive wall_ghost = boundary_ghost_primitive(
      wall_cell, freestream, {1.0, 0.0}, BoundaryType::no_slip_adiabatic_wall,
      gas);
  PrimitiveGradient wall_gradient{};
  wall_gradient[2] = {1.0, 0.0};
  const PrimitiveGradient wall_corrected = corrected_primitive_face_gradient(
      wall_gradient, wall_gradient, wall_cell, wall_ghost, {1.0, 0.0},
      {1.0, 0.0});
  const Primitive wall_state = boundary_surface_primitive(
      wall_cell, freestream, {1.0, 0.0}, BoundaryType::no_slip_adiabatic_wall,
      gas);
  const Conservative wall_viscous = viscous_normal_flux(
      wall_state, wall_corrected, {1.0, 0.0}, 0.5, gas);
  assert(near(wall_corrected[2].x, 1.0));
  assert(near(wall_viscous[2], 0.5));

  const std::vector<Vec2> offsets{{1.0, 0.0}, {-1.0, 0.0}};
  assert(near(barth_jespersen_limiter(1.0, {2.0, 0.0}, 0.0, 2.0, offsets), 0.5));
  const Conservative valid = primitive_to_conservative({1.0, 0.0, 0.0, 1.0}, gas);
  Conservative invalid = valid;
  invalid[3] = 0.0;
  const Conservative limited = positivity_limited_state(valid, invalid, gas);
  assert(physically_valid(limited, gas));
  assert(positivity_scale(valid, invalid, gas) < 1.0);

  assert(convective_face_spectral_radius(primitive, {1.0, 0.0}, 2.0, gas) > 0.0);
  assert(viscous_face_spectral_radius(primitive, 1.0, 0.5, 0.01, gas) > 0.0);
  assert(local_pseudo_timestep(2.0, 3.0, 1.0, 2.0) == 1.0);
  std::cout << "physics/numerics tests passed\n";
}
