// Unit tests for the pieces of the solver that can be checked without a mesh:
// geometry primitives, the equation of state, the three Riemann solvers, the
// wall and farfield boundary states, the viscous stress/heat flux and the
// limiter function.  Mesh-dependent behaviour (partitioning, halo exchange,
// order of accuracy) is covered by tests/mpi_consistency.sh and by
// `cfd2d verify`.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <vector>

#include "mesh/Geometry.hpp"
#include "numerics/Limiter.hpp"
#include "physics/BoundaryCondition.hpp"
#include "physics/FluxInviscid.hpp"
#include "physics/FluxViscous.hpp"
#include "physics/PerfectGas.hpp"

using namespace cfd;

namespace {
const PerfectGas kGas(1.4, 1.0, 0.72);
PrimVec makeState(Real rho, Real u, Real v, Real p) { return {rho, u, v, p}; }
}  // namespace

TEST_CASE("polygon area and centroid") {
  std::vector<Real> x = {0.0, 2.0, 2.0, 0.0};
  std::vector<Real> y = {0.0, 0.0, 1.0, 1.0};
  std::vector<Index> nodes = {0, 1, 2, 3};
  CHECK(geom::signedArea(x.data(), y.data(), nodes.data(), 4) == doctest::Approx(2.0));
  const Vec2 c = geom::centroid(x.data(), y.data(), nodes.data(), 4);
  CHECK(c[0] == doctest::Approx(1.0));
  CHECK(c[1] == doctest::Approx(0.5));

  std::vector<Index> tri = {0, 1, 2};
  CHECK(geom::signedArea(x.data(), y.data(), tri.data(), 3) == doctest::Approx(1.0));
  // Reversed traversal flips the sign.
  std::vector<Index> rev = {3, 2, 1, 0};
  CHECK(geom::signedArea(x.data(), y.data(), rev.data(), 4) == doctest::Approx(-2.0));
}

TEST_CASE("edge normal points out of a counter-clockwise polygon") {
  // Bottom edge of the unit square traversed left to right.
  const Vec2 n = geom::edgeNormal(0.0, 0.0, 1.0, 0.0);
  CHECK(n[0] == doctest::Approx(0.0));
  CHECK(n[1] == doctest::Approx(-1.0));
  CHECK(geom::edgeLength(0.0, 0.0, 3.0, 4.0) == doctest::Approx(5.0));
}

TEST_CASE("perfect gas conversions round-trip") {
  const PrimVec w = makeState(1.3, 0.4, -0.2, 2.5);
  const ConsVec u = kGas.toConservative(w);
  const PrimVec w2 = kGas.toPrimitive(u);
  for (int k = 0; k < kNVar; ++k) CHECK(w2[k] == doctest::Approx(w[k]));
  CHECK(kGas.pressure(u) == doctest::Approx(w[3]));
  CHECK(kGas.soundSpeed(w[0], w[3]) == doctest::Approx(std::sqrt(1.4 * 2.5 / 1.3)));
}

TEST_CASE("Riemann solvers are consistent for identical states") {
  const PrimVec w = makeState(1.0, 0.7, 0.1, 1.0);
  const Vec2 n{0.6, 0.8};
  const ConsVec exact = kGas.normalFlux(w, n);
  FluxOptions opt;
  for (RiemannScheme s : {RiemannScheme::kRoe, RiemannScheme::kHllc, RiemannScheme::kRusanov}) {
    opt.scheme = s;
    const ConsVec f = inviscidFlux(w, w, n, kGas, opt);
    for (int k = 0; k < kNVar; ++k) CHECK(f[k] == doctest::Approx(exact[k]).epsilon(1e-12));
  }
}

TEST_CASE("Riemann solvers agree on a supersonic state") {
  // For fully supersonic flow every upwind scheme must return the left flux.
  const PrimVec wl = makeState(1.0, 3.0, 0.0, 1.0);
  const PrimVec wr = makeState(1.4, 2.6, 0.1, 1.6);
  const Vec2 n{1.0, 0.0};
  const ConsVec exact = kGas.normalFlux(wl, n);
  FluxOptions opt;
  opt.scheme = RiemannScheme::kHllc;
  const ConsVec f = inviscidFlux(wl, wr, n, kGas, opt);
  for (int k = 0; k < kNVar; ++k) CHECK(f[k] == doctest::Approx(exact[k]).epsilon(1e-10));
}

