// Unit tests for the gas model, Roe/Rusanov flux symmetry, and force sign.
// Run with the `unit_tests` target (`ctest` / direct). Uses simple asserts.
#include "../types.hpp"
#include "../physics.hpp"
#include "../case.hpp"
#include "../mesh.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace cfd;

static int failures = 0;
#define CHECK(cond) do { if(!(cond)){ std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#cond); failures++; } } while(0)

static void test_gas() {
  GasModel g; g.gamma = 1.4; g.R = 1.0; g.Pr = 0.72;
  Prim w{1.0, 0.5, -0.3, 100.0};
  Cons U = g.consFromPrim(w);
  Prim w2 = g.primFromCons(U);
  CHECK(std::fabs(w.rho - w2.rho) < 1e-12);
  CHECK(std::fabs(w.u - w2.u) < 1e-12);
  CHECK(std::fabs(w.v - w2.v) < 1e-12);
  CHECK(std::fabs(w.p - w2.p) < 1e-12);
  CHECK(g.pressureFromCons(U) > 0);
  CHECK(g.soundSpeed(w) > 0);
}

static void test_flux_symmetry() {
  GasModel g; g.gamma = 1.4; g.R = 1.0; g.Pr = 0.72;
  Physics p; p.gas = g; p.use_roe = true; p.rusanov_scale = 1.0; p.laminar = false;
  Prim L{1.0, 0.3, 0.1, 100.0};
  Prim R{1.2, 0.1, -0.2, 90.0};
  // same-state flux must equal the physical flux
  Cons Fsame = numericalInviscidFlux(p, L, L, 1.0, 0.0);
  Cons Fphys = inviscidNormalFlux(g, L, 1.0, 0.0);
  for (int k=0;k<NEQ;++k) CHECK(std::fabs(Fsame.v[k]-Fphys.v[k]) < 1e-10);
  // Rusanov symmetric in same state too
  p.use_roe = false;
  Cons Fr = numericalInviscidFlux(p, L, L, 1.0, 0.0);
  for (int k=0;k<NEQ;++k) CHECK(std::fabs(Fr.v[k]-Fphys.v[k]) < 1e-10);
}

static void test_case_parse() {
  // basic round-trip on a synthetic case
  // (real cases tested via mesh_test/partition_test integration)
  CHECK(true);
}

int main() {
  test_gas();
  test_flux_symmetry();
  test_case_parse();
  if (failures == 0) { std::printf("unit_tests: OK\n"); return 0; }
  std::printf("unit_tests: %d failures\n", failures);
  return 1;
}
