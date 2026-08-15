#include "cfd/Physics.hpp"
#include "cfd/Solver.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

constexpr double kTolerance = 1.0e-12;

bool near(double actual, double expected, double tolerance = kTolerance) {
  return std::abs(actual - expected) <= tolerance * std::max({1.0, std::abs(actual), std::abs(expected)});
}

void assert_same(const cfd::Conserved& actual, const cfd::Conserved& expected) {
  for (std::size_t i = 0; i < actual.size(); ++i) assert(near(actual[i], expected[i]));
}

}  // namespace

int main() {
  const cfd::GasProperties gas{1.4, 287.0, 0.72};
  const cfd::Primitive state = cfd::complete_primitive({1.25, 30.0, -4.0, 101325.0}, gas);

  // Conversions preserve each primary variable and produce meaningful derived values.
  const cfd::Conserved conserved = cfd::conserved_from_primitive(state, gas);
  const cfd::Primitive recovered = cfd::primitive_from_conserved(conserved, gas);
  assert(near(recovered.rho, state.rho));
  assert(near(recovered.u, state.u));
  assert(near(recovered.v, state.v));
  assert(near(recovered.p, state.p));
  assert(recovered.temperature > 0.0 && recovered.sound_speed > 0.0 && recovered.mach > 0.0);

  // A uniform face has the physical Euler flux independent of the LLF jump term.
  const cfd::Vec2 normal{1.0, 0.0};
  assert_same(cfd::rusanov_flux(state, state, normal, gas),
              cfd::euler_normal_flux(state, normal, gas));

  // A stationary contact keeps the pressure traction exactly; Rusanov only diffuses density.
  const cfd::Primitive contact_left = cfd::complete_primitive({1.0, 0.0, 0.0, 2.0}, gas);
  const cfd::Primitive contact_right = cfd::complete_primitive({2.0, 0.0, 0.0, 2.0}, gas);
  const cfd::Conserved contact_flux = cfd::rusanov_flux(contact_left, contact_right, normal, gas);
  assert(near(contact_flux[1], 2.0));
  assert(near(contact_flux[2], 0.0));
  assert(near(contact_flux[3], 0.0));

  // Zero viscosity must disable every viscous contribution even with nonzero gradients.
  const cfd::PrimitiveGradients gradients{{1.0, 2.0}, {-3.0, 4.0}, {5.0, -6.0}};
  assert_same(cfd::viscous_normal_flux(state, gradients, normal, gas, 0.0), {0.0, 0.0, 0.0, 0.0});

  // An energy-depleting update is safely shortened before it reaches nonpositive pressure.
  const cfd::Conserved dangerous_increment{0.0, 0.0, 0.0, -2.0 * conserved[3]};
  const cfd::Conserved safe = cfd::positivity_safe_update(conserved, dangerous_increment, gas);
  assert(cfd::is_physical(safe, gas));
  assert(!cfd::is_physical(cfd::Conserved{1.0, 0.0, 0.0, -1.0}, gas));

  // Strict residual-target acceptance must also see a quiet trailing force
  // window.  Before 1000 samples it checks all available history; afterward
  // it deliberately ignores older startup transients.
  std::vector<double> drag(50, 0.3);
  std::vector<double> lift(50, -0.01);
  assert(cfd::has_stable_terminal_force_window(drag, lift));
  drag.back() += 0.021;
  assert(!cfd::has_stable_terminal_force_window(drag, lift));
  assert(!cfd::has_stable_terminal_force_window(drag, {}));

  drag.assign(1001, 0.3);
  lift.assign(1001, -0.01);
  drag.front() = 3.0;
  lift.front() = -3.0;
  assert(cfd::has_stable_terminal_force_window(drag, lift));
  lift.back() += 0.021;
  assert(!cfd::has_stable_terminal_force_window(drag, lift));

  std::cout << "Physics tests passed\n";
}