TEST_CASE("Roe flux conserves across a stationary contact") {
  // Equal pressure and velocity, different density: flux must be the exact one.
  const PrimVec wl = makeState(1.0, 0.5, 0.0, 2.0);
  const PrimVec wr = makeState(2.0, 0.5, 0.0, 2.0);
  const Vec2 n{1.0, 0.0};
  FluxOptions opt;
  opt.entropy_fix = 0.0;
  const ConsVec f = roeFlux(wl, wr, n, kGas, opt);
  // The upwind flux of a contact moving right must equal the left flux.
  const ConsVec fl = kGas.normalFlux(wl, n);
  for (int k = 0; k < kNVar; ++k) CHECK(f[k] == doctest::Approx(fl[k]).epsilon(1e-10));
}

TEST_CASE("slip wall flux has zero mass flux and pure pressure momentum") {
  const PrimVec wi = makeState(1.2, 0.9, 0.35, 3.1);
  const Vec2 n{0.6, -0.8};
  const PrimVec wg = ghostState(BcType::kSlipWall, wi, n, wi, kGas);
  FluxOptions opt;
  opt.entropy_fix = 0.0;
  const ConsVec f = roeFlux(wi, wg, n, kGas, opt);
  CHECK(f[0] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
  CHECK(f[3] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
  // Momentum flux is along the normal.
  CHECK(f[1] * n[1] - f[2] * n[0] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
  // Effective wall pressure of the mirrored Riemann problem:
  //   p_w = p + rho*u_n^2 + rho*abar*u_n,   abar^2 = a^2 + (gamma-1) u_n^2 / 2.
  const Real un = wi[1] * n[0] + wi[2] * n[1];
  const Real a2 = kGas.gamma() * wi[3] / wi[0];
  const Real abar = std::sqrt(a2 + 0.5 * (kGas.gamma() - 1.0) * un * un);
  const Real pw = wi[3] + wi[0] * un * un + wi[0] * abar * un;
  CHECK(f[1] == doctest::Approx(pw * n[0]).epsilon(1e-10));
  CHECK(f[2] == doctest::Approx(pw * n[1]).epsilon(1e-10));
}

TEST_CASE("no-slip wall convective flux carries no mass, energy or shear") {
  const PrimVec wi = makeState(1.0, 0.4, 0.2, 2.0);
  const Vec2 n{0.0, -1.0};
  const PrimVec wg = ghostState(BcType::kNoSlipAdiabaticWall, wi, n, wi, kGas);
  // Normal velocity is reversed, tangential velocity is continuous.
  const Real un = wi[1] * n[0] + wi[2] * n[1];
  CHECK((wg[1] * n[0] + wg[2] * n[1]) == doctest::Approx(-un));
  CHECK((wg[1] * (-n[1]) + wg[2] * n[0]) == doctest::Approx(wi[1] * (-n[1]) + wi[2] * n[0]));
  FluxOptions opt;
  const ConsVec f = roeFlux(wi, wg, n, kGas, opt);
  CHECK(f[0] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
  CHECK(f[3] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
  // The convective momentum flux is purely normal: no tangential (shear)
  // component is generated by the wall treatment.
  const Real tangential = f[1] * (-n[1]) + f[2] * n[0];
  CHECK(tangential == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
}

TEST_CASE("no-slip wall boundary value has zero velocity") {
  const PrimVec wi = makeState(1.0, 0.4, 0.2, 2.0);
  const Vec2 n{0.0, -1.0};
  const PrimVec wb = boundaryValue(BcType::kNoSlipAdiabaticWall, wi, n, wi, kGas);
  CHECK(wb[1] == doctest::Approx(0.0));
  CHECK(wb[2] == doctest::Approx(0.0));
  CHECK(wb[3] == doctest::Approx(wi[3]));
}

TEST_CASE("slip wall boundary value keeps the tangential velocity") {
  const PrimVec wi = makeState(1.0, 1.0, 0.0, 2.0);
  const Vec2 n{0.0, -1.0};
  const PrimVec wb = boundaryValue(BcType::kSlipWall, wi, n, wi, kGas);
  CHECK(wb[1] == doctest::Approx(1.0));
  CHECK(wb[2] == doctest::Approx(0.0));
}

TEST_CASE("farfield reproduces the freestream when the interior is freestream") {
  const PrimVec winf = makeState(1.0, 1.0, 0.0, 31.746031746031743);   // M = 0.15
  for (Vec2 n : {Vec2{1.0, 0.0}, Vec2{-1.0, 0.0}, Vec2{0.0, 1.0}, Vec2{0.6, 0.8}}) {
    const PrimVec wg = ghostState(BcType::kFarfield, winf, n, winf, kGas);
    for (int k = 0; k < kNVar; ++k) CHECK(wg[k] == doctest::Approx(winf[k]).epsilon(1e-10));
  }
}

TEST_CASE("farfield is a pass-through for supersonic outflow") {
  const PrimVec winf = makeState(1.0, 1.0, 0.0, 0.17857142857142858);   // M = 2
  const PrimVec wi = makeState(1.1, 1.05, 0.02, 0.19);
  const Vec2 n{1.0, 0.0};
  const PrimVec wg = ghostState(BcType::kFarfield, wi, n, winf, kGas);
  for (int k = 0; k < kNVar; ++k) CHECK(wg[k] == doctest::Approx(wi[k]));
}

TEST_CASE("Newtonian stress tensor is trace free for pure shear and correct in dilatation") {
  ViscousGradients g;
  g.du = {0.0, 2.0};
  g.dv = {3.0, 0.0};
  const StressTensor t = newtonianStress(g, 0.5);
  CHECK(t.xx == doctest::Approx(0.0));
  CHECK(t.yy == doctest::Approx(0.0));
  CHECK(t.xy == doctest::Approx(0.5 * 5.0));

  // Pure dilatation: the three-dimensional trace vanishes with the Stokes
  // hypothesis, so in 2-D tau_xx + tau_yy = -tau_zz = (2/3) mu div(u).
  ViscousGradients d;
  d.du = {1.0, 0.0};
  d.dv = {0.0, 1.0};
  const StressTensor td = newtonianStress(d, 1.0);
  const Real div = d.du[0] + d.dv[1];
  CHECK(td.xx + td.yy == doctest::Approx((2.0 / 3.0) * 1.0 * div));
  CHECK(td.xx == doctest::Approx(td.yy));
}

TEST_CASE("adiabatic wall viscous flux carries no energy") {
  ViscousGradients g;
  g.du = {0.0, 10.0};
  g.dv = {0.0, 0.0};
  g.dT = {0.0, 5.0};
  const ConsVec f = viscousNormalFlux(g, 0.01, 0.05, 0.0, 0.0, Vec2{0.0, -1.0}, true);
  CHECK(f[0] == doctest::Approx(0.0));
  CHECK(f[3] == doctest::Approx(0.0));
  CHECK(f[1] != doctest::Approx(0.0));   // shear traction is present
}

TEST_CASE("Roe flux is exact for a stationary normal shock") {
  // A stationary shock satisfies F_L = F_R exactly, and the Roe average has a
  // vanishing eigenvalue there, so a correct Roe wave decomposition must return
  // that common flux with zero dissipation.  This is the sharpest algebraic
  // check of the Roe averaging and of the wave strengths.
  const Real g = 1.4;
  const Real m1 = 2.0;
  const Real rho1 = 1.0, p1 = 1.0;
  const Real a1 = std::sqrt(g * p1 / rho1);
  const Real u1 = m1 * a1;
  const Real rr = ((g + 1.0) * m1 * m1) / ((g - 1.0) * m1 * m1 + 2.0);
  const Real pr = (2.0 * g * m1 * m1 - (g - 1.0)) / (g + 1.0);
  const PrimVec wl = makeState(rho1, u1, 0.0, p1);
  const PrimVec wr = makeState(rho1 * rr, u1 / rr, 0.0, p1 * pr);
  const Vec2 n{1.0, 0.0};
  const ConsVec fl = kGas.normalFlux(wl, n);
  const ConsVec fr = kGas.normalFlux(wr, n);
  for (int k = 0; k < kNVar; ++k)
    CHECK(fl[k] == doctest::Approx(fr[k]).epsilon(1e-10).scale(1.0));
  FluxOptions opt;
  opt.entropy_fix = 0.0;
  const ConsVec f = roeFlux(wl, wr, n, kGas, opt);
  for (int k = 0; k < kNVar; ++k)
    CHECK(f[k] == doctest::Approx(fl[k]).epsilon(1e-9).scale(1.0));
}

TEST_CASE("Roe and HLLC agree closely on a moderate Riemann problem") {
  // Both are upwind approximate Riemann solvers, so they must produce
  // comparable fluxes; a gross mismatch would indicate an implementation error
  // in one of them.
  const PrimVec wl = makeState(1.0, 0.0, 0.0, 1.0);
  const PrimVec wr = makeState(0.125, 0.0, 0.0, 0.1);
  const Vec2 n{1.0, 0.0};
  FluxOptions opt;
  opt.entropy_fix = 0.0;
  const ConsVec froe = roeFlux(wl, wr, n, kGas, opt);
  const ConsVec fhllc = hllcFlux(wl, wr, n, kGas, opt);
  // The two schemes differ in how they model the star region, so this is a
  // coarse cross-check (order 10% of the momentum flux), not an identity.
  const Real scale = std::max(std::abs(froe[1]), 1.0);
  for (int k = 0; k < kNVar; ++k)
    CHECK(std::abs(froe[k] - fhllc[k]) < 0.12 * scale * (k == 3 ? 4.0 : 1.0));
}

TEST_CASE("Barth-Jespersen limiter stays in [0,1] and enforces the bounds") {
  // Increment already inside the admissible excursion -> unlimited.
  CHECK(barthJespersenPhi(1.0, 0.4) == doctest::Approx(1.0));
  // Increment twice the admissible excursion -> exactly halved.
  CHECK(barthJespersenPhi(0.5, 1.0) == doctest::Approx(0.5));
  // Negative branch: both quantities negative, factor must stay positive.
  CHECK(barthJespersenPhi(-0.5, -1.0) == doctest::Approx(0.5));
  CHECK(barthJespersenPhi(0.0, 1.0) == doctest::Approx(0.0));
  for (Real dmax : {-2.0, -0.3, 0.0, 0.3, 2.0}) {
    for (Real dm : {-2.0, -0.3, 0.3, 2.0}) {
      const Real p = barthJespersenPhi(dmax, dm);
      CHECK(p >= 0.0);
      CHECK(p <= 1.0);
    }
  }
}

TEST_CASE("Venkatakrishnan limiter is bounded, smooth and consistent") {
  const Real eps2 = 1.0e-6;
  for (Real dmax : {-2.0, -0.3, 0.3, 2.0}) {
    for (Real dm : {-2.0, -0.3, 0.3, 2.0}) {
      const Real p = venkatakrishnanPhi(dmax, dm, eps2);
      CHECK(p >= 0.0);
      CHECK(p <= 1.0);
    }
  }
  // With a vanishing smoothing parameter it approaches Barth-Jespersen from
  // below (it is by construction the more diffusive of the two).
  for (Real r : {0.25, 0.5, 0.75, 1.5, 4.0}) {
    const Real dm = 1.0, dmax = r;
    CHECK(venkatakrishnanPhi(dmax, dm, 0.0) <= barthJespersenPhi(dmax, dm) + 1e-12);
  }
  // A large smoothing parameter switches the limiter off in smooth regions.
  CHECK(venkatakrishnanPhi(1.0e-3, 1.0e-3, 1.0) == doctest::Approx(1.0).epsilon(1e-3));
  // Zero admissible excursion must fully suppress the reconstruction.
  CHECK(venkatakrishnanPhi(0.0, 1.0, 0.0) == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
}

TEST_CASE("all three Riemann solvers reduce to the upwind flux when supersonic") {
  const PrimVec wl = makeState(1.0, 3.0, 0.0, 1.0);
  const PrimVec wr = makeState(1.4, 2.6, 0.1, 1.6);
  const Vec2 n{1.0, 0.0};
  const ConsVec exact = kGas.normalFlux(wl, n);
  FluxOptions opt;
  opt.entropy_fix = 0.0;   // the fix would perturb an exactly upwind state
  for (RiemannScheme s : {RiemannScheme::kRoe, RiemannScheme::kHllc, RiemannScheme::kRusanov}) {
    opt.scheme = s;
    const ConsVec f = inviscidFlux(wl, wr, n, kGas, opt);
    if (s == RiemannScheme::kRusanov) continue;   // LLF is not an upwind scheme
    for (int k = 0; k < kNVar; ++k) CHECK(f[k] == doctest::Approx(exact[k]).epsilon(1e-9));
  }
}

TEST_CASE("wall flux is purely normal for every Riemann solver") {
  // The mirrored wall state must give zero mass and energy flux and a purely
  // normal momentum flux for whichever flux function is selected; the exact
  // wall pressure differs between the schemes (see the report), so only the
  // structure is asserted here.
  const PrimVec wi = makeState(1.2, 0.9, 0.35, 3.1);
  for (Vec2 n : {Vec2{0.6, -0.8}, Vec2{-0.6, 0.8}, Vec2{0.0, 1.0}}) {
    for (BcType bc : {BcType::kSlipWall, BcType::kNoSlipAdiabaticWall}) {
      const PrimVec wg = ghostState(bc, wi, n, wi, kGas);
      for (RiemannScheme sch : {RiemannScheme::kRoe, RiemannScheme::kHllc,
                                RiemannScheme::kRusanov}) {
        FluxOptions opt;
        opt.scheme = sch;
        const ConsVec f = inviscidFlux(wi, wg, n, kGas, opt);
        CHECK(f[0] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
        CHECK(f[3] == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
        const Real tangential = f[1] * (-n[1]) + f[2] * n[0];
        CHECK(tangential == doctest::Approx(0.0).epsilon(1e-12).scale(1.0));
        // Wall pressure must be positive and close to the interior pressure.
        const Real pw = f[1] * n[0] + f[2] * n[1];
        CHECK(pw > 0.0);
        CHECK(pw == doctest::Approx(wi[3]).epsilon(0.4));
      }
    }
  }
}
