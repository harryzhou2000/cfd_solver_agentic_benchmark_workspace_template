#include "cfd/case_config.hpp"
#include "cfd/physics.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

cfd::CaseConfig test_case() {
  cfd::CaseConfig config;
  config.physics_mode = "laminar";
  config.viscous = true;
  config.reynolds = 100.0;
  config.gas = {1.4, 1.0, 0.72};
  config.freestream = {0.5, 0.0, 1.0, 1.0, 1.0 / (1.4 * 0.25)};
  config.reference = {1.0, 1.0, {}, 1.0};
  return config;
}

}  // namespace

int main() {
  try {
    const auto config = test_case();
    const cfd::PerfectGasPhysics physics(config);
    const cfd::Primitive p{1.2, 0.7, -0.3, 2.4};
    const auto u = physics.to_conserved(p);
    const auto roundtrip = physics.to_primitive(u);
    require(std::abs(roundtrip.rho - p.rho) < 1.0e-13, "primitive/conservative density round trip");
    require(std::abs(roundtrip.u - p.u) < 1.0e-13, "primitive/conservative velocity round trip");
    require(std::abs(roundtrip.p - p.p) < 1.0e-13, "primitive/conservative pressure round trip");

    const cfd::Vec2 normal{0.6, 0.8};
    const auto hllc = physics.hllc_flux(p, p, normal);
    const auto exact = physics.inviscid_physical_flux(p, normal);
    for (int k = 0; k < cfd::nvars; ++k) {
      require(std::abs(hllc.flux[static_cast<std::size_t>(k)] - exact[static_cast<std::size_t>(k)]) < 1.0e-12,
              "HLLC consistency");
    }
    const auto slip = physics.boundary_state(p, cfd::BoundaryType::slip_wall, normal);
    require(std::abs((slip.u + p.u) * normal.x + (slip.v + p.v) * normal.y) < 1.0e-12,
            "slip reflection must reverse normal velocity");
    require(std::abs(physics.viscosity() - 0.01) < 1.0e-14, "Reynolds-matched viscosity");
    std::cout << "all unit tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "unit test failure: " << error.what() << '\n';
    return 1;
  }
}
