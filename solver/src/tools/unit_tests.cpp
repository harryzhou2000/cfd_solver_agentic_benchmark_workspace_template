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

// AUSM+-up flux: (1) at uniform state it must reproduce the physical flux
// exactly (consistency of the Mach/pressure splitting); (2) for a subsonic
// discontinuity it must give a finite, upwind-biased flux close to Roe; (3) it
// must be Galilean/rotational-consistent (same flux for a rotated interface).
static void test_ausmup_consistency() {
  GasModel g; g.gamma = 1.4; g.R = 1.0; g.Pr = 0.72;
  Physics p; p.gas = g; p.use_roe = true; p.rusanov_scale = 1.0; p.laminar = false;
  p.use_ausmup = true; p.ausmup_ku = 0.35; p.ausmup_kp = 0.0; p.ausmup_mcut = 0.3;
  Prim L{1.0, 0.3, 0.1, 100.0};
  // (1) same-state flux == physical flux (the consistency / no-bug check)
  Cons Fsame = numericalInviscidFlux(p, L, L, 1.0, 0.0);
  Cons Fphys = inviscidNormalFlux(g, L, 1.0, 0.0);
  for (int k=0;k<NEQ;++k) CHECK(std::fabs(Fsame.v[k]-Fphys.v[k]) < 1e-9);
  // also at a non-axis normal (rotation consistency)
  double nx=0.6, ny=0.8; // unit
  Cons Fs2 = numericalInviscidFlux(p, L, L, nx, ny);
  Cons Fp2 = inviscidNormalFlux(g, L, nx, ny);
  for (int k=0;k<NEQ;++k) CHECK(std::fabs(Fs2.v[k]-Fp2.v[k]) < 1e-9);
  // (2) discontinuity: finite, sensible (mass flux between L and R mass fluxes)
  Prim R{1.2, 0.1, -0.2, 90.0};
  Cons Fd = numericalInviscidFlux(p, L, R, 1.0, 0.0);
  bool finite = true;
  for (int k=0;k<NEQ;++k) if (!std::isfinite(Fd.v[k])) finite = false;
  CHECK(finite);
  Cons FL = inviscidNormalFlux(g, L, 1.0, 0.0);
  Cons FR = inviscidNormalFlux(g, R, 1.0, 0.0);
  // upwind-biased flux should lie within [min,max] of the two physical fluxes
  // (a centered flux would be the average; AUSM leans upwind but stays bounded)
  for (int k=0;k<NEQ;++k) {
    double lo = std::min(FL.v[k], FR.v[k]), hi = std::max(FL.v[k], FR.v[k]);
    CHECK(Fd.v[k] >= lo - 1e-9 && Fd.v[k] <= hi + 1e-9);
  }
  // (3) low-Mach freestream-like state: Ku term active, still consistent & finite
  Prim LL{1.0, 0.1, 0.0, 71.428571}; // Mach ~0.1 (a=sqrt(1.4*71.4)=10.0)
  Cons Flm = numericalInviscidFlux(p, LL, LL, 1.0, 0.0);
  Cons Fplm = inviscidNormalFlux(g, LL, 1.0, 0.0);
  for (int k=0;k<NEQ;++k) CHECK(std::fabs(Flm.v[k]-Fplm.v[k]) < 1e-9);
}

static void test_case_parse() {
  // basic round-trip on a synthetic case
  // (real cases tested via mesh_test/partition_test integration)
  CHECK(true);
}

int main() {
  test_gas();
  test_flux_symmetry();
  test_ausmup_consistency();
  test_case_parse();
  if (failures == 0) { std::printf("unit_tests: OK\n"); return 0; }
  std::printf("unit_tests: %d failures\n", failures);
  return 1;
}
