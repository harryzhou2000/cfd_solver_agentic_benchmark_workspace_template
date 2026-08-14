#include "fluxes.h"
#include <cstdio>
#include <cmath>

namespace cfd2d {

int runTests() {
  GasPhysics gas;
  gas.gamma = 1.4;
  gas.R = 1.0;
  gas.prandtl = 0.72;
  gas.rusanovScale = 1.0;
  gas.viscous = false;
  gas.rhoInf = 1.0;
  gas.uInf = 0.15;
  gas.vInf = 0.0;
  gas.pInf = 1.0 / (1.4 * 0.15 * 0.15);
  gas.machInf = 0.15;
  gas.mu = 0.0;

  // Test conservative <-> primitive round trip
  PrimState P = {1.0, 0.15, 0.0, gas.pInf};
  ConsState U = gas.primToCons(P);
  PrimState P2 = gas.consToPrim(U);
  bool ok = true;
  for (int i = 0; i < 4; ++i) {
    if (std::abs(P[i] - P2[i]) > 1e-10) { ok = false; printf("consToPrim round trip failed at %d\n", i); }
  }

  // Test sound speed
  double a = gas.soundSpeed(P);
  double expected = std::sqrt(1.4 * gas.pInf / 1.0);
  if (std::abs(a - expected) > 1e-10) { ok = false; printf("sound speed failed: %f vs %f\n", a, expected); }

  // Test Rusanov flux symmetry (should be symmetric for identical states)
  ConsState F = gas.rusanovFlux(U, U, 1.0, 0.0);
  // For identical states, flux = inviscid flux
  ConsState Fexp, Gexp;
  gas.inviscidFlux(U, Fexp, Gexp);
  for (int i = 0; i < 4; ++i) {
    if (std::abs(F[i] - Fexp[i]) > 1e-10) { ok = false; printf("rusanov identical-state flux failed at %d: %f vs %f\n", i, F[i], Fexp[i]); }
  }

  if (ok) printf("All tests passed.\n");
  return ok ? 0 : 1;
}

} // namespace cfd2d

int main() {
  return cfd2d::runTests();
}
